// Screen-space ambient occlusion from the depth buffer alone: creases and contact areas get darker.
//
// Three steps, one pixel entry point each:
//   PSOcclusion  estimates occlusion at half resolution;
//   PSBlur       smooths it without crossing depth edges (run horizontally, then vertically);
//   PSComposite  darkens the HDR scene with it.

#include "Common.hlsli"
#include "Fullscreen.hlsli"

// Mirrors FSSAOConstants in FPostProcessRHI.cpp.
cbuffer PassConstants : register(b1) {
    float4x4 u_Projection;        // view space -> clip space (depth 0..1)
    float4x4 u_InverseProjection; // clip space -> view space
    float4 u_Samples[16];         // xyz = points in a unit hemisphere around +Z
    float4 u_SSAOParams;          // x = radius (metres), y = bias, z = number of samples, w = intensity
    float4 u_BlurParams;          // xy = 1 / size of the occlusion texture, z = 1 for the horizontal pass, w = 1 to show occlusion only
};

Texture2D<float> t_Depth : register(t0);  // scene depth (PSOcclusion, PSBlur)
Texture2D<float4> t_Input : register(t1); // occlusion so far (PSBlur, PSComposite)
Texture2D<float4> t_Scene : register(t2); // HDR scene (PSComposite)

FFullscreenOutput VSMain(uint InVertexId : SV_VertexID) {
    return FullscreenVS(InVertexId);
}

/**
 * Depth of the texel under a screen position, read exactly (no filtering between surfaces).
 * OutTexelUV is the screen position of that texel's centre: positions must be rebuilt from it, not
 * from InUV, or they drift off the surface by up to half a texel.
 *
 * The occlusion pass runs at half resolution, where every pixel centre falls exactly on the corner
 * shared by four depth texels. The small offset makes the choice between them the same everywhere;
 * left to rounding it changed from column to column and drew stripes on walls seen at an angle.
 */
float LoadDepth(float2 InUV, out float2 OutTexelUV) {
    float width;
    float height;
    t_Depth.GetDimensions(width, height);
    const float2 size = float2(width, height);
    const int2 pixel = clamp(int2(floor(saturate(InUV) * size + 0.01)), int2(0, 0), int2(size) - 1);
    OutTexelUV = (float2(pixel) + 0.5) / size;
    return t_Depth.Load(int3(pixel, 0));
}

float LoadDepth(float2 InUV) {
    float2 texelUV;
    return LoadDepth(InUV, texelUV);
}

float3 ReconstructViewPosition(float2 InUV, float InDepth) {
    const float4 clip = float4(InUV.x * 2.0 - 1.0, 1.0 - InUV.y * 2.0, InDepth, 1.0);
    const float4 view = mul(u_InverseProjection, clip);
    return view.xyz / view.w;
}

float InterleavedGradientNoise(float2 InPixel) {
    return frac(52.9829189 * frac(dot(InPixel, float2(0.06711056, 0.00583715))));
}

float4 PSOcclusion(FFullscreenOutput InPixel) : SV_Target {
    // Rebuild the view-space position and the surface's plane, look at points in the hemisphere above
    // it, and count those where the depth buffer holds something clearly in front of that plane.
    // Comparing depths alone would make large floors occlude themselves.
    float2 texelUV;
    const float depth = LoadDepth(InPixel.TexCoord, texelUV);
    if (depth >= 0.999)
        return float4(1.0, 1.0, 1.0, 1.0); // sky

    const float3 viewPosition = ReconstructViewPosition(texelUV, depth);
    float3 normal = normalize(cross(ddx(viewPosition), ddy(viewPosition)));
    if (dot(normal, -viewPosition) < 0.0)
        normal = -normal;

    // A different rotation of the sample pattern per pixel trades banding for noise the blur removes.
    const float angle = InterleavedGradientNoise(InPixel.Position.xy) * 6.28318530718;
    const float3 rotated = float3(cos(angle), sin(angle), 0.0);
    const float3 tangent = normalize(rotated - normal * dot(rotated, normal));
    const float3 bitangent = cross(normal, tangent);

    const float radius = max(u_SSAOParams.x, 1e-4);
    const float planeBias = max(u_SSAOParams.y, radius * 0.18);
    const int sampleCount = clamp(int(u_SSAOParams.z), 1, 16);
    float occlusion = 0.0;
    for (int i = 0; i < sampleCount; ++i) {
        const float3 offset = tangent * u_Samples[i].x + bitangent * u_Samples[i].y + normal * u_Samples[i].z;
        const float4 clip = mul(u_Projection, float4(viewPosition + offset * radius, 1.0));
        if (abs(clip.w) < 1e-5)
            continue;
        const float2 sampleUV = float2(clip.x / clip.w * 0.5 + 0.5, 0.5 - clip.y / clip.w * 0.5);
        if (sampleUV.x < 0.0 || sampleUV.x > 1.0 || sampleUV.y < 0.0 || sampleUV.y > 1.0)
            continue;
        float2 sampleTexelUV;
        const float sampleDepth = LoadDepth(sampleUV, sampleTexelUV);
        if (sampleDepth >= 0.999)
            continue;
        const float3 toOccluder = ReconstructViewPosition(sampleTexelUV, sampleDepth) - viewPosition;
        const float distance = length(toOccluder);
        if (distance < 1e-4 || distance > radius)
            continue;
        // Points on the same plane sit at ~0 along the normal; counting them paints a moving band.
        if (dot(toOccluder, normal) <= planeBias)
            continue;
        occlusion += 1.0 - smoothstep(radius * 0.55, radius, distance);
    }
    const float ao = 1.0 - occlusion / float(sampleCount);
    return float4(ao, ao, ao, 1.0);
}

float4 PSBlur(FFullscreenOutput InPixel) : SV_Target {
    static const float kKernel[5] = {0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216};
    const float centerDepth = LoadDepth(InPixel.TexCoord);
    float result = 0.0;
    float weightSum = 0.0;
    for (int i = -4; i <= 4; ++i) {
        const float2 offset = u_BlurParams.z > 0.5 ? float2(float(i) * u_BlurParams.x, 0.0)
                                                   : float2(0.0, float(i) * u_BlurParams.y);
        const float2 uv = InPixel.TexCoord + offset;
        // Samples at a different depth belong to another surface: they count for little.
        const float weight = kKernel[min(abs(i), 4)] / (1.0 + abs(centerDepth - LoadDepth(uv)) * 80.0);
        result += t_Input.SampleLevel(s_LinearClamp, uv, 0.0).r * weight;
        weightSum += weight;
    }
    const float ao = result / max(weightSum, 1e-4);
    return float4(ao, ao, ao, 1.0);
}

float4 PSComposite(FFullscreenOutput InPixel) : SV_Target {
    const float3 hdr = t_Scene.SampleLevel(s_LinearClamp, InPixel.TexCoord, 0.0).rgb;
    float ao = t_Input.SampleLevel(s_LinearClamp, InPixel.TexCoord, 0.0).r;
    ao = lerp(1.0, ao, clamp(u_SSAOParams.w, 0.0, 2.0));
    if (u_BlurParams.w > 0.5)
        return float4(ao, ao, ao, 1.0);

    // There is no separate lighting buffer, so occlusion can only darken the final colour. Bright
    // pixels are mostly reflections and highlights, which occlusion should not dim: keep those.
    const float luminance = max(dot(hdr, float3(0.2126, 0.7152, 0.0722)), 0.0);
    return float4(hdr * lerp(ao, 1.0, smoothstep(0.2, 1.25, luminance)), 1.0);
}
