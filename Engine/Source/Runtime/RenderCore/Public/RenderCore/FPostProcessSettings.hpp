#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <string>

namespace Leon {

    /**
     * The look laid over the tone-mapped image. Everything is neutral by default, and a neutral part
     * is skipped by the shaders: the image is then exactly what it is without a look.
     */
    struct FPostProcessLook {
        /** How much the corners darken (0 = no vignette, 1 = to black). */
        float VignetteIntensity = 0.0f;
        /** Where the darkening starts and how far it takes to reach full strength: 0 = centre, 1 = corners. */
        float VignetteRadius = 0.5f;
        float VignetteSoftness = 0.5f;

        /** Amplitude of the film grain in display values (0 = none; 0.03 to 0.06 is visible but quiet). */
        float FilmGrainIntensity = 0.0f;
        /** True: the pattern changes every frame. False: it stays still (FilmGrainSeed picks it). */
        bool bFilmGrainAnimated = true;
        uint32_t FilmGrainSeed = 0;

        // Colour grading, applied to the encoded colour in this order (see GradeColor in ToneMapping.hlsl).
        /** Raises black towards this colour; white stays. */
        glm::vec3 Lift{0.0f};
        /** Bends the middle of the range; above 1 brightens it. Black and white stay. */
        glm::vec3 Gamma{1.0f};
        /** Multiplies the colour: moves white; black stays. */
        glm::vec3 Gain{1.0f};
        /** Distance to mid grey (1 = unchanged). */
        float Contrast = 1.0f;
        /** Distance to the pixel's own grey (0 = black and white, 1 = unchanged). */
        float Saturation = 1.0f;

        /** How far red and blue are pulled apart at the edge of the image, as a fraction of it (0 = off). */
        float ChromaticAberration = 0.0f;
    };

    /** The tone mapping operators, by the value of FPostProcessSettings::ToneMapper. */
    inline const char* ToneMapperName(int InToneMapper) {
        switch (InToneMapper) {
        case 1:
            return "Reinhard";
        case 2:
            return "Clamp";
        case 3:
            return "Uncharted2";
        default:
            return "ACES";
        }
    }

    /** The inverse of ToneMapperName; the number is accepted too. Anything else is ACES. */
    inline int ParseToneMapper(const std::string& InValue) {
        if (InValue == "Reinhard" || InValue == "1")
            return 1;
        if (InValue == "Clamp" || InValue == "2")
            return 2;
        if (InValue == "Uncharted2" || InValue == "3")
            return 3;
        return 0;
    }

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

        /** Vignette, film grain, colour grading and chromatic aberration. Not applied to the debug views. */
        FPostProcessLook Look;
    };

} // namespace Leon
