#pragma once

#include <cstdint>

namespace Leon {

    /** The Direct3D 12 backend of the RHI. */
    class FD3D12RHI {
    public:
        /** Makes ERHIBackend::D3D12 available to FRHI::Init. Safe to call more than once. */
        static void Register();

        /**
         * Errors reported by the Direct3D debug layer since the device was created. Always zero when
         * the device was created without FRHIInitDesc::bEnableDebugLayer. Tests assert this stays zero.
         */
        static uint64_t GetValidationErrorCount();
    };

} // namespace Leon
