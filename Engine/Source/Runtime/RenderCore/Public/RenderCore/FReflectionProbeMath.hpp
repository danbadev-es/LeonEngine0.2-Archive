#pragma once

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

// Local reflection probes: a cube map of a room, read as if its walls were the faces of a box.
//
// The functions here are the C++ twins of Engine/Shaders/ReflectionProbes.hlsli (the shader does the
// same maths per pixel) plus the choice of which probes an object reads, which is made once per draw.

namespace Leon {

    /** How many probes a map may hold: one cube each in the array the surface shader reads. */
    inline constexpr uint32_t kMaxReflectionProbes = 32;
    /** How many of them one draw can read. */
    inline constexpr uint32_t kReflectionProbesPerDraw = 2;
    /** Levels of a probe's cube: level n holds the reflection of a surface of roughness n / 4, like the sky's. */
    inline constexpr uint32_t kReflectionProbeMipLevels = 5;

    /** A probe as the renderer sees it: an axis-aligned box in world space and where its cube was captured. */
    struct FReflectionProbeBox {
        glm::vec3 BoxMin{0.0f};
        glm::vec3 BoxMax{0.0f};
        glm::vec3 CapturePosition{0.0f};
        float Intensity = 1.0f;
        /** Metres outside the box over which the probe fades out. */
        float BlendDistance = 0.5f;
    };

    /** Distance along a ray that starts inside the box to the face it leaves through. */
    inline float BoxExitDistance(const glm::vec3& InPosition, const glm::vec3& InDirection, const glm::vec3& InBoxMin,
                                 const glm::vec3& InBoxMax) {
        float exit = 1.0e30f;
        for (int axis = 0; axis < 3; ++axis) {
            const float direction = InDirection[axis];
            if (std::abs(direction) < 1.0e-6f)
                continue;
            const float face = direction > 0.0f ? InBoxMax[axis] : InBoxMin[axis];
            exit = std::min(exit, (face - InPosition[axis]) / direction);
        }
        return std::max(exit, 0.0f);
    }

    /**
     * The direction to read a probe's cube with, for a reflection that leaves InPosition along
     * InDirection: towards the point of the box the ray hits, seen from where the cube was captured.
     * Not normalised. A position outside the box is first moved onto it.
     */
    inline glm::vec3 BoxProjectedDirection(const glm::vec3& InPosition, const glm::vec3& InDirection,
                                           const FReflectionProbeBox& InProbe) {
        const glm::vec3 inside = glm::clamp(InPosition, InProbe.BoxMin, InProbe.BoxMax);
        const glm::vec3 hit = inside + InDirection * BoxExitDistance(inside, InDirection, InProbe.BoxMin, InProbe.BoxMax);
        return hit - InProbe.CapturePosition;
    }

    /** How much a probe counts at a point: 1 inside its box, falling to 0 over BlendDistance outside it. */
    inline float ReflectionProbeWeight(const glm::vec3& InPosition, const FReflectionProbeBox& InProbe) {
        const glm::vec3 outside =
            glm::max(glm::max(InProbe.BoxMin - InPosition, InPosition - InProbe.BoxMax), glm::vec3(0.0f));
        return glm::clamp(1.0f - glm::length(outside) / std::max(InProbe.BlendDistance, 1.0e-4f), 0.0f, 1.0f);
    }

    /**
     * The probes an object reads, best first: up to kReflectionProbesPerDraw indices into InProbes, -1
     * where there is none. Only probes whose box (with its fade) touches the object's bounds are
     * candidates. They are ranked by their weight at the centre of the bounds, then by how much of
     * the bounds they cover, then by index, so the answer depends on the object alone and never on
     * the camera: nothing changes when the view moves.
     */
    inline void SelectReflectionProbes(const std::vector<FReflectionProbeBox>& InProbes, const glm::vec3& InBoundsMin,
                                       const glm::vec3& InBoundsMax, int32_t OutIndices[kReflectionProbesPerDraw]) {
        float bestWeight[kReflectionProbesPerDraw];
        float bestCover[kReflectionProbesPerDraw];
        for (uint32_t slot = 0; slot < kReflectionProbesPerDraw; ++slot) {
            OutIndices[slot] = -1;
            bestWeight[slot] = -1.0f;
            bestCover[slot] = -1.0f;
        }
        const glm::vec3 center = 0.5f * (InBoundsMin + InBoundsMax);
        for (uint32_t index = 0; index < InProbes.size(); ++index) {
            const FReflectionProbeBox& probe = InProbes[index];
            const glm::vec3 reachMin = probe.BoxMin - glm::vec3(probe.BlendDistance);
            const glm::vec3 reachMax = probe.BoxMax + glm::vec3(probe.BlendDistance);
            const glm::vec3 overlap = glm::min(InBoundsMax, reachMax) - glm::max(InBoundsMin, reachMin);
            if (overlap.x < 0.0f || overlap.y < 0.0f || overlap.z < 0.0f)
                continue;
            const float weight = ReflectionProbeWeight(center, probe);
            // Flat bounds (a floor plane) still cover an area: a millimetre stands in for a zero side.
            const glm::vec3 thick = glm::max(overlap, glm::vec3(1.0e-3f));
            const float cover = thick.x * thick.y * thick.z;
            int32_t candidate = static_cast<int32_t>(index);
            float candidateWeight = weight;
            float candidateCover = cover;
            for (uint32_t slot = 0; slot < kReflectionProbesPerDraw; ++slot) {
                const bool bBetter = OutIndices[slot] < 0 || candidateWeight > bestWeight[slot] ||
                                     (candidateWeight == bestWeight[slot] && candidateCover > bestCover[slot]);
                if (!bBetter)
                    continue;
                std::swap(candidate, OutIndices[slot]);
                std::swap(candidateWeight, bestWeight[slot]);
                std::swap(candidateCover, bestCover[slot]);
                if (candidate < 0)
                    break;
            }
        }
    }

    /**
     * The share each of two probes and the environment get at a point, from the probes' weights there.
     * Symmetric in the two probes, so it does not matter which one an object lists first.
     */
    inline void BlendReflectionProbeWeights(float InWeight0, float InWeight1, float& OutShare0, float& OutShare1,
                                            float& OutEnvironmentShare) {
        const float total = InWeight0 + InWeight1;
        const float scale = total > 1.0f ? 1.0f / total : 1.0f;
        OutShare0 = InWeight0 * scale;
        OutShare1 = InWeight1 * scale;
        OutEnvironmentShare = 1.0f - std::min(total, 1.0f);
    }

} // namespace Leon
