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

SamplerState s_LinearWrap : register(s0);            // trilinear, anisotropic, repeat
SamplerState s_LinearClamp : register(s1);           // trilinear, clamp
SamplerState s_PointClamp : register(s2);            // nearest, clamp
SamplerState s_PointWrap : register(s3);             // nearest, repeat
SamplerComparisonState s_ShadowCompare : register(s4); // linear, less-equal, white border

cbuffer FrameConstants : register(b0) {
    float4x4 u_ViewProjection;
    float4x4 u_View;
    float4 u_ViewPosition; // xyz = camera position in world space
    float4 u_ViewportSize; // xy = size in pixels, zw = 1 / size
};

static const float kPi = 3.14159265358979;

float3 LinearToSRGB(float3 InColor) {
    return pow(saturate(InColor), 1.0 / 2.2);
}

#endif
