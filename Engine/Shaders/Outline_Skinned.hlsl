// Silhouette around a skinned mesh (friend/foe highlight): the mesh is drawn again, pushed out along
// its normals, showing only its back faces.

#include "Common.hlsli"
#include "Skinning.hlsli"

// Mirrors FOutlineConstants in FWorldRendererRHI.cpp.
cbuffer MaterialConstants : register(b2) {
    float4 u_OutlineColorWidth; // rgb = colour, a = width in metres
};

// The start of FDrawConstants in FWorldRendererRHI.cpp.
cbuffer DrawConstants : register(b3) {
    float4x4 u_Model;
    float4x4 u_NormalMatrix;
};

struct FVertexInput {
    float3 Position : ATTRIBUTE0;
    float3 Normal : ATTRIBUTE1;
    float2 TexCoord : ATTRIBUTE2;
    float4 Tangent : ATTRIBUTE3;
    float3 Color : ATTRIBUTE4;
    float2 LightmapUV : ATTRIBUTE5;
    int4 BoneIndices : ATTRIBUTE6;
    float4 BoneWeights : ATTRIBUTE7;
};

float4 VSMain(FVertexInput InVertex) : SV_Position {
    const float4x4 skin = SkinMatrix(InVertex.BoneIndices, InVertex.BoneWeights);
    const float3 skinnedNormal = SafeNormalize(mul((float3x3)skin, InVertex.Normal), float3(0.0, 1.0, 0.0));
    const float3 worldNormal = SafeNormalize(mul((float3x3)u_NormalMatrix, skinnedNormal), float3(0.0, 1.0, 0.0));
    float4 worldPosition = mul(u_Model, mul(skin, float4(InVertex.Position, 1.0)));
    worldPosition.xyz += worldNormal * u_OutlineColorWidth.a;
    return mul(u_ViewProjection, worldPosition);
}

float4 PSMain() : SV_Target {
    return float4(u_OutlineColorWidth.rgb, 1.0);
}
