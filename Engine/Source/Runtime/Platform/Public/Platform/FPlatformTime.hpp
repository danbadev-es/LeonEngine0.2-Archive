#pragma once

namespace Leon {

    /** Monotonic clock of the platform layer. */
    class FPlatformTime {
    public:
        /** Seconds since the platform layer started. */
        static double Seconds();
    };

} // namespace Leon
