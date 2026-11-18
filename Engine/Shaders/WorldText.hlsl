// Text placed in the world: quads in world space textured with a font atlas.

#include "Common.hlsli"

Texture2D<float4> t_FontAtlas : register(t0);

struct FVertexInput {
    float3 Position : ATTRIBUTE0;
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
    output.Position = mul(u_ViewProjection, float4(InVertex.Position, 1.0));
    output.TexCoord = InVertex.TexCoord;
    output.Color = InVertex.Color;
    return output;
}

float4 PSMain(FVertexOutput InPixel) : SV_Target {
    const float alpha = t_FontAtlas.Sample(s_LinearClamp, InPixel.TexCoord).a;
    // The empty part of a glyph's quad must not write depth, or it would hide what is behind it.
    if (alpha < 0.01)
        discard;
    return float4(InPixel.Color.rgb, InPixel.Color.a * alpha);
}
