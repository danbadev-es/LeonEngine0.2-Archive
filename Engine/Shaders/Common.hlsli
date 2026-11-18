#ifndef LEON_COMMON_HLSLI
#define LEON_COMMON_HLSLI

// Shared by every engine shader.
//
// Conventions (see FShaderCompiler.hpp and RHITypes.hpp):
//  - Matrices are column-major and vectors are columns: write mul(Matrix, Vector).
//  - World space is right-handed, Y up. Clip space depth runs 0 (near) to 1 (far).
//  - Vertex inputs use the semantic ATTRIBUTE<n>, n being the position in the vertex layout.
//  - Constant buffers: b0 frame, b1 pass, b2 material, b3 draw, b4 extra (bones / instances).
//  - Textures: t0..t15. Samplers are fixed, below.
//  - Screen UVs have their origin at the top left, like render targets.

SamplerState s_LinearWrap : register(s0);            // trilinear, anisotropic, repeat
SamplerState s_LinearClamp : register(s1);           // trilinear, clamp
SamplerState s_PointClamp : register(s2);            // nearest, clamp
SamplerState s_PointWrap : register(s3);             // nearest, repeat
SamplerComparisonState s_ShadowCompare : register(s4); // linear, less-equal, clamp

// Mirrors FFrameConstants in FWorldRendererRHI.cpp.
cbuffer FrameConstants : register(b0) {
    float4x4 u_ViewProjection;
    float4x4 u_View;
    float4 u_ViewPosition;  // xyz = camera position in world space
    float4 u_CameraForward; // xyz = view direction in world space
    float4 u_ViewportSize;  // xy = size in pixels, zw = 1 / size
};

static const float kPi = 3.14159265358979323846;

/** Exact sRGB encoding (IEC 61966-2-1). */
float3 LinearToSRGB(float3 InColor) {
    const float3 color = max(InColor, 0.0);
    const float3 low = color * 12.92;
    const float3 high = 1.055 * pow(color, 1.0 / 2.4) - 0.055;
    return select(color <= 0.0031308, low, high);
}

float3 SafeNormalize(float3 InVector, float3 InFallback) {
    const float lengthSquared = dot(InVector, InVector);
    return lengthSquared > 1e-8 ? InVector * rsqrt(lengthSquared) : InFallback;
}

#endif
