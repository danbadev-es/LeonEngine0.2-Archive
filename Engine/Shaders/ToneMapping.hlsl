// Turns the linear HDR scene into the image shown on screen: exposure, tone curve, sRGB encoding.
// Alpha carries perceptual luma for the anti-aliasing pass that follows.

#include "Common.hlsli"
#include "Fullscreen.hlsli"

// Mirrors FToneMappingConstants in FWorldRendererRHI.cpp.
cbuffer PassConstants : register(b1) {
    float4 u_ToneParams; // x = exposure, y = bloom intensity, z = gamma
    int4 u_ToneFlags;    // x = operator (0 ACES, 1 Reinhard, 2 clamp, 3 Uncharted 2), y = use bloom, z = debug view
};

Texture2D<float4> t_HDRScene : register(t0);
Texture2D<float4> t_Bloom : register(t1);

FFullscreenOutput VSMain(uint InVertexId : SV_VertexID) {
    return FullscreenVS(InVertexId);
}

// ACES filmic curve (Narkowicz fit).
float3 ACESFilm(float3 x) {
    return saturate((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14));
}

float3 ReinhardExtended(float3 x) {
    const float whitePoint = 4.0;
    return (x * (1.0 + x / (whitePoint * whitePoint))) / (1.0 + x);
}

float3 Uncharted2Partial(float3 x) {
    const float A = 0.15;
    const float B = 0.50;
    const float C = 0.10;
    const float D = 0.20;
    const float E = 0.02;
    const float F = 0.30;
    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

float3 Uncharted2(float3 x) {
    const float3 whiteScale = 1.0 / Uncharted2Partial(float3(11.2, 11.2, 11.2));
    return saturate(Uncharted2Partial(x * 2.0) * whiteScale);
}

float4 PSMain(FFullscreenOutput InPixel) : SV_Target {
    const float3 sceneHDR = t_HDRScene.SampleLevel(s_LinearClamp, InPixel.TexCoord, 0.0).rgb;
    float3 bloomHDR = float3(0.0, 0.0, 0.0);
    if (u_ToneFlags.y == 1)
        bloomHDR = t_Bloom.SampleLevel(s_LinearClamp, InPixel.TexCoord, 0.0).rgb;

    if (u_ToneFlags.z == 1)
        return float4(sceneHDR, 1.0); // raw linear scene
    if (u_ToneFlags.z == 2)
        return float4(bloomHDR * u_ToneParams.y, 1.0); // bloom only

    const float3 exposed = (sceneHDR + bloomHDR * u_ToneParams.y) * max(u_ToneParams.x, 0.0);

    float3 ldr;
    if (u_ToneFlags.x == 1)
        ldr = saturate(ReinhardExtended(exposed));
    else if (u_ToneFlags.x == 2)
        ldr = saturate(exposed);
    else if (u_ToneFlags.x == 3)
        ldr = Uncharted2(exposed);
    else
        ldr = ACESFilm(exposed);

    // Gamma 2.2 means "standard sRGB"; any other value is a plain power curve.
    float3 encoded;
    if (abs(u_ToneParams.z - 2.2) < 0.05)
        encoded = LinearToSRGB(ldr);
    else
        encoded = pow(max(ldr, 0.0), 1.0 / max(u_ToneParams.z, 0.0001));

    return float4(encoded, dot(encoded, float3(0.299, 0.587, 0.114)));
}
