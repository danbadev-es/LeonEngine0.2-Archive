// Dear ImGui (the editor's interface): textured, vertex-coloured triangles.

#include "Common.hlsli"

// Mirrors FImGuiConstants in the editor's FImGuiRHI.cpp.
cbuffer PassConstants : register(b1) {
    float4x4 u_Projection; // ImGui display space -> clip space
};

Texture2D<float4> t_Texture : register(t0);

struct FVertexInput {
    float2 Position : ATTRIBUTE0;
    float2 TexCoord : ATTRIBUTE1;
    float4 Color : ATTRIBUTE2;
};

struct FVertexOutput {
    float4 Position : SV_Position;
    float2 TexCoord : TEXCOORD0;
    float4 Color : COLOR0;
};

FVertexOutput VSMain(FVertexInput InVertex) {
    FVertexOutput output;
    output.Position = mul(u_Projection, float4(InVertex.Position, 0.0, 1.0));
    output.TexCoord = InVertex.TexCoord;
    output.Color = InVertex.Color;
    return output;
}

float4 PSMain(FVertexOutput InPixel) : SV_Target {
    return InPixel.Color * t_Texture.Sample(s_LinearClamp, InPixel.TexCoord);
}
