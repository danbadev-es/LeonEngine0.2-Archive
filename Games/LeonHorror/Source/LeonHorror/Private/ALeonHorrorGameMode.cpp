#include "LeonHorror/ALeonHorrorGameMode.hpp"

#include "LeonHorror/ALeonHorrorCharacter.hpp"
#include "LeonHorror/ALeonHorrorEnemy.hpp"
#include "LeonHorror/ALeonHorrorFlashlight.hpp"
#include "LeonHorror/ALeonHorrorInteractable.hpp"
#include "LeonHorror/ULeonHorrorCombatComponent.hpp"
#include "LeonHorror/ULeonHorrorInventoryComponent.hpp"
#include "LeonHorror/ULeonHorrorGameInstance.hpp"

#include "Core/FApplication.hpp"
#include "Core/FLog.hpp"
#include "Core/FProjectPaths.hpp"
#include "Engine/UEngine.hpp"
#include "Engine/UWorld.hpp"
#include "Gameplay/APlayerController.hpp"
#include "Gameplay/UCharacterMovementComponent.hpp"
#include "Gameplay/UGameplayStatics.hpp"

#include <filesystem>

namespace Leon {

    ALeonHorrorGameMode::ALeonHorrorGameMode(entt::entity InHandle, UWorld* InWorld, const std::string& InName)
        : AGameModeBase(InHandle, InWorld, InName) {
        SetClass("ALeonHorrorGameMode");
        DefaultPawnClass = "ALeonHorrorCharacter";
        PlayerControllerClass = "ALeonHorrorPlayerController";
        HUDClass = "ALeonHorrorHUD";
        GameStateClass = "AGameStateBase";
        PlayerStateClass = "APlayerState";
        MaxPlayers = 1;
        SetCanEverTick(true);
    }

    void ALeonHorrorGameMode::Tick(float DeltaSeconds) {
        AGameModeBase::Tick(DeltaSeconds);
        // First tick after the map began play: every actor exists, so a save can be applied.
        if (!bPendingSaveChecked) {
            bPendingSaveChecked = true;
            // The engine does not build navigation by itself. Without a bounds volume this does
            // nothing and enemies walk straight at their goal.
            if (World)
                World->RebuildNavigation();
            ApplyPendingSaveFromInstance();
        }
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

    void ALeonHorrorGameMode::NotifyEnemyKilled(const ALeonHorrorEnemy& InEnemy) {
        if (!InEnemy.GetSpawnerGuid().empty())
            DeadEnemies.insert(InEnemy.GetSpawnerGuid());
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
        // Dead: back to the last save (or the start if there is none). Finished: start over.
        if (bSliceCompleted)
            RestartMap();
        else if (bPlayerDied)
            LoadLastSave();
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

    std::string ALeonHorrorGameMode::GetSaveFilePath() const {
        if (!SaveFilePath.empty())
            return SaveFilePath;
        return (std::filesystem::path(FProjectPaths::ProjectSavedDir()) / "SaveGames" / "Slot0.lsav").string();
    }

    bool ALeonHorrorGameMode::HasSaveFile() const {
        std::error_code ec;
        return std::filesystem::exists(GetSaveFilePath(), ec);
    }

    bool ALeonHorrorGameMode::RequestSave(ALeonHorrorCharacter& InPlayer) {
        if (InPlayer.IsDead())
            return false;
        const FLeonHorrorSaveGame save = CaptureSave(InPlayer);
        if (!save.SaveToFile(GetSaveFilePath())) {
            LE_CORE_ERROR("LeonHorror: could not write the save file '{0}'", GetSaveFilePath());
            return false;
        }
        ++SaveCount;
        LE_CORE_INFO("LeonHorror: game saved to '{0}'", GetSaveFilePath());
        return true;
    }

    FLeonHorrorSaveGame ALeonHorrorGameMode::CaptureSave(const ALeonHorrorCharacter& InPlayer) const {
        FLeonHorrorSaveGame save;
        save.MapName = GetCurrentMapPath();
        save.PlayerLocation = InPlayer.GetActorLocation();
        save.PlayerYaw = InPlayer.GetControlYaw();
        save.PlayerPitch = InPlayer.GetControlPitch();
        if (const UHealthComponent* health = InPlayer.GetHealth())
            save.Health = health->GetHealth();
        save.Stamina = InPlayer.GetStamina();
        save.bFlashlightOn = InPlayer.IsFlashlightOn();
        if (const auto* combat = InPlayer.GetCombat()) {
            save.EquippedWeapon = static_cast<int32_t>(combat->GetEquipped());
            save.LoadedRounds = combat->GetLoadedRounds();
        }
        if (const auto* inventory = InPlayer.GetInventory())
            save.Items = inventory->GetItems();
        save.CollectedPickups = CollectedPickups;
        for (const auto& [guid, door] : DoorStates)
            save.Doors[guid] = {door.bLocked, door.bOpen};
        save.DeadEnemies = DeadEnemies;
        save.ReadNotes = ReadNotes;
        return save;
    }

    void ALeonHorrorGameMode::ApplySave(const FLeonHorrorSaveGame& InSave) {
        if (!World)
            return;

        CollectedPickups = InSave.CollectedPickups;
        DeadEnemies = InSave.DeadEnemies;
        ReadNotes = InSave.ReadNotes;
        DoorStates.clear();
        for (const auto& [guid, door] : InSave.Doors)
            DoorStates[guid] = {door.bLocked, door.bOpen};

        // Snapshot: applying destroys actors.
        const std::vector<TRef<AActor>> actors = World->GetAllActors();
        for (const auto& actorRef : actors) {
            AActor* actor = actorRef.get();
            if (!actor || actor->IsPendingKill())
                continue;
            const std::string guid = actor->GetActorGuid().ToString();
            if (auto* pickup = dynamic_cast<ALeonHorrorPickup*>(actor)) {
                if (CollectedPickups.count(guid) > 0)
                    pickup->Destroy();
            } else if (auto* door = dynamic_cast<ALeonHorrorDoor*>(actor)) {
                const auto found = DoorStates.find(guid);
                if (found != DoorStates.end())
                    door->SetState(found->second.bLocked, found->second.bOpen);
            } else if (auto* enemy = dynamic_cast<ALeonHorrorEnemy*>(actor)) {
                // Spawners have already run: take back the enemies that were dead in the save.
                if (DeadEnemies.count(enemy->GetSpawnerGuid()) > 0) {
                    if (AController* controller = enemy->GetController())
                        controller->Destroy();
                    enemy->Destroy();
                }
            }
        }

        if (ALeonHorrorCharacter* player = GetPlayerCharacter()) {
            player->SetActorLocation(InSave.PlayerLocation);
            player->SetControlRotation({InSave.PlayerPitch, InSave.PlayerYaw, 0.0f});
            if (auto movement = player->GetCharacterMovement())
                movement->StopMovementImmediately();
            if (UHealthComponent* health = player->GetHealth())
                health->SetHealth(InSave.Health);
            player->SetStamina(InSave.Stamina);
            if (auto* inventory = player->GetInventory()) {
                inventory->Clear();
                for (const auto& [item, count] : InSave.Items)
                    inventory->SetCount(item, count);
            }
            if (auto* combat = player->GetCombat()) {
                combat->Equip(static_cast<ELeonHorrorWeapon>(InSave.EquippedWeapon));
                combat->SetLoadedRounds(InSave.LoadedRounds);
            }
            if (ALeonHorrorFlashlight* light = player->GetFlashlight())
                light->SetOn(InSave.bFlashlightOn);
        }
        LE_CORE_INFO("LeonHorror: save applied ({0} pickups taken, {1} doors, {2} enemies dead)",
                     CollectedPickups.size(), DoorStates.size(), DeadEnemies.size());
    }

    void ALeonHorrorGameMode::ApplyPendingSaveFromInstance() {
        if (!UEngine::HasInstance())
            return;
        auto instance = std::dynamic_pointer_cast<ULeonHorrorGameInstance>(UEngine::Get().GetGameInstance());
        if (!instance)
            return;
        if (auto save = instance->TakePendingSave())
            ApplySave(*save);
    }

    bool ALeonHorrorGameMode::LoadLastSave() {
        FLeonHorrorSaveGame save;
        if (!FLeonHorrorSaveGame::LoadFromFile(GetSaveFilePath(), save))
            return RestartMap();
        LastTravelRequest = ELeonHorrorTravel::LoadSave;
        if (!UEngine::HasInstance() || !World)
            return false;
        auto instance = std::dynamic_pointer_cast<ULeonHorrorGameInstance>(UEngine::Get().GetGameInstance());
        if (!instance)
            return false;
        // Reload the map so everything destroyed since the save exists again, then apply the save.
        instance->SetPendingSave(save);
        std::string map = save.MapName.empty() ? GetCurrentMapPath() : save.MapName;
        if (map.empty() || map[0] != '/')
            map = "/Game/Maps/" + map;
        UGameplayStatics::OpenLevel(World, map);
        return true;
    }

    bool ALeonHorrorGameMode::RestartMap() {
        LastTravelRequest = ELeonHorrorTravel::Restart;
        if (!UEngine::HasInstance() || !World)
            return false;
        UGameplayStatics::OpenLevel(World, GetCurrentMapPath());
        return true;
    }

    std::string ALeonHorrorGameMode::GetCurrentMapPath() const {
        if (UEngine::HasInstance() && !UEngine::Get().GetCurrentMapName().empty())
            return UEngine::Get().GetCurrentMapName();
        return World ? World->GetName() : std::string();
    }

    void ALeonHorrorGameMode::RequestQuit() {
        bQuitRequested = true;
        if (FApplication::HasInstance())
            FApplication::Get().Close();
    }

    void ALeonHorrorGameMode::NotifySliceCompleted() {
        bSliceCompleted = true;
        RefreshPause();
        LE_CORE_INFO("LeonHorror: slice completed.");
    }

    void ALeonHorrorGameMode::TickSmokeRun() {
        // Only meaningful in the real executable; headless tests have no application or engine.
        if (bSmokeRunFinished || !FApplication::HasInstance() || !UEngine::HasInstance())
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
