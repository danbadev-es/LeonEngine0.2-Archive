// Physically based surface shading: material textures, direct lights, baked lightmaps,
// image-based lighting and fog. Writes linear HDR colour; tone mapping happens afterwards.
//
// Not brought over from the OpenGL renderer yet: shadows, planar reflections, instancing.

#include "PBR_Common.hlsli"

// Mirrors FMaterialConstants in FWorldRendererRHI.cpp.
cbuffer MaterialConstants : register(b2) {
    float4 u_AlbedoColor;    // rgb
    float4 u_EmissiveColor;  // rgb = colour, a = intensity
    float4 u_SurfaceParams;  // x = metallic, y = roughness, z = ambient occlusion, w = normal scale
    float4 u_SurfaceParams2; // x = occlusion strength, y = alpha cutoff
    float4 u_UVTransform;    // xy = tiling, zw = offset
    int4 u_MaterialFlags;    // x = texture bits (below), y = alpha mode (0 opaque, 1 mask, 2 blend), z = double sided
};

#define MATERIAL_HAS_ALBEDO 1
#define MATERIAL_HAS_NORMAL 2
#define MATERIAL_HAS_METALLIC 4
#define MATERIAL_HAS_AO 8
#define MATERIAL_HAS_ROUGHNESS 16
#define MATERIAL_HAS_EMISSIVE 32

// Mirrors FDrawConstants in FWorldRendererRHI.cpp.
cbuffer DrawConstants : register(b3) {
    float4x4 u_Model;
    float4x4 u_NormalMatrix;    // inverse transpose of u_Model (upper 3x3 used)
    float4 u_LightmapScaleBias; // xy = scale, zw = bias of this object's chart in the atlas
    int4 u_DrawFlags;           // x = has lightmap, y = lightmap uses the first UV set
};

Texture2D<float4> t_Albedo : register(t0);
Texture2D<float4> t_Normal : register(t1);
Texture2D<float4> t_Metallic : register(t2);
Texture2D<float4> t_AO : register(t3);
Texture2D<float4> t_Roughness : register(t4);
Texture2D<float4> t_BRDFLUT : register(t6);
TextureCube<float4> t_Irradiance : register(t7);
TextureCube<float4> t_Prefilter : register(t8);
Texture2D<float4> t_Emissive : register(t9);
Texture2D<float4> t_Lightmap : register(t12);

struct FVertexInput {
    float3 Position : ATTRIBUTE0;
    float3 Normal : ATTRIBUTE1;
    float2 TexCoord : ATTRIBUTE2;
    float4 Tangent : ATTRIBUTE3; // xyz + handedness
    float3 Color : ATTRIBUTE4;
    float2 LightmapUV : ATTRIBUTE5;
};

struct FVertexOutput {
    float4 Position : SV_Position;
    float3 WorldPosition : TEXCOORD0;
    float3 Normal : TEXCOORD1;
    float3 Tangent : TEXCOORD2;
    float3 Bitangent : TEXCOORD3;
    float2 TexCoord : TEXCOORD4;
    float2 LightmapUV : TEXCOORD5;
};

FVertexOutput VSMain(FVertexInput InVertex) {
    FVertexOutput output;
    const float4 worldPosition = mul(u_Model, float4(InVertex.Position, 1.0));
    const float3x3 normalMatrix = (float3x3)u_NormalMatrix;

    const float3 N = normalize(mul(normalMatrix, InVertex.Normal));
    float3 T = SafeNormalize(mul(normalMatrix, InVertex.Tangent.xyz), float3(1.0, 0.0, 0.0));
    T = SafeNormalize(T - N * dot(N, T), float3(1.0, 0.0, 0.0));
    // A mirrored transform flips the handedness of the tangent frame.
    const float mirror = determinant(normalMatrix) < 0.0 ? -1.0 : 1.0;

    output.Position = mul(u_ViewProjection, worldPosition);
    output.WorldPosition = worldPosition.xyz;
    output.Normal = N;
    output.Tangent = T;
    output.Bitangent = cross(N, T) * InVertex.Tangent.w * mirror;
    output.TexCoord = InVertex.TexCoord;
    output.LightmapUV = InVertex.LightmapUV;
    return output;
}

float4 PSMain(FVertexOutput InPixel, bool bInFrontFace : SV_IsFrontFace) : SV_Target {
    const int textureBits = u_MaterialFlags.x;
    const float2 uv = InPixel.TexCoord * u_UVTransform.xy + u_UVTransform.zw;

    // --- Material ---
    float4 albedoSample = float4(1.0, 1.0, 1.0, 1.0);
    if (textureBits & MATERIAL_HAS_ALBEDO)
        albedoSample = t_Albedo.Sample(s_LinearWrap, uv);
    const float alpha = albedoSample.a;
    if (u_MaterialFlags.y == 1 && alpha < u_SurfaceParams2.y)
        discard;
    const float3 albedo = u_AlbedoColor.rgb * albedoSample.rgb;

    float3 geometricNormal = SafeNormalize(InPixel.Normal, float3(0.0, 1.0, 0.0));
    if (u_MaterialFlags.z == 1 && !bInFrontFace)
        geometricNormal = -geometricNormal;
    float3 T = SafeNormalize(InPixel.Tangent, float3(1.0, 0.0, 0.0));
    T = SafeNormalize(T - geometricNormal * dot(geometricNormal, T), float3(1.0, 0.0, 0.0));
    float3 B = SafeNormalize(InPixel.Bitangent, float3(0.0, 0.0, 1.0));
    B = SafeNormalize(B - geometricNormal * dot(geometricNormal, B) - T * dot(T, B), cross(geometricNormal, T));

    float3 N = geometricNormal;
    if (textureBits & MATERIAL_HAS_NORMAL) {
        float3 tangentNormal = t_Normal.Sample(s_LinearWrap, uv).rgb * 2.0 - 1.0;
        tangentNormal.xy *= u_SurfaceParams.w;
        tangentNormal = normalize(tangentNormal);
        N = normalize(T * tangentNormal.x + B * tangentNormal.y + geometricNormal * tangentNormal.z);
    }

    float metallic = u_SurfaceParams.x;
    if (textureBits & MATERIAL_HAS_METALLIC)
        metallic *= t_Metallic.Sample(s_LinearWrap, uv).r;
    metallic = saturate(metallic);

    float roughness = u_SurfaceParams.y;
    if (textureBits & MATERIAL_HAS_ROUGHNESS)
        roughness *= t_Roughness.Sample(s_LinearWrap, uv).r;
    roughness = clamp(roughness, 0.04, 1.0);

    float ao = u_SurfaceParams.z;
    if (textureBits & MATERIAL_HAS_AO)
        ao *= lerp(1.0, t_AO.Sample(s_LinearWrap, uv).r, u_SurfaceParams2.x);
    ao = saturate(ao);

    float3 emissive = u_EmissiveColor.rgb * u_EmissiveColor.a;
    if (textureBits & MATERIAL_HAS_EMISSIVE)
        emissive *= t_Emissive.Sample(s_LinearWrap, uv).rgb;

    FSurface surface;
    surface.Albedo = albedo;
    surface.N = N;
    surface.V = normalize(u_ViewPosition.xyz - InPixel.WorldPosition);
    surface.F0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);
    surface.Metallic = metallic;
    surface.Roughness = roughness;
    const float3 R = reflect(-surface.V, N);

    // --- Direct lights ---
    float3 directDiffuse = float3(0.0, 0.0, 0.0);
    float3 directSpecular = float3(0.0, 0.0, 0.0);

    if (u_DirLight.Direction.w > 0.5)
        ShadeLight(surface, normalize(-u_DirLight.Direction.xyz), u_DirLight.Color.rgb * u_DirLight.Color.a,
                   directDiffuse, directSpecular);

    const int pointCount = min(u_LightCounts.x, MAX_POINT_LIGHTS);
    for (int pointIndex = 0; pointIndex < pointCount; ++pointIndex) {
        const FPointLight light = u_PointLights[pointIndex];
        if (light.Position.w < 0.5)
            continue;
        const float3 toLight = light.Position.xyz - InPixel.WorldPosition;
        const float distance = length(toLight);
        const float3 radiance = light.Color.rgb * light.Color.a * LightAttenuation(distance, light.Params.x);
        ShadeLight(surface, toLight / max(distance, 1e-5), radiance, directDiffuse, directSpecular);
    }

    const int spotCount = min(u_LightCounts.y, MAX_SPOT_LIGHTS);
    for (int spotIndex = 0; spotIndex < spotCount; ++spotIndex) {
        const FSpotLight light = u_SpotLights[spotIndex];
        if (light.Position.w < 0.5)
            continue;
        const float3 toLight = light.Position.xyz - InPixel.WorldPosition;
        const float distance = length(toLight);
        const float3 L = toLight / max(distance, 1e-5);
        const float theta = dot(L, normalize(-light.Direction.xyz));
        const float innerCos = light.Direction.w;
        const float outerCos = light.Color.a;
        if (theta <= outerCos)
            continue;
        const float cone = smoothstep(0.0, 1.0, saturate((theta - outerCos) / max(innerCos - outerCos, 0.0001)));
        const float3 radiance = light.Color.rgb * light.Params.y * LightAttenuation(distance, light.Params.x) * cone;
        ShadeLight(surface, L, radiance, directDiffuse, directSpecular);
    }
    const float3 direct = directDiffuse + directSpecular;

    // --- Indirect light ---
    const float NdotV = max(dot(N, surface.V), 0.0);
    const float3 fresnelIBL = FresnelSchlickRoughness(NdotV, surface.F0, roughness);
    const float3 diffuseWeightIBL = (1.0 - fresnelIBL) * (1.0 - metallic);
    const float2 envBRDF = t_BRDFLUT.SampleLevel(s_LinearClamp, float2(NdotV, roughness), 0.0).rg;

    float3 diffuseIBL;
    float3 reflection;
    if (u_PassFlags.x == 1) {
        const float kMaxReflectionLod = 4.0;
        diffuseIBL = t_Irradiance.SampleLevel(s_LinearClamp, N, 0.0).rgb * u_EnvSkyColor.a * albedo / kPi;
        reflection = t_Prefilter.SampleLevel(s_LinearClamp, R, roughness * kMaxReflectionLod).rgb * u_EnvSkyColor.a;
    } else {
        diffuseIBL = GetHemisphereIrradiance(N) * albedo / kPi;
        reflection = SampleEnvironmentAtmosphere(R);
    }
    const float3 specularIBL = reflection * (fresnelIBL * envBRDF.x + envBRDF.y);
    float3 ambient = (diffuseWeightIBL * diffuseIBL + specularIBL) * ao;

    // A lightmap holds baked diffuse irradiance; it replaces the diffuse part of the ambient term.
    float3 lightmapIrradiance = float3(0.0, 0.0, 0.0);
    float3 baked = float3(0.0, 0.0, 0.0);
    float2 lightmapUV = float2(0.0, 0.0);
    if (u_DrawFlags.x == 1) {
        lightmapUV = (u_DrawFlags.y == 1 ? InPixel.TexCoord : InPixel.LightmapUV) * u_LightmapScaleBias.xy +
                     u_LightmapScaleBias.zw;
        lightmapIrradiance = t_Lightmap.SampleLevel(s_LinearClamp, lightmapUV, 0.0).rgb;
        baked = diffuseWeightIBL * (albedo / kPi) * lightmapIrradiance;
        ambient = specularIBL * ao;
    }

    float3 color = ambient + direct + baked + emissive;

    // --- Debug views (numbers match the editor's view modes) ---
    const int debugView = u_PassFlags.y;
    if (debugView != 0) {
        float3 view = color;
        bool bHandled = true;
        if (debugView >= 1 && debugView <= 6)
            view = t_Prefilter.SampleLevel(s_LinearClamp, R, max(float(debugView) - 2.0, 0.0)).rgb;
        else if (debugView == 7)
            view = t_Irradiance.SampleLevel(s_LinearClamp, N, 0.0).rgb;
        else if (debugView == 8)
            view = float3(envBRDF, 0.0);
        else if (debugView == 9)
            view = specularIBL;
        else if (debugView == 10 || debugView == 36)
            view = direct;
        else if (debugView == 11 || debugView == 17)
            view = N * 0.5 + 0.5;
        else if (debugView == 12)
            view = R * 0.5 + 0.5;
        else if (debugView == 14)
            view = albedo;
        else if (debugView == 15)
            view = float3(metallic, metallic, metallic);
        else if (debugView == 16)
            view = float3(roughness, roughness, roughness);
        else if (debugView == 18)
            view = float3(ao, ao, ao);
        else if (debugView == 19)
            view = emissive;
        else if (debugView == 20)
            view = T * 0.5 + 0.5;
        else if (debugView == 21)
            view = B * 0.5 + 0.5;
        else if (debugView == 22)
            view = float3(frac(uv), 0.0);
        else if (debugView == 23) {
            const float3 sun = u_DirLight.Direction.w > 0.5 ? normalize(-u_DirLight.Direction.xyz) : float3(0.0, 1.0, 0.0);
            const float facing = max(dot(N, sun), 0.0);
            view = float3(facing, facing, facing);
        } else if (debugView == 31)
            view = baked;
        else if (debugView == 32)
            view = lightmapIrradiance;
        else if (debugView == 33)
            view = float3(frac(lightmapUV), u_DrawFlags.x == 1 ? 0.0 : 1.0);
        else if (debugView == 34)
            view = direct + baked;
        else if (debugView == 35)
            view = diffuseIBL;
        else if (debugView == 37)
            view = color;
        else if (debugView == 38)
            view = directDiffuse;
        else if (debugView == 39)
            view = directSpecular;
        else
            bHandled = false;
        if (bHandled)
            return float4(view, 1.0);
    }

    color = ApplyDistanceFog(color, InPixel.WorldPosition);
    return float4(color, alpha);
}
