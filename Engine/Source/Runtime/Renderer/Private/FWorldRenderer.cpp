#include "Renderer/FWorldRenderer.hpp"

#include "FWorldRendererInternals.hpp"

#include "Assets/USkeletalMesh.hpp"
#include "Assets/UStaticMesh.hpp"
#include "Engine/Components.hpp"
#include "Engine/UWorld.hpp"
#include "RHI/FFrameProfiler.hpp"
#include "RHI/FRHIFrame.hpp"
#include "RHI/FRenderer.hpp"
#include "RHI/FTexture.hpp"
#include "Renderer/FLight.hpp"
#include "Renderer/FRenderingMath.hpp"

#include <algorithm>

namespace Leon {

    FWorldRenderer::FWorldRenderer(UWorld* InWorld) : World(InWorld) {
        if (!InWorld)
            return;
        if (InWorld->HasPendingRendererDefaults()) {
            ShadowSettings.CascadeResolution = InWorld->GetPendingShadowMapResolution();
            ShadowSettings.CascadeCount = InWorld->GetPendingCascadeCount();
            ShadowSettings.ShadowDistance = InWorld->GetPendingShadowDistance();
            ShadowSettings.SpotResolution = InWorld->GetPendingSpotResolution();
            ShadowSettings.PointShadowResolution = InWorld->GetPendingPointShadowResolution();
            ShadowSettings.MaxShadowedPointLights = InWorld->GetPendingMaxShadowedPointLights();
            bEnablePlanarReflection = InWorld->GetPendingPlanarReflectionEnabled();
            PlanarQuality = InWorld->GetPendingPlanarReflectionQuality();
            PlanarResolutionScale = InWorld->GetPendingPlanarReflectionResolutionScale();
        }
        PostProcessSettings.bSSAOEnabled = InWorld->GetPendingSSAOEnabled();
        PostProcessSettings.SSAORadius = InWorld->GetPendingSSAORadius();
        PostProcessSettings.SSAOIntensity = InWorld->GetPendingSSAOIntensity();
        PostProcessSettings.SSAOBias = InWorld->GetPendingSSAOBias();
        PostProcessSettings.bBloomEnabled = InWorld->GetPendingBloomEnabled();
        PostProcessSettings.bFXAAEnabled = InWorld->GetPendingFXAAEnabled();
        ShadowSettings.FilterMode = InWorld->GetPendingShadowFilter();
    }

    void FWorldRenderer::ApplyProjectRendererDefaults(uint32_t InShadowMapResolution, bool bInEnablePlanarReflection,
                                                      EPlanarReflectionQuality InPlanarQuality,
                                                      float InPlanarResolutionScale) {
        bEnablePlanarReflection = bInEnablePlanarReflection;
        PlanarQuality = InPlanarQuality;
        PlanarResolutionScale = InPlanarResolutionScale > 0.0f
                                    ? ClampPlanarReflectionResolutionScale(InPlanarResolutionScale)
                                    : PlanarReflectionScaleFor(InPlanarQuality);
        if (InShadowMapResolution != 0)
            ShadowSettings.CascadeResolution = InShadowMapResolution;
    }

    void FWorldRenderer::InvalidateEnvironment() {
        IBLEnvironment = {};
        bEnvironmentGenerated = false;
        LoadedHDRPath.clear();
    }

    void FWorldRenderer::SetPlanarReflectionQuality(EPlanarReflectionQuality InQuality) {
        PlanarQuality = InQuality;
        PlanarResolutionScale = PlanarReflectionScaleFor(InQuality);
    }

    void FWorldRenderer::SetPlanarReflectionResolutionScale(float InScale) {
        PlanarResolutionScale = ClampPlanarReflectionResolutionScale(InScale);
    }

    uint32_t FWorldRenderer::PlanarCaptureWidth() const {
        const uint32_t width = ViewportWidth > 0 ? ViewportWidth : 1280;
        return std::max(8u, static_cast<uint32_t>(static_cast<float>(width) * PlanarResolutionScale + 0.5f));
    }

    uint32_t FWorldRenderer::PlanarCaptureHeight() const {
        const uint32_t height = ViewportHeight > 0 ? ViewportHeight : 720;
        return std::max(8u, static_cast<uint32_t>(static_cast<float>(height) * PlanarResolutionScale + 0.5f));
    }

    void FWorldRenderer::OnViewportResize(uint32_t InWidth, uint32_t InHeight) {
        ViewportWidth = InWidth;
        ViewportHeight = InHeight;
    }

    void FWorldRenderer::Render(const FPerspectiveCamera& InCamera) {
        IRHICommandList* commands = FRHIFrame::GetCommands();
        IRHITexture* output = FRHIFrame::GetOutput();
        if (commands && output)
            RenderRHI(InCamera, *commands, *output);
        else
            UpdateViewState(InCamera);
    }

    void FWorldRenderer::UpdateViewState(const FPerspectiveCamera& InCamera) {
        if (!World)
            return;
        FRenderer::ResetStats();
        FFrameProfiler::Working().ShadowDrawCalls = 0;
        auto& registry = World->GetRegistry();

        // Spot lights aim where their actor points.
        auto spotView = registry.view<FSpotLightComponent, FTransformComponent>();
        for (auto entity : spotView) {
            auto [spot, transform] = spotView.get<FSpotLightComponent, FTransformComponent>(entity);
            if (spot.bEnabled)
                SyncSpotLightFromTransform(spot.Light, transform.Translation, transform.Rotation);
        }

        // The environment follows the world's sky; a world without one still gets a default.
        {
            FFrameProfiler::FScope timer(&FFrameProfiler::Working().IBLMs);
            const auto skyView = registry.view<FSkyboxComponent>();
            if (skyView.begin() != skyView.end())
                UpdateIBL(registry.get<FSkyboxComponent>(*skyView.begin()));
            else if (!bEnvironmentGenerated)
                UpdateIBL(FSkyboxComponent{});
        }

        // Level of detail of every static mesh, by how large it appears.
        auto staticView = registry.view<FTransformComponent, FStaticMeshComponent>();
        for (auto entity : staticView) {
            auto [transform, component] = staticView.get<FTransformComponent, FStaticMeshComponent>(entity);
            if (component.bVisible && component.StaticMesh)
                UpdateStaticMeshLOD(component, ResolveActorWorldMatrix(World, entity, transform), &InCamera);
        }

        if (ShadowSettings.bEnableShadows)
            RefreshSkinnedShadowCasterSelection(InCamera.GetPosition());

        // Mirrored cameras for the reflection captures.
        bWallPlanarActive = false;
        if (bEnablePlanarReflection) {
            const glm::mat4 projection = InCamera.GetProjectionMatrix();
            const glm::mat4 view = InCamera.GetViewMatrix();
            const FPlanarReflectionPlane floor = SelectFloorPlane();
            PlanarPlaneNormal = floor.Normal;
            PlanarPlaneDistance = floor.Distance;
            PlanarViewProjection = projection * view * PlanarReflectionMatrix(floor.Normal, floor.Distance);

            FPlanarReflectionPlane wall;
            if (SelectWallMirrorPlane(InCamera, wall)) {
                WallPlanarPlaneNormal = wall.Normal;
                WallPlanarPlaneDistance = wall.Distance;
                WallPlanarViewProjection = projection * view * PlanarReflectionMatrix(wall.Normal, wall.Distance);
                bWallPlanarActive = true;
            }
        }
    }

    void FWorldRenderer::UpdateIBL(const FSkyboxComponent& InSkybox) {
        std::string currentHdr = InSkybox.HDREnvironmentMapPath;
        if (currentHdr.empty() && InSkybox.HDREnvironmentMap)
            currentHdr = InSkybox.HDREnvironmentMap->GetPath();

        if (!bEnvironmentGenerated ||
            (InSkybox.bUseHDREnvironmentMap && !currentHdr.empty() && currentHdr != LoadedHDRPath) ||
            (!InSkybox.bUseHDREnvironmentMap && !LoadedHDRPath.empty())) {
            IBLEnvironment = FIBLGenerator::CreateEnvironmentFromSkybox(InSkybox);
            LoadedHDRPath = InSkybox.bUseHDREnvironmentMap ? currentHdr : "";
            bEnvironmentGenerated = true;
        }
    }

    // --- Skinned shadow casters -----------------------------------------------------------------

    void FWorldRenderer::RefreshSkinnedShadowCasterSelection(const glm::vec3& InCameraPos) {
        SelectedSkinnedShadowCasters.clear();
        const bool bCapCount = ShadowSettings.MaxSkinnedShadowCasters > 0;
        const bool bCapDistance = ShadowSettings.SkinnedShadowMaxDistance > 0.0f;
        bSkinnedShadowSelectionUnlimited = !bCapCount && !bCapDistance;
        if (bSkinnedShadowSelectionUnlimited || !World)
            return;

        std::vector<FSkinnedShadowCasterRank> ranks;
        auto skinnedView = World->GetRegistry().view<FTransformComponent, FSkinnedMeshRenderState>();
        for (auto entity : skinnedView) {
            auto [transform, skinned] = skinnedView.get<FTransformComponent, FSkinnedMeshRenderState>(entity);
            if (!skinned.SkeletalMesh || !skinned.bCastShadows || !skinned.bVisible)
                continue;
            const glm::vec3 position = glm::vec3(ResolveActorWorldMatrix(World, entity, transform)[3]);
            const glm::vec3 delta = position - InCameraPos;
            ranks.push_back({glm::dot(delta, delta), static_cast<uint32_t>(entity)});
        }
        SelectClosestSkinnedShadowCasters(ranks, ShadowSettings.MaxSkinnedShadowCasters,
                                          ShadowSettings.SkinnedShadowMaxDistance);
        SelectedSkinnedShadowCasters.reserve(ranks.size());
        for (const FSkinnedShadowCasterRank& rank : ranks)
            SelectedSkinnedShadowCasters.insert(rank.Id);
    }

    bool FWorldRenderer::IsSkinnedShadowCasterSelected(uint32_t InEntityId) const {
        if (bSkinnedShadowSelectionUnlimited)
            return true;
        return SelectedSkinnedShadowCasters.find(InEntityId) != SelectedSkinnedShadowCasters.end();
    }

    // --- Planar reflection planes -----------------------------------------------------------------

    void FWorldRenderer::ClearPlanarReflectionPlanes() {
        PlanarReflectionPlanes.clear();
    }

    void FWorldRenderer::AddPlanarReflectionPlane(const glm::vec3& InNormal, float InDistance) {
        PlanarReflectionPlanes.push_back({SafeNormalize(InNormal, glm::vec3(0.0f, 1.0f, 0.0f)), InDistance});
    }

    FWorldRenderer::FPlanarReflectionPlane FWorldRenderer::SelectFloorPlane() const {
        for (const FPlanarReflectionPlane& plane : PlanarReflectionPlanes) {
            if (IsHorizontalPlanarPlane(plane.Normal))
                return {SafeNormalize(plane.Normal, glm::vec3(0.0f, 1.0f, 0.0f)), plane.Distance};
        }
        return {{0.0f, 1.0f, 0.0f}, 0.0f};
    }

    bool FWorldRenderer::SelectWallMirrorPlane(const FPerspectiveCamera& InCamera,
                                               FPlanarReflectionPlane& OutPlane) const {
        const glm::vec3 cameraPosition = InCamera.GetPosition();
        const glm::vec3 cameraForward = InCamera.GetForwardDirection();
        float bestScore = kWallPlanarCaptureMinScore;
        bool bFound = false;
        for (const FPlanarReflectionPlane& plane : PlanarReflectionPlanes) {
            if (IsHorizontalPlanarPlane(plane.Normal))
                continue;
            const glm::vec3 normal = SafeNormalize(plane.Normal, glm::vec3(0.0f, 0.0f, 1.0f));
            const float score = PlanarReflectionPlaneScore(normal, plane.Distance, cameraPosition, cameraForward);
            if (score > bestScore) {
                bestScore = score;
                OutPlane = {normal, plane.Distance};
                bFound = true;
            }
        }
        return bFound;
    }

} // namespace Leon
