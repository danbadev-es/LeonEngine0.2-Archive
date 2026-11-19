#pragma once

#include "RenderCore/FLightProbeGrid.hpp"

#include <cstdint>
#include <string>

namespace Leon {

    /**
     * The light probes of a map on disk (`Content/Lightmaps/<Map>.lprobes`), written by the baker next
     * to the lightmap and read when the map loads.
     *
     * Layout, little endian:
     *   uint32   magic "LLPV"
     *   uint32   version (1)
     *   uint64   hash of the bake's inputs, the same number the lightmap and the map carry
     *   float32  origin x, y, z      position of probe (0, 0, 0)
     *   float32  spacing x, y, z     metres between probes
     *   uint32   count x, y, z
     *   uint32   reserved (0)
     *   uint8    valid[count]        1 = usable, 0 = inside a wall (holds what its neighbours hold)
     *   float16  coefficients[count][12]   per probe: red xyzw, green xyzw, blue xyzw (FLightProbeSample)
     * Probe (x, y, z) is entry x + countX * (y + countY * z).
     */
    class FLightProbeAsset {
    public:
        static constexpr uint32_t kMagic = 0x56504C4Cu; // "LLPV"
        static constexpr uint32_t kVersion = 1;
        /** More probes than this in a file is taken for damage, not for a level. */
        static constexpr uint64_t kMaxProbes = 1u << 20;

        static bool Save(const std::string& InPath, const FLightProbeGrid& InGrid, uint64_t InContentHash);
        /** False (and OutGrid empty) when the file is missing, cut short or not of this format. */
        static bool Load(const std::string& InPath, FLightProbeGrid& OutGrid, uint64_t& OutContentHash);
    };

} // namespace Leon
