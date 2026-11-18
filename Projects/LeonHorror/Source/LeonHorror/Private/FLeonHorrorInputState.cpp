#include "LeonHorror/FLeonHorrorInputState.hpp"

#include "Core/FInput.hpp"
#include "Core/FInputSettings.hpp"

namespace Leon {

    FLeonHorrorInputState FLeonHorrorInputSampler::Sample() {
        const FInputSettings& settings = FInputSettings::Get();
        FLeonHorrorInputState state;

        if (FInput::IsKeyPressed(settings.MoveForwardKey))
            state.Move.y += 1.0f;
        if (FInput::IsKeyPressed(settings.MoveBackwardKey))
            state.Move.y -= 1.0f;
        if (FInput::IsKeyPressed(settings.MoveRightKey))
            state.Move.x += 1.0f;
        if (FInput::IsKeyPressed(settings.MoveLeftKey))
            state.Move.x -= 1.0f;

        bool bPadAttack = false;
        bool bPadAim = false;
        bool bPadInteract = false;
        bool bPadReload = false;
        bool bPadFlashlight = false;
        bool bPadSprint = false;
        if (settings.bEnableGamepad && FInput::IsGamepadConnected(settings.GamepadId)) {
            const int pad = settings.GamepadId;
            auto [lx, ly] = FInput::GetGamepadLeftStick(pad, settings.GamepadDeadzone);
            state.Move.x += lx;
            state.Move.y -= ly; // stick up is negative
            const float threshold = settings.GamepadTriggerThreshold;
            bPadAttack = FInput::GetGamepadTrigger(GamepadAxis::RightTrigger, pad, threshold) > 0.0f;
            bPadAim = FInput::GetGamepadTrigger(GamepadAxis::LeftTrigger, pad, threshold) > 0.0f;
            bPadInteract = FInput::IsGamepadButtonPressed(GamepadButton::A, pad);
            bPadReload = FInput::IsGamepadButtonPressed(GamepadButton::X, pad);
            bPadFlashlight = FInput::IsGamepadButtonPressed(GamepadButton::Y, pad);
            bPadSprint = FInput::IsGamepadButtonPressed(GamepadButton::LeftBumper, pad);
        }

        state.bSprintHeld = FInput::IsKeyPressed(settings.SprintKey) || bPadSprint;
        state.bAimHeld = FInput::IsMouseButtonPressed(Mouse::ButtonRight) || bPadAim;
        state.bAttackPressed = Attack.Update(FInput::IsMouseButtonPressed(Mouse::ButtonLeft) || bPadAttack);
        state.bInteractPressed = Interact.Update(FInput::IsKeyPressed(Key::E) || bPadInteract);
        state.bFlashlightPressed = Flashlight.Update(FInput::IsKeyPressed(Key::F) || bPadFlashlight);
        state.bReloadPressed = Reload.Update(FInput::IsKeyPressed(Key::R) || bPadReload);
        state.bSelectMeleePressed = SelectMelee.Update(FInput::IsKeyPressed(Key::D1));
        state.bSelectPistolPressed = SelectPistol.Update(FInput::IsKeyPressed(Key::D2));
        return state;
    }

} // namespace Leon
