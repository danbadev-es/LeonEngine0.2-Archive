// Draws shadow casters into a shadow map: depth only.
//
// Compiled with SKINNED for meshes that follow a skeleton. Two pixel entry points: PSMain keeps the
// depth the rasteriser produced (sun and spot maps); PSMainLinear writes distance to the light over
// its radius, which is what the point-light cube maps store.

#include "Common.hlsli"
#ifdef SKINNED
#include "Skinning.hlsli"
#endif

// Mirrors FShadowPassConstants in FWorldRendererRHI.cpp.
cbuffer PassConstants : register(b1) {
    float4x4 u_LightViewProjection;
    float4 u_PointLight; // xyz = light position, w = radius (used by PSMainLinear)
};

// Mirrors FShadowMaterialConstants in FWorldRendererRHI.cpp.
cbuffer MaterialConstants : register(b2) {
    float4 u_UVTransform; // xy = tiling, zw = offset
    float4 u_AlphaTest;   // x = 1 when the albedo alpha cuts holes, y = cutoff
};

// The start of FDrawConstants in FWorldRendererRHI.cpp.
cbuffer DrawConstants : register(b3) {
    float4x4 u_Model;
};

Texture2D<float4> t_Albedo : register(t0);

struct FVertexInput {
    float3 Position : ATTRIBUTE0;
    float3 Normal : ATTRIBUTE1;
    float2 TexCoord : ATTRIBUTE2;
    float4 Tangent : ATTRIBUTE3;
    float3 Color : ATTRIBUTE4;
    float2 LightmapUV : ATTRIBUTE5;
#ifdef SKINNED
    int4 BoneIndices : ATTRIBUTE6;
    float4 BoneWeights : ATTRIBUTE7;
#endif
};

struct FVertexOutput {
    float4 Position : SV_Position;
    float3 WorldPosition : TEXCOORD0;
    float2 TexCoord : TEXCOORD1;
};

FVertexOutput VSMain(FVertexInput InVertex) {
    FVertexOutput output;
#ifdef SKINNED
    const float4 localPosition = mul(SkinMatrix(InVertex.BoneIndices, InVertex.BoneWeights), float4(InVertex.Position, 1.0));
#else
    const float4 localPosition = float4(InVertex.Position, 1.0);
#endif
    const float4 worldPosition = mul(u_Model, localPosition);
    output.Position = mul(u_LightViewProjection, worldPosition);
    output.WorldPosition = worldPosition.xyz;
    output.TexCoord = InVertex.TexCoord * u_UVTransform.xy + u_UVTransform.zw;
    return output;
}

void AlphaTest(float2 InTexCoord) {
    if (u_AlphaTest.x > 0.5 && t_Albedo.Sample(s_LinearWrap, InTexCoord).a < u_AlphaTest.y)
        discard;
}

void PSMain(FVertexOutput InPixel) {
    AlphaTest(InPixel.TexCoord);
}

float PSMainLinear(FVertexOutput InPixel) : SV_Depth {
    AlphaTest(InPixel.TexCoord);
    return saturate(length(InPixel.WorldPosition - u_PointLight.xyz) / max(u_PointLight.w, 0.0001));
}
