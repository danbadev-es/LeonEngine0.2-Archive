#include "LeonHorror/ALeonHorrorGameMode.hpp"

#include "LeonHorror/ALeonHorrorCharacter.hpp"
#include "LeonHorror/ALeonHorrorInteractable.hpp"
#include "LeonHorror/ULeonHorrorInventoryComponent.hpp"
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
        PlayerControllerClass = "ALeonHorrorPlayerController";
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
        RefreshPause(); // reading takes no game time
    }

    void ALeonHorrorGameMode::DismissNote() {
        if (!bNoteOpen)
            return;
        bNoteOpen = false;
        RefreshPause();
    }

    ELeonHorrorScreen ALeonHorrorGameMode::GetActiveScreen() const {
        if (bSliceCompleted)
            return ELeonHorrorScreen::Completed;
        if (bPlayerDied)
            return ELeonHorrorScreen::Dead;
        if (bNoteOpen)
            return ELeonHorrorScreen::Note;
        if (bPauseMenuOpen)
            return ELeonHorrorScreen::Pause;
        if (bInventoryOpen)
            return ELeonHorrorScreen::Inventory;
        return ELeonHorrorScreen::None;
    }

    void ALeonHorrorGameMode::RefreshPause() {
        if (World)
            World->SetPaused(bNoteOpen || bInventoryOpen || bPauseMenuOpen || bSliceCompleted);
    }

    void ALeonHorrorGameMode::SetInventoryOpen(bool bInOpen) {
        bInventoryOpen = bInOpen && !bPlayerDied && !bSliceCompleted;
        RefreshPause();
    }

    void ALeonHorrorGameMode::SetPauseMenuOpen(bool bInOpen) {
        bPauseMenuOpen = bInOpen && !bSliceCompleted;
        RefreshPause();
    }

    void ALeonHorrorGameMode::ConfirmEndScreen() {
        ++EndScreenConfirms;
    }

    bool ALeonHorrorGameMode::UseMedkit(ALeonHorrorCharacter& InPlayer) {
        auto* inventory = InPlayer.GetInventory();
        UHealthComponent* health = InPlayer.GetHealth();
        if (!inventory || !health || InPlayer.IsDead())
            return false;
        if (health->GetHealth() >= health->GetMaxHealth())
            return false; // do not waste one at full health
        if (!inventory->RemoveItem(LeonHorrorItems::Medkit))
            return false;
        health->Heal(MedkitHealAmount);
        return true;
    }

    bool ALeonHorrorGameMode::RequestSave(ALeonHorrorCharacter& InPlayer) {
        (void)InPlayer;
        ++SaveCount;
        return true;
    }

    void ALeonHorrorGameMode::NotifySliceCompleted() {
        bSliceCompleted = true;
        RefreshPause();
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
