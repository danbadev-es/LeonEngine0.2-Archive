#pragma once

#include "Gameplay/AGameModeBase.hpp"

#include <map>
#include <set>
#include <string>

namespace Leon {

    class ALeonHorrorCharacter;
    class ALeonHorrorDoor;
    class ALeonHorrorNote;
    class ALeonHorrorPickup;

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

        /** Opens a note for reading; the HUD shows it and the world pauses until it is dismissed. */
        void ShowNote(const ALeonHorrorNote& InNote);
        void DismissNote();
        bool IsNoteOpen() const { return bNoteOpen; }
        const std::string& GetNoteTitle() const { return NoteTitle; }
        const std::string& GetNoteText() const { return NoteText; }

        /** Asked by a save point. Returns true when the game was saved. */
        bool RequestSave(ALeonHorrorCharacter& InPlayer);
        int32_t GetSaveCount() const { return SaveCount; }

        void NotifySliceCompleted();
        bool IsSliceCompleted() const { return bSliceCompleted; }

    private:
        void TickSmokeRun();

        float ElapsedPlaySeconds = 0.0f;
        bool bSmokeRunFinished = false;
        bool bPlayerDied = false;

        std::set<std::string> CollectedPickups;
        std::map<std::string, FLeonHorrorDoorState> DoorStates;
        std::set<std::string> ReadNotes;
        bool bNoteOpen = false;
        std::string NoteTitle;
        std::string NoteText;
        int32_t SaveCount = 0;
        bool bSliceCompleted = false;
    };

} // namespace Leon
