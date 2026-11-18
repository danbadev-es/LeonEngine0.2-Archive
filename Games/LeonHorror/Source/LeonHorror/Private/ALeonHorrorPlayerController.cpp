#include "LeonHorror/ALeonHorrorPlayerController.hpp"

#include "LeonHorror/ALeonHorrorCharacter.hpp"
#include "LeonHorror/ALeonHorrorGameMode.hpp"

#include "Core/FApplication.hpp"
#include "Platform/FInput.hpp"
#include "Platform/FInputSettings.hpp"
#include "Engine/UWorld.hpp"

namespace Leon {

    ALeonHorrorPlayerController::ALeonHorrorPlayerController(entt::entity InHandle, UWorld* InWorld,
                                                             const std::string& InName)
        : APlayerController(InHandle, InWorld, InName) {
        SetClass("ALeonHorrorPlayerController");
    }

    void ALeonHorrorPlayerController::Tick(float DeltaSeconds) {
        APlayerController::Tick(DeltaSeconds);
        if (FApplication::HasInstance())
            ApplyMenuInput(SampleMenuInput());
    }

    FLeonHorrorMenuInput ALeonHorrorPlayerController::SampleMenuInput() {
        const FInputSettings& settings = FInputSettings::Get();
        bool bInventory = FInput::IsKeyPressed(Key::Tab);
        bool bPause = FInput::IsKeyPressed(Key::Escape);
        bool bConfirm = FInput::IsKeyPressed(Key::E) || FInput::IsKeyPressed(Key::Enter);
        bool bMedkit = FInput::IsKeyPressed(Key::H);
        const bool bLoad = FInput::IsKeyPressed(Key::L);
        const bool bQuit = FInput::IsKeyPressed(Key::Q);
        if (settings.bEnableGamepad && FInput::IsGamepadConnected(settings.GamepadId)) {
            const int pad = settings.GamepadId;
            bInventory = bInventory || FInput::IsGamepadButtonPressed(GamepadButton::Back, pad);
            bPause = bPause || FInput::IsGamepadButtonPressed(GamepadButton::Start, pad);
            bConfirm = bConfirm || FInput::IsGamepadButtonPressed(GamepadButton::A, pad);
            bMedkit = bMedkit || FInput::IsGamepadButtonPressed(GamepadButton::DPadUp, pad);
        }

        FLeonHorrorMenuInput input;
        input.bInventoryPressed = bInventory && !bInventoryWasDown;
        input.bPausePressed = bPause && !bPauseWasDown;
        input.bConfirmPressed = bConfirm && !bConfirmWasDown;
        input.bUseMedkitPressed = bMedkit && !bMedkitWasDown;
        bInventoryWasDown = bInventory;
        bPauseWasDown = bPause;
        bConfirmWasDown = bConfirm;
        bMedkitWasDown = bMedkit;
        input.bLoadPressed = bLoad && !bLoadWasDown;
        input.bQuitPressed = bQuit && !bQuitWasDown;
        bLoadWasDown = bLoad;
        bQuitWasDown = bQuit;
        return input;
    }

    void ALeonHorrorPlayerController::ApplyMenuInput(const FLeonHorrorMenuInput& InInput) {
        auto* gameMode = World ? dynamic_cast<ALeonHorrorGameMode*>(World->GetGameMode()) : nullptr;
        if (!gameMode)
            return;
        auto* player = dynamic_cast<ALeonHorrorCharacter*>(GetPawn());

        switch (gameMode->GetActiveScreen()) {
        case ELeonHorrorScreen::Note:
            if (InInput.bConfirmPressed || InInput.bPausePressed || InInput.bInventoryPressed)
                gameMode->DismissNote();
            break;
        case ELeonHorrorScreen::Pause:
            if (InInput.bPausePressed || InInput.bConfirmPressed) {
                gameMode->SetPauseMenuOpen(false);
            } else if (InInput.bLoadPressed) {
                gameMode->SetPauseMenuOpen(false);
                gameMode->LoadLastSave();
            } else if (InInput.bQuitPressed) {
                gameMode->RequestQuit();
            }
            break;
        case ELeonHorrorScreen::Inventory:
            if (InInput.bInventoryPressed || InInput.bPausePressed)
                gameMode->SetInventoryOpen(false);
            else if (InInput.bUseMedkitPressed && player)
                gameMode->UseMedkit(*player);
            break;
        case ELeonHorrorScreen::Dead:
        case ELeonHorrorScreen::Completed:
            if (InInput.bConfirmPressed)
                gameMode->ConfirmEndScreen();
            break;
        case ELeonHorrorScreen::None:
            if (InInput.bInventoryPressed)
                gameMode->SetInventoryOpen(true);
            else if (InInput.bPausePressed)
                gameMode->SetPauseMenuOpen(true);
            else if (InInput.bUseMedkitPressed && player)
                gameMode->UseMedkit(*player);
            break;
        }
    }

} // namespace Leon
