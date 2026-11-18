// Unlit coloured geometry in world space: debug lines, and particles (as triangles).

#include "Common.hlsli"

struct FVertexInput {
    float3 Position : ATTRIBUTE0;
    float4 Color : ATTRIBUTE1;
};

struct FVertexOutput {
    float4 Position : SV_Position;
    float4 Color : COLOR0;
};

FVertexOutput VSMain(FVertexInput InVertex) {
    FVertexOutput output;
    output.Position = mul(u_ViewProjection, float4(InVertex.Position, 1.0));
    // A slight pull towards the camera keeps lines drawn on a surface visible, while staying small
    // enough for geometry in front to hide them.
    output.Position.z -= 0.0002 * output.Position.w;
    output.Color = InVertex.Color;
    return output;
}

float4 PSMain(FVertexOutput InPixel) : SV_Target {
    return InPixel.Color;
}
