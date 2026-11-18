#pragma once

#include <cstdint>

namespace Leon {

    /**
     * A backend with no GPU behind it. Resources exist and keep the data written to them (a texture
     * reads back what was uploaded), draws are counted and nothing is rasterised.
     *
     * Used by tests and tools that exercise code paths which create GPU resources.
     */
    class FNullRHI {
    public:
        /** Makes ERHIBackend::Null available to FRHI::Init. Safe to call more than once. */
        static void Register();

        struct FStats {
            uint64_t Frames = 0;
            uint64_t RenderPasses = 0;
            uint64_t DrawCalls = 0;
        };
        /** Counters of the current device; zero when there is none. */
        static FStats GetStats();
    };

} // namespace Leon
