#include "Gameplay/AReflectionProbe.hpp"

#include "Scene/Components.hpp"

namespace Leon {

    AReflectionProbe::AReflectionProbe(entt::entity InHandle, UWorld* InWorld, const std::string& InName)
        : AActor(InHandle, InWorld, InName) {
        SetClass("AReflectionProbe");
    }

    void AReflectionProbe::PostInitializeComponents() {
        if (!HasComponent<FReflectionProbeComponent>())
            AddComponent<FReflectionProbeComponent>();
        SetCanEverTick(false);
    }

} // namespace Leon
