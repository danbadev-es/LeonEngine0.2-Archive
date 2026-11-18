#pragma once

#include "Gameplay/AActor.hpp"

namespace Leon {

    /**
     * The player's flashlight: a movable spot light on its own actor.
     *
     * It is a separate actor because the renderer aims a spot light with the actor's own rotation, and a
     * character only yaws. The owner repositions it every tick from its view point; actor attachment
     * does not propagate transforms. It should be the only non-static spot light in a level: the
     * renderer shadows a single spot.
     */
    class ALeonHorrorFlashlight : public AActor {
    public:
        ALeonHorrorFlashlight() = default;
        ALeonHorrorFlashlight(entt::entity InHandle, UWorld* InWorld, const std::string& InName = "Flashlight");

        void PostInitializeComponents() override;

        void SetOn(bool bInOn);
        bool IsOn() const { return bOn; }
        void Toggle() { SetOn(!bOn); }

        /** Places the light just beside the eye, pointing along the view direction. */
        void UpdateFromView(const glm::vec3& InEyeLocation, const glm::vec3& InForward);

        glm::vec3 GetLightDirection() const;
        glm::vec3 GetLightPosition() const;

    private:
        bool bOn = true;
    };

} // namespace Leon
