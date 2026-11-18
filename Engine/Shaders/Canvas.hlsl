// 2D shapes on top of the finished frame: HUD panels and text, debug overlays.

#include "Common.hlsli"

// Mirrors FCanvasConstants in FImmediateDrawRHI.cpp.
cbuffer PassConstants : register(b1) {
    float4x4 u_Projection; // canvas units (pixels, origin top left) -> clip space
    int4 u_CanvasFlags;    // x = 1 when the alpha comes multiplied by the texture's (glyphs)
};

Texture2D<float4> t_Alpha : register(t0);

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
    float alpha = InPixel.Color.a;
    if (u_CanvasFlags.x == 1)
        alpha *= t_Alpha.Sample(s_LinearClamp, InPixel.TexCoord).a;
    return float4(InPixel.Color.rgb, alpha);
}
