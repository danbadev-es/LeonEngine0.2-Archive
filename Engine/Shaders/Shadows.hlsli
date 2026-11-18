#ifndef LEON_SHADOWS_HLSLI
#define LEON_SHADOWS_HLSLI

// Shadow lookups for lit surfaces: cascaded maps for the sun, one map for a spot light and cube maps
// for point lights. Include after PBR_Common.hlsli (it declares the constants used here).
//
// Every function returns how much of the light is blocked: 0 = fully lit, 1 = fully in shadow.
//
// The light matrices take a world position straight to shadow-map space: xy = texture coordinates,
// z = the depth stored in the map (0 near the light, 1 far). The C++ side folds the clip-to-texture
// conversion into them.

Texture2DArray<float> t_CascadeShadow : register(t10);
Texture2D<float> t_SpotShadow : register(t11);
TextureCubeArray<float> t_PointShadow : register(t14);

/** Passed as the cascade index to mean "the spot light's map". */
static const int kSpotShadowMap = -1;

// Keep in sync with FShadowSettings::kCascadeBlendMinMeters.
static const float kCascadeBlendMinMeters = 3.0;

static const float2 kPoissonDisk[16] = {
    float2(-0.94201624, -0.39906216), float2(0.94558609, -0.76890725), float2(-0.09418410, -0.92938870),
    float2(0.34495938, 0.29387760),   float2(-0.91588581, 0.45771432), float2(-0.81544232, -0.87912464),
    float2(-0.38277543, 0.27676845),  float2(0.97484398, 0.75648377),  float2(0.44323325, -0.97511554),
    float2(0.53742981, -0.47373420),  float2(-0.26496911, -0.41893023), float2(0.79197514, 0.19090188),
    float2(-0.24188840, 0.99706507),  float2(-0.81409955, 0.91437590), float2(0.19984126, 0.78641367),
    float2(0.14383161, -0.14100790)};

float InterleavedGradientNoise(float2 InPixel) {
    return frac(52.9829189 * frac(dot(InPixel, float2(0.06711056, 0.00583715))));
}

/** Rotation by a per-pixel pseudo-random angle, as (cos, sin): turns banding into noise. */
float2 ShadowKernelRotation(float2 InPixel) {
    const float angle = InterleavedGradientNoise(InPixel) * 2.0 * kPi;
    return float2(cos(angle), sin(angle));
}

float2 RotateKernel(float2 InOffset, float2 InRotation) {
    return float2(InRotation.x * InOffset.x - InRotation.y * InOffset.y,
                  InRotation.y * InOffset.x + InRotation.x * InOffset.y);
}

float ShadowSlopeTan(float3 InNormal, float3 InLightDir) {
    const float NdotL = saturate(dot(InNormal, InLightDir));
    const float sinTheta = sqrt(max(0.0, 1.0 - NdotL * NdotL));
    // Clamped at grazing angles: tan would grow without bound and detach the contact shadow.
    return min(sinTheta / max(NdotL, 0.08), 8.0);
}

/**
 * The normal of the triangle being drawn, which is the plane the shadow map saw. Interpolated vertex
 * normals bias smooth and concave surfaces too little. Call outside loops (it uses derivatives).
 */
float3 ShadowFaceNormal(float3 InWorldPosition, float3 InVertexNormal) {
    float3 faceNormal = cross(ddx(InWorldPosition), ddy(InWorldPosition));
    const float lengthSquared = dot(faceNormal, faceNormal);
    if (lengthSquared < 1e-12)
        return InVertexNormal;
    faceNormal *= rsqrt(lengthSquared);
    return dot(faceNormal, InVertexNormal) < 0.0 ? -faceNormal : faceNormal;
}

/** Depth change per unit of shadow UV along the receiver's plane, so wide filters follow the surface. */
float2 ReceiverPlaneDzDuv(float3 InDpdx, float3 InDpdy) {
    float2 dzDuv;
    dzDuv.x = InDpdy.y * InDpdx.z - InDpdx.y * InDpdy.z;
    dzDuv.y = InDpdx.x * InDpdy.z - InDpdy.x * InDpdx.z;
    const float det = InDpdx.x * InDpdy.y - InDpdx.y * InDpdy.x;
    const float invDet = 1.0 / ((det < 0.0 ? -1.0 : 1.0) * max(abs(det), 1e-6));
    return clamp(dzDuv * invDet, -16.0, 16.0);
}

bool ShadowUvValid(float2 InUV) {
    return InUV.x >= 0.0 && InUV.x <= 1.0 && InUV.y >= 0.0 && InUV.y <= 1.0;
}

int ActiveCascadeCount() {
    const int count = u_ShadowSettings.z;
    return count <= 0 ? 4 : clamp(count, 1, 4);
}

/** 1 where the map says "lit" at this depth, filtered between the four nearest texels. */
float ShadowTap(int InCascade, float2 InUV, float InDepth) {
    if (InCascade == kSpotShadowMap)
        return t_SpotShadow.SampleCmpLevelZero(s_ShadowCompare, InUV, InDepth);
    return t_CascadeShadow.SampleCmpLevelZero(s_ShadowCompare, float3(InUV, float(InCascade)), InDepth);
}

float2 ShadowTexelSize(int InCascade) {
    float width;
    float height;
    if (InCascade == kSpotShadowMap) {
        t_SpotShadow.GetDimensions(width, height);
    } else {
        float layers;
        t_CascadeShadow.GetDimensions(width, height, layers);
    }
    return 1.0 / float2(width, height);
}

/** InDzDuv comes from ReceiverPlaneDzDuv of the shadow-space position. */
float FilterShadowMap(int InCascade, float3 InShadowPosition, float InDepth, float2 InDzDuv, float2 InPixel) {
    const float2 texelSize = ShadowTexelSize(InCascade);
    const int filterMode = u_ShadowSettings.x;
    if (filterMode == 0)
        return 1.0 - ShadowTap(InCascade, InShadowPosition.xy, InDepth);

    float lit = 0.0;
    float taps = 0.0;
    if (filterMode == 1 || filterMode == 2) {
        const int radius = filterMode == 1 ? 1 : 2;
        for (int x = -radius; x <= radius; ++x) {
            for (int y = -radius; y <= radius; ++y) {
                const float2 offset = float2(float(x), float(y)) * texelSize;
                const float2 uv = InShadowPosition.xy + offset;
                if (!ShadowUvValid(uv))
                    continue;
                lit += ShadowTap(InCascade, uv, InDepth + dot(InDzDuv, offset));
                taps += 1.0;
            }
        }
    } else {
        const float2 rotation = ShadowKernelRotation(InPixel);
        for (int i = 0; i < 16; ++i) {
            const float2 offset = RotateKernel(kPoissonDisk[i], rotation) * 1.75 * texelSize;
            const float2 uv = InShadowPosition.xy + offset;
            if (!ShadowUvValid(uv))
                continue;
            lit += ShadowTap(InCascade, uv, InDepth + dot(InDzDuv, offset));
            taps += 1.0;
        }
    }
    return taps < 0.5 ? 0.0 : 1.0 - lit / taps;
}

/** What a shadow lookup needs to know about the pixel. Build once per pixel, outside loops. */
struct FShadowReceiver {
    float3 WorldPosition;
    float3 FaceNormal; // from ShadowFaceNormal
    float3 DpDx;       // screen-space derivatives of WorldPosition
    float3 DpDy;
    float2 Pixel;      // SV_Position.xy
};

FShadowReceiver MakeShadowReceiver(float3 InWorldPosition, float3 InVertexNormal, float2 InPixel) {
    FShadowReceiver receiver;
    receiver.WorldPosition = InWorldPosition;
    receiver.FaceNormal = ShadowFaceNormal(InWorldPosition, InVertexNormal);
    receiver.DpDx = ddx(InWorldPosition);
    receiver.DpDy = ddy(InWorldPosition);
    receiver.Pixel = InPixel;
    return receiver;
}

/**
 * Looks one map up. InBiasScale multiplies the configured constant and slope bias.
 * OutInBounds is false when the position falls outside the map (the result is then 0).
 */
float SampleShadowMap(int InCascade, float4x4 InLightMatrix, FShadowReceiver InReceiver, float3 InLightDir,
                      float InBiasScale, out bool OutInBounds) {
    OutInBounds = false;
    const float3 normal = InReceiver.FaceNormal;
    const float slopeFactor = max(1.0 - max(dot(normal, InLightDir), 0.0), 0.0);

    // Step off the surface along its normal before projecting: removes acne on surfaces facing away.
    const float3 offset = normal * (u_ShadowParams.z * slopeFactor);
    const float4 projected = mul(InLightMatrix, float4(InReceiver.WorldPosition + offset, 1.0));
    if (projected.w <= 0.0)
        return 0.0;
    float3 shadowPosition = projected.xyz / projected.w;

    // A wall under a near-vertical light covers about one texel of the map; depth bias cannot move the
    // sample off that sliver, a push along the surface normal in texture space can.
    const float2 texelSize = ShadowTexelSize(InCascade);
    const float4 normalProjected = mul(InLightMatrix, float4(normal, 0.0));
    const float2 normalUV = normalProjected.xy - 0.5 * normalProjected.w;
    const float normalLength = length(normalUV);
    if (normalLength >= 1e-6)
        shadowPosition.xy += (normalUV / normalLength) * min(texelSize * (4.0 + 4.0 * slopeFactor), 0.02);

    if (shadowPosition.z > 1.0 || shadowPosition.z < 0.0 || !ShadowUvValid(shadowPosition.xy))
        return 0.0;
    OutInBounds = true;

    // How the shadow-space position changes across the screen, from the world-space derivatives.
    // (The perspective divide is ignored: exact for the sun, a small error for the spot light.)
    const float3 dpdx = mul(InLightMatrix, float4(InReceiver.DpDx, 0.0)).xyz / projected.w;
    const float3 dpdy = mul(InLightMatrix, float4(InReceiver.DpDy, 0.0)).xyz / projected.w;
    const float bias = (u_ShadowParams.x + u_ShadowParams.y * ShadowSlopeTan(normal, InLightDir)) * InBiasScale;
    return FilterShadowMap(InCascade, shadowPosition, shadowPosition.z - bias, ReceiverPlaneDzDuv(dpdx, dpdy),
                           InReceiver.Pixel);
}

float CascadeSplitNear(int InCascade) {
    if (InCascade <= 0)
        return 0.0;
    if (InCascade == 1)
        return u_CascadeSplits.x;
    if (InCascade == 2)
        return u_CascadeSplits.y;
    return u_CascadeSplits.z;
}

float CascadeSplitFar(int InCascade) {
    if (InCascade <= 0)
        return u_CascadeSplits.x;
    if (InCascade == 1)
        return u_CascadeSplits.y;
    if (InCascade == 2)
        return u_CascadeSplits.z;
    return u_CascadeSplits.w;
}

/** 0 inside a cascade, rising to 1 across the band where it hands over to the next one. */
float CascadeBlendAlpha(float InViewDepth, int InCascade) {
    const float blendWidth = u_ShadowParams.w;
    if (blendWidth < 0.001 || InCascade + 1 >= ActiveCascadeCount())
        return 0.0;
    const float splitFar = CascadeSplitFar(InCascade);
    const float cascadeLength = max(splitFar - CascadeSplitNear(InCascade), 0.001);
    const float blendMeters = min(max(cascadeLength * blendWidth, kCascadeBlendMinMeters), cascadeLength * 0.90);
    const float threshold = splitFar - blendMeters;
    if (InViewDepth <= threshold)
        return 0.0;
    return saturate((InViewDepth - threshold) / max(blendMeters, 0.001));
}

float SampleCascade(int InCascade, FShadowReceiver InReceiver, float3 InLightDir, out bool OutInBounds) {
    // Slices that cover a long depth range need proportionally more bias or the floor shadows itself.
    // The range is read from the length of the matrix row that produces depth (times 2: the matrix
    // stores depth at half the scale of clip space). The length, not a single element of the row:
    // one element changes with the direction of the sun and made the bias swallow whole shadows.
    const float depthScale = max(length(u_LightSpaceMatrices[InCascade][2].xyz) * 2.0, 1e-4);
    return SampleShadowMap(InCascade, u_LightSpaceMatrices[InCascade], InReceiver, InLightDir,
                           max(1.0, 0.025 / depthScale), OutInBounds);
}

float CalculateCascadedDirectionalShadow(FShadowReceiver InReceiver, float3 InLightDir, out int OutCascade) {
    OutCascade = 0;
    if (u_ShadowFlags.x == 0)
        return 0.0;

    const int cascadeCount = ActiveCascadeCount();
    const int lastCascade = cascadeCount - 1;
    // Depth along the camera's forward axis: the same measure the cascades were split by.
    const float viewDepth = dot(InReceiver.WorldPosition - u_ViewPosition.xyz, u_CameraForward.xyz);
    int cascade = lastCascade;
    if (viewDepth < u_CascadeSplits.x)
        cascade = 0;
    else if (cascadeCount > 1 && viewDepth < u_CascadeSplits.y)
        cascade = 1;
    else if (cascadeCount > 2 && viewDepth < u_CascadeSplits.z)
        cascade = 2;
    cascade = min(cascade, lastCascade);
    OutCascade = cascade;

    bool bInBounds = false;
    float shadow = SampleCascade(cascade, InReceiver, InLightDir, bInBounds);

    const float blend = CascadeBlendAlpha(viewDepth, cascade);
    if (blend > 0.0) {
        bool bNextInBounds = false;
        const float nextShadow = SampleCascade(cascade + 1, InReceiver, InLightDir, bNextInBounds);
        if (bNextInBounds)
            shadow = lerp(shadow, nextShadow, blend);
    }

    // Fade out past the last cascade instead of ending on a line.
    const float farSplit = CascadeSplitFar(lastCascade);
    if (viewDepth > farSplit)
        shadow = lerp(shadow, 0.0, saturate((viewDepth - farSplit) / 15.0));
    return shadow;
}

float CalculateSpotShadow(FShadowReceiver InReceiver, float3 InLightDir) {
    if (u_ShadowFlags.y == 0)
        return 0.0;
    bool bInBounds = false;
    return SampleShadowMap(kSpotShadowMap, u_SpotLightSpaceMatrix, InReceiver, InLightDir, 1.5, bInBounds);
}

/** 1 when the cube map holds something nearer to the light than InDepth along InDirection. */
float PointShadowTap(float3 InDirection, int InCube, float InDepth, float InBias) {
    const float stored = t_PointShadow.SampleLevel(s_PointClamp, float4(InDirection, float(InCube)), 0.0);
    return InDepth > stored + InBias ? 1.0 : 0.0;
}

/** InLight is the point light: its position, radius and which cube holds its shadows. */
float CalculatePointShadow(FShadowReceiver InReceiver, FPointLight InLight) {
    if (u_ShadowFlags.z == 0 || InLight.Params.y < 0.0)
        return 0.0;
    const int cube = int(InLight.Params.y + 0.5);
    const float radius = max(InLight.Params.x, 0.01);
    const float3 fromLight = InReceiver.WorldPosition - InLight.Position.xyz;
    const float distance = length(fromLight);
    if (distance > radius || distance < 1e-5)
        return 0.0;

    const float3 L = -fromLight / distance;
    const float NdotL = max(dot(InReceiver.FaceNormal, L), 0.0);
    // The cube stores distance / radius, linear in 0..1, so these are not the sun's bias values.
    const float slopeFactor = max(1.0 - NdotL, 0.0);
    const float depth = saturate(distance / radius - (0.004 + 0.028 * slopeFactor));
    const float bias = 0.003 + 0.006 * slopeFactor;

    const float3 direction = fromLight / distance;
    const float3 up = abs(direction.y) < 0.99 ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
    const float3 tangent = SafeNormalize(cross(up, direction), float3(1.0, 0.0, 0.0));
    const float3 bitangent = cross(direction, tangent);
    const float diskRadius = 0.012;
    const int filterMode = u_ShadowSettings.x;

    float shadow = 0.0;
    if (filterMode == 0) {
        shadow = PointShadowTap(direction, cube, depth, bias);
    } else if (filterMode == 3) {
        const float2 rotation = ShadowKernelRotation(InReceiver.Pixel);
        for (int i = 0; i < 16; ++i) {
            const float2 offset = RotateKernel(kPoissonDisk[i], rotation) * diskRadius * 1.75;
            shadow += PointShadowTap(SafeNormalize(direction + tangent * offset.x + bitangent * offset.y, direction),
                                     cube, depth, bias);
        }
        shadow /= 16.0;
    } else {
        const int radiusTaps = filterMode == 1 ? 1 : 2;
        float taps = 0.0;
        for (int x = -radiusTaps; x <= radiusTaps; ++x) {
            for (int y = -radiusTaps; y <= radiusTaps; ++y) {
                const float3 tapDirection = direction + tangent * (float(x) * diskRadius) + bitangent * (float(y) * diskRadius);
                shadow += PointShadowTap(SafeNormalize(tapDirection, direction), cube, depth, bias);
                taps += 1.0;
            }
        }
        shadow /= max(taps, 1.0);
    }
    return shadow * smoothstep(0.0, 0.22, NdotL);
}

#endif
