#include "Engine/UWorld.hpp"
#include "Core/FLog.hpp"
#include "RHI/FFrameProfiler.hpp"
#include "Gameplay/AActor.hpp"
#include "Gameplay/AGameModeBase.hpp"
#include "Gameplay/AGameStateBase.hpp"
#include "Gameplay/AHUD.hpp"
#include "Gameplay/APawn.hpp"
#include "Gameplay/ACharacter.hpp"
#include "Gameplay/AAIController.hpp"
#include "Gameplay/APlayerCameraManager.hpp"
#include "Gameplay/APlayerController.hpp"
#include "Gameplay/APlayerState.hpp"
#include "Gameplay/UPrimitiveComponent.hpp"
#include "Renderer/FWorldRenderer.hpp"
#include "Engine/FParticleSimulation.hpp"
#include "Scene/FParticleTypes.hpp"
#include "Scene/Components.hpp"
#include "Engine/UNetDriver.hpp"
#include "Assets/FMaterialInstance.hpp"
#include "Assets/UAssetManager.hpp"
#include "Assets/UStaticMesh.hpp"
#include "Gameplay/USkeletalMeshComponent.hpp"
#include "Engine/FSimplePhysicsScene.hpp"
#include "Engine/FCollisionQuery.hpp"
#include "Gameplay/FGameplayDebugger.hpp"
#include "Physics/IPhysicsScene.hpp"
#include "AI/UNavigationSystem.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace Leon {

    namespace {

        /** The registered physics backend, or the engine's own simple scene when there is none. */
        TRef<IPhysicsScene> CreatePhysicsScene() {
            if (TRef<IPhysicsScene> scene = FPhysicsModule::CreateScene())
                return scene;
            return CreateRef<FSimplePhysicsScene>();
        }

    } // namespace

    TRef<UWorld> UWorld::Create(const std::string& InName) {
        return CreateRef<UWorld>(InName);
    }

    UWorld::UWorld(const std::string& InName) : UObject(InName) {
        PhysicsScene = CreatePhysicsScene();
        if (PhysicsScene)
            PhysicsScene->SetWorld(this);
    }

    UWorld::~UWorld() {
        Clear();
    }

    void UWorld::Clear() {
        EndPlay();
        PendingDestroy.clear();
        GameMode = nullptr;
        GameState = nullptr;
        PlayerControllers.clear();
        AIControllers.clear();
        EntityToActor.clear();
        Actors.clear();
        Registry.clear();
        Renderer.reset();
        NetDriver = nullptr;
        PhysicsScene.reset();
        NavigationSystem.reset();
        TimerManager.Clear();
        ActorIterationDepth = 0;
        bPaused = false;
        bLightmapsTrusted = true;
        EditorFolders.clear();
        TransientParticleEntity = entt::null;
        TransientParticles.clear();
        TransientParticleRng = 1u;
    }

    UNavigationSystem* UWorld::GetNavigationSystem() {
        if (!NavigationSystem)
            NavigationSystem = MakeScope<UNavigationSystem>();
        return NavigationSystem.get();
    }

    void UWorld::RebuildNavigation() {
        GetNavigationSystem()->Rebuild(*this);
    }

    void UWorld::SetEditorFolders(std::vector<std::string> InFolders) {
        EditorFolders.clear();
        for (const auto& path : InFolders)
            RegisterEditorFolder(path);
    }

    void UWorld::RegisterEditorFolder(const std::string& InPath) {
        const std::string normalized = AActor::NormalizeFolderPath(InPath);
        if (normalized.empty())
            return;

        // Register every parent segment so nested empty folders survive.
        size_t start = 0;
        while (start < normalized.size()) {
            size_t slash = normalized.find('/', start);
            std::string segment =
                slash == std::string::npos ? normalized : normalized.substr(0, slash);
            if (!segment.empty()) {
                bool bExists = false;
                for (const auto& existing : EditorFolders) {
                    if (existing == segment) {
                        bExists = true;
                        break;
                    }
                }
                if (!bExists)
                    EditorFolders.push_back(segment);
            }
            if (slash == std::string::npos)
                break;
            start = slash + 1;
        }
        std::sort(EditorFolders.begin(), EditorFolders.end());
    }

    void UWorld::UnregisterEditorFolder(const std::string& InPath) {
        const std::string normalized = AActor::NormalizeFolderPath(InPath);
        if (normalized.empty())
            return;

        EditorFolders.erase(std::remove_if(EditorFolders.begin(), EditorFolders.end(),
                                           [&](const std::string& f) {
                                               return f == normalized || f.rfind(normalized + "/", 0) == 0;
                                           }),
                            EditorFolders.end());

        for (auto& actorRef : Actors) {
            if (!actorRef)
                continue;
            const std::string& fp = actorRef->GetFolderPath();
            if (fp == normalized || fp.rfind(normalized + "/", 0) == 0)
                actorRef->SetFolderPath("");
        }
    }

    void UWorld::RenameEditorFolder(const std::string& InOldPath, const std::string& InNewPath) {
        const std::string oldPath = AActor::NormalizeFolderPath(InOldPath);
        const std::string newPath = AActor::NormalizeFolderPath(InNewPath);
        if (oldPath.empty() || newPath.empty() || oldPath == newPath)
            return;

        std::vector<std::string> updated;
        updated.reserve(EditorFolders.size());
        for (const auto& f : EditorFolders) {
            if (f == oldPath)
                updated.push_back(newPath);
            else if (f.rfind(oldPath + "/", 0) == 0)
                updated.push_back(newPath + f.substr(oldPath.size()));
            else
                updated.push_back(f);
        }
        SetEditorFolders(std::move(updated));

        for (auto& actorRef : Actors) {
            if (!actorRef)
                continue;
            const std::string& fp = actorRef->GetFolderPath();
            if (fp == oldPath)
                actorRef->SetFolderPath(newPath);
            else if (fp.rfind(oldPath + "/", 0) == 0)
                actorRef->SetFolderPath(newPath + fp.substr(oldPath.size()));
        }
    }

    void UWorld::InitWorld() {
        if (!PhysicsScene) {
            PhysicsScene = CreatePhysicsScene();
            if (PhysicsScene)
                PhysicsScene->SetWorld(this);
        }
        if (GameMode) {
            GameMode->InitGame();
        }
    }

    void UWorld::BeginPlay() {
        if (bBegunPlay)
            return;
        bBegunPlay = true;

        LE_CORE_INFO("UWorld: Beginning play across all actors (Count: {0})...", Actors.size());

        {
            // BeginPlay callbacks may spawn (appends to Actors) or destroy (deferred by the scope) actors.
            FActorIterationScope iteration(*this);

            // Flow: StartPlay/Login spawns & wires PC/HUD/Pawn first; BeginPlay runs after wiring.
            bDeferSpawnedActorBeginPlay = true;
            if (GameMode) {
                GameMode->StartPlay();
            }
            bDeferSpawnedActorBeginPlay = false;

            for (size_t i = 0; i < Actors.size(); ++i) {
                const TRef<AActor> actor = Actors[i]; // keep alive; Actors may reallocate during the call
                if (actor && !actor->HasBegunPlay() && !actor->IsPendingKill()) {
                    actor->ExecuteBeginPlay();
                    actor->MarkBegunPlay();
                }
            }
        }
        if (PhysicsScene)
            PhysicsScene->NotifyBeginPlayFinished();
        EnsureTransientParticleEntity();
    }

    void UWorld::Tick(FTimestep InTs) {
        float deltaSeconds = std::max(InTs.GetSeconds(), 0.0f);
        // Destroys requested anywhere in this tick are queued and flushed when the scope ends.
        FActorIterationScope iteration(*this);

        if (NetDriver) {
            NetDriver->ConsumeIncomingInput();
            NetDriver->ConsumeIncomingRPCs();
        }

        if (!bPaused)
            TimerManager.Tick(deltaSeconds);

        if (bBegunPlay) {
            FFrameProfiler::Working().ActorCount = static_cast<int32_t>(Actors.size());
            auto tickOnce = [&](AActor* actor) {
                if (!actor || actor->IsPendingKill() || !actor->HasBegunPlay() || !actor->CanEverTick())
                    return;
                if (bPaused && !actor->ShouldTickWhenPaused())
                    return;
                actor->ExecuteTick(deltaSeconds);
            };

            // Stable order: GameMode, GameState, PlayerControllers, then remaining actors once.
            tickOnce(GameMode);
            tickOnce(GameState);
            {
                FFrameProfiler::FScope controllers(&FFrameProfiler::Working().ControllersMs);
                std::vector<APlayerController*> pcs = PlayerControllers;
                for (APlayerController* pc : pcs) {
                    tickOnce(pc);
                }
            }
            {
                FFrameProfiler::FScope ai(&FFrameProfiler::Working().AIMs);
                std::vector<AAIController*> ais = AIControllers;
                for (AAIController* aiCtrl : ais) {
                    tickOnce(aiCtrl);
                }
            }

            for (size_t i = 0; i < Actors.size(); ++i) {
                AActor* actor = Actors[i].get();
                if (!actor || actor == GameMode || actor == GameState)
                    continue;
                bool bSkip = false;
                for (APlayerController* pc : PlayerControllers) {
                    if (pc == actor) {
                        bSkip = true;
                        break;
                    }
                }
                if (!bSkip) {
                    for (AAIController* ai : AIControllers) {
                        if (ai == actor) {
                            bSkip = true;
                            break;
                        }
                    }
                }
                if (bSkip)
                    continue;
                const bool bCharacter = dynamic_cast<ACharacter*>(actor) != nullptr;
                if (bCharacter) {
                    FFrameProfiler::FScope characters(&FFrameProfiler::Working().CharactersMs);
                    tickOnce(actor);
                } else {
                    tickOnce(actor);
                }
            }
        }

        if (bBegunPlay && !bPaused)
            TickTransientParticles(deltaSeconds);

        if (PhysicsScene && !bPaused) {
            FFrameProfiler::FScope physics(&FFrameProfiler::Working().PhysicsMs);
            PhysicsScene->SyncKinematicTransforms();
            PhysicsScene->Tick(deltaSeconds);
            PhysicsScene->SyncDynamicTransforms();
        }

        if (bBegunPlay && !bPaused) {
            for (size_t i = 0; i < Actors.size(); ++i) {
                AActor* actor = Actors[i].get();
                if (!actor || actor->IsPendingKill())
                    continue;
                if (auto* character = dynamic_cast<ACharacter*>(actor))
                    character->ConstrainRagdollToFloor();
                const auto& comps = actor->GetActorComponents();
                for (size_t c = 0; c < comps.size(); ++c) {
                    auto* skel = dynamic_cast<USkeletalMeshComponent*>(comps[c].get());
                    if (skel && skel->IsRagdoll())
                        skel->ApplyRagdollPoseFromBodies();
                }
            }
        }

        if (PhysicsScene && !bPaused)
            PhysicsScene->DrainContacts();

        if (bBegunPlay && !bPaused) {
            FFrameProfiler::FScope overlaps(&FFrameProfiler::Working().OverlapsMs);
            UpdateComponentOverlaps();
        }

        if (NetDriver) {
            FFrameProfiler::FScope net(&FFrameProfiler::Working().NetworkMs);
            NetDriver->Tick(deltaSeconds);
        }
    }

    void UWorld::EndPlay() {
        if (!bBegunPlay)
            return;

        LE_CORE_INFO("UWorld: Ending play across all actors...");
        {
            // EndPlay handlers destroy other actors (inventories, showcases); the scope defers the erase.
            FActorIterationScope iteration(*this);
            for (size_t i = 0; i < Actors.size(); ++i) {
                const TRef<AActor> actor = Actors[i];
                if (actor && actor->HasBegunPlay()) {
                    actor->ExecuteEndPlay();
                }
            }
        }

        bBegunPlay = false;
    }

    AActor* UWorld::SpawnActor(const std::string& InName) {
        return SpawnActor<AActor>(InName);
    }

    void UWorld::DestroyActor(AActor* InActor) {
        if (!InActor || InActor->IsPendingKill())
            return;

        InActor->MarkPendingKill();
        UnbindActorAliases(InActor);

        // Break attachment links in both directions so neither side keeps a pointer to a dead actor.
        InActor->DetachFromActor();
        const std::vector<AActor*> children = InActor->GetAttachedActors();
        for (AActor* child : children) {
            if (child)
                child->DetachFromActor();
        }

        if (bBegunPlay && InActor->HasBegunPlay()) {
            InActor->ExecuteEndPlay();
        }

        if (ActorIterationDepth > 0) {
            PendingDestroy.push_back(InActor);
            return;
        }

        DestroyActorImmediate(InActor);
    }

    UWorld::FActorIterationScope::FActorIterationScope(UWorld& InWorld) : World(InWorld) {
        ++World.ActorIterationDepth;
    }

    UWorld::FActorIterationScope::~FActorIterationScope() {
        if (--World.ActorIterationDepth == 0)
            World.FlushPendingDestroy();
    }

    void UWorld::DestroyActorImmediate(AActor* InActor) {
        if (!InActor)
            return;

        entt::entity handle = InActor->GetEntityHandle();
        if (handle != entt::null)
            EntityToActor.erase(static_cast<uint32_t>(handle));
        if (handle != entt::null && Registry.valid(handle)) {
            Registry.destroy(handle);
        }

        auto it = std::find_if(Actors.begin(), Actors.end(),
                               [InActor](const TRef<AActor>& item) { return item.get() == InActor; });
        if (it != Actors.end()) {
            Actors.erase(it);
        }
    }

    void UWorld::UnbindActorAliases(AActor* InActor) {
        if (!InActor)
            return;

        auto* asPC = dynamic_cast<APlayerController*>(InActor);
        auto* asPawn = dynamic_cast<APawn*>(InActor);
        auto* asPS = dynamic_cast<APlayerState*>(InActor);
        auto* asHUD = dynamic_cast<AHUD*>(InActor);
        auto* asPCM = dynamic_cast<APlayerCameraManager*>(InActor);
        auto* asGM = dynamic_cast<AGameModeBase*>(InActor);
        auto* asGS = dynamic_cast<AGameStateBase*>(InActor);

        if (asGM && GameMode == asGM)
            GameMode = nullptr;
        if (asGS && GameState == asGS)
            GameState = nullptr;

        if (asPC) {
            asPC->UnPossess();
            asPC->SetViewTarget(nullptr);
            asPC->SetHUD(nullptr);
            asPC->SetPlayerCameraManager(nullptr);
            asPC->SetPlayerState(nullptr);
            RemovePlayerController(asPC);
        }

        if (auto* asAI = dynamic_cast<AAIController*>(InActor)) {
            asAI->UnPossess();
            asAI->SetPlayerState(nullptr);
            auto it = std::find(AIControllers.begin(), AIControllers.end(), asAI);
            if (it != AIControllers.end())
                AIControllers.erase(it);
        }

        if (asPawn && asPawn->GetController()) {
            asPawn->GetController()->UnPossess();
        }

        if (GameState && asPS)
            GameState->RemovePlayerState(asPS);

        std::vector<APlayerController*> pcs = PlayerControllers;
        for (APlayerController* pc : pcs) {
            if (!pc)
                continue;
            if (pc->GetHUD() == asHUD)
                pc->SetHUD(nullptr);
            if (pc->GetPlayerCameraManager() == asPCM)
                pc->SetPlayerCameraManager(nullptr);
            if (pc->GetPlayerState() == asPS)
                pc->SetPlayerState(nullptr);
            if (pc->GetViewTarget() == InActor)
                pc->SetViewTarget(nullptr);
            if (asPawn && pc->GetPawn() == asPawn)
                pc->UnPossess();
        }
    }

    void UWorld::FlushPendingDestroy() {
        // Releasing an actor can destroy more actors from destructors; loop until the queue stays empty.
        while (!PendingDestroy.empty()) {
            std::vector<AActor*> pending = std::move(PendingDestroy);
            PendingDestroy.clear();
            for (AActor* actor : pending) {
                DestroyActorImmediate(actor);
            }
        }
    }

    void UWorld::RemovePlayerController(APlayerController* InPC) {
        auto it = std::find(PlayerControllers.begin(), PlayerControllers.end(), InPC);
        if (it != PlayerControllers.end())
            PlayerControllers.erase(it);
    }

    AActor* UWorld::FindActorByName(const std::string& InName) {
        for (auto& actor : Actors) {
            if (actor && !actor->IsPendingKill() && actor->GetName() == InName) {
                return actor.get();
            }
        }
        return nullptr;
    }

    AActor* UWorld::FindActorByGuid(const FUUID& InGuid) {
        if (!InGuid.IsValid())
            return nullptr;
        for (auto& actor : Actors) {
            if (actor && !actor->IsPendingKill() && actor->GetActorGuid() == InGuid) {
                return actor.get();
            }
        }
        return nullptr;
    }

    AActor* UWorld::FindActorByEntity(entt::entity InEntity) const {
        if (InEntity == entt::null)
            return nullptr;
        auto it = EntityToActor.find(static_cast<uint32_t>(InEntity));
        if (it == EntityToActor.end() || !it->second || it->second->IsPendingKill())
            return nullptr;
        return it->second;
    }

    void UWorld::AddPlayerController(APlayerController* InPC) {
        if (InPC && std::find(PlayerControllers.begin(), PlayerControllers.end(), InPC) == PlayerControllers.end()) {
            PlayerControllers.push_back(InPC);
        }
    }

    void UWorld::AddAIController(AAIController* InAI) {
        if (InAI && std::find(AIControllers.begin(), AIControllers.end(), InAI) == AIControllers.end()) {
            AIControllers.push_back(InAI);
        }
    }

    APlayerController* UWorld::GetFirstPlayerController() const {
        return PlayerControllers.empty() ? nullptr : PlayerControllers.front();
    }

    uint64_t UWorld::GetStaticLightingHash() const {
        if (!bLightmapsTrusted)
            return 0;
        const auto view = Registry.view<FWorldSettingsComponent>();
        if (view.begin() == view.end())
            return 0;
        const FWorldSettingsComponent& settings = view.get<FWorldSettingsComponent>(*view.begin());
        return settings.bStaticLighting ? settings.LightmapBakeHash : 0;
    }

    FWorldRenderer* UWorld::GetWorldRenderer() {
        if (!Renderer) {
            Renderer = CreateScope<FWorldRenderer>(this);
            // What the project's settings asked for before there was a renderer to give it to.
            FShadowSettings& shadows = Renderer->GetShadowSettings();
            if (HasPendingRendererDefaults()) {
                Renderer->ApplyProjectRendererDefaults(GetPendingShadowMapResolution(), GetPendingPlanarReflectionEnabled(),
                                                       GetPendingPlanarReflectionQuality(),
                                                       GetPendingPlanarReflectionResolutionScale());
                shadows.CascadeResolution = GetPendingShadowMapResolution();
                shadows.CascadeCount = GetPendingCascadeCount();
                shadows.ShadowDistance = GetPendingShadowDistance();
                shadows.SpotResolution = GetPendingSpotResolution();
                shadows.PointShadowResolution = GetPendingPointShadowResolution();
                shadows.MaxShadowedPointLights = GetPendingMaxShadowedPointLights();
                shadows.MaxShadowedSpotLights = GetPendingMaxShadowedSpotLights();
            }
            shadows.FilterMode = GetPendingShadowFilter();
            Renderer->SetReflectionProbeResolution(PendingReflectionProbeResolution);
            FPostProcessSettings& post = Renderer->GetPostProcessSettings();
            post.bSSAOEnabled = GetPendingSSAOEnabled();
            post.SSAORadius = GetPendingSSAORadius();
            post.SSAOIntensity = GetPendingSSAOIntensity();
            post.SSAOBias = GetPendingSSAOBias();
            post.bBloomEnabled = GetPendingBloomEnabled();
            post.bFXAAEnabled = GetPendingFXAAEnabled();
        }
        return Renderer.get();
    }

    glm::mat4 UWorld::GetEntityWorldMatrix(entt::entity InEntity, const FTransformComponent& InTransform) const {
        if (AActor* actor = FindActorByEntity(InEntity))
            return actor->GetActorWorldMatrix();
        return InTransform.GetTransform();
    }

    bool UWorld::WantsDebugDraw() const {
        return FGameplayDebugger::IsEnabled();
    }

    void UWorld::DrawDebug() {
        if (FGameplayDebugger::ShowPhysics())
            DrawDebugWorldColliders(*this);
    }

    void UWorld::SetProjectRendererDefaults(uint32_t InShadowMapResolution, bool bInEnablePlanarReflection,
                                            uint32_t InCascadeCount, float InShadowDistance,
                                            EPlanarReflectionQuality InPlanarQuality, float InPlanarResolutionScale) {
        bHasPendingRendererDefaults = true;
        PendingShadowMapResolution = InShadowMapResolution > 0 ? InShadowMapResolution : 2048;
        bPendingPlanarReflection = bInEnablePlanarReflection;
        PendingPlanarQuality = InPlanarQuality;
        PendingPlanarResolutionScale = InPlanarResolutionScale > 0.0f
                                           ? ClampPlanarReflectionResolutionScale(InPlanarResolutionScale)
                                           : PlanarReflectionScaleFor(InPlanarQuality);
        if (InCascadeCount > 0)
            PendingCascadeCount = std::min(InCascadeCount, 4u);
        if (InShadowDistance > 0.0f)
            PendingShadowDistance = InShadowDistance;
        if (Renderer) {
            Renderer->ApplyProjectRendererDefaults(PendingShadowMapResolution, bPendingPlanarReflection,
                                                   PendingPlanarQuality, PendingPlanarResolutionScale);
            Renderer->GetShadowSettings().CascadeCount = PendingCascadeCount;
            Renderer->GetShadowSettings().ShadowDistance = PendingShadowDistance;
        }
    }

    void UWorld::SetProjectSSAODefaults(bool bInEnabled, float InRadius, float InIntensity, float InBias) {
        bPendingSSAOEnabled = bInEnabled;
        PendingSSAORadius = InRadius > 0.0f ? InRadius : 0.5f;
        PendingSSAOIntensity = InIntensity >= 0.0f ? InIntensity : 1.0f;
        PendingSSAOBias = InBias >= 0.0f ? InBias : 0.025f;
        if (Renderer) {
            auto& pp = Renderer->GetPostProcessSettings();
            pp.bSSAOEnabled = bPendingSSAOEnabled;
            pp.SSAORadius = PendingSSAORadius;
            pp.SSAOIntensity = PendingSSAOIntensity;
            pp.SSAOBias = PendingSSAOBias;
        }
    }

    void UWorld::SetProjectPostProcessToggles(bool bInBloomEnabled, bool bInFXAAEnabled) {
        bPendingBloomEnabled = bInBloomEnabled;
        bPendingFXAAEnabled = bInFXAAEnabled;
        if (Renderer) {
            auto& pp = Renderer->GetPostProcessSettings();
            pp.bBloomEnabled = bPendingBloomEnabled;
            pp.bFXAAEnabled = bPendingFXAAEnabled;
        }
    }

    void UWorld::SetProjectShadowFilter(EShadowFilterMode InFilter) {
        PendingShadowFilter = InFilter;
        if (Renderer)
            Renderer->GetShadowSettings().FilterMode = PendingShadowFilter;
    }

    void UWorld::SetProjectOmniShadowDefaults(uint32_t InSpotResolution, uint32_t InPointShadowResolution,
                                              uint32_t InMaxShadowedPointLights, uint32_t InMaxShadowedSpotLights) {
        PendingSpotResolution = InSpotResolution > 0 ? InSpotResolution : 1024;
        PendingPointShadowResolution = InPointShadowResolution > 0 ? InPointShadowResolution : 512;
        PendingMaxShadowedPointLights = std::min(std::max(InMaxShadowedPointLights, 1u), 4u);
        PendingMaxShadowedSpotLights =
            std::clamp(InMaxShadowedSpotLights, 1u, FShadowSettings::kMaxShadowedSpotLights);
        if (Renderer) {
            auto& sh = Renderer->GetShadowSettings();
            sh.SpotResolution = PendingSpotResolution;
            sh.PointShadowResolution = PendingPointShadowResolution;
            sh.MaxShadowedPointLights = PendingMaxShadowedPointLights;
            sh.MaxShadowedSpotLights = PendingMaxShadowedSpotLights;
        }
    }

    void UWorld::SetProjectReflectionProbeResolution(uint32_t InResolution) {
        PendingReflectionProbeResolution = InResolution > 0 ? InResolution : 128;
        if (Renderer)
            Renderer->SetReflectionProbeResolution(PendingReflectionProbeResolution);
    }

    void UWorld::ReloadTextures() {
        UAssetManager::ReloadAllTextures();

        for (auto entity : Registry.view<FSkyboxComponent>()) {
            auto& sky = Registry.get<FSkyboxComponent>(entity);
            if (!sky.HDREnvironmentMapPath.empty())
                sky.HDREnvironmentMap = UAssetManager::GetHDRTexture(sky.HDREnvironmentMapPath);
        }

        for (auto entity : Registry.view<FMaterialComponent>()) {
            auto& materialComponent = Registry.get<FMaterialComponent>(entity);
            if (!materialComponent.AssetPath.empty()) {
                materialComponent.MaterialInstance = UAssetManager::GetMaterialInstance(materialComponent.AssetPath);
                continue;
            }
            if (!materialComponent.MaterialInstance)
                continue;
            if (const TRef<FMaterial> parent = materialComponent.MaterialInstance->GetParent())
                parent->ReloadTextures();
            materialComponent.MaterialInstance->ReloadTextureOverrides();
        }

        if (FWorldRenderer* renderer = GetWorldRendererIfInitialized())
            renderer->InvalidateEnvironment();
    }

    void UWorld::OnRender(const FPerspectiveCamera& InCamera) {
        GetWorldRenderer()->RenderScene(InCamera);
    }

    bool UWorld::OverlapAABB(const glm::vec3& InWorldMin, const glm::vec3& InWorldMax, AActor* InIgnore,
                             FHitResult& OutHit) const {
        glm::vec3 center = (InWorldMin + InWorldMax) * 0.5f;
        glm::vec3 half = (InWorldMax - InWorldMin) * 0.5f;
        std::vector<FHitResult> hits;
        if (OverlapMultiByChannel(center, half, ECollisionChannel::Visibility, InIgnore, hits) <= 0) {
            OutHit = {};
            return false;
        }
        OutHit = hits.front();
        return true;
    }

    bool UWorld::SweepAABB(const glm::vec3& InWorldMin, const glm::vec3& InWorldMax, const glm::vec3& InDelta,
                           AActor* InIgnore, FHitResult& OutHit) const {
        glm::vec3 start = (InWorldMin + InWorldMax) * 0.5f;
        glm::vec3 half = (InWorldMax - InWorldMin) * 0.5f;
        float radius = std::max(half.x, std::max(half.y, half.z));
        return SweepSingleByChannel(start, start + InDelta, radius, ECollisionChannel::WorldStatic, InIgnore, OutHit);
    }

    bool UWorld::LineTrace(const glm::vec3& InStart, const glm::vec3& InEnd, AActor* InIgnore,
                           FHitResult& OutHit) const {
        return LineTraceSingleByChannel(InStart, InEnd, ECollisionChannel::Visibility, InIgnore, OutHit);
    }

    bool UWorld::LineTraceByChannel(const glm::vec3& InStart, const glm::vec3& InEnd, ECollisionChannel InChannel,
                                    AActor* InIgnore, FHitResult& OutHit) const {
        return LineTraceSingleByChannel(InStart, InEnd, InChannel, InIgnore, OutHit);
    }

    bool UWorld::LineTraceSingleByChannel(const glm::vec3& InStart, const glm::vec3& InEnd, ECollisionChannel InChannel,
                                          AActor* InIgnore, FHitResult& OutHit) const {
        OutHit = {};
        if (PhysicsScene)
            return PhysicsScene->LineTraceSingleByChannel(InStart, InEnd, InChannel, InIgnore, OutHit);
        return false;
    }

    int32_t UWorld::LineTraceMultiByChannel(const glm::vec3& InStart, const glm::vec3& InEnd,
                                            ECollisionChannel InChannel, AActor* InIgnore,
                                            std::vector<FHitResult>& OutHits) const {
        if (!PhysicsScene) {
            OutHits.clear();
            return 0;
        }
        return PhysicsScene->LineTraceMultiByChannel(InStart, InEnd, InChannel, InIgnore, OutHits);
    }

    bool UWorld::SweepSingleByChannel(const glm::vec3& InStart, const glm::vec3& InEnd, float InRadius,
                                      ECollisionChannel InChannel, AActor* InIgnore, FHitResult& OutHit) const {
        OutHit = {};
        if (!PhysicsScene)
            return false;
        return PhysicsScene->SweepSingleByChannel(InStart, InEnd, InRadius, InChannel, InIgnore, OutHit);
    }

    int32_t UWorld::SweepMultiByChannel(const glm::vec3& InStart, const glm::vec3& InEnd, float InRadius,
                                        ECollisionChannel InChannel, AActor* InIgnore,
                                        std::vector<FHitResult>& OutHits) const {
        if (!PhysicsScene) {
            OutHits.clear();
            return 0;
        }
        return PhysicsScene->SweepMultiByChannel(InStart, InEnd, InRadius, InChannel, InIgnore, OutHits);
    }

    bool UWorld::SweepCapsuleSingleByChannel(const glm::vec3& InStart, const glm::vec3& InEnd, float InRadius,
                                             float InHalfHeight, ECollisionChannel InChannel, AActor* InIgnore,
                                             FHitResult& OutHit) const {
        OutHit = {};
        if (!PhysicsScene)
            return false;
        return PhysicsScene->SweepCapsuleSingleByChannel(InStart, InEnd, InRadius, InHalfHeight, InChannel, InIgnore,
                                                         OutHit);
    }

    int32_t UWorld::SweepCapsuleMultiByChannel(const glm::vec3& InStart, const glm::vec3& InEnd, float InRadius,
                                               float InHalfHeight, ECollisionChannel InChannel, AActor* InIgnore,
                                               std::vector<FHitResult>& OutHits) const {
        if (!PhysicsScene) {
            OutHits.clear();
            return 0;
        }
        return PhysicsScene->SweepCapsuleMultiByChannel(InStart, InEnd, InRadius, InHalfHeight, InChannel, InIgnore,
                                                        OutHits);
    }

    bool UWorld::OverlapAnyTestByChannel(const glm::vec3& InPos, const glm::vec3& InHalfExtent,
                                         ECollisionChannel InChannel, AActor* InIgnore) const {
        return PhysicsScene && PhysicsScene->OverlapAnyTestByChannel(InPos, InHalfExtent, InChannel, InIgnore);
    }

    int32_t UWorld::OverlapMultiByChannel(const glm::vec3& InPos, const glm::vec3& InHalfExtent,
                                          ECollisionChannel InChannel, AActor* InIgnore,
                                          std::vector<FHitResult>& OutHits) const {
        if (!PhysicsScene) {
            OutHits.clear();
            return 0;
        }
        return PhysicsScene->OverlapMultiByChannel(InPos, InHalfExtent, InChannel, InIgnore, OutHits);
    }

    void UWorld::UpdateComponentOverlaps() {
        std::vector<UPrimitiveComponent*> generators;
        std::vector<UPrimitiveComponent*> candidates;
        generators.reserve(32);
        candidates.reserve(64);

        for (const auto& actorRef : Actors) {
            AActor* actor = actorRef.get();
            if (!actor || actor->IsPendingKill() || !actor->HasBegunPlay())
                continue;
            for (const auto& compRef : actor->GetActorComponents()) {
                UPrimitiveComponent* prim = compRef ? compRef->AsPrimitiveComponent() : nullptr;
                if (!prim || prim->GetCollisionEnabled() == ECollisionEnabled::NoCollision)
                    continue;
                candidates.push_back(prim);
                if (prim->GetGenerateOverlapEvents())
                    generators.push_back(prim);
            }
        }

        // Overlap callbacks destroy actors (pickups, projectiles). The scope keeps every gathered component
        // alive until the loop ends; UpdateOverlaps itself skips owners that became pending-kill.
        FActorIterationScope iteration(*this);
        for (UPrimitiveComponent* gen : generators)
            gen->UpdateOverlaps(candidates);
    }

    void UWorld::EnsureTransientParticleEntity() {
        if (TransientParticleEntity != entt::null && Registry.valid(TransientParticleEntity))
            return;
        TransientParticleEntity = Registry.create();
        Registry.emplace<FParticleRenderComponent>(TransientParticleEntity);
    }

    void UWorld::TickTransientParticles(float InDeltaSeconds) {
        if (TransientParticles.empty()) {
            if (TransientParticleEntity != entt::null && Registry.valid(TransientParticleEntity)) {
                auto& render = Registry.get<FParticleRenderComponent>(TransientParticleEntity);
                render.Particles.clear();
                render.bVisible = false;
            }
            return;
        }
        SimulateParticles(TransientParticles, InDeltaSeconds);
        SyncTransientParticleRender();
    }

    void UWorld::SyncTransientParticleRender() {
        EnsureTransientParticleEntity();
        auto& render = Registry.get<FParticleRenderComponent>(TransientParticleEntity);
        CopyParticlesForRender(TransientParticles, render.Particles);
        render.bVisible = !render.Particles.empty();
    }

    bool UWorld::SpawnTransientParticles(const FParticleEmitterSettings& InSettings, const glm::vec3& InLocation) {
        if (static_cast<int32_t>(TransientParticles.size()) >= kMaxTransientParticles)
            return false;
        EnsureTransientParticleEntity();
        bool bSpawned = false;
        if (InSettings.Kind == EParticleKind::Beam) {
            bSpawned = AppendBeamParticle(TransientParticles, InSettings, InLocation, kMaxTransientParticles);
        } else {
            bSpawned = AppendBurstParticles(TransientParticles, InSettings, InLocation, TransientParticleRng,
                                            kMaxTransientParticles) > 0;
        }
        if (bSpawned)
            SyncTransientParticleRender();
        return bSpawned;
    }

} // namespace Leon
