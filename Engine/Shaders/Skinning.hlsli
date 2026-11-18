#ifndef LEON_SKINNING_HLSLI
#define LEON_SKINNING_HLSLI

// Linear blend skinning: each vertex follows up to four bones.

#define MAX_BONES 128

// Mirrors the palette uploaded by FWorldRendererRHI.cpp (kMaxBones matrices).
cbuffer BonePalette : register(b4) {
    float4x4 u_Bones[MAX_BONES];
};

float4x4 SkinMatrix(int4 InBoneIndices, float4 InBoneWeights) {
    const int4 bone = clamp(InBoneIndices, 0, MAX_BONES - 1);
    return u_Bones[bone.x] * InBoneWeights.x + u_Bones[bone.y] * InBoneWeights.y + u_Bones[bone.z] * InBoneWeights.z +
           u_Bones[bone.w] * InBoneWeights.w;
}

/**
 * The matrix that carries normals through InMatrix: its inverse transpose, up to a positive scale
 * (callers normalise). Built from cross products, so it needs no matrix inverse.
 */
float3x3 NormalTransform(float3x3 InMatrix) {
    const float3 row0 = cross(InMatrix[1], InMatrix[2]);
    const float3 row1 = cross(InMatrix[2], InMatrix[0]);
    const float3 row2 = cross(InMatrix[0], InMatrix[1]);
    const float determinantSign = dot(InMatrix[0], row0) < 0.0 ? -1.0 : 1.0;
    return float3x3(row0, row1, row2) * determinantSign;
}

#endif
