#pragma once

#include <cstdint>

namespace Leon {

    /** What the post-process chain does to the HDR scene (see FPostProcessRHI). */
    struct FPostProcessSettings {
        bool bEnabled = true;

        // Bloom Configuration
        bool bBloomEnabled = true;
        float BloomThreshold = 1.0f;
        float BloomSoftKnee = 0.5f;
        float BloomIntensity = 0.05f;
        float BloomRadius = 1.0f;
        uint32_t BloomMipCount = 5;

        // Tone Mapping Configuration
        int ToneMapper = 0; // 0 = ACES Filmic, 1 = Reinhard Extended, 2 = Neutral Clamp, 3 = Uncharted 2
        float Exposure = 1.0f;
        float Gamma = 2.2f;

        // Anti-Aliasing (FXAA)
        bool bFXAAEnabled = true;

        // Screen-space ambient occlusion (depth-only, half-res + bilateral blur)
        bool bSSAOEnabled = true;
        float SSAORadius = 0.5f;
        float SSAOIntensity = 1.0f;
        float SSAOBias = 0.025f;
        int SSAOKernelSize = 16;

        // Forensic Debug Mode
        // 0: Full Composite Post-Process
        // 1: Raw HDR Linear Scene (Before Tone Mapping)
        // 2: Bloom Glow Output Only
        // 3: Bright Pass Extraction Only
        // 4: Tone Mapping Output Only (No FXAA)
        // 5: SSAO only
        int DebugMode = 0;
    };

} // namespace Leon
