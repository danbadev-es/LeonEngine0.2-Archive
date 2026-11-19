#pragma once

#include "RenderCore/FPostProcessSettings.hpp"
#include "RenderCore/FShaderConstants.hpp"

#include <glm/glm.hpp>

#include <algorithm>
#include <cstdint>

// How the post-process settings become the constants of the last two passes. Kept apart from the
// passes so the tests hand the shaders exactly what the renderer does.

namespace Leon {

    /** The look is for the finished image: not for the debug views, nor with post-processing off. */
    inline bool IsLookApplied(const FPostProcessSettings& InSettings) {
        return InSettings.bEnabled && InSettings.DebugMode == 0;
    }

    inline bool IsGradeNeutral(const FPostProcessLook& InLook) {
        return InLook.Lift == glm::vec3(0.0f) && InLook.Gamma == glm::vec3(1.0f) && InLook.Gain == glm::vec3(1.0f) &&
               InLook.Contrast == 1.0f && InLook.Saturation == 1.0f;
    }

    inline FToneMappingConstants MakeToneMappingConstants(const FPostProcessSettings& InSettings, float InExposure,
                                                          bool bInBloom) {
        FToneMappingConstants constants;
        constants.ToneParams = glm::vec4(InExposure, InSettings.BloomIntensity, InSettings.Gamma, 0.0f);
        constants.ToneFlags = glm::ivec4(InSettings.ToneMapper, bInBloom ? 1 : 0, InSettings.DebugMode, 0);
        if (!IsLookApplied(InSettings))
            return constants;
        const FPostProcessLook& look = InSettings.Look;
        constants.ToneParams.w = std::max(look.ChromaticAberration, 0.0f);
        constants.Vignette = glm::vec4(glm::clamp(look.VignetteIntensity, 0.0f, 1.0f),
                                       std::max(look.VignetteRadius, 0.0f), std::max(look.VignetteSoftness, 0.0f), 0.0f);
        if (!IsGradeNeutral(look)) {
            constants.ToneFlags.w = 1;
            constants.GradeLift = glm::vec4(look.Lift, std::max(look.Saturation, 0.0f));
            constants.GradeGamma = glm::vec4(1.0f / glm::max(look.Gamma, glm::vec3(0.01f)), std::max(look.Contrast, 0.0f));
            constants.GradeGain = glm::vec4(glm::max(look.Gain, glm::vec3(0.0f)), 0.0f);
        }
        return constants;
    }

    /**
     * InFrameIndex counts the frames drawn; it moves the grain only when the look asks for animated
     * grain, so a still pattern (and every test) gets the same image each time.
     */
    inline FFXAAConstants MakeFXAAConstants(const FPostProcessSettings& InSettings, const glm::vec2& InTexelSize,
                                            bool bInAntiAlias, uint32_t InFrameIndex) {
        FFXAAConstants constants;
        constants.FXAAParams = glm::vec4(InTexelSize, bInAntiAlias ? 1.0f : 0.0f, 0.0f);
        if (IsLookApplied(InSettings) && InSettings.Look.FilmGrainIntensity > 0.0f) {
            const uint32_t seed = InSettings.Look.FilmGrainSeed + (InSettings.Look.bFilmGrainAnimated ? InFrameIndex : 0u);
            // The seed travels as a float: kept below 2^24, where every whole number is exact.
            constants.GrainParams = glm::vec4(InSettings.Look.FilmGrainIntensity, static_cast<float>(seed & 0xFFFFFFu),
                                              0.0f, 0.0f);
        }
        return constants;
    }

} // namespace Leon
