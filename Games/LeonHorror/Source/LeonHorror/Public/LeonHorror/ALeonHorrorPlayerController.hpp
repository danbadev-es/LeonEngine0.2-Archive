#pragma once

#include "Gameplay/APlayerController.hpp"

namespace Leon {

    /** One frame of menu intent ("Pressed" = edge). Separate from character input on purpose. */
    struct FLeonHorrorMenuInput {
        bool bInventoryPressed = false;
        bool bPausePressed = false;
        /** Accept / dismiss: E, Enter or the gamepad A button. */
        bool bConfirmPressed = false;
        bool bUseMedkitPressed = false;
        /** Pause menu only. */
        bool bLoadPressed = false;
        bool bQuitPressed = false;
    };

    /**
     * Owns the keys that open and close screens. The character does not tick while the world is
     * paused, so it could never close the inventory it opened; player controllers keep ticking.
     */
    class ALeonHorrorPlayerController : public APlayerController {
    public:
        ALeonHorrorPlayerController() = default;
        ALeonHorrorPlayerController(entt::entity InHandle, UWorld* InWorld,
                                    const std::string& InName = "LeonHorrorPlayerController");

        void Tick(float DeltaSeconds) override;

        /** Acts on one frame of menu intent. Hardware input and tests both end up here. */
        void ApplyMenuInput(const FLeonHorrorMenuInput& InInput);

    private:
        FLeonHorrorMenuInput SampleMenuInput();

        bool bInventoryWasDown = false;
        bool bPauseWasDown = false;
        bool bConfirmWasDown = false;
        bool bMedkitWasDown = false;
        bool bLoadWasDown = false;
        bool bQuitWasDown = false;
    };

} // namespace Leon
