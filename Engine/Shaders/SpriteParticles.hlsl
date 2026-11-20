// Sprite particles (FSpriteParticleBatch): camera-facing quads blended over the HDR scene after its
// pass, the farthest first. Lit per vertex by the frame's point and spot lights (without their shadows)
// and by the ambient light of the emitter's light probes, fogged like surfaces, and faded out where
// they near the surface behind them.

#include "PBR_Common.hlsli"

// Mirrors FSpriteParticleConstants in FShaderConstants.hpp.
cbuffer MaterialConstants : register(b2) {
    float4 u_DepthParams; // x = near, y = far of the projection the depth buffer was drawn with
};

Texture2D t_Sprite : register(t0);
Texture2D<float> t_SceneDepth : register(t1);

struct FVertexInput {
    float3 Position : ATTRIBUTE0;
    float3 UVFade : ATTRIBUTE1; // xy = texture coordinates, z = fade distance in metres
    float4 Color : ATTRIBUTE2;
    float4 Ambient : ATTRIBUTE3; // rgb = ambient irradiance, w = share of the lamps' light (-1 = unlit)
};

struct FVertexOutput {
    float4 Position : SV_Position;
    float3 WorldPosition : TEXCOORD0;
    float2 UV : TEXCOORD1;
    float ViewDepth : TEXCOORD2;
    float FadeDistance : TEXCOORD3;
    float4 Color : COLOR0;
};

/** The light the frame's point and spot lights send to a point, as on a surface facing each of them. */
float3 LampIrradiance(float3 InPosition) {
    float3 arriving = float3(0.0, 0.0, 0.0);
    const int pointCount = min(u_LightCounts.x, MAX_POINT_LIGHTS);
    for (int pointIndex = 0; pointIndex < pointCount; ++pointIndex) {
        const FPointLight light = u_PointLights[pointIndex];
        if (light.Position.w < 0.5)
            continue;
        const float distance = length(light.Position.xyz - InPosition);
        arriving += light.Color.rgb * light.Color.a * LightAttenuation(distance, light.Params.x);
    }
    const int spotCount = min(u_LightCounts.y, MAX_SPOT_LIGHTS);
    for (int spotIndex = 0; spotIndex < spotCount; ++spotIndex) {
        const FSpotLight light = u_SpotLights[spotIndex];
        if (light.Position.w < 0.5)
            continue;
        const float3 toLight = light.Position.xyz - InPosition;
        const float distance = length(toLight);
        const float theta = dot(toLight / max(distance, 1e-5), normalize(-light.Direction.xyz));
        const float innerCos = light.Direction.w;
        const float outerCos = light.Color.a;
        if (theta <= outerCos)
            continue;
        const float cone = smoothstep(0.0, 1.0, saturate((theta - outerCos) / max(innerCos - outerCos, 0.0001)));
        arriving += light.Color.rgb * light.Params.y * LightAttenuation(distance, light.Params.x) * cone;
    }
    return arriving;
}

FVertexOutput VSMain(FVertexInput InVertex) {
    FVertexOutput output;
    output.Position = mul(u_ViewProjection, float4(InVertex.Position, 1.0));
    output.WorldPosition = InVertex.Position;
    output.UV = InVertex.UVFade.xy;
    output.FadeDistance = InVertex.UVFade.z;
    output.ViewDepth = dot(InVertex.Position - u_ViewPosition.xyz, u_CameraForward.xyz);
    float3 color = InVertex.Color.rgb;
    if (InVertex.Ambient.w >= 0.0) {
        // A white sprite sends back what a white surface would (albedo / pi of the light on it).
        const float3 irradiance = InVertex.Ambient.rgb + LampIrradiance(InVertex.Position) * InVertex.Ambient.w;
        color = color * irradiance / kPi;
    }
    output.Color = float4(color, InVertex.Color.a);
    return output;
}

/** The depth buffer's 0..1 back to metres along the camera's axis. */
float LinearSceneDepth(float InDepth) {
    const float nearClip = u_DepthParams.x;
    const float farClip = u_DepthParams.y;
    const float ndc = InDepth * 2.0 - 1.0;
    return 2.0 * nearClip * farClip / ((farClip + nearClip) - ndc * (farClip - nearClip));
}

float4 PSMain(FVertexOutput InPixel) : SV_Target {
    const float4 texel = t_Sprite.Sample(s_LinearClamp, InPixel.UV);
    // Behind the surface the depth buffer holds there: nothing. In front of it: fading in over the
    // fade distance, so that a puff crossing a wall or the floor shows no hard line.
    const float sceneDepth = LinearSceneDepth(t_SceneDepth.Load(int3(InPixel.Position.xy, 0)));
    const float fade = saturate((sceneDepth - InPixel.ViewDepth) / max(InPixel.FadeDistance, 1e-3));
    const float alpha = saturate(InPixel.Color.a * texel.a) * fade;
    if (alpha <= 0.0)
        discard;
    const float3 color = ApplyDistanceFog(InPixel.Color.rgb * texel.rgb, InPixel.WorldPosition);
    // Within what the half-float scene can hold: a sum past it would stay infinite.
    return float4(min(color, 60000.0), alpha);
}
