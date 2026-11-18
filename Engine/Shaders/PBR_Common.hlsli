#ifndef LEON_PBR_COMMON_HLSLI
#define LEON_PBR_COMMON_HLSLI

// Lighting shared by the surface shaders: the constants a lit pass provides, the Cook-Torrance
// terms, the procedural environment and fog.

#include "Common.hlsli"

#define MAX_POINT_LIGHTS 16
#define MAX_SPOT_LIGHTS 8

struct FDirectionalLight {
    float4 Direction; // xyz = direction the light travels, w = enabled (1 / 0)
    float4 Color;     // rgb = colour, a = intensity
};

struct FPointLight {
    float4 Position; // xyz = position, w = enabled (1 / 0)
    float4 Color;    // rgb = colour, a = intensity
    float4 Params;   // x = radius, y = shadow cube index (-1 = none)
};

struct FSpotLight {
    float4 Position;  // xyz = position, w = enabled (1 / 0)
    float4 Direction; // xyz = direction, w = cos(inner angle)
    float4 Color;     // rgb = colour, a = cos(outer angle)
    float4 Params;    // x = radius, y = intensity
};

// Mirrors FLitPassConstants in FWorldRendererRHI.cpp.
cbuffer PassConstants : register(b1) {
    FDirectionalLight u_DirLight;
    FPointLight u_PointLights[MAX_POINT_LIGHTS];
    FSpotLight u_SpotLights[MAX_SPOT_LIGHTS];
    int4 u_LightCounts;       // x = point lights, y = spot lights
    float4 u_EnvSkyColor;     // rgb = zenith colour, a = environment intensity
    float4 u_EnvHorizonColor; // rgb
    float4 u_EnvGroundColor;  // rgb
    float4 u_FogColorDensity; // rgb = fog colour, a = density per metre (0 = off)
    float4 u_FogParams;       // x = start distance, y = height falloff, z = base height, w = max opacity
    int4 u_PassFlags;         // x = image-based lighting available, y = debug view
    // Shadows (see Shadows.hlsli). The matrices take world positions to shadow-map space.
    float4x4 u_LightSpaceMatrices[4]; // one per sun cascade
    float4x4 u_SpotLightSpaceMatrix;
    float4 u_CascadeSplits;   // far distance of each cascade along the camera's forward axis
    float4 u_ShadowParams;    // x = constant bias, y = slope bias, z = normal offset, w = cascade blend width
    int4 u_ShadowSettings;    // x = filter (0 hard, 1 3x3, 2 5x5, 3 Poisson), y = spot light with the shadow map, z = cascades
    int4 u_ShadowFlags;       // 1 where a map was drawn this frame: x = sun, y = spot, z = point
};

// --- Cook-Torrance -----------------------------------------------------------------------------

float DistributionGGX(float3 N, float3 H, float InRoughness) {
    const float a = InRoughness * InRoughness;
    const float a2 = a * a;
    const float NdotH = max(dot(N, H), 0.0);
    const float f = NdotH * NdotH * (a2 - 1.0) + 1.0;
    const float denominator = kPi * f * f;
    return denominator > 1e-20 ? a2 / denominator : 0.0;
}

float GeometrySchlickGGX(float NdotV, float InRoughness) {
    const float r = InRoughness + 1.0;
    const float k = (r * r) / 8.0;
    return NdotV / max(NdotV * (1.0 - k) + k, 0.0000001);
}

float GeometrySmith(float3 N, float3 V, float3 L, float InRoughness) {
    return GeometrySchlickGGX(max(dot(N, L), 0.0), InRoughness) * GeometrySchlickGGX(max(dot(N, V), 0.0), InRoughness);
}

float3 FresnelSchlick(float InCosTheta, float3 F0) {
    return F0 + (1.0 - F0) * pow(saturate(1.0 - InCosTheta), 5.0);
}

float3 FresnelSchlickRoughness(float InCosTheta, float3 F0, float InRoughness) {
    const float3 ceiling = max(float3(1.0, 1.0, 1.0) * (1.0 - InRoughness), F0);
    return F0 + (ceiling - F0) * pow(saturate(1.0 - InCosTheta), 5.0);
}

float3 SafeHalfVector(float3 V, float3 L) {
    const float3 h = V + L;
    const float h2 = dot(h, h);
    return h2 > 1e-8 ? h * rsqrt(h2) : float3(0.0, 0.0, 0.0);
}

/** What a surface needs to answer "how much of this light do I reflect". */
struct FSurface {
    float3 Albedo;
    float3 N;
    float3 V;
    float3 F0;
    float Metallic;
    float Roughness;
};

/**
 * Light reflected towards the viewer from one light of the given radiance arriving along L.
 * Diffuse and specular are returned apart because the debug views show them separately.
 */
void ShadeLight(FSurface InSurface, float3 L, float3 InRadiance, inout float3 OutDiffuse, inout float3 OutSpecular) {
    const float3 H = SafeHalfVector(InSurface.V, L);
    const float NdotL = max(dot(InSurface.N, L), 0.0);
    const float NdotV = max(dot(InSurface.N, InSurface.V), 0.0);

    const float distribution = DistributionGGX(InSurface.N, H, InSurface.Roughness);
    const float geometry = GeometrySmith(InSurface.N, InSurface.V, L, InSurface.Roughness);
    const float3 fresnel = FresnelSchlick(max(dot(H, InSurface.V), 0.0), InSurface.F0);

    const float3 specular = (distribution * geometry * fresnel) / (4.0 * NdotV * NdotL + 0.0001);
    const float3 diffuseWeight = (1.0 - fresnel) * (1.0 - InSurface.Metallic);

    const float3 arriving = InRadiance * NdotL;
    OutDiffuse += (diffuseWeight * InSurface.Albedo / kPi) * arriving;
    OutSpecular += specular * arriving;
}

/** Inverse-square falloff that reaches exactly zero at the light's radius. */
float LightAttenuation(float InDistance, float InRadius) {
    const float ratio = InDistance / max(InRadius, 0.0001);
    const float window = saturate(1.0 - ratio * ratio * ratio * ratio);
    return (window * window) / (InDistance * InDistance + 1.0);
}

// --- Environment without image-based lighting ---------------------------------------------------

float3 SampleEnvironmentAtmosphere(float3 InDirection) {
    const float height = InDirection.y;
    float3 sky;
    if (height >= 0.0)
        sky = lerp(u_EnvSkyColor.rgb, u_EnvHorizonColor.rgb, pow(1.0 - height, 4.0));
    else
        sky = lerp(u_EnvHorizonColor.rgb, u_EnvGroundColor.rgb, saturate(-height * 3.0));
    return sky * u_EnvSkyColor.a;
}

float3 GetHemisphereIrradiance(float3 InNormal) {
    const float upFraction = InNormal.y * 0.5 + 0.5;
    return lerp(u_EnvGroundColor.rgb, u_EnvSkyColor.rgb, upFraction) * u_EnvSkyColor.a * kPi;
}

// --- Fog ----------------------------------------------------------------------------------------

/** Distance and height fog. A density of zero returns the colour untouched. */
float3 ApplyDistanceFog(float3 InColor, float3 InWorldPosition) {
    const float density = u_FogColorDensity.a;
    if (density <= 0.0)
        return InColor;
    const float distance = max(length(InWorldPosition - u_ViewPosition.xyz) - u_FogParams.x, 0.0);
    float heightFactor = 1.0;
    if (u_FogParams.y > 0.0)
        heightFactor = exp(-max(InWorldPosition.y - u_FogParams.z, 0.0) * u_FogParams.y);
    const float fog = min(1.0 - exp(-distance * density * heightFactor), u_FogParams.w);
    return lerp(InColor, u_FogColorDensity.rgb, fog);
}

#endif
