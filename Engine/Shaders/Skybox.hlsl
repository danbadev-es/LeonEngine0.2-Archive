// The sky: an HDR panorama or a procedural gradient with a sun. Drawn as one triangle at the far
// plane after the opaque geometry, so it only fills the pixels nothing else covered.

#include "Common.hlsli"
#include "Fullscreen.hlsli"

// Mirrors FSkyConstants in FWorldRendererRHI.cpp.
cbuffer MaterialConstants : register(b2) {
    float4x4 u_ClipToDirection; // clip position at the far plane -> world direction from the camera
    float4 u_SunDirection;      // xyz = towards the sun
    float4 u_SkyColor;          // rgb = zenith, a = environment intensity
    float4 u_HorizonColor;      // rgb
    float4 u_GroundColor;       // rgb
    float4 u_SunColor;          // rgb = colour, a = intensity
    int4 u_SkyFlags;            // x = use the panorama
};

Texture2D<float4> t_Panorama : register(t0);

struct FSkyOutput {
    float4 Position : SV_Position;
    float3 Direction : TEXCOORD0;
};

FSkyOutput VSMain(uint InVertexId : SV_VertexID) {
    const FFullscreenOutput triangleVertex = FullscreenVS(InVertexId);
    FSkyOutput output;
    output.Position = float4(triangleVertex.Position.xy, 1.0, 1.0);
    const float4 direction = mul(u_ClipToDirection, output.Position);
    output.Direction = direction.xyz / direction.w;
    return output;
}

float4 PSMain(FSkyOutput InPixel) : SV_Target {
    const float3 direction = normalize(InPixel.Direction);

    if (u_SkyFlags.x == 1) {
        // Equirectangular lookup. U wraps where atan2 jumps from +pi to -pi; the screen-space
        // derivative there is ~1 and would select the smallest mip, drawing a seam, so it is unwrapped.
        const float2 uv = float2(0.5 + atan2(direction.z, direction.x) / (2.0 * kPi),
                                 0.5 - asin(clamp(direction.y, -1.0, 1.0)) / kPi);
        float2 dx = ddx(uv);
        float2 dy = ddy(uv);
        if (abs(dx.x) > 0.5)
            dx.x -= sign(dx.x);
        if (abs(dy.x) > 0.5)
            dy.x -= sign(dy.x);
        return float4(t_Panorama.SampleGrad(s_LinearWrap, uv, dx, dy).rgb * u_SkyColor.a, 1.0);
    }

    const float height = direction.y;
    float3 sky;
    if (height >= 0.0)
        sky = lerp(u_SkyColor.rgb, u_HorizonColor.rgb, pow(1.0 - height, 4.0));
    else
        sky = lerp(u_HorizonColor.rgb, u_GroundColor.rgb, saturate(-height * 3.0));

    // Sun disc and the glow around it.
    const float cosTheta = max(dot(direction, normalize(u_SunDirection.xyz)), 0.0);
    const float disc = step(0.9995, cosTheta);
    const float halo = pow(cosTheta, 32.0) * 1.5 + pow(cosTheta, 256.0) * 3.0;
    const float3 sun = u_SunColor.rgb * u_SunColor.a * (disc * 8.0 + halo);

    return float4((sky + sun) * u_SkyColor.a, 1.0);
}
