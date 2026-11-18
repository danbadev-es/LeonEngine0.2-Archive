#include "Gameplay/AActor.hpp"
#include "Gameplay/USceneComponent.hpp"
#include "Engine/UWorld.hpp"
#include "Engine/UNetDriver.hpp"
#include "Physics/IPhysicsScene.hpp"
#include "RHI/FFrameProfiler.hpp"

#include <cctype>
#include <cstdlib>

namespace Leon {

    std::string AActor::NormalizeFolderPath(const std::string& InPath) {
        std::string out;
        out.reserve(InPath.size());
        bool bPrevSlash = true; // strip leading separators
        for (char c : InPath) {
            if (c == '\\')
                c = '/';
            if (c == '/') {
                if (bPrevSlash)
                    continue;
                bPrevSlash = true;
                out.push_back('/');
                continue;
            }
            if (std::isspace(static_cast<unsigned char>(c)) && (out.empty() || bPrevSlash))
                continue;
            bPrevSlash = false;
            out.push_back(c);
        }
        while (!out.empty() && (out.back() == '/' || std::isspace(static_cast<unsigned char>(out.back()))))
            out.pop_back();
        return out;
    }

    AActor::AActor(entt::entity InHandle, UWorld* InWorld, const std::string& InName)
        : UObject(InName), EntityHandle(InHandle), World(InWorld) {
        ActorGuid = FUUID::Generate();
    }

    void AActor::CallServerRPC(uint16_t InFunctionId, const std::vector<uint8_t>& InPayload) {
        if (InPayload.size() > kMaxNetRPCPayloadBytes)
            return;
        if (IsNetworkAuthority()) {
            HandleServerRPC(InFunctionId, InPayload.data(), InPayload.size());
            return;
        }
        if (!World || World->GetNetMode() != ENetMode::Client)
            return;
        UNetDriver* driver = World->GetNetDriver();
        if (!driver || driver->GetConnections().empty() || !driver->GetConnections().front())
            return;
        UNetDriver::AppendOutgoingRPC(*driver->GetConnections().front(), ActorGuid, ENetRPCKind::Server, InFunctionId,
                                      InPayload);
    }

    void AActor::CallClientRPC(uint16_t InFunctionId, const std::vector<uint8_t>& InPayload) {
        if (!IsNetworkAuthority() || InPayload.size() > kMaxNetRPCPayloadBytes)
            return;
        if (!World)
            return;
        UNetDriver* driver = World->GetNetDriver();
        if (!driver)
            return;
        for (const auto& conn : driver->GetConnections()) {
            if (conn)
                UNetDriver::AppendOutgoingRPC(*conn, ActorGuid, ENetRPCKind::Client, InFunctionId, InPayload);
        }
    }

    void AActor::CallMulticastRPC(uint16_t InFunctionId, const std::vector<uint8_t>& InPayload) {
        if (!IsNetworkAuthority() || InPayload.size() > kMaxNetRPCPayloadBytes)
            return;
        HandleClientRPC(InFunctionId, InPayload.data(), InPayload.size());
        if (!World)
            return;
        UNetDriver* driver = World->GetNetDriver();
        if (!driver)
            return;
        for (const auto& conn : driver->GetConnections()) {
            if (conn)
                UNetDriver::AppendOutgoingRPC(*conn, ActorGuid, ENetRPCKind::Multicast, InFunctionId, InPayload);
        }
    }

    bool AActor::IsNetRelevantFor(const glm::vec3& InViewerLocation) const {
        if (!bReplicates || bPendingKill)
            return false;
        if (bAlwaysRelevant)
            return true;
        if (NetCullDistanceSquared <= 0.0f)
            return true;
        const glm::vec3 delta = GetActorLocation() - InViewerLocation;
        return glm::dot(delta, delta) <= NetCullDistanceSquared;
    }

    // The three lifecycle loops below are index-based and hold a local reference to each component:
    // callbacks may add components (reallocating the vector) or destroy the owning actor.

    void AActor::ExecuteBeginPlay() {
        BeginPlay();
        for (size_t i = 0; i < ActorComponents.size(); ++i) {
            const TRef<UActorComponent> comp = ActorComponents[i];
            if (comp && !comp->HasBegunPlay()) {
                comp->BeginPlay();
                comp->MarkBegunPlay();
            }
        }
        if (World && World->GetPhysicsScene())
            World->GetPhysicsScene()->CreatePhysicsState(this);
    }

    void AActor::ExecuteTick(float DeltaSeconds) {
        auto& timing = FFrameProfiler::Working();
        ++timing.TickActors;
        Tick(DeltaSeconds);
        for (size_t i = 0; i < ActorComponents.size(); ++i) {
            // Destroyed from its own Tick or from a component: EndPlay already ran, stop ticking.
            if (bPendingKill)
                break;
            const TRef<UActorComponent> comp = ActorComponents[i];
            if (comp && comp->IsComponentTickEnabled()) {
                ++timing.TickComponents;
                comp->Tick(DeltaSeconds);
            }
        }
    }

    void AActor::ExecuteEndPlay() {
        // Cleared first so a Destroy() issued from an EndPlay handler cannot re-enter this function.
        bHasBegunPlay = false;
        if (World && World->GetPhysicsScene())
            World->GetPhysicsScene()->DestroyPhysicsState(this);
        for (size_t i = 0; i < ActorComponents.size(); ++i) {
            const TRef<UActorComponent> comp = ActorComponents[i];
            if (comp) {
                comp->EndPlay();
                comp->ClearBegunPlay();
            }
        }
        EndPlay();
        // Components are intentionally kept: they are released with the actor. Clearing them here freed
        // components that were still on the call stack (a component destroying its owner from Tick) and
        // left RootComponent dangling.
    }

    const std::string AActor::EmptyProperty;

    bool AActor::IsValidPropertyKey(const std::string& InKey) {
        if (InKey.empty() || !std::isalpha(static_cast<unsigned char>(InKey.front())))
            return false;
        for (char c : InKey) {
            const bool bAllowed = std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.';
            if (!bAllowed)
                return false;
        }
        return true;
    }

    bool AActor::SetProperty(const std::string& InKey, const std::string& InValue) {
        if (!IsValidPropertyKey(InKey))
            return false;
        Properties[InKey] = InValue;
        return true;
    }

    const std::string& AActor::GetProperty(const std::string& InKey, const std::string& InDefault) const {
        const auto it = Properties.find(InKey);
        return it != Properties.end() ? it->second : InDefault;
    }

    float AActor::GetPropertyFloat(const std::string& InKey, float InDefault) const {
        const auto it = Properties.find(InKey);
        if (it == Properties.end() || it->second.empty())
            return InDefault;
        char* end = nullptr;
        const float value = std::strtof(it->second.c_str(), &end);
        return end == it->second.c_str() ? InDefault : value;
    }

    int32_t AActor::GetPropertyInt(const std::string& InKey, int32_t InDefault) const {
        const auto it = Properties.find(InKey);
        if (it == Properties.end() || it->second.empty())
            return InDefault;
        char* end = nullptr;
        const long value = std::strtol(it->second.c_str(), &end, 10);
        return end == it->second.c_str() ? InDefault : static_cast<int32_t>(value);
    }

    bool AActor::GetPropertyBool(const std::string& InKey, bool bInDefault) const {
        const auto it = Properties.find(InKey);
        if (it == Properties.end() || it->second.empty())
            return bInDefault;
        const std::string& v = it->second;
        if (v == "true" || v == "True" || v == "1")
            return true;
        if (v == "false" || v == "False" || v == "0")
            return false;
        return bInDefault;
    }

    void AActor::SetName(const std::string& InName) {
        UObject::SetName(InName);
        if (HasComponent<FTagComponent>()) {
            GetComponent<FTagComponent>().Tag = InName;
        } else {
            AddComponent<FTagComponent>(InName);
        }
    }

    void AActor::Destroy() {
        if (World) {
            World->DestroyActor(this);
        }
    }

    FTransformComponent& AActor::GetTransform() {
        return GetComponent<FTransformComponent>();
    }

    const FTransformComponent& AActor::GetTransform() const {
        return GetComponent<FTransformComponent>();
    }

    void AActor::SetRootComponent(USceneComponent* NewRoot) {
        if (RootComponent == NewRoot)
            return;
        // Previous root stays owned by the actor component list; only the pointer changes.
        RootComponent = NewRoot;
        if (RootComponent) {
            // Invariant: root relative transform is identity (actor transform owns world pose).
            RootComponent->SetRelativeLocation(glm::vec3(0.0f));
            RootComponent->SetRelativeRotation(glm::vec3(0.0f));
            RootComponent->SetRegistered(true);
        }
    }

    glm::vec3 AActor::GetActorLocation() const {
        return GetTransform().Translation;
    }

    void AActor::SetActorLocation(const glm::vec3& InLocation) {
        GetTransform().Translation = InLocation;
    }

    glm::vec3 AActor::GetActorRotation() const {
        return GetTransform().Rotation;
    }

    void AActor::SetActorRotation(const glm::vec3& InRotation) {
        GetTransform().Rotation = InRotation;
    }

    glm::vec3 AActor::GetActorForwardVector() const {
        glm::vec3 dir = glm::vec3(GetTransform().GetTransform() * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f));
        float len = glm::length(dir);
        return len > 1e-6f ? dir / len : glm::vec3(0.0f, 0.0f, -1.0f);
    }

    glm::vec3 AActor::GetActorRightVector() const {
        glm::vec3 dir = glm::vec3(GetTransform().GetTransform() * glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
        float len = glm::length(dir);
        return len > 1e-6f ? dir / len : glm::vec3(1.0f, 0.0f, 0.0f);
    }

    glm::vec3 AActor::GetActorUpVector() const {
        glm::vec3 dir = glm::vec3(GetTransform().GetTransform() * glm::vec4(0.0f, 1.0f, 0.0f, 0.0f));
        float len = glm::length(dir);
        return len > 1e-6f ? dir / len : glm::vec3(0.0f, 1.0f, 0.0f);
    }

    glm::vec3 AActor::GetActorScale() const {
        return GetTransform().Scale;
    }

    void AActor::SetActorScale(const glm::vec3& InScale) {
        GetTransform().Scale = InScale;
    }

    void AActor::AttachToActor(AActor* InParent) {
        if (InParent == this || InParent == ParentActor)
            return;
        // Refuse cycles: attaching to one of our own descendants would orphan the whole chain.
        for (AActor* ancestor = InParent; ancestor; ancestor = ancestor->ParentActor) {
            if (ancestor == this)
                return;
        }
        if (ParentActor) {
            DetachFromActor();
        }
        if (InParent) {
            ParentActor = InParent;
            InParent->ChildActors.push_back(this);
        }
    }

    void AActor::DetachFromActor() {
        if (!ParentActor)
            return;
        auto& children = ParentActor->ChildActors;
        children.erase(std::remove(children.begin(), children.end(), this), children.end());
        ParentActor = nullptr;
    }

    glm::vec3 AActor::GetRelativeLocation() const {
        if (ParentActor) {
            return GetActorLocation() - ParentActor->GetActorLocation();
        }
        return GetActorLocation();
    }

    void AActor::SetRelativeLocation(const glm::vec3& InLocation) {
        if (ParentActor) {
            SetActorLocation(ParentActor->GetActorLocation() + InLocation);
        } else {
            SetActorLocation(InLocation);
        }
    }

    glm::vec3 AActor::GetRelativeRotation() const {
        if (ParentActor) {
            return GetActorRotation() - ParentActor->GetActorRotation();
        }
        return GetActorRotation();
    }

    void AActor::SetRelativeRotation(const glm::vec3& InRotation) {
        if (ParentActor) {
            SetActorRotation(ParentActor->GetActorRotation() + InRotation);
        } else {
            SetActorRotation(InRotation);
        }
    }

    glm::vec3 AActor::GetRelativeScale() const {
        if (ParentActor) {
            glm::vec3 parentScale = ParentActor->GetActorScale();
            return GetActorScale() / glm::max(parentScale, glm::vec3(0.001f));
        }
        return GetActorScale();
    }

    void AActor::SetRelativeScale(const glm::vec3& InScale) {
        if (ParentActor) {
            SetActorScale(ParentActor->GetActorScale() * InScale);
        } else {
            SetActorScale(InScale);
        }
    }

    glm::mat4 AActor::GetActorWorldMatrix() const {
        if (RootComponent)
            return RootComponent->GetComponentWorldMatrix();
        if (HasComponent<FTransformComponent>())
            return GetTransform().GetTransform();
        return glm::mat4(1.0f);
    }

} // namespace Leon
