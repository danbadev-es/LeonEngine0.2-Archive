#pragma once

#include "Assets/FLightmapAsset.hpp"
#include "Engine/EMobility.hpp"
#include "RenderCore/FLight.hpp"
#include "Engine/Components.hpp"
#include "Engine/FLightmapBakeInputs.hpp"

#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace Leon {

    class UWorld;

    struct FLightmassBakeResult {
        bool bSuccess = false;
        std::string Message;
        std::string LightmapPath;
        uint64_t BakeHash = 0;
        uint32_t StaticMeshCount = 0;
        uint32_t StaticLightCount = 0;
        uint32_t AtlasWidth = 0;
        uint32_t AtlasHeight = 0;
    };

    /**
     * @brief Offline static lighting (Unreal Lightmass analogue).
     * Does not run inside FWorldRenderer. Invoked by LightmassTool.
     *
     * Flow: load map → filter Static receivers + Static/Stationary lights → UV1 →
     * atlas → CPU bake → write .llightmap → stamp .lmap metadata (hash excludes that metadata).
     */
    class FLightmass {
    public:
        static FLightmassBakeResult BakeMap(const std::string& InMapPath, const FLightmassSettings& InSettings,
                                            bool bForce = false);

        static bool ValidateMap(const std::string& InMapPath, std::string& OutMessage);

        /** FLightmapBakeInputs::ComputeHash, kept here for the tools and tests that ask the baker. */
        static uint64_t ComputeBakeInputHash(const UWorld& InWorld, const FLightmassSettings& InSettings);

        /** FLightmapBakeInputs::RefreshRuntimeTrust. */
        static void RefreshRuntimeLightmapTrust(UWorld& InWorld);
    };

} // namespace Leon
