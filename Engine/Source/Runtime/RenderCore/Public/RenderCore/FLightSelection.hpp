#pragma once

#include "RenderCore/FFrustumCull.hpp"

#include <glm/glm.hpp>

#include <algorithm>
#include <cstdint>
#include <vector>

// Which lights get the renderer's limited slots (lit slots in the shader, shadow maps) when a world
// has more than fit.
//
// Lights are ranked for one camera, most important first:
//   1. lights whose sphere of influence reaches into the view, before those that cannot light anything
//      on screen;
//   2. Movable lights before Stationary and Static ones. Movable lights are the few that gameplay
//      carries around (the player's flashlight): they must never lose their slot to level lighting;
//   3. by how much light arrives at the camera (intensity with the shader's falloff);
//   4. by distance to the camera, for lights that do not reach it;
//   5. by identity, so equal lights always come out in the same order.
// A light that had a slot in the previous frame gets a small advantage in 3 and 4, so two lights of
// nearly equal importance do not swap places every frame while the camera sways.

namespace Leon {

    /** A light competing for a slot. */
    struct FLightCandidate {
        glm::vec3 Position{0.0f};
        float Radius = 0.0f;
        /** Intensity times the brightest colour channel. */
        float Strength = 0.0f;
        bool bMovable = false;
        /** It held one of these slots in the previous frame. */
        bool bWasSelected = false;
        /** Stays the same from frame to frame (the entity). */
        uint32_t Id = 0;
    };

    /** How much a light that already has a slot is favoured over one that wants it. */
    inline constexpr float kLightSelectionHysteresis = 1.25f;

    /** The falloff of PBR_Common.hlsli (LightAttenuation): inverse square, zero at the radius. */
    inline float LightFalloff(float InDistance, float InRadius) {
        const float ratio = InDistance / std::max(InRadius, 0.0001f);
        const float window = glm::clamp(1.0f - ratio * ratio * ratio * ratio, 0.0f, 1.0f);
        return (window * window) / (InDistance * InDistance + 1.0f);
    }

    inline bool SphereIntersectsFrustum(const glm::vec3& InCenter, float InRadius, const FFrustumPlanes& InFrustum) {
        for (const glm::vec4& plane : InFrustum.Planes) {
            if (glm::dot(glm::vec3(plane), InCenter) + plane.w < -InRadius)
                return false;
        }
        return true;
    }

    /**
     * The indices of the lights that get a slot, most important first; at most InMaxCount of them.
     * bInViewOnly leaves out lights that cannot reach anything on screen even when slots are free
     * (a shadow map for such a light would be drawn for nothing).
     */
    inline std::vector<uint32_t> SelectLightsByImportance(const std::vector<FLightCandidate>& InLights,
                                                          const glm::vec3& InViewPosition,
                                                          const FFrustumPlanes& InFrustum, uint32_t InMaxCount,
                                                          bool bInViewOnly = false) {
        struct FRank {
            uint32_t Index = 0;
            uint32_t Id = 0;
            bool bInView = false;
            bool bMovable = false;
            float Arriving = 0.0f;
            float Distance = 0.0f;
        };
        std::vector<FRank> ranks;
        ranks.reserve(InLights.size());
        for (uint32_t index = 0; index < InLights.size(); ++index) {
            const FLightCandidate& light = InLights[index];
            FRank rank;
            rank.Index = index;
            rank.Id = light.Id;
            rank.bInView = SphereIntersectsFrustum(light.Position, light.Radius, InFrustum);
            if (bInViewOnly && !rank.bInView)
                continue;
            rank.bMovable = light.bMovable;
            rank.Distance = glm::length(light.Position - InViewPosition);
            rank.Arriving = light.Strength * LightFalloff(rank.Distance, light.Radius);
            if (light.bWasSelected) {
                rank.Arriving *= kLightSelectionHysteresis;
                rank.Distance /= kLightSelectionHysteresis;
            }
            ranks.push_back(rank);
        }
        std::sort(ranks.begin(), ranks.end(), [](const FRank& InA, const FRank& InB) {
            if (InA.bInView != InB.bInView)
                return InA.bInView;
            if (InA.bMovable != InB.bMovable)
                return InA.bMovable;
            if (InA.Arriving != InB.Arriving)
                return InA.Arriving > InB.Arriving;
            if (InA.Distance != InB.Distance)
                return InA.Distance < InB.Distance;
            return InA.Id < InB.Id;
        });
        if (ranks.size() > InMaxCount)
            ranks.resize(InMaxCount);
        std::vector<uint32_t> selected;
        selected.reserve(ranks.size());
        for (const FRank& rank : ranks)
            selected.push_back(rank.Index);
        return selected;
    }

} // namespace Leon
