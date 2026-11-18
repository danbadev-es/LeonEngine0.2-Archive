// Opaque surface shading for static geometry.
//
// First stage of the Direct3D 12 port: geometry, base colour and a light fixed to the camera, so the
// level is readable while lights, lightmaps, fog, IBL and shadows are brought over.

#include "Common.hlsli"

cbuffer MaterialConstants : register(b2) {
    float4 u_BaseColor; // rgb = albedo, a = opacity
};

cbuffer DrawConstants : register(b3) {
    float4x4 u_Model;
    float4x4 u_NormalMatrix; // inverse transpose of u_Model (upper 3x3 used)
};

struct FVertexInput {
    float3 Position : ATTRIBUTE0;
    float3 Normal : ATTRIBUTE1;
    float2 TexCoord : ATTRIBUTE2;
    float4 Tangent : ATTRIBUTE3;
    float3 Color : ATTRIBUTE4;
    float2 LightmapUV : ATTRIBUTE5;
};

struct FVertexOutput {
    float4 Position : SV_Position;
    float3 WorldPosition : TEXCOORD0;
    float3 WorldNormal : TEXCOORD1;
    float2 TexCoord : TEXCOORD2;
};

FVertexOutput VSMain(FVertexInput InVertex) {
    FVertexOutput output;
    const float4 worldPosition = mul(u_Model, float4(InVertex.Position, 1.0));
    output.Position = mul(u_ViewProjection, worldPosition);
    output.WorldPosition = worldPosition.xyz;
    output.WorldNormal = mul((float3x3)u_NormalMatrix, InVertex.Normal);
    output.TexCoord = InVertex.TexCoord;
    return output;
}

float4 PSMain(FVertexOutput InPixel) : SV_Target {
    const float3 normal = normalize(InPixel.WorldNormal);
    const float3 toCamera = normalize(u_ViewPosition.xyz - InPixel.WorldPosition);
    // A light at the camera: surfaces facing the viewer are bright, grazing ones dark.
    const float facing = saturate(dot(normal, toCamera));
    const float3 color = u_BaseColor.rgb * (0.18 + 0.82 * facing);
    return float4(LinearToSRGB(color), 1.0);
}
