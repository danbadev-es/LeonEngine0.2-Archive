// Light in the air: the beam of a flashlight, the cone under a lamp, and the dark shafts that whatever
// stands in them cuts out.
//
// Three steps, one pixel entry point each:
//   PSScatter    at half resolution, follows each pixel's view ray from the camera to the surface it
//                ends on and adds up the light the air sends towards the camera along it;
//   PSBlur       smooths the result without crossing depth edges (run horizontally, then vertically);
//   PSComposite  lays it over the HDR scene at full resolution: scene * a + rgb.
//
// The air has one density everywhere, so what it takes out of a ray is exp(-density * distance) and
// needs no marching. Only the light of the lamps does: it is there in part of the ray only and is cut
// by shadows. Each light is sampled along the piece of the ray it can reach (inside its radius and,
// for a spot light, inside its cone), a fixed number of steps offset per pixel so that too few steps
// show as noise, which the blur removes, and not as bands.
//
// Only lights with a shadow map this frame are followed: without one a lamp would shine through walls.

#include "Common.hlsli"
#include "Fullscreen.hlsli"

// Keep in sync with kMaxVolumetricFogSpotLights / kMaxVolumetricFogPointLights.
#define MAX_FOG_SPOT_LIGHTS 4
#define MAX_FOG_POINT_LIGHTS 4

struct FFogSpotLight {
    float4 Position;  // xyz = position, w = radius
    float4 Direction; // xyz = direction (unit length), w = cos(inner angle)
    float4 Color;     // rgb = colour * intensity, a = cos(outer angle)
    float4 Shadow;    // x = layer of the spot shadow map
};

struct FFogPointLight {
    float4 Position; // xyz = position, w = radius
    float4 Color;    // rgb = colour * intensity, a = cube of the point shadow map
};

// Mirrors FVolumetricFogConstants in FShaderConstants.hpp.
cbuffer PassConstants : register(b1) {
    float4x4 u_ClipToWorld;   // clip space (depth 0..1) -> world space
    float4 u_CameraPosition;  // xyz = camera, w = metres from it beyond which the air is not looked at
    float4 u_FogParams;       // x = density (extinction per metre), y = scattering, z = Henyey-Greenstein g, w = steps
    float4 u_FogAmbient;      // rgb = light the air has from everywhere, a = 1: the air also dims the scene behind it
    float4 u_DepthParams;     // view depth of a depth-buffer value = x / (y - value)
    float4 u_BlurParams;      // xy = from one tap of the blur to the next, in texture coordinates
    int4 u_FogLightCounts;    // x = spot lights, y = point lights
    FFogSpotLight u_FogSpotLights[MAX_FOG_SPOT_LIGHTS];
    float4x4 u_FogSpotMatrices[MAX_FOG_SPOT_LIGHTS]; // world position -> shadow-map space, one per spot light
    FFogPointLight u_FogPointLights[MAX_FOG_POINT_LIGHTS];
};

Texture2D<float> t_Depth : register(t0);              // scene depth (every step)
Texture2DArray<float> t_SpotShadow : register(t1);    // PSScatter
TextureCubeArray<float> t_PointShadow : register(t2); // PSScatter
Texture2D<float4> t_Fog : register(t3);               // rgb = light scattered towards the camera, a = how much of the scene is left (PSBlur, PSComposite)

// A sample is tested against the shadow map this far (metres) towards the light: the air next to a
// lit surface must not count as shadowed by that surface.
static const float kShadowPull = 0.05;

FFullscreenOutput VSMain(uint InVertexId : SV_VertexID) {
    return FullscreenVS(InVertexId);
}

/**
 * Depth of the texel under a screen position, read exactly, and the screen position of that texel's
 * centre. At half resolution every pixel centre falls on the corner shared by four depth texels; the
 * small offset makes the choice between them the same everywhere (see SSAO.hlsl).
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

float3 WorldPositionAt(float2 InUV, float InDepth) {
    const float4 world = mul(u_ClipToWorld, float4(InUV.x * 2.0 - 1.0, 1.0 - InUV.y * 2.0, InDepth, 1.0));
    return world.xyz / world.w;
}

/** Distance in front of the camera, in metres, of a value of the depth buffer. */
float ViewDepth(float InDepth) {
    return u_DepthParams.x / max(u_DepthParams.y - InDepth, 1e-6);
}

/** How much a neighbour at another depth counts: surfaces more than a tenth of their distance apart do not mix. */
float DepthWeight(float InCenterDepth, float InDepth) {
    return saturate(1.0 - abs(InDepth - InCenterDepth) / (0.08 * InCenterDepth + 0.04));
}

float InterleavedGradientNoise(float2 InPixel) {
    return frac(52.9829189 * frac(dot(InPixel, float2(0.06711056, 0.00583715))));
}

/** The falloff of the surface shader's lights (LightAttenuation in PBR_Common.hlsli): the air is lit as surfaces are. */
float LightAttenuation(float InDistance, float InRadius) {
    const float ratio = InDistance / max(InRadius, 0.0001);
    const float window = saturate(1.0 - ratio * ratio * ratio * ratio);
    return (window * window) / (InDistance * InDistance + 1.0);
}

/**
 * Henyey-Greenstein: the share of the light arriving at a point of the air that leaves it per unit of
 * solid angle at the angle whose cosine is InCosine from where it was going. g = 0 is 1 / (4 pi) in
 * every direction.
 */
float PhaseHG(float InCosine, float InG) {
    const float g2 = InG * InG;
    const float d = max(1.0 + g2 - 2.0 * InG * InCosine, 1e-4);
    return (1.0 - g2) / (4.0 * kPi * d * sqrt(d));
}

/** Narrows [InOutNear, InOutFar] to the part of the ray inside a sphere. False: no part is. */
bool ClipRayToSphere(float3 InOrigin, float3 InDirection, float3 InCenter, float InRadius, inout float InOutNear,
                     inout float InOutFar) {
    const float3 fromCenter = InOrigin - InCenter;
    const float b = dot(fromCenter, InDirection);
    const float h = b * b - (dot(fromCenter, fromCenter) - InRadius * InRadius);
    if (h <= 0.0)
        return false;
    const float root = sqrt(h);
    InOutNear = max(InOutNear, -b - root);
    InOutFar = min(InOutFar, -b + root);
    return InOutFar > InOutNear;
}

/**
 * Narrows [InOutNear, InOutFar] to the part of the ray inside a cone (apex, unit axis, cosine of its
 * half angle). False: no part is. Points on the ray satisfy dot(p - apex, axis)^2 >= cos^2 |p - apex|^2
 * inside the cone and inside its mirror image behind the apex; which of the two a piece is in is
 * decided by the side of the apex it lies on.
 */
bool ClipRayToCone(float3 InOrigin, float3 InDirection, float3 InApex, float3 InAxis, float InCosine,
                   inout float InOutNear, inout float InOutFar) {
    // A cone wider than a half space is not worth cutting to: its sphere is the bound.
    if (InCosine < 0.05)
        return true;
    const float3 fromApex = InOrigin - InApex;
    const float cosine2 = InCosine * InCosine;
    const float along = dot(InDirection, InAxis);
    const float apexAlong = dot(fromApex, InAxis);
    const float a = along * along - cosine2;
    const float b = 2.0 * (along * apexAlong - cosine2 * dot(InDirection, fromApex));
    const float c = apexAlong * apexAlong - cosine2 * dot(fromApex, fromApex);
    // A ray that runs along the cone's side: the equation loses its square term. Left uncut; every
    // sample is still weighed by the cone.
    if (abs(a) < 1e-4)
        return true;
    const float discriminant = b * b - 4.0 * a * c;
    if (discriminant <= 0.0) {
        // The ray never crosses the side. Steeper than the cone (a < 0) it is outside all along.
        if (a < 0.0)
            return false;
        // Otherwise it runs inside: in the cone on the axis' side of the apex, in the mirror image on the other.
        const float crossing = -apexAlong / along;
        if (along > 0.0)
            InOutNear = max(InOutNear, crossing);
        else
            InOutFar = min(InOutFar, crossing);
        return InOutFar > InOutNear;
    }
    const float root = sqrt(discriminant);
    const float first = (-b - root) / (2.0 * a);
    const float second = (-b + root) / (2.0 * a);
    const float entry = min(first, second);
    const float leave = max(first, second);
    if (a < 0.0) {
        // Inside between the two crossings, which are both on the cone or both on its mirror image.
        if (apexAlong + 0.5 * (entry + leave) * along < 0.0)
            return false;
        InOutNear = max(InOutNear, entry);
        InOutFar = min(InOutFar, leave);
    } else if (along > 0.0) {
        // Inside before the first crossing (the mirror image) and after the second (the cone).
        InOutNear = max(InOutNear, leave);
    } else {
        InOutFar = min(InOutFar, entry);
    }
    return InOutFar > InOutNear;
}

/** 1 where the spot light's shadow map says the point is lit. */
float SpotLightVisibility(int InLight, float3 InPosition) {
    const float4 projected = mul(u_FogSpotMatrices[InLight], float4(InPosition, 1.0));
    if (projected.w <= 0.0)
        return 1.0;
    const float3 shadow = projected.xyz / projected.w;
    if (shadow.x < 0.0 || shadow.x > 1.0 || shadow.y < 0.0 || shadow.y > 1.0 || shadow.z < 0.0 || shadow.z > 1.0)
        return 1.0;
    return t_SpotShadow.SampleCmpLevelZero(s_ShadowCompare, float3(shadow.xy, u_FogSpotLights[InLight].Shadow.x),
                                           shadow.z);
}

/** 1 where the point light's cube says the point is lit. The cube stores distance / radius. */
float PointLightVisibility(FFogPointLight InLight, float3 InPosition) {
    const float3 fromLight = InPosition - InLight.Position.xyz;
    const float stored = t_PointShadow.SampleLevel(s_PointClamp, float4(fromLight, InLight.Color.a), 0.0);
    return length(fromLight) / max(InLight.Position.w, 0.01) > stored + 0.004 ? 0.0 : 1.0;
}

/**
 * The light one spot light sends towards the camera along a view ray, before the air's own density
 * and scattering are applied: the sum over the ray of phase * light arriving * what the air between
 * the sample and the camera lets through, times the length each sample stands for.
 */
float3 ScatterSpotLight(int InLight, float3 InCamera, float3 InDirection, float InReach, float InJitter, int InSteps) {
    const FFogSpotLight light = u_FogSpotLights[InLight];
    const float3 axis = light.Direction.xyz;
    float rayNear = 0.0;
    float rayFar = InReach;
    if (!ClipRayToSphere(InCamera, InDirection, light.Position.xyz, light.Position.w, rayNear, rayFar))
        return float3(0.0, 0.0, 0.0);
    if (!ClipRayToCone(InCamera, InDirection, light.Position.xyz, axis, light.Color.a, rayNear, rayFar))
        return float3(0.0, 0.0, 0.0);

    const float stepLength = (rayFar - rayNear) / float(InSteps);
    float sum = 0.0;
    for (int index = 0; index < InSteps; ++index) {
        const float along = rayNear + (float(index) + InJitter) * stepLength;
        const float3 position = InCamera + InDirection * along;
        const float3 toLight = light.Position.xyz - position;
        const float distance = length(toLight);
        const float3 L = toLight / max(distance, 1e-5);
        const float theta = dot(L, -axis);
        const float cone = smoothstep(0.0, 1.0, saturate((theta - light.Color.a) / max(light.Direction.w - light.Color.a, 0.0001)));
        if (cone <= 0.0)
            continue;
        const float visibility = SpotLightVisibility(InLight, position + L * kShadowPull);
        // Light travelling from the lamp to the sample goes along -L; the camera sees what leaves along -InDirection.
        sum += PhaseHG(dot(L, InDirection), u_FogParams.z) * LightAttenuation(distance, light.Position.w) * cone *
               visibility * exp(-u_FogParams.x * along);
    }
    return light.Color.rgb * (sum * stepLength);
}

float3 ScatterPointLight(int InLight, float3 InCamera, float3 InDirection, float InReach, float InJitter, int InSteps) {
    const FFogPointLight light = u_FogPointLights[InLight];
    float rayNear = 0.0;
    float rayFar = InReach;
    if (!ClipRayToSphere(InCamera, InDirection, light.Position.xyz, light.Position.w, rayNear, rayFar))
        return float3(0.0, 0.0, 0.0);

    const float stepLength = (rayFar - rayNear) / float(InSteps);
    float sum = 0.0;
    for (int index = 0; index < InSteps; ++index) {
        const float along = rayNear + (float(index) + InJitter) * stepLength;
        const float3 position = InCamera + InDirection * along;
        const float3 toLight = light.Position.xyz - position;
        const float distance = length(toLight);
        const float3 L = toLight / max(distance, 1e-5);
        const float visibility = PointLightVisibility(light, position + L * kShadowPull);
        sum += PhaseHG(dot(L, InDirection), u_FogParams.z) * LightAttenuation(distance, light.Position.w) * visibility *
               exp(-u_FogParams.x * along);
    }
    return light.Color.rgb * (sum * stepLength);
}

float4 PSScatter(FFullscreenOutput InPixel) : SV_Target {
    float2 texelUV;
    const float depth = LoadDepth(InPixel.TexCoord, texelUV);
    const float3 camera = u_CameraPosition.xyz;
    const float3 toSurface = WorldPositionAt(texelUV, depth) - camera;
    const float surfaceDistance = length(toSurface);
    const float3 direction = toSurface / max(surfaceDistance, 1e-5);
    // The air is looked at as far as the surface the ray ends on (the sky is beyond every distance).
    const float reach = min(surfaceDistance, u_CameraPosition.w);

    const float density = u_FogParams.x;
    const int steps = clamp(int(u_FogParams.w), 1, 64);
    const float jitter = InterleavedGradientNoise(InPixel.Position.xy);

    float3 light = float3(0.0, 0.0, 0.0);
    const int spotCount = min(u_FogLightCounts.x, MAX_FOG_SPOT_LIGHTS);
    for (int spot = 0; spot < spotCount; ++spot)
        light += ScatterSpotLight(spot, camera, direction, reach, jitter, steps);
    const int pointCount = min(u_FogLightCounts.y, MAX_FOG_POINT_LIGHTS);
    for (int pointLight = 0; pointLight < pointCount; ++pointLight)
        light += ScatterPointLight(pointLight, camera, direction, reach, jitter, steps);
    // Of the light a metre of air takes from a beam (density), the part it sends on (scattering).
    float3 scattered = light * (density * u_FogParams.y);

    // What is left of the scene behind the air, and the air's own glow in its place. Where the map's
    // distance fog is on it has done both to the surfaces already: only the lamps are added here.
    float sceneLeft = 1.0;
    if (u_FogAmbient.a > 0.5) {
        sceneLeft = exp(-density * reach);
        scattered += u_FogAmbient.rgb * (u_FogParams.y * (1.0 - sceneLeft));
    }
    return float4(clamp(scattered, 0.0, 16384.0), sceneLeft);
}

float4 PSBlur(FFullscreenOutput InPixel) : SV_Target {
    static const float kKernel[4] = {0.227027, 0.1945946, 0.1216216, 0.054054};
    const float centerDepth = ViewDepth(LoadDepth(InPixel.TexCoord));
    float4 result = float4(0.0, 0.0, 0.0, 0.0);
    float weightSum = 0.0;
    for (int i = -3; i <= 3; ++i) {
        const float2 uv = InPixel.TexCoord + u_BlurParams.xy * float(i);
        const float weight = kKernel[abs(i)] * DepthWeight(centerDepth, ViewDepth(LoadDepth(uv)));
        result += t_Fog.SampleLevel(s_PointClamp, uv, 0.0) * weight;
        weightSum += weight;
    }
    // The centre tap always counts in full, so the sum is never empty.
    return result / weightSum;
}

float4 PSComposite(FFullscreenOutput InPixel) : SV_Target {
    // The four fog texels around the pixel, each counted by how near it is and by whether it was
    // marched to the surface this pixel shows: across an edge the fog of the other surface is left out.
    const float depth = ViewDepth(t_Depth.Load(int3(int2(InPixel.Position.xy), 0)));
    float width;
    float height;
    t_Fog.GetDimensions(width, height);
    const float2 size = float2(width, height);
    const float2 position = InPixel.TexCoord * size - 0.5;
    const float2 corner = floor(position);
    const float2 fraction = position - corner;

    float4 sum = float4(0.0, 0.0, 0.0, 0.0);
    float weightSum = 0.0;
    float4 nearest = float4(0.0, 0.0, 0.0, 1.0);
    float nearestDifference = 1.0e30;
    for (int i = 0; i < 4; ++i) {
        const float2 offset = float2(float(i & 1), float(i >> 1));
        const int2 texel = clamp(int2(corner + offset), int2(0, 0), int2(size) - 1);
        const float texelDepth = ViewDepth(LoadDepth((float2(texel) + 0.5) / size));
        const float4 fog = t_Fog.Load(int3(texel, 0));
        const float2 share = lerp(1.0 - fraction, fraction, offset);
        const float weight = share.x * share.y * DepthWeight(depth, texelDepth);
        sum += fog * weight;
        weightSum += weight;
        const float difference = abs(texelDepth - depth);
        if (difference < nearestDifference) {
            nearestDifference = difference;
            nearest = fog;
        }
    }
    // None of the four is on this pixel's surface (a thin thing in front of a far wall): the closest in depth.
    return weightSum > 1e-4 ? sum / weightSum : nearest;
}
