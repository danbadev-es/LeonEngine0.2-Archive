// Halves an image: each output pixel is the average of the four source pixels it covers.
// Used to build the mip chain of a render target (IRHICommandList::GenerateMips), so it declares no
// constants: one bilinear sample at the centre of the output pixel reads all four.

#include "Common.hlsli"
#include "Fullscreen.hlsli"

Texture2D<float4> t_Source : register(t0);

FFullscreenOutput VSMain(uint InVertexId : SV_VertexID) {
    return FullscreenVS(InVertexId);
}

float4 PSMain(FFullscreenOutput InPixel) : SV_Target {
    return t_Source.SampleLevel(s_LinearClamp, InPixel.TexCoord, 0.0);
}
