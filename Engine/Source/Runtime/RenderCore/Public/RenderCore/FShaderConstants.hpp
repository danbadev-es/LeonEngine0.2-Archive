#pragma once

#include "RenderCore/FShadowTypes.hpp"

#include <glm/glm.hpp>

#include <cstdint>

// The C++ side of the engine's shaders: the constant buffers they declare, the texture registers of
// the surface shader, and the conversions from the engine's clip space to the one Direct3D expects.
//
// Each structure mirrors a cbuffer in Engine/Shaders (named in the comment). Keep field order and
// types identical, and every field a multiple of 16 bytes, so C++ and HLSL agree on the layout.

namespace Leon {

    inline constexpr uint32_t kMaxPointLights = 16;
    inline constexpr uint32_t kMaxSpotLights = 8;

    // Texture registers of PBR_Lit.hlsl.
    inline constexpr uint32_t kSlotAlbedo = 0;
    inline constexpr uint32_t kSlotNormal = 1;
    inline constexpr uint32_t kSlotMetallic = 2;
    inline constexpr uint32_t kSlotAO = 3;
    inline constexpr uint32_t kSlotRoughness = 4;
    inline constexpr uint32_t kSlotPlanarFloor = 5;
    inline constexpr uint32_t kSlotBRDFLUT = 6;
    inline constexpr uint32_t kSlotIrradiance = 7;
    inline constexpr uint32_t kSlotPrefilter = 8;
    inline constexpr uint32_t kSlotEmissive = 9;
    inline constexpr uint32_t kSlotCascadeShadow = 10;
    inline constexpr uint32_t kSlotSpotShadow = 11;
    inline constexpr uint32_t kSlotLightmap = 12;
    inline constexpr uint32_t kSlotPlanarWall = 13;
    inline constexpr uint32_t kSlotPointShadow = 14;
    inline constexpr uint32_t kSlotReflectionProbes = 15;

    // The structures below mirror constant buffers in Engine/Shaders. Keep field order and types
    // identical; every field is a multiple of 16 bytes so C++ and HLSL agree on the layout.

    struct FFrameConstants { // Common.hlsli
        glm::mat4 ViewProjection;
        glm::mat4 View;
        glm::vec4 ViewPosition;
        glm::vec4 CameraForward;
        glm::vec4 ViewportSize;
    };

    struct FDirectionalLightConstants {
        glm::vec4 Direction{0.0f, -1.0f, 0.0f, 0.0f};
        glm::vec4 Color{1.0f, 1.0f, 1.0f, 0.0f};
    };

    struct FPointLightConstants {
        glm::vec4 Position{0.0f};
        glm::vec4 Color{0.0f};
        glm::vec4 Params{0.0f, -1.0f, 0.0f, 0.0f};
    };

    struct FSpotLightConstants {
        glm::vec4 Position{0.0f};
        glm::vec4 Direction{0.0f};
        glm::vec4 Color{0.0f};
        glm::vec4 Params{0.0f, 0.0f, -1.0f, 0.0f};
    };

    struct FLitPassConstants { // PBR_Common.hlsli
        FDirectionalLightConstants DirLight;
        FPointLightConstants PointLights[kMaxPointLights];
        FSpotLightConstants SpotLights[kMaxSpotLights];
        glm::ivec4 LightCounts{0};
        glm::vec4 EnvSkyColor{0.0f};
        glm::vec4 EnvHorizonColor{0.0f};
        glm::vec4 EnvGroundColor{0.0f};
        glm::vec4 FogColorDensity{0.0f};
        glm::vec4 FogParams{0.0f};
        glm::ivec4 PassFlags{0};
        glm::mat4 LightSpaceMatrices[4]{glm::mat4(1.0f), glm::mat4(1.0f), glm::mat4(1.0f), glm::mat4(1.0f)};
        glm::mat4 SpotLightSpaceMatrices[FShadowSettings::kMaxShadowedSpotLights]{glm::mat4(1.0f), glm::mat4(1.0f),
                                                                                 glm::mat4(1.0f), glm::mat4(1.0f)};
        glm::vec4 CascadeSplits{0.0f};
        glm::vec4 ShadowParams{0.0f};
        glm::ivec4 ShadowSettings{0};
        glm::ivec4 ShadowFlags{0};
        glm::mat4 PlanarMatrix[2]{glm::mat4(1.0f), glm::mat4(1.0f)};
        glm::vec4 PlanarPlane[2]{glm::vec4(0.0f, 1.0f, 0.0f, 0.0f), glm::vec4(0.0f, 0.0f, 1.0f, 0.0f)};
        glm::vec4 ClipPlane{0.0f, 1.0f, 0.0f, 0.0f};
        glm::ivec4 PlanarFlags{0};
    };

    struct FShadowPassConstants { // ShadowDepth.hlsl
        glm::mat4 LightViewProjection;
        glm::vec4 PointLight;
    };

    struct FShadowMaterialConstants { // ShadowDepth.hlsl
        glm::vec4 UVTransform;
        glm::vec4 AlphaTest;
    };

    struct FMaterialConstants { // PBR_Lit.hlsl
        glm::vec4 AlbedoColor;
        glm::vec4 EmissiveColor;
        glm::vec4 SurfaceParams;
        glm::vec4 SurfaceParams2;
        glm::vec4 UVTransform;
        glm::ivec4 MaterialFlags;
    };

    struct FDrawConstants { // PBR_Lit.hlsl
        glm::mat4 Model;
        glm::mat4 NormalMatrix;
        glm::vec4 LightmapScaleBias;
        glm::ivec4 DrawFlags;
        // The two reflection probes the object reads; ProbeBoxMax.w < 0 = none (FReflectionProbeMath.hpp).
        glm::vec4 ProbeBoxMin[2]{glm::vec4(0.0f, 0.0f, 0.0f, 1.0f), glm::vec4(0.0f, 0.0f, 0.0f, 1.0f)};
        glm::vec4 ProbeBoxMax[2]{glm::vec4(0.0f, 0.0f, 0.0f, -1.0f), glm::vec4(0.0f, 0.0f, 0.0f, -1.0f)};
        glm::vec4 ProbeCapture[2]{glm::vec4(0.0f, 0.0f, 0.0f, 1.0f), glm::vec4(0.0f, 0.0f, 0.0f, 1.0f)};
    };

    /** Copies of one mesh drawn in a single call (PBR_Lit.hlsl compiled with INSTANCED). */
    inline constexpr uint32_t kMaxInstances = 64;

    struct FInstanceConstants { // PBR_Lit.hlsl: InstanceConstants (b4)
        struct FInstance {
            glm::mat4 Model{1.0f};
            glm::mat4 NormalMatrix{1.0f};
        };
        FInstance Instances[kMaxInstances];
    };

    struct FOutlineConstants { // Outline_Skinned.hlsl
        glm::vec4 ColorWidth;
    };

    struct FSkyConstants { // Skybox.hlsl
        glm::mat4 ClipToDirection;
        glm::vec4 SunDirection;
        glm::vec4 SkyColor;
        glm::vec4 HorizonColor;
        glm::vec4 GroundColor;
        glm::vec4 SunColor;
        glm::ivec4 SkyFlags;
    };

    /**
     * The engine's projection matrices produce clip depth in -1..1; Direct3D expects 0..1.
     * Applied here until the cameras build 0..1 projections themselves.
     */
    inline glm::mat4 ClipDepthZeroToOne() {
        glm::mat4 fix(1.0f);
        fix[2][2] = 0.5f;
        fix[3][2] = 0.5f;
        return fix;
    }

    /**
     * Takes a light's clip space (x, y and z in -1..1) to shadow-map space: xy = texture coordinates
     * with the origin at the top left, z = stored depth in 0..1.
     */
    inline glm::mat4 ClipToShadowMap() {
        glm::mat4 bias(1.0f);
        bias[0][0] = 0.5f;
        bias[1][1] = -0.5f;
        bias[2][2] = 0.5f;
        bias[3] = glm::vec4(0.5f, 0.5f, 0.5f, 1.0f);
        return bias;
    }

    /**
     * Mirrors clip space vertically. Cube map faces are drawn with the view matrices the engine
     * already had, which assume the image origin at the bottom left; Direct3D puts it at the top.
     */
    inline glm::mat4 ClipFlipY() {
        glm::mat4 flip(1.0f);
        flip[1][1] = -1.0f;
        return flip;
    }

    struct FProbeFilterConstants { // ReflectionProbeFilter.hlsl
        glm::vec4 FaceForward{0.0f};
        glm::vec4 FaceRight{0.0f};
        glm::vec4 FaceUp{0.0f};
        glm::vec4 FilterParams{0.0f};
    };

    struct FBloomConstants { // Bloom.hlsl
        glm::vec4 BloomParams{0.0f};
        glm::vec4 TexelSize{0.0f};
    };

    struct FSSAOConstants { // SSAO.hlsl
        glm::mat4 Projection{1.0f};
        glm::mat4 InverseProjection{1.0f};
        glm::vec4 Samples[16]{};
        glm::vec4 SSAOParams{0.0f};
        glm::vec4 BlurParams{0.0f};
    };

    struct FToneMappingConstants { // ToneMapping.hlsl
        glm::vec4 ToneParams{0.0f};
        glm::ivec4 ToneFlags{0};
        glm::vec4 Vignette{0.0f};
        glm::vec4 GradeLift{0.0f, 0.0f, 0.0f, 1.0f};
        glm::vec4 GradeGamma{1.0f};
        glm::vec4 GradeGain{1.0f};
    };

    struct FFXAAConstants { // FXAA.hlsl
        glm::vec4 FXAAParams{0.0f};
        glm::vec4 GrainParams{0.0f};
    };

    struct FCanvasConstants { // Canvas.hlsl
        glm::mat4 Projection{1.0f};
        glm::ivec4 CanvasFlags{0};
    };

} // namespace Leon
