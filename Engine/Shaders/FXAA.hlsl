// FXAA 3.11 (quality preset): smooths jagged edges in the finished image.
// Expects perceptual luma in the alpha channel, which the tone mapping pass writes.
// The last pass of the image, so film grain is added here, on top of the smoothed result.

#include "Common.hlsli"
#include "Fullscreen.hlsli"

// Mirrors FFXAAConstants in FPostProcessRHI.cpp.
cbuffer PassConstants : register(b1) {
    float4 u_FXAAParams;  // xy = 1 / size of the image, z = 1 when enabled (0 copies the image)
    float4 u_GrainParams; // x = film grain intensity (0 = off), y = seed (a whole number)
};

Texture2D<float4> t_Image : register(t0);

#define EDGE_THRESHOLD_MIN 0.0312
#define EDGE_THRESHOLD_MAX 0.125
#define SUBPIXEL_QUALITY 0.75
#define ITERATIONS 12

static const float kStepScale[ITERATIONS] = {1.0, 1.0, 1.0, 1.0, 1.0, 1.5, 2.0, 2.0, 2.0, 2.0, 4.0, 8.0};

FFullscreenOutput VSMain(uint InVertexId : SV_VertexID) {
    return FullscreenVS(InVertexId);
}

float LumaAt(float2 InUV) {
    return t_Image.SampleLevel(s_LinearClamp, InUV, 0.0).a;
}

float LumaOffset(float2 InUV, int InX, int InY) {
    return t_Image.SampleLevel(s_LinearClamp, InUV + float2(float(InX), float(InY)) * u_FXAAParams.xy, 0.0).a;
}

uint HashBits(uint InValue) {
    uint value = InValue;
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return value;
}

/** The same for a pixel and a seed on every machine, uniform in -0.5..0.5 with mean zero. */
float GrainNoise(uint2 InPixel, uint InSeed) {
    const uint bits = HashBits(InPixel.x + HashBits(InPixel.y + HashBits(InSeed)));
    return (float(bits >> 8) + 0.5) / 16777216.0 - 0.5;
}

float4 AntiAlias(FFullscreenOutput InPixel) {
    const float2 uv = InPixel.TexCoord;
    const float2 inverseSize = u_FXAAParams.xy;
    const float4 center = t_Image.SampleLevel(s_LinearClamp, uv, 0.0);
    if (u_FXAAParams.z < 0.5)
        return float4(center.rgb, 1.0);

    // "North" and "south" are only names here: the algorithm is symmetric in each axis.
    const float lumaM = center.a > 0.0 ? center.a : dot(center.rgb, float3(0.299, 0.587, 0.114));
    const float lumaN = LumaOffset(uv, 0, 1);
    const float lumaS = LumaOffset(uv, 0, -1);
    const float lumaE = LumaOffset(uv, 1, 0);
    const float lumaW = LumaOffset(uv, -1, 0);

    const float lumaMin = min(lumaM, min(min(lumaN, lumaS), min(lumaE, lumaW)));
    const float lumaMax = max(lumaM, max(max(lumaN, lumaS), max(lumaE, lumaW)));
    const float lumaRange = lumaMax - lumaMin;
    // Not an edge: leave the pixel alone.
    if (lumaRange < max(EDGE_THRESHOLD_MIN, lumaMax * EDGE_THRESHOLD_MAX))
        return float4(center.rgb, 1.0);

    const float lumaNW = LumaOffset(uv, -1, 1);
    const float lumaNE = LumaOffset(uv, 1, 1);
    const float lumaSW = LumaOffset(uv, -1, -1);
    const float lumaSE = LumaOffset(uv, 1, -1);

    const float lumaDownUp = lumaN + lumaS;
    const float lumaLeftRight = lumaE + lumaW;
    const float lumaCorners = lumaNW + lumaNE + lumaSW + lumaSE;

    // Is the edge horizontal or vertical?
    const float edgeHorizontal = abs(-2.0 * lumaM + lumaDownUp) * 2.0 + abs(-2.0 * lumaE + (lumaNE + lumaSE)) +
                                 abs(-2.0 * lumaW + (lumaNW + lumaSW));
    const float edgeVertical = abs(-2.0 * lumaM + lumaLeftRight) * 2.0 + abs(-2.0 * lumaN + (lumaNW + lumaNE)) +
                               abs(-2.0 * lumaS + (lumaSW + lumaSE));
    const bool bHorizontal = edgeHorizontal >= edgeVertical;

    // Which side of the pixel the edge is on.
    const float luma1 = bHorizontal ? lumaS : lumaW;
    const float luma2 = bHorizontal ? lumaN : lumaE;
    const float gradient1 = luma1 - lumaM;
    const float gradient2 = luma2 - lumaM;
    const bool bFirstSteeper = abs(gradient1) >= abs(gradient2);
    const float gradientScaled = 0.25 * max(abs(gradient1), abs(gradient2));

    float stepLength = bHorizontal ? inverseSize.y : inverseSize.x;
    float lumaLocalAverage;
    if (bFirstSteeper) {
        stepLength = -stepLength;
        lumaLocalAverage = 0.5 * (luma1 + lumaM);
    } else {
        lumaLocalAverage = 0.5 * (luma2 + lumaM);
    }

    float2 edgeUV = uv;
    if (bHorizontal)
        edgeUV.y += stepLength * 0.5;
    else
        edgeUV.x += stepLength * 0.5;

    // Walk along the edge both ways until it ends.
    const float2 along = bHorizontal ? float2(inverseSize.x, 0.0) : float2(0.0, inverseSize.y);
    float2 uvPositive = edgeUV + along;
    float2 uvNegative = edgeUV - along;
    float lumaEndPositive = LumaAt(uvPositive) - lumaLocalAverage;
    float lumaEndNegative = LumaAt(uvNegative) - lumaLocalAverage;
    bool bReachedPositive = abs(lumaEndPositive) >= gradientScaled;
    bool bReachedNegative = abs(lumaEndNegative) >= gradientScaled;
    if (!bReachedPositive)
        uvPositive += along;
    if (!bReachedNegative)
        uvNegative -= along;

    for (int i = 2; i < ITERATIONS && !(bReachedPositive && bReachedNegative); ++i) {
        if (!bReachedPositive)
            lumaEndPositive = LumaAt(uvPositive) - lumaLocalAverage;
        if (!bReachedNegative)
            lumaEndNegative = LumaAt(uvNegative) - lumaLocalAverage;
        bReachedPositive = abs(lumaEndPositive) >= gradientScaled;
        bReachedNegative = abs(lumaEndNegative) >= gradientScaled;
        if (!bReachedPositive)
            uvPositive += along * kStepScale[i];
        if (!bReachedNegative)
            uvNegative -= along * kStepScale[i];
    }

    // How far along the edge the pixel is decides how much it blends with its neighbour.
    const float distancePositive = bHorizontal ? (uvPositive.x - uv.x) : (uvPositive.y - uv.y);
    const float distanceNegative = bHorizontal ? (uv.x - uvNegative.x) : (uv.y - uvNegative.y);
    const bool bNearerPositive = distancePositive < distanceNegative;
    const float pixelOffset = -min(distancePositive, distanceNegative) / (distancePositive + distanceNegative) + 0.5;

    const bool bCenterDarker = lumaM < lumaLocalAverage;
    const bool bCorrectVariation = ((bNearerPositive ? lumaEndPositive : lumaEndNegative) < 0.0) != bCenterDarker;
    float finalOffset = bCorrectVariation ? pixelOffset : 0.0;

    // Single bright or dark pixels: blend by how much the pixel differs from its neighbourhood.
    const float lumaAverage = (1.0 / 12.0) * (2.0 * (lumaDownUp + lumaLeftRight) + lumaCorners);
    const float subPixel1 = saturate(abs(lumaAverage - lumaM) / lumaRange);
    const float subPixel2 = (-2.0 * subPixel1 + 3.0) * subPixel1 * subPixel1;
    finalOffset = max(finalOffset, subPixel2 * subPixel2 * SUBPIXEL_QUALITY);

    float2 finalUV = uv;
    if (bHorizontal)
        finalUV.y += finalOffset * stepLength;
    else
        finalUV.x += finalOffset * stepLength;
    return float4(t_Image.SampleLevel(s_LinearClamp, finalUV, 0.0).rgb, 1.0);
}

float4 PSMain(FFullscreenOutput InPixel) : SV_Target {
    float4 color = AntiAlias(InPixel);
    if (u_GrainParams.x > 0.0) {
        // One value for the three channels: grain is a change of brightness, not of colour.
        const float grain = GrainNoise(uint2(InPixel.Position.xy), uint(u_GrainParams.y));
        color.rgb = saturate(color.rgb + grain * u_GrainParams.x);
    }
    return color;
}
