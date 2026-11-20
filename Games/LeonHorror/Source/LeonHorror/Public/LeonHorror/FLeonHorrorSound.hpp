#pragma once

#include <glm/glm.hpp>

#include <string>

namespace Leon {

    class AActor;
    class UWorld;

    /**
     * The game's sounds by name. A cue is a file of Content/Audio without its extension
     * ("SFX_Pickup_Key"); see Content/Audio/README.md for what there is.
     *
     * Everything the game plays goes through here, which also keeps count: without a sound card
     * (tests, tools) nothing is heard, and the count is how a test knows a cue was asked for.
     */
    namespace LeonHorrorSound {

        /** Heard the same everywhere: the player's own noises, the interface, stingers. */
        void Play(const std::string& InCue, float InVolume = 1.0f);
        /** Heard from a place, fading to nothing at InRadius metres. */
        void PlayAt(const std::string& InCue, const glm::vec3& InLocation, float InVolume = 1.0f, float InRadius = 18.0f);
        /** Replaces the music that is playing. */
        void Music(const std::string& InCue, float InVolume = 1.0f);

        /** One of InPrefix_01 .. InPrefix_NN, never the one it gave last time for that prefix. */
        std::string Variation(const std::string& InPrefix, int InCount);

        /** The step cue for the floor under a point: "SFX_Step_Tile" on tiles and concrete, "SFX_Step_Vinyl" otherwise. */
        std::string StepSurface(UWorld& InWorld, const glm::vec3& InFeet, AActor* InIgnore);

        /**
         * Decodes every short sound of Content/Audio now, so none is decoded the first time it is
         * heard, in the middle of play. Returns how many were made ready (0 without a sound card).
         */
        int PreloadAll();

        /** How many cues whose name starts with InPrefix have been asked for since the last ResetCounts. */
        int CountPlayed(const std::string& InPrefix);
        void ResetCounts();

    } // namespace LeonHorrorSound

} // namespace Leon
