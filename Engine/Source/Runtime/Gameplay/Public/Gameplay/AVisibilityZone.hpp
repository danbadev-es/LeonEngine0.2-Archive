#pragma once

#include "Gameplay/AActor.hpp"

namespace Leon {

    /**
     * A room of the level for visibility: an axis-aligned box around the actor's location (its
     * FVisibilityZoneComponent, Scene/Components.hpp). Zones and the portals between them let the
     * renderer leave out what the camera cannot see and the lights cannot reach.
     */
    class AVisibilityZone : public AActor {
    public:
        AVisibilityZone() = default;
        AVisibilityZone(entt::entity InHandle, UWorld* InWorld, const std::string& InName = "VisibilityZone");

        void PostInitializeComponents() override;
    };

} // namespace Leon
