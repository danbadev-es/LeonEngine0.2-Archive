#include "Gameplay/ABlockingVolume.hpp"

namespace Leon {

    ABlockingVolume::ABlockingVolume(entt::entity InHandle, UWorld* InWorld, const std::string& InName)
        : AActor(InHandle, InWorld, InName) {
        SetClass("ABlockingVolume");
    }

    void ABlockingVolume::PostInitializeComponents() {
        if (!Box)
            Box = AddActorComponent<UBoxComponent>("Collision");
        Box->SetCollisionObjectType(ECollisionChannel::WorldStatic);
        Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        Box->SetCollisionResponseToAllChannels(ECollisionResponse::Block);
        // Half a metre each way; the box component applies the actor's scale on top.
        Box->SetBoxExtent(glm::vec3(0.5f));
        SetCanEverTick(false);
    }

} // namespace Leon
