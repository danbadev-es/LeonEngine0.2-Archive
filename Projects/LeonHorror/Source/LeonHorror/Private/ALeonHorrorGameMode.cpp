#include "LeonHorror/ALeonHorrorGameMode.hpp"

#include "LeonHorror/ALeonHorrorCharacter.hpp"
#include "LeonHorror/ALeonHorrorInteractable.hpp"
#include "LeonHorror/ULeonHorrorGameInstance.hpp"

#include "Core/FApplication.hpp"
#include "Core/FLog.hpp"
#include "Engine/UEngine.hpp"
#include "Engine/UWorld.hpp"
#include "Gameplay/APlayerController.hpp"

namespace Leon {

    ALeonHorrorGameMode::ALeonHorrorGameMode(entt::entity InHandle, UWorld* InWorld, const std::string& InName)
        : AGameModeBase(InHandle, InWorld, InName) {
        SetClass("ALeonHorrorGameMode");
        DefaultPawnClass = "ALeonHorrorCharacter";
        PlayerControllerClass = "APlayerController";
        HUDClass = "AHUD";
        GameStateClass = "AGameStateBase";
        PlayerStateClass = "APlayerState";
        MaxPlayers = 1;
        SetCanEverTick(true);
    }

    void ALeonHorrorGameMode::Tick(float DeltaSeconds) {
        AGameModeBase::Tick(DeltaSeconds);
        ElapsedPlaySeconds += DeltaSeconds;
        TickSmokeRun();
    }

    ALeonHorrorCharacter* ALeonHorrorGameMode::GetPlayerCharacter() const {
        APlayerController* pc = World ? World->GetFirstPlayerController() : nullptr;
        return pc ? dynamic_cast<ALeonHorrorCharacter*>(pc->GetPawn()) : nullptr;
    }

    void ALeonHorrorGameMode::NotifyPlayerDied(ALeonHorrorCharacter& InCharacter) {
        (void)InCharacter;
        bPlayerDied = true;
        LE_CORE_INFO("LeonHorror: the player died.");
    }

    void ALeonHorrorGameMode::NotifyPickupCollected(const ALeonHorrorPickup& InPickup) {
        CollectedPickups.insert(InPickup.GetActorGuid().ToString());
    }

    void ALeonHorrorGameMode::NotifyDoorChanged(const ALeonHorrorDoor& InDoor) {
        DoorStates[InDoor.GetActorGuid().ToString()] = {InDoor.IsLocked(), InDoor.IsOpen()};
    }

    void ALeonHorrorGameMode::ShowNote(const ALeonHorrorNote& InNote) {
        NoteTitle = InNote.GetTitle();
        NoteText = InNote.GetText();
        bNoteOpen = true;
        ReadNotes.insert(InNote.GetActorGuid().ToString());
        if (World)
            World->SetPaused(true); // reading takes no game time
    }

    void ALeonHorrorGameMode::DismissNote() {
        if (!bNoteOpen)
            return;
        bNoteOpen = false;
        if (World)
            World->SetPaused(false);
    }

    bool ALeonHorrorGameMode::RequestSave(ALeonHorrorCharacter& InPlayer) {
        (void)InPlayer;
        ++SaveCount;
        return true;
    }

    void ALeonHorrorGameMode::NotifySliceCompleted() {
        bSliceCompleted = true;
        LE_CORE_INFO("LeonHorror: slice completed.");
    }

    void ALeonHorrorGameMode::TickSmokeRun() {
        // Only meaningful in the real executable; headless tests have no application or engine.
        if (bSmokeRunFinished || !FApplication::HasInstance())
            return;
        auto instance = std::dynamic_pointer_cast<ULeonHorrorGameInstance>(UEngine::Get().GetGameInstance());
        if (!instance || instance->GetValidateSeconds() <= 0.0f)
            return;
        if (ElapsedPlaySeconds < instance->GetValidateSeconds())
            return;
        bSmokeRunFinished = true;
        LE_CORE_INFO("LeonHorror: smoke run finished after {0} s, closing.", ElapsedPlaySeconds);
        FApplication::Get().Close();
    }

} // namespace Leon
