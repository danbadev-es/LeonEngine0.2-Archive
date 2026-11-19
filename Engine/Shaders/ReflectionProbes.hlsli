#ifndef LEON_REFLECTION_PROBES_HLSLI
#define LEON_REFLECTION_PROBES_HLSLI

// Local reflection probes: a cube map captured inside a room, read as if the walls of the room were
// the faces of an axis-aligned box (parallax correction). The same maths, for the CPU, is in
// RenderCore/FReflectionProbeMath.hpp; keep the two alike.

#include "Common.hlsli"

// One cube per probe of the map; level n holds the reflection of a surface of roughness n / 4.
TextureCubeArray<float4> t_ReflectionProbes : register(t15);

/** Distance along a ray that starts inside the box to the face it leaves through. */
float BoxExitDistance(float3 InPosition, float3 InDirection, float3 InBoxMin, float3 InBoxMax) {
    // A component of zero never reaches its pair of faces: give it a face infinitely far away.
    const float3 faces = select(InDirection > 0.0, InBoxMax, InBoxMin);
    const float3 distances = select(abs(InDirection) < 1.0e-6, 1.0e30, (faces - InPosition) / InDirection);
    return max(min(distances.x, min(distances.y, distances.z)), 0.0);
}

/**
 * The direction to read a probe's cube with, for a reflection that leaves InPosition along
 * InDirection: towards the point of the box the ray hits, seen from where the cube was captured.
 * A position outside the box (a wall that stands on a face) is first moved onto it.
 */
float3 BoxProjectedDirection(float3 InPosition, float3 InDirection, float3 InBoxMin, float3 InBoxMax,
                             float3 InCapturePosition) {
    const float3 inside = clamp(InPosition, InBoxMin, InBoxMax);
    const float3 hit = inside + InDirection * BoxExitDistance(inside, InDirection, InBoxMin, InBoxMax);
    return hit - InCapturePosition;
}

/** How much a probe counts at a point: 1 inside its box, falling to 0 over InBlendDistance outside it. */
float ReflectionProbeWeight(float3 InPosition, float3 InBoxMin, float3 InBoxMax, float InBlendDistance) {
    const float3 outside = max(max(InBoxMin - InPosition, InPosition - InBoxMax), 0.0);
    return saturate(1.0 - length(outside) / max(InBlendDistance, 1.0e-4));
}

#endif
