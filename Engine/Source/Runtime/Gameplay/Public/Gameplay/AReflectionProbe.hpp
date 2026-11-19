#pragma once

#include "Gameplay/AActor.hpp"

namespace Leon {

    /**
     * What a room reflects: a box around the room and the point its cube map is captured from.
     * The actor's location is the centre of the box; the box is axis-aligned whatever the actor's
     * rotation. The settings are its FReflectionProbeComponent (Scene/Components.hpp); the renderer
     * captures and uses the probe by itself.
     */
    class AReflectionProbe : public AActor {
    public:
        AReflectionProbe() = default;
        AReflectionProbe(entt::entity InHandle, UWorld* InWorld, const std::string& InName = "ReflectionProbe");

        void PostInitializeComponents() override;
    };

} // namespace Leon
