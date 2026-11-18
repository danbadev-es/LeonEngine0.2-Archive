#pragma once

#include "Gameplay/AActor.hpp"
#include "Gameplay/UPrimitiveComponent.hpp"

namespace Leon {

    /**
     * An invisible box that blocks movement and traces: the collision of walls, floors and anything
     * else that is part of the level.
     *
     * The box is one metre a side and takes the actor's transform, so the actor's scale is its size in
     * metres, and moving, turning or scaling the actor is all there is to edit. Meshes whose collision
     * is switched off (FStaticMeshComponent::bCollision) are then decoration only.
     */
    class ABlockingVolume : public AActor {
    public:
        ABlockingVolume() = default;
        ABlockingVolume(entt::entity InHandle, UWorld* InWorld, const std::string& InName = "BlockingVolume");

        void PostInitializeComponents() override;

        TRef<UBoxComponent> GetBoxComponent() const { return Box; }

    private:
        TRef<UBoxComponent> Box;
    };

} // namespace Leon
