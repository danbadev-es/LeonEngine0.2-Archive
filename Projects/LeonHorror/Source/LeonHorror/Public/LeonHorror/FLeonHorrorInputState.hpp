#pragma once

#include <glm/glm.hpp>

namespace Leon {

    /**
     * One frame of player intent, independent of the hardware it came from.
     *
     * The character samples the keyboard, mouse and gamepad into this struct and then acts on it in
     * ApplyInputState. Tests build the struct directly, so gameplay can be driven without a window.
     * "Pressed" fields are edges (true only on the frame the key went down); "Held" fields are levels.
     */
    struct FLeonHorrorInputState {
        /** x = strafe right, y = forward; each in [-1, 1]. */
        glm::vec2 Move{0.0f};
        /** Degrees to add to control yaw (x) and pitch (y) this frame. */
        glm::vec2 LookDelta{0.0f};
        bool bSprintHeld = false;
        bool bAimHeld = false;
        bool bAttackPressed = false;
        bool bInteractPressed = false;
        bool bFlashlightPressed = false;
        bool bReloadPressed = false;
        bool bSelectMeleePressed = false;
        bool bSelectPistolPressed = false;
    };

    /** Reads the hardware into an input state, turning key levels into press edges. */
    class FLeonHorrorInputSampler {
    public:
        FLeonHorrorInputState Sample();

    private:
        struct FEdge {
            bool bWasDown = false;
            bool Update(bool bInDown) {
                const bool bPressed = bInDown && !bWasDown;
                bWasDown = bInDown;
                return bPressed;
            }
        };
        FEdge Attack, Interact, Flashlight, Reload, SelectMelee, SelectPistol;
    };

} // namespace Leon
