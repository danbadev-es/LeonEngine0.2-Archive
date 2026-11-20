#pragma once

#include "Gameplay/AActor.hpp"

namespace Leon {

    /**
     * An opening between two visibility zones: a rectangle in the actor's local XY plane, centred on it
     * (its FPortalComponent, Scene/Components.hpp). Place it in the middle of the doorway, the size of
     * the opening. A door that closes it is named in the component; the game reports the door shut
     * with UWorld::SetPortalDoorClosed.
     */
    class APortal : public AActor {
    public:
        APortal() = default;
        APortal(entt::entity InHandle, UWorld* InWorld, const std::string& InName = "Portal");

        void PostInitializeComponents() override;
    };

} // namespace Leon
