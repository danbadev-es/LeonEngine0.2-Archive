// Turns the capture of a reflection probe into the levels the surface shader reads: level n of a
// probe's cube holds the scene as a surface of roughness n / 4 reflects it (the GGX lobe around the
// mirror direction, as for the sky's prefiltered map). Drawn once per face and level with a
// full-screen triangle; PSShrink makes the smaller copy of the capture the wide lobes read from.

#include "Common.hlsli"
#include "Fullscreen.hlsli"

// Mirrors FProbeFilterConstants in FShaderConstants.hpp.
cbuffer PassConstants : register(b1) {
    float4 u_FaceForward; // xyz = direction at the centre of the face being drawn
    float4 u_FaceRight;   // xyz = direction added across the face, left to right
    float4 u_FaceUp;      // xyz = direction added across the face, bottom to top
    float4 u_FilterParams; // x = roughness, y = number of samples, z = 1 / size of a source face
};

TextureCube<float4> t_Source : register(t0);

FFullscreenOutput VSMain(uint InVertexId : SV_VertexID) {
    return FullscreenVS(InVertexId);
}

float3 FaceDirection(float2 InTexCoord) {
    return normalize(u_FaceForward.xyz + (InTexCoord.x * 2.0 - 1.0) * u_FaceRight.xyz +
                     (1.0 - InTexCoord.y * 2.0) * u_FaceUp.xyz);
}

float RadicalInverse(uint InBits) {
    InBits = (InBits << 16u) | (InBits >> 16u);
    InBits = ((InBits & 0x55555555u) << 1u) | ((InBits & 0xAAAAAAAAu) >> 1u);
    InBits = ((InBits & 0x33333333u) << 2u) | ((InBits & 0xCCCCCCCCu) >> 2u);
    InBits = ((InBits & 0x0F0F0F0Fu) << 4u) | ((InBits & 0xF0F0F0F0u) >> 4u);
    InBits = ((InBits & 0x00FF00FFu) << 8u) | ((InBits & 0xFF00FF00u) >> 8u);
    return float(InBits) * 2.3283064365386963e-10;
}

/** A half vector of the GGX distribution around N. */
float3 ImportanceSampleGGX(float2 InPoint, float3 N, float InRoughness) {
    const float a = InRoughness * InRoughness;
    const float phi = 2.0 * kPi * InPoint.x;
    const float cosTheta = sqrt((1.0 - InPoint.y) / (1.0 + (a * a - 1.0) * InPoint.y));
    const float sinTheta = sqrt(max(1.0 - cosTheta * cosTheta, 0.0));
    const float3 up = abs(N.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    const float3 tangent = normalize(cross(up, N));
    const float3 bitangent = cross(N, tangent);
    return normalize(tangent * (sinTheta * cos(phi)) + bitangent * (sinTheta * sin(phi)) + N * cosTheta);
}

float4 PSMain(FFullscreenOutput InPixel) : SV_Target {
    const float3 N = FaceDirection(InPixel.TexCoord);
    const float roughness = u_FilterParams.x;
    if (roughness <= 0.0)
        return float4(t_Source.SampleLevel(s_LinearClamp, N, 0.0).rgb, 1.0);

    // The view and the mirror direction are taken along the normal, as for the sky's map.
    const uint sampleCount = uint(max(u_FilterParams.y, 1.0));
    float3 sum = float3(0.0, 0.0, 0.0);
    float weight = 0.0;
    for (uint index = 0u; index < sampleCount; ++index) {
        const float2 point2 = float2((float(index) + 0.5) / float(sampleCount), RadicalInverse(index));
        const float3 H = ImportanceSampleGGX(point2, N, roughness);
        const float3 L = normalize(2.0 * dot(N, H) * H - N);
        const float NdotL = dot(N, L);
        if (NdotL <= 0.0)
            continue;
        sum += t_Source.SampleLevel(s_LinearClamp, L, 0.0).rgb * NdotL;
        weight += NdotL;
    }
    return float4(sum / max(weight, 1.0e-4), 1.0);
}

/** A face a quarter of the size: each pixel is the average of the sixteen source pixels it covers. */
float4 PSShrink(FFullscreenOutput InPixel) : SV_Target {
    // Four bilinear reads, each at the corner shared by four source pixels.
    const float2 offsets[4] = {float2(-1.0, -1.0), float2(1.0, -1.0), float2(-1.0, 1.0), float2(1.0, 1.0)};
    float3 sum = float3(0.0, 0.0, 0.0);
    for (int i = 0; i < 4; ++i)
        sum += t_Source.SampleLevel(s_LinearClamp, FaceDirection(InPixel.TexCoord + offsets[i] * u_FilterParams.z), 0.0).rgb;
    return float4(sum * 0.25, 1.0);
}
