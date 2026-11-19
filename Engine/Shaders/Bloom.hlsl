// Bloom: the glow around very bright parts of the image.
//
// Three steps, one pixel entry point each:
//   PSBrightPass  keeps only what is brighter than a threshold, at half resolution;
//   PSDownsample  halves the result repeatedly (13-tap filter, Jimenez / Karis);
//   PSUpsample    adds the levels back together from the smallest up (3x3 tent filter).

#include "Common.hlsli"
#include "Fullscreen.hlsli"

// Mirrors FBloomConstants in FPostProcessRHI.cpp.
cbuffer PassConstants : register(b1) {
    float4 u_BloomParams; // x = threshold, y = soft knee, z = upsample filter radius, w = 1 on the first downsample
    float4 u_TexelSize;   // xy = 1 / size of the source texture
};

Texture2D<float4> t_Source : register(t0);

FFullscreenOutput VSMain(uint InVertexId : SV_VertexID) {
    return FullscreenVS(InVertexId);
}

float Luminance(float3 InColor) {
    return dot(InColor, float3(0.2126, 0.7152, 0.0722));
}

float3 Tap(float2 InUV, float InOffsetX, float InOffsetY) {
    return t_Source.SampleLevel(s_LinearClamp, InUV + float2(InOffsetX, InOffsetY) * u_TexelSize.xy, 0.0).rgb;
}

// Brighter than anything a scene lights a pixel with on purpose; what is above it is a fault
// (a highlight that overflowed, a division by nearly nothing) and is cut down to it.
static const float kMaxBloomLuminance = 512.0;

/**
 * What the bright pass may work with. A pixel that is not a number, or infinite, glows with nothing:
 * the levels below average their neighbours, so a single such pixel would otherwise turn into a
 * rectangle of them at every level and blank a large part of the picture.
 */
float3 UsableSceneColor(float3 InColor) {
    // Read from the bits: a comparison with itself is something a compiler may fold away.
    const uint3 exponent = asuint(InColor) & 0x7F800000u;
    if (any(exponent == 0x7F800000u))
        return float3(0.0, 0.0, 0.0);
    const float3 color = max(InColor, 0.0);
    const float luma = Luminance(color);
    return luma > kMaxBloomLuminance ? color * (kMaxBloomLuminance / luma) : color;
}

float4 PSBrightPass(FFullscreenOutput InPixel) : SV_Target {
    const float3 color = UsableSceneColor(t_Source.SampleLevel(s_LinearClamp, InPixel.TexCoord, 0.0).rgb);
    const float luma = max(Luminance(color), 0.0);

    // A soft knee: the cut-off fades in instead of switching on at the threshold.
    const float knee = max(u_BloomParams.y, 0.0001);
    float soft = clamp(luma - u_BloomParams.x + knee, 0.0, 2.0 * knee);
    soft = (soft * soft) / (4.0 * knee + 0.00001);
    const float contribution = max(soft, luma - u_BloomParams.x) / max(luma, 0.0001);
    return float4(color * max(contribution, 0.0), 1.0);
}

// Weights a group of samples down by its brightness, so one very bright pixel does not flicker.
float KarisWeight(float3 InColor) {
    return 1.0 / (1.0 + Luminance(InColor));
}

float4 PSDownsample(FFullscreenOutput InPixel) : SV_Target {
    const float2 uv = InPixel.TexCoord;
    // a - b - c
    // - d - e -
    // f - g - h
    // - i - j -
    // k - l - m
    const float3 a = Tap(uv, -2.0, 2.0);
    const float3 b = Tap(uv, 0.0, 2.0);
    const float3 c = Tap(uv, 2.0, 2.0);
    const float3 d = Tap(uv, -1.0, 1.0);
    const float3 e = Tap(uv, 1.0, 1.0);
    const float3 f = Tap(uv, -2.0, 0.0);
    const float3 g = Tap(uv, 0.0, 0.0);
    const float3 h = Tap(uv, 2.0, 0.0);
    const float3 i = Tap(uv, -1.0, -1.0);
    const float3 j = Tap(uv, 1.0, -1.0);
    const float3 k = Tap(uv, -2.0, -2.0);
    const float3 l = Tap(uv, 0.0, -2.0);
    const float3 m = Tap(uv, 2.0, -2.0);

    float3 result;
    if (u_BloomParams.w > 0.5) {
        // First level: brightness-weighted groups, which removes single-pixel sparkles.
        const float3 box1 = (a + b + d + g) * 0.25;
        const float3 box2 = (b + c + g + e) * 0.25;
        const float3 box3 = (d + g + f + k) * 0.25;
        const float3 box4 = (g + e + l + m) * 0.25;
        const float3 box5 = (d + e + i + j) * 0.25;
        const float w1 = KarisWeight(box1);
        const float w2 = KarisWeight(box2);
        const float w3 = KarisWeight(box3);
        const float w4 = KarisWeight(box4);
        const float w5 = KarisWeight(box5);
        result = (box1 * w1 + box2 * w2 + box3 * w3 + box4 * w4 + box5 * w5) / (w1 + w2 + w3 + w4 + w5 + 0.00001);
    } else {
        result = (d + e + i + j) * 0.125 + (a + b + f + g) * 0.03125 + (b + c + g + h) * 0.03125 +
                 (f + g + k + l) * 0.03125 + (g + h + l + m) * 0.03125;
    }
    return float4(result, 1.0);
}

float4 PSUpsample(FFullscreenOutput InPixel) : SV_Target {
    const float2 uv = InPixel.TexCoord;
    const float radius = u_BloomParams.z;
    // 1 2 1
    // 2 4 2  / 16
    // 1 2 1
    float3 result = Tap(uv, 0.0, 0.0) * 4.0;
    result += (Tap(uv, 0.0, radius) + Tap(uv, -radius, 0.0) + Tap(uv, radius, 0.0) + Tap(uv, 0.0, -radius)) * 2.0;
    result += Tap(uv, -radius, radius) + Tap(uv, radius, radius) + Tap(uv, -radius, -radius) + Tap(uv, radius, -radius);
    return float4(result / 16.0, 1.0);
}
