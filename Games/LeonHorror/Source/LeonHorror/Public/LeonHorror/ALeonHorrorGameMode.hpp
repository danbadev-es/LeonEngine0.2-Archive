#pragma once

#include "Gameplay/AGameModeBase.hpp"
#include "Gameplay/TWeakObjectPtr.hpp"

#include "LeonHorror/FLeonHorrorSaveGame.hpp"

#include <map>
#include <set>
#include <string>

#include <vector>

namespace Leon {

    class ALeonHorrorCharacter;
    class ALeonHorrorDoor;
    class ALeonHorrorEnemy;
    class ALeonHorrorNote;
    class ALeonHorrorPickup;

    /** The one full-screen state the player is in, highest priority first when several apply. */
    enum class ELeonHorrorScreen : uint8_t { None, Inventory, Pause, Note, Dead, Completed };

    /** What the game last asked the engine to travel to. */
    enum class ELeonHorrorTravel : uint8_t { None, LoadSave, Restart };

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

        // --- Electricity: see ALeonHorrorPower.hpp ---
        /** Turns a power group on or off and applies it to everything in the map that belongs to it. */
        void SetPowerGroup(const std::string& InGroup, bool bInPowered);
        bool IsPowered(const std::string& InGroup) const;
        const std::set<std::string>& GetPoweredGroups() const { return PoweredGroups; }
        /** Looks at the map again for things that need power or flicker; for actors added during play. */
        void RescanPower() { InitialisePower(); }

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
        /** Recorded even when there is no engine to travel with (headless tests). */
        ELeonHorrorTravel GetLastTravelRequest() const { return LastTravelRequest; }

        /** Closes the game (from the pause menu). */
        void RequestQuit();
        bool IsQuitRequested() const { return bQuitRequested; }

        void NotifySliceCompleted();
        bool IsSliceCompleted() const { return bSliceCompleted; }
        /** Something is after the player and the chase track is on. */
        bool IsChaseMusicPlaying() const { return bChaseMusic; }

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
        void TickMusic(float DeltaSeconds);

        float ElapsedPlaySeconds = 0.0f;
        bool bSmokeRunFinished = false;
        bool bSmokeRunEquipped = false;
        /** The chase track is playing; it gives way to the level's own music once nothing hunts for a while. */
        bool bChaseMusic = false;
        float CalmSeconds = 0.0f;
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
        void InitialisePower();
        void ApplyPowerGroup(const std::string& InGroup, bool bInPowered);
        void TickFlicker();

        struct FFlickeringLight {
            TWeakObjectPtr<AActor> Actor;
            float BaseIntensity = 1.0f;
            float Amount = 0.5f;
            float Phase = 0.0f;
        };
        std::set<std::string> PoweredGroups;
        std::vector<FFlickeringLight> FlickeringLights;

        /** Virtual path of the running map, e.g. /Game/Maps/Slice. */
        std::string GetCurrentMapPath() const;

        std::string SaveFilePath;
        bool bPendingSaveChecked = false;
        ELeonHorrorTravel LastTravelRequest = ELeonHorrorTravel::None;
        bool bQuitRequested = false;
    };

} // namespace Leon
