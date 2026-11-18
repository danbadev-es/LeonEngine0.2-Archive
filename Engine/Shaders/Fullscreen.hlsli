#ifndef LEON_FULLSCREEN_HLSLI
#define LEON_FULLSCREEN_HLSLI

// A triangle that covers the whole target, generated from the vertex index: draw 3 vertices with no
// vertex buffer. TexCoord is (0,0) at the top left of the target and (1,1) at the bottom right.

struct FFullscreenOutput {
    float4 Position : SV_Position;
    float2 TexCoord : TEXCOORD0;
};

FFullscreenOutput FullscreenVS(uint InVertexId : SV_VertexID) {
    FFullscreenOutput output;
    output.TexCoord = float2((InVertexId << 1) & 2, InVertexId & 2);
    output.Position = float4(output.TexCoord.x * 2.0 - 1.0, 1.0 - output.TexCoord.y * 2.0, 0.0, 1.0);
    return output;
}

#endif
