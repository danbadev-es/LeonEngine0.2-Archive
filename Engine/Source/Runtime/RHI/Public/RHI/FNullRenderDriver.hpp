#pragma once

namespace Leon {

    /**
     * A render driver that creates every resource and draws nothing.
     *
     * It lets executables run without a GPU backend: gameplay, loading, travel and saving all work,
     * the window stays empty. Tests that want GPU-shaped objects without a GPU can register it too.
     */
    class FNullRenderDriver {
    public:
        /** Registers the driver and makes it the active API. Safe to call more than once. */
        static void Register();
    };

} // namespace Leon
