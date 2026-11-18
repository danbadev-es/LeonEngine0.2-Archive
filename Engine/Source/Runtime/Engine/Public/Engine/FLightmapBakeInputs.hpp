#pragma once

#include "Engine/Components.hpp"

#include <cstdint>
#include <string>

namespace Leon {

    class UWorld;

    struct FLightmassSettings {
        uint32_t LightmapResolution = 64;
        uint32_t NumIndirectBounces = 2;
        uint32_t SamplesPerTexel = 16;
        float IndirectIntensity = 1.0f;
        bool bAmbientOcclusion = true;
        float AOIntensity = 1.0f;
        float AORadius = 1.0f;
        float WorldScale = 1.0f;
        uint64_t DeterministicSeed = 0x4C454F4E4C4D4153ull; // "LEONLMAS"
        float TexelPadding = 2.0f;
        ELightingBuildQuality LightingBuildQuality = ELightingBuildQuality::Draft;
    };

    inline void ApplyLightingBuildQuality(ELightingBuildQuality InQuality, FLightmassSettings& OutSettings) {
        OutSettings.LightingBuildQuality = InQuality;
        switch (InQuality) {
        case ELightingBuildQuality::Preview:
            OutSettings.LightmapResolution = 32;
            OutSettings.SamplesPerTexel = 4;
            OutSettings.NumIndirectBounces = 1;
            break;
        case ELightingBuildQuality::Production:
            OutSettings.LightmapResolution = 128;
            OutSettings.SamplesPerTexel = 32;
            OutSettings.NumIndirectBounces = 3;
            break;
        case ELightingBuildQuality::Draft:
        default:
            OutSettings.LightmapResolution = 64;
            OutSettings.SamplesPerTexel = 8;
            OutSettings.NumIndirectBounces = 2;
            break;
        }
    }

    /**
     * What a lightmap bake was made from, as one number.
     *
     * The baker (a development tool) stamps the hash of its inputs in the map. The game computes the
     * same hash when it loads that map and uses the lightmaps only if the two agree: geometry, lights
     * or materials that changed since the bake would otherwise be lit by the wrong data.
     *
     * The hash must come out the same in the tool and in the game. It therefore depends only on the
     * map's data: not on where the project folder is, nor on whether a renderer is running.
     */
    class FLightmapBakeInputs {
    public:
        /** The world's lighting settings over the defaults of its build quality. */
        static void ApplyWorldSettings(const FWorldSettingsComponent& InWorldSettings, FLightmassSettings& OutSettings,
                                       std::string& OutLightmapVirtual);

        /**
         * Ignores what a previous bake stamped in the map (the hash itself, and the scale, bias, index
         * and asset of each lightmap).
         */
        static uint64_t ComputeHash(const UWorld& InWorld, const FLightmassSettings& InSettings);

        /** Compares the stored hash with the world as loaded and stops lightmaps being used when stale. */
        static void RefreshRuntimeTrust(UWorld& InWorld);
    };

} // namespace Leon
