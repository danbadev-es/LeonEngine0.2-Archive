#include "Gameplay/APortal.hpp"

#include "Scene/Components.hpp"

namespace Leon {

    APortal::APortal(entt::entity InHandle, UWorld* InWorld, const std::string& InName)
        : AActor(InHandle, InWorld, InName) {
        SetClass("APortal");
    }

    void APortal::PostInitializeComponents() {
        if (!HasComponent<FPortalComponent>())
            AddComponent<FPortalComponent>();
        SetCanEverTick(false);
    }

} // namespace Leon
