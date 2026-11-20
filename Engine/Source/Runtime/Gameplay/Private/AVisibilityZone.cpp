#include "Gameplay/AVisibilityZone.hpp"

#include "Scene/Components.hpp"

namespace Leon {

    AVisibilityZone::AVisibilityZone(entt::entity InHandle, UWorld* InWorld, const std::string& InName)
        : AActor(InHandle, InWorld, InName) {
        SetClass("AVisibilityZone");
    }

    void AVisibilityZone::PostInitializeComponents() {
        if (!HasComponent<FVisibilityZoneComponent>())
            AddComponent<FVisibilityZoneComponent>();
        SetCanEverTick(false);
    }

} // namespace Leon
