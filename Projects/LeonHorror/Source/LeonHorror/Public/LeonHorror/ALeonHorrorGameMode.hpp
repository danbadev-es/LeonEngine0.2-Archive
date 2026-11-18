#pragma once

#include "Gameplay/AGameModeBase.hpp"

#include "LeonHorror/FLeonHorrorSaveGame.hpp"

#include <map>
#include <set>
#include <string>

namespace Leon {

    class ALeonHorrorCharacter;
    class ALeonHorrorDoor;
    class ALeonHorrorEnemy;
    class ALeonHorrorNote;
    class ALeonHorrorPickup;

    /** The one full-screen state the player is in, highest priority first when several apply. */
    enum class ELeonHorrorScreen : uint8_t { None, Inventory, Pause, Note, Dead, Completed };

    /** Lock and open state of a door, as stored in a save. */
    struct FLeonHorrorDoorState {
        bool bLocked = false;
        bool bOpen = false;
    };

    /**
     * Single-player rules for the horror slice: one player, spawned at the map's PlayerStart,
     * playing immediately (no lobby, no match state).
     */
    class ALeonHorrorGameMode : public AGameModeBase {
    public:
        ALeonHorrorGameMode() = default;
        ALeonHorrorGameMode(entt::entity InHandle, UWorld* InWorld, const std::string& InName = "LeonHorrorGameMode");

        void Tick(float DeltaSeconds) override;

        float GetElapsedPlaySeconds() const { return ElapsedPlaySeconds; }

        /** The character controlled by the (single) player, or null before login or after travel. */
        ALeonHorrorCharacter* GetPlayerCharacter() const;

        /** Called by the player character when its health reaches zero. */
        void NotifyPlayerDied(ALeonHorrorCharacter& InCharacter);
        bool HasPlayerDied() const { return bPlayerDied; }

        // --- World state reported by interactables, keyed by actor GUID so it can be saved ---
        void NotifyPickupCollected(const ALeonHorrorPickup& InPickup);
        void NotifyDoorChanged(const ALeonHorrorDoor& InDoor);
        const std::set<std::string>& GetCollectedPickups() const { return CollectedPickups; }
        const std::map<std::string, FLeonHorrorDoorState>& GetDoorStates() const { return DoorStates; }
        const std::set<std::string>& GetReadNotes() const { return ReadNotes; }
        /** Enemies are recorded by the GUID of the spawner that created them. */
        void NotifyEnemyKilled(const ALeonHorrorEnemy& InEnemy);
        void MarkEnemyDead(const std::string& InSpawnerGuid) { DeadEnemies.insert(InSpawnerGuid); }
        const std::set<std::string>& GetDeadEnemies() const { return DeadEnemies; }

        /** Opens a note for reading; the HUD shows it and the world pauses until it is dismissed. */
        void ShowNote(const ALeonHorrorNote& InNote);
        void DismissNote();
        bool IsNoteOpen() const { return bNoteOpen; }
        const std::string& GetNoteTitle() const { return NoteTitle; }
        const std::string& GetNoteText() const { return NoteText; }

        /** Asked by a save point. Writes the save file; returns true when it was written. */
        bool RequestSave(ALeonHorrorCharacter& InPlayer);
        int32_t GetSaveCount() const { return SaveCount; }

        /** Snapshot of the player and of the world differences recorded so far. */
        FLeonHorrorSaveGame CaptureSave(const ALeonHorrorCharacter& InPlayer) const;
        /**
         * Applies a save to a freshly loaded map: removes collected pickups and dead enemies, sets the
         * doors, and restores the player. Must run after the world has begun play.
         */
        void ApplySave(const FLeonHorrorSaveGame& InSave);

        /** File used by RequestSave and LoadLastSave. Defaults to <Project>/Saved/SaveGames/Slot0.lsav. */
        void SetSaveFilePath(const std::string& InPath) { SaveFilePath = InPath; }
        std::string GetSaveFilePath() const;
        bool HasSaveFile() const;
        /**
         * Reloads the map and applies the last save (or restarts the map when there is none).
         * Needs the running engine; returns false in headless runs.
         */
        bool LoadLastSave();
        /** Reloads the map as authored, discarding progress. Needs the running engine. */
        bool RestartMap();

        void NotifySliceCompleted();
        bool IsSliceCompleted() const { return bSliceCompleted; }

        // --- Screens. The world is paused while a note, the inventory, the pause menu or the end
        //     screen is up; death keeps running so the fall plays out. ---
        ELeonHorrorScreen GetActiveScreen() const;
        void SetInventoryOpen(bool bInOpen);
        bool IsInventoryOpen() const { return bInventoryOpen; }
        void SetPauseMenuOpen(bool bInOpen);
        bool IsPauseMenuOpen() const { return bPauseMenuOpen; }
        /** Confirm on the death or completion screen. */
        void ConfirmEndScreen();
        int32_t GetEndScreenConfirmCount() const { return EndScreenConfirms; }

        /** Spends one first aid kit to restore health. False when none is held or it is not needed. */
        bool UseMedkit(ALeonHorrorCharacter& InPlayer);
        static constexpr float MedkitHealAmount = 50.0f;

    private:
        void TickSmokeRun();

        float ElapsedPlaySeconds = 0.0f;
        bool bSmokeRunFinished = false;
        bool bPlayerDied = false;

        std::set<std::string> CollectedPickups;
        std::map<std::string, FLeonHorrorDoorState> DoorStates;
        std::set<std::string> ReadNotes;
        std::set<std::string> DeadEnemies;
        bool bNoteOpen = false;
        std::string NoteTitle;
        std::string NoteText;
        int32_t SaveCount = 0;
        bool bSliceCompleted = false;
        bool bInventoryOpen = false;
        bool bPauseMenuOpen = false;
        int32_t EndScreenConfirms = 0;

        void RefreshPause();
        void ApplyPendingSaveFromInstance();

        std::string SaveFilePath;
        bool bPendingSaveChecked = false;
    };

} // namespace Leon
