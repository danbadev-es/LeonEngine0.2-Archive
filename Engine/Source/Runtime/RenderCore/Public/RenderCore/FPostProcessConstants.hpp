#pragma once

#include "RenderCore/FPostProcessSettings.hpp"
#include "RenderCore/FShaderConstants.hpp"

#include <glm/glm.hpp>

#include <algorithm>
#include <cstdint>

// How the post-process settings become the constants of the last two passes, and the settings of a
// map's air and the lights of a frame those of the volumetric fog. Kept apart from the passes so the
// tests hand the shaders exactly what the renderer does.

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
     * The constants of VolumetricFog.hlsl for one view.
     *
     * InLighting is the lit pass of the frame, after its shadow maps were drawn: the lights followed
     * through the air are the ones that were given a layer or a cube, since a light without a shadow
     * map would shine through walls. InViewProjection and InProjection are the camera's as the scene
     * was drawn with them (clip depth 0..1). bInDistanceFog: the map's distance fog is on and has
     * dimmed the surfaces already, so the air adds the light of the lamps and nothing else.
     * InDefaultSteps is used where the map names no step count.
     */
    inline FVolumetricFogConstants MakeVolumetricFogConstants(const FVolumetricFogSettings& InSettings,
                                                              const FLitPassConstants& InLighting,
                                                              const glm::mat4& InViewProjection,
                                                              const glm::mat4& InProjection,
                                                              const glm::vec3& InCameraPosition, bool bInDistanceFog,
                                                              uint32_t InDefaultSteps) {
        FVolumetricFogConstants constants;
        constants.ClipToWorld = glm::inverse(InViewProjection);
        constants.CameraPosition = glm::vec4(InCameraPosition, std::max(InSettings.MaxDistance, 0.0f));
        const uint32_t steps = std::clamp(InSettings.StepCount > 0 ? InSettings.StepCount : InDefaultSteps,
                                          kMinVolumetricFogSteps, kMaxVolumetricFogSteps);
        // The phase function has no limit at g = 1 or -1 (all the light in one direction).
        constants.FogParams = glm::vec4(std::max(InSettings.Density, 0.0f), std::max(InSettings.Scattering, 0.0f),
                                        glm::clamp(InSettings.Anisotropy, -0.95f, 0.95f), static_cast<float>(steps));
        if (!bInDistanceFog)
            constants.FogAmbient = glm::vec4(glm::max(InSettings.AmbientColor, glm::vec3(0.0f)) *
                                                 std::max(InSettings.AmbientIntensity, 0.0f),
                                             1.0f);
        // View depth of a depth-buffer value z: z = A - B / depth, with A and B from the projection.
        constants.DepthParams = glm::vec4(-InProjection[3][2], -InProjection[2][2], 0.0f, 0.0f);

        int spotCount = 0;
        const int litSpots = std::min(InLighting.LightCounts.y, static_cast<int>(kMaxSpotLights));
        for (int i = 0; InLighting.ShadowFlags.y != 0 && i < litSpots; ++i) {
            const FSpotLightConstants& light = InLighting.SpotLights[i];
            const int layer = static_cast<int>(light.Params.z + 0.5f);
            if (light.Position.w < 0.5f || light.Params.z < 0.0f ||
                layer >= static_cast<int>(FShadowSettings::kMaxShadowedSpotLights) ||
                spotCount >= static_cast<int>(kMaxVolumetricFogSpotLights))
                continue;
            FVolumetricFogConstants::FSpotLight& spot = constants.SpotLights[spotCount];
            spot.Position = glm::vec4(glm::vec3(light.Position), light.Params.x);
            spot.Direction = light.Direction;
            spot.Color = glm::vec4(glm::vec3(light.Color) * light.Params.y, light.Color.a);
            spot.Shadow = glm::vec4(static_cast<float>(layer), 0.0f, 0.0f, 0.0f);
            constants.SpotMatrices[spotCount] = InLighting.SpotLightSpaceMatrices[layer];
            ++spotCount;
        }
        int pointCount = 0;
        const int litPoints = std::min(InLighting.LightCounts.x, static_cast<int>(kMaxPointLights));
        for (int i = 0; InLighting.ShadowFlags.z != 0 && i < litPoints; ++i) {
            const FPointLightConstants& light = InLighting.PointLights[i];
            if (light.Position.w < 0.5f || light.Params.y < 0.0f ||
                pointCount >= static_cast<int>(kMaxVolumetricFogPointLights))
                continue;
            FVolumetricFogConstants::FPointLight& point = constants.PointLights[pointCount];
            point.Position = glm::vec4(glm::vec3(light.Position), light.Params.x);
            point.Color = glm::vec4(glm::vec3(light.Color) * light.Color.a, light.Params.y);
            ++pointCount;
        }
        constants.LightCounts = glm::ivec4(spotCount, pointCount, 0, 0);
        return constants;
    }

    /**
     * False when the passes would change nothing: air without density, or (under the map's distance
     * fog) no light to show in it. The frame is then drawn without them.
     */
    inline bool IsVolumetricFogDrawn(const FVolumetricFogConstants& InConstants) {
        if (InConstants.FogParams.x <= 0.0f || InConstants.CameraPosition.w <= 0.0f)
            return false;
        return InConstants.FogAmbient.a > 0.5f || InConstants.LightCounts.x > 0 || InConstants.LightCounts.y > 0;
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
