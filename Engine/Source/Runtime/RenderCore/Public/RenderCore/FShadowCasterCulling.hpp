#pragma once

#include "RenderCore/FFrustumCull.hpp"

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>

// Which casters the shadow map of a spot or point light needs. A shadow view's frustum (a spot
// light's projection, a cube face) holds more than the light can reach: its corners lie outside the
// light's sphere and, for a spot light, outside its cone. And of what the light does reach, only the
// casters whose shadow can land somewhere the camera sees matter for the camera's frame.
//
// Every test here is conservative: it may keep a caster that is not needed, never drop one that is.

namespace Leon {

    /** What a local light can light: a sphere, and for a spot light the cone inside it. */
    struct FLightReach {
        glm::vec3 Position{0.0f};
        float Radius = 0.0f;
        bool bCone = false;
        /** Unit axis of the cone; the half angle includes AngularMargin. */
        glm::vec3 Direction{0.0f, -1.0f, 0.0f};
        float CosHalfAngle = 0.0f;
        float SinHalfAngle = 1.0f;
        /**
         * How far a lookup in the map strays from the direction of the receiver (radians): the shadow
         * filter's taps and the push along the normal in texture space (Shadows.hlsli).
         */
        float AngularMargin = 0.0f;
        /** How far it strays in world space: the normal offset before projecting, and some slack. */
        float Margin = 0.0f;
    };

    inline bool AABBIntersectsSphere(const glm::vec3& InMin, const glm::vec3& InMax, const glm::vec3& InCenter,
                                     float InRadius) {
        const glm::vec3 closest = glm::clamp(InCenter, InMin, InMax);
        const glm::vec3 delta = closest - InCenter;
        return glm::dot(delta, delta) <= InRadius * InRadius;
    }

    /**
     * A sphere against an infinite cone (apex, unit axis, half angle below 90 degrees). The distance
     * used is to the cone's side, which is never more than the true distance: a sphere near the apex
     * behind the cone may pass.
     */
    inline bool SphereIntersectsCone(const glm::vec3& InCenter, float InRadius, const glm::vec3& InApex,
                                     const glm::vec3& InAxis, float InCosHalfAngle, float InSinHalfAngle) {
        const glm::vec3 offset = InCenter - InApex;
        const float along = glm::dot(offset, InAxis);
        const float across = std::sqrt(std::max(glm::dot(offset, offset) - along * along, 0.0f));
        return across * InCosHalfAngle - along * InSinHalfAngle <= InRadius;
    }

    /**
     * Spot: the cone a spot light lights, held to its sphere. InOuterDegrees is the light's outer
     * cut-off (half the cone), InFieldOfViewDegrees the full angle of its shadow projection and
     * InResolution the map's edge in texels: they decide the margins.
     */
    inline FLightReach SpotLightReach(const glm::vec3& InPosition, const glm::vec3& InDirection, float InRadius,
                                      float InOuterDegrees, float InFieldOfViewDegrees, uint32_t InResolution,
                                      float InNormalBias) {
        FLightReach reach;
        reach.Position = InPosition;
        reach.Radius = std::max(InRadius, 0.0f);
        // A lookup moves up to 0.02 of the map's width along the normal and filters about four texels
        // around that; across the map that is an angle of at most 2 tan(half view) times as much.
        const float tanHalfView = std::tan(glm::radians(std::clamp(InFieldOfViewDegrees, 1.0f, 170.0f) * 0.5f));
        const float slack = 0.02f + 4.0f / static_cast<float>(std::max(InResolution, 1u));
        reach.AngularMargin = 2.0f * tanHalfView * slack;
        reach.Margin = InNormalBias + 0.05f;
        // The cone is the outer cut-off plus a degree (the light fades to nothing at the cut-off).
        const float halfAngle = glm::radians(std::max(InOuterDegrees, 0.0f) + 1.0f) + reach.AngularMargin;
        const float length = glm::length(InDirection);
        reach.bCone = halfAngle < glm::radians(85.0f) && length > 1e-6f;
        if (reach.bCone) {
            reach.Direction = InDirection / length;
            reach.CosHalfAngle = std::cos(halfAngle);
            reach.SinHalfAngle = std::sin(halfAngle);
        }
        return reach;
    }

    /** Point: the light's sphere. The cube's lookups spread over about 0.03 radians (Shadows.hlsli). */
    inline FLightReach PointLightReach(const glm::vec3& InPosition, float InRadius, float InNormalBias) {
        FLightReach reach;
        reach.Position = InPosition;
        reach.Radius = std::max(InRadius, 0.0f);
        reach.AngularMargin = 0.04f;
        reach.Margin = InNormalBias + 0.05f;
        return reach;
    }

    /** The caster's bounds touch what the light reaches. */
    inline bool CasterInLightReach(const glm::vec3& InMin, const glm::vec3& InMax, const FLightReach& InReach) {
        // A tap that strays sideways still reads along a ray no longer than the radius, near enough.
        const float radius = InReach.Radius * (1.0f + InReach.AngularMargin) + InReach.Margin;
        if (!AABBIntersectsSphere(InMin, InMax, InReach.Position, radius))
            return false;
        if (!InReach.bCone)
            return true;
        const glm::vec3 center = 0.5f * (InMin + InMax);
        const float boundsRadius = 0.5f * glm::length(InMax - InMin);
        return SphereIntersectsCone(center, boundsRadius + InReach.Margin, InReach.Position, InReach.Direction,
                                    InReach.CosHalfAngle, InReach.SinHalfAngle);
    }

    /**
     * Bounds of everything a caster can shadow: the points behind it as seen from the light, out to
     * the light's radius, and the caster itself. False when there are none to give (the light is
     * inside or at the caster, or the caster spans most of what the light sees): it can shadow
     * anywhere.
     *
     * Why the bounds hold: let u_i be the unit directions from the light to the eight corners and d
     * their mean direction, with c = min dot(u_i, d) > 0. A point q in the shadow lies on a ray from
     * the light through a point p of the box, beyond p, with |q - L| <= R. Writing q - L as a
     * non-negative mix of the u_i, the weights add up to at most R / c (their projection on d is at
     * least c times their sum and at most |q - L|). So q is on the segment from p to a point of the
     * hull of L + (R / c) u_i: inside the hull of the box's corners and those eight far points.
     */
    inline bool CasterShadowBounds(const glm::vec3& InMin, const glm::vec3& InMax, const glm::vec3& InLight,
                                   float InRadius, glm::vec3& OutMin, glm::vec3& OutMax) {
        glm::vec3 directions[8];
        glm::vec3 sum(0.0f);
        for (uint32_t corner = 0; corner < 8; ++corner) {
            const glm::vec3 point((corner & 1) ? InMax.x : InMin.x, (corner & 2) ? InMax.y : InMin.y,
                                  (corner & 4) ? InMax.z : InMin.z);
            const glm::vec3 toPoint = point - InLight;
            const float length = glm::length(toPoint);
            if (length < 1e-3f)
                return false;
            directions[corner] = toPoint / length;
            sum += directions[corner];
        }
        const float sumLength = glm::length(sum);
        if (sumLength < 1e-3f)
            return false;
        const glm::vec3 axis = sum / sumLength;
        float minCos = 1.0f;
        for (const glm::vec3& direction : directions)
            minCos = std::min(minCos, glm::dot(direction, axis));
        // Wider than about 80 degrees each way: the far points go off to where bounds stop meaning much.
        if (minCos < 0.17f)
            return false;
        const float reach = InRadius / minCos;
        OutMin = InMin;
        OutMax = InMax;
        for (const glm::vec3& direction : directions) {
            const glm::vec3 far = InLight + direction * reach;
            OutMin = glm::min(OutMin, far);
            OutMax = glm::max(OutMax, far);
        }
        return true;
    }

    /**
     * The caster may shadow something inside InView (the camera's frustum, where every receiver of
     * the frame lies: the surfaces drawn and the air the volumetric fog marches through).
     */
    inline bool CasterCanShadowView(const glm::vec3& InMin, const glm::vec3& InMax, const FLightReach& InReach,
                                    const FFrustumPlanes& InView) {
        glm::vec3 shadowMin;
        glm::vec3 shadowMax;
        if (!CasterShadowBounds(InMin, InMax, InReach.Position, InReach.Radius, shadowMin, shadowMax))
            return true;
        // A receiver reads the map a little to the side of where it is: at most the angular margin
        // across the light's radius, plus the world margin.
        const glm::vec3 margin(InReach.Radius * InReach.AngularMargin + InReach.Margin);
        return AABBIntersectsFrustum(shadowMin - margin, shadowMax + margin, InView);
    }

} // namespace Leon
