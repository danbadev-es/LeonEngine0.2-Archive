#pragma once

#include "RenderCore/FFrustumCull.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

// What the camera can see of a level made of rooms: zones (axis-aligned boxes, one per room) joined
// by portals (the rectangles of the doorways). The camera's zone is seen whole; a zone beyond a portal
// is seen when the portal is in view, and only through it: the camera's frustum is narrowed to the
// part of the portal it sees, and narrowed again at each further portal. A closed portal (a shut door)
// lets nothing through. Lights work the same way in reverse: a light reaches the zones it can get to
// through open portals within its radius.
//
// It is right as long as the zones are sealed: light and sight pass from one zone to another only
// through portals, and whatever can be seen or lit inside a zone lies inside its box. Every test errs
// towards drawing more: anything not held by a single zone, a camera outside every zone, more zones
// or portal paths than the limits below, all mean "draw it".

namespace Leon {

    /** Zones beyond this many are ignored, and the level is drawn without zones. */
    inline constexpr uint32_t kMaxVisibilityZones = 64;
    /** Slack around a zone's box when deciding whether it holds something (metres). */
    inline constexpr float kZoneHoldSlack = 0.05f;

    struct FZoneBox {
        glm::vec3 Min{0.0f};
        glm::vec3 Max{0.0f};
    };

    /** A rectangle between two zones. */
    struct FZonePortal {
        glm::vec3 Center{0.0f};
        /** Unit directions along the width and the height; the normal is their cross product. */
        glm::vec3 AxisX{1.0f, 0.0f, 0.0f};
        glm::vec3 AxisY{0.0f, 1.0f, 0.0f};
        glm::vec2 HalfSize{0.5f, 1.0f};
        /** The zones on either side (ResolvePortalZones); -1 = outside every zone. */
        int32_t Zones[2] = {-1, -1};
        bool bOpen = true;

        glm::vec3 Normal() const { return glm::normalize(glm::cross(AxisX, AxisY)); }
        /** The four corners, in order around the rectangle. */
        void Corners(glm::vec3 (&OutCorners)[4]) const;
    };

    /** A convex region: planes ax + by + cz + d, inside where >= 0. */
    struct FViewVolume {
        std::vector<glm::vec4> Planes;
    };

    struct FZoneVisibility {
        /** False: nothing to cull with (no zones, too many, or the camera in none). Draw everything. */
        bool bActive = false;
        /** Per zone: seen at all. */
        std::vector<uint8_t> Visible;
        /** Per zone: seen with the camera's whole frustum (the camera's own zone). */
        std::vector<uint8_t> Whole;
        /** Per zone not seen whole: the narrowed frustums it is seen through. */
        std::vector<std::vector<FViewVolume>> Views;
        /** A portal to the outside of every zone is in view. */
        bool bOutsideVisible = false;

        uint32_t VisibleCount() const;
    };

    /** The zone whose box holds InPoint, the smallest if several; -1 when none. */
    int32_t FindZoneAt(const std::vector<FZoneBox>& InZones, const glm::vec3& InPoint);

    /**
     * Which zone each side of each portal opens into: the zones holding the points a little in front
     * of and behind its centre. A portal with the same zone on both sides joins nothing (-1, -1).
     */
    void ResolvePortalZones(const std::vector<FZoneBox>& InZones, std::vector<FZonePortal>& InOutPortals);

    /**
     * What the camera sees. InFrustum is the camera's; InNearRadius how far from the camera its near
     * plane reaches (a portal that close is passed through whole: both its zones are seen whole).
     */
    FZoneVisibility ComputeZoneVisibility(const std::vector<FZoneBox>& InZones,
                                          const std::vector<FZonePortal>& InPortals,
                                          const glm::vec3& InCameraPosition, const FFrustumPlanes& InFrustum,
                                          float InNearRadius);

    /** Bit i: zone i's box, grown by kZoneHoldSlack, holds the bounds whole. 0 = no single zone does. */
    uint64_t ZonesHolding(const std::vector<FZoneBox>& InZones, const glm::vec3& InMin, const glm::vec3& InMax);
    /** Bit i: the bounds touch zone i's box. */
    uint64_t ZonesTouching(const std::vector<FZoneBox>& InZones, const glm::vec3& InMin, const glm::vec3& InMax);

    /**
     * Whether something held by the zones of InHolding (ZonesHolding) can be seen. Something held by
     * no zone is always seen.
     */
    bool IsHeldBoundsVisible(const FZoneVisibility& InVisibility, uint64_t InHolding, const glm::vec3& InMin,
                             const glm::vec3& InMax);

    /** Where one light can shine. */
    struct FZoneLightReach {
        /** False: the light is in no zone (or the portals were too many to follow); it may reach anywhere. */
        bool bKnown = false;
        /** Bit i: zone i gets light from it. */
        uint64_t Zones = 0;
        /** It shines out through a portal to the outside of every zone. */
        bool bOutside = false;
        /** The zones it lights as seen from it: whole (its own) or through the portals it shines through. */
        FZoneVisibility Lit;
    };

    /**
     * As the camera sees zones, from the light's position, every way, through open portals that come
     * within InRadius of it.
     */
    FZoneLightReach ComputeZoneLightReach(const std::vector<FZoneBox>& InZones,
                                          const std::vector<FZonePortal>& InPortals, const glm::vec3& InPosition,
                                          float InRadius);

    /**
     * Whether the light can fall on a caster that touches the zones of InTouching (ZonesTouching): in a
     * zone it lights, inside what it sees of that zone. A caster touching no zone, or a light whose
     * reach is unknown or goes outside every zone, always counts.
     */
    bool IsCasterLit(const FZoneLightReach& InReach, uint64_t InTouching, const glm::vec3& InMin, const glm::vec3& InMax);

    /** Distance from a point to the portal's rectangle. */
    float DistanceToPortal(const FZonePortal& InPortal, const glm::vec3& InPoint);

    /** InPolygon cut to the inside of every plane (Sutherland-Hodgman). */
    std::vector<glm::vec3> ClipPolygon(const std::vector<glm::vec3>& InPolygon, const std::vector<glm::vec4>& InPlanes);

    bool AABBIntersectsVolume(const glm::vec3& InMin, const glm::vec3& InMax, const FViewVolume& InVolume);

} // namespace Leon
