#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <map>
#include <set>
#include <string>

namespace Leon {

    /**
     * Everything needed to put the player back where they saved.
     *
     * World state is stored as differences from the map, keyed by actor GUID: which pickups are gone,
     * which doors changed, which enemies are dead, which notes were read. Loading reloads the map and
     * applies these on top, so anything not listed is exactly as authored.
     *
     * The file is line-based text, one record per line, so a save can be read and edited by hand.
     */
    struct FLeonHorrorSaveGame {
        static constexpr int32_t CurrentVersion = 1;

        std::string MapName;

        glm::vec3 PlayerLocation{0.0f};
        float PlayerYaw = -90.0f;
        float PlayerPitch = 0.0f;
        float Health = 100.0f;
        float Stamina = 100.0f;
        bool bFlashlightOn = true;
        /** ELeonHorrorWeapon as an integer. */
        int32_t EquippedWeapon = 0;
        int32_t LoadedRounds = 0;
        std::map<std::string, int32_t> Items;

        std::set<std::string> CollectedPickups;
        struct FDoor {
            bool bLocked = false;
            bool bOpen = false;
            bool operator==(const FDoor& InOther) const { return bLocked == InOther.bLocked && bOpen == InOther.bOpen; }
        };
        std::map<std::string, FDoor> Doors;
        std::set<std::string> DeadEnemies;
        std::set<std::string> ReadNotes;

        std::string ToText() const;
        /** Returns false, leaving OutSave untouched, when the text is not a save this build can read. */
        static bool FromText(const std::string& InText, FLeonHorrorSaveGame& OutSave);

        /** Writes atomically (temp file, then rename). */
        bool SaveToFile(const std::string& InPath) const;
        static bool LoadFromFile(const std::string& InPath, FLeonHorrorSaveGame& OutSave);
    };

} // namespace Leon
