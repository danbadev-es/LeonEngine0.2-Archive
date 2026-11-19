// Turns the linear HDR scene into the image shown on screen: exposure, tone curve, sRGB encoding, and
// the look of the map on top (chromatic aberration, colour grading, vignette). Each part of the look is
// skipped when it is neutral, so a map without one gets exactly the plain image.
// Alpha carries perceptual luma for the anti-aliasing pass that follows. Film grain is added there,
// after anti-aliasing (FXAA.hlsl): here it would be read as edges and smeared.

#include "Common.hlsli"
#include "Fullscreen.hlsli"

// Mirrors FToneMappingConstants in FWorldRendererRHI.cpp.
cbuffer PassConstants : register(b1) {
    float4 u_ToneParams;  // x = exposure, y = bloom intensity, z = gamma, w = chromatic aberration (0 = off)
    int4 u_ToneFlags;     // x = operator (0 ACES, 1 Reinhard, 2 clamp, 3 Uncharted 2), y = use bloom, z = debug view,
                          // w = 1 when the colour grade is not neutral
    float4 u_Vignette;    // x = intensity (0 = off), y = radius, z = softness
    float4 u_GradeLift;   // rgb = lift, a = saturation
    float4 u_GradeGamma;  // rgb = 1 / gamma, a = contrast
    float4 u_GradeGain;   // rgb = gain
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

/**
 * Lift, gamma and gain on the encoded colour, then contrast and saturation.
 *   lift raises black towards the lift colour and leaves white alone;
 *   gain multiplies, so it moves white and leaves black alone;
 *   gamma bends the middle (above 1 brightens it) and leaves black and white alone;
 *   contrast scales the distance to mid grey; saturation the distance to the pixel's own grey.
 */
float3 GradeColor(float3 InColor) {
    float3 color = u_GradeGain.rgb * (InColor + u_GradeLift.rgb * (1.0 - InColor));
    color = pow(max(color, 0.0), u_GradeGamma.rgb);
    color = (color - 0.5) * u_GradeGamma.a + 0.5;
    const float grey = dot(color, float3(0.2126, 0.7152, 0.0722));
    return saturate(lerp(float3(grey, grey, grey), color, u_GradeLift.a));
}

/** 1 inside the radius, falling to 1 - intensity over the softness. Distances: 0 at the centre, 1 in the corners. */
float VignetteFactor(float2 InUV) {
    const float distance = length(InUV - 0.5) * 1.41421356;
    return 1.0 - u_Vignette.x * smoothstep(u_Vignette.y, u_Vignette.y + max(u_Vignette.z, 0.0001), distance);
}

float4 PSMain(FFullscreenOutput InPixel) : SV_Target {
    float3 sceneHDR = t_HDRScene.SampleLevel(s_LinearClamp, InPixel.TexCoord, 0.0).rgb;
    if (u_ToneParams.w > 0.0) {
        // A cheap lens: red is read a little further from the centre and blue a little nearer, more
        // towards the edges and not at all in the middle.
        const float2 shift = (InPixel.TexCoord - 0.5) * u_ToneParams.w;
        sceneHDR.r = t_HDRScene.SampleLevel(s_LinearClamp, InPixel.TexCoord + shift, 0.0).r;
        sceneHDR.b = t_HDRScene.SampleLevel(s_LinearClamp, InPixel.TexCoord - shift, 0.0).b;
    }
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

    if (u_ToneFlags.w == 1)
        encoded = GradeColor(encoded);
    if (u_Vignette.x > 0.0)
        encoded *= VignetteFactor(InPixel.TexCoord);

    return float4(encoded, dot(encoded, float3(0.299, 0.587, 0.114)));
}
