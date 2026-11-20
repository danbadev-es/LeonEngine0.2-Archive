#include "RenderCore/FZoneVisibility.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Leon {

    namespace {

        /** Narrowed frustums kept per zone; past this the zone is taken as seen whole. */
        constexpr size_t kMaxViewsPerZone = 16;
        /** Portal crossings one frame may follow; past this the level is drawn without zones. */
        constexpr uint32_t kMaxPortalSteps = 4096;
        /** How far in front of and behind a portal its zones are looked for (metres). */
        constexpr float kPortalProbeDistance = 0.3f;

        bool BoxHolds(const FZoneBox& InBox, const glm::vec3& InPoint, float InSlack) {
            return glm::all(glm::greaterThanEqual(InPoint, InBox.Min - InSlack)) &&
                   glm::all(glm::lessThanEqual(InPoint, InBox.Max + InSlack));
        }

        float BoxVolume(const FZoneBox& InBox) {
            const glm::vec3 size = glm::max(InBox.Max - InBox.Min, glm::vec3(0.0f));
            return size.x * size.y * size.z;
        }

        FViewVolume FrustumVolume(const FFrustumPlanes& InFrustum) {
            FViewVolume volume;
            volume.Planes.assign(std::begin(InFrustum.Planes), std::end(InFrustum.Planes));
            return volume;
        }

        /**
         * The part of space seen from InEye through InPolygon (convex, in order, in a portal's plane): a
         * plane through the eye and each edge, and the portal's plane, beyond which (towards InBeyond,
         * a unit normal pointing into the zone entered) everything seen through it lies. False when the
         * eye is not on the near side of the portal: nothing can be narrowed.
         */
        bool NarrowThrough(const glm::vec3& InEye, const std::vector<glm::vec3>& InPolygon, const glm::vec3& InBeyond,
                           FViewVolume& OutVolume) {
            glm::vec3 centroid(0.0f);
            for (const glm::vec3& point : InPolygon)
                centroid += point;
            centroid /= static_cast<float>(InPolygon.size());
            if (glm::dot(InBeyond, InEye - InPolygon[0]) > -1e-4f)
                return false;
            OutVolume.Planes.clear();
            for (size_t index = 0; index < InPolygon.size(); ++index) {
                const glm::vec3& a = InPolygon[index];
                const glm::vec3& b = InPolygon[(index + 1) % InPolygon.size()];
                glm::vec3 normal = glm::cross(a - InEye, b - InEye);
                const float length = glm::length(normal);
                // An edge seen end on bounds nothing; leaving its plane out only widens the volume.
                if (length < 1e-7f)
                    continue;
                normal /= length;
                float distance = -glm::dot(normal, InEye);
                if (glm::dot(normal, centroid) + distance < 0.0f) {
                    normal = -normal;
                    distance = -distance;
                }
                OutVolume.Planes.emplace_back(normal, distance);
            }
            OutVolume.Planes.emplace_back(InBeyond, -glm::dot(InBeyond, InPolygon[0]));
            return true;
        }

    } // namespace

    void FZonePortal::Corners(glm::vec3 (&OutCorners)[4]) const {
        const glm::vec3 x = AxisX * HalfSize.x;
        const glm::vec3 y = AxisY * HalfSize.y;
        OutCorners[0] = Center - x - y;
        OutCorners[1] = Center + x - y;
        OutCorners[2] = Center + x + y;
        OutCorners[3] = Center - x + y;
    }

    uint32_t FZoneVisibility::VisibleCount() const {
        return static_cast<uint32_t>(std::count(Visible.begin(), Visible.end(), uint8_t(1)));
    }

    int32_t FindZoneAt(const std::vector<FZoneBox>& InZones, const glm::vec3& InPoint) {
        int32_t found = -1;
        for (size_t index = 0; index < InZones.size(); ++index) {
            if (!BoxHolds(InZones[index], InPoint, 0.0f))
                continue;
            if (found < 0 || BoxVolume(InZones[index]) < BoxVolume(InZones[static_cast<size_t>(found)]))
                found = static_cast<int32_t>(index);
        }
        return found;
    }

    void ResolvePortalZones(const std::vector<FZoneBox>& InZones, std::vector<FZonePortal>& InOutPortals) {
        for (FZonePortal& portal : InOutPortals) {
            const glm::vec3 normal = portal.Normal();
            portal.Zones[0] = FindZoneAt(InZones, portal.Center + normal * kPortalProbeDistance);
            portal.Zones[1] = FindZoneAt(InZones, portal.Center - normal * kPortalProbeDistance);
            if (portal.Zones[0] == portal.Zones[1]) {
                portal.Zones[0] = -1;
                portal.Zones[1] = -1;
            }
        }
    }

    float DistanceToPortal(const FZonePortal& InPortal, const glm::vec3& InPoint) {
        const glm::vec3 offset = InPoint - InPortal.Center;
        const float x = glm::clamp(glm::dot(offset, InPortal.AxisX), -InPortal.HalfSize.x, InPortal.HalfSize.x);
        const float y = glm::clamp(glm::dot(offset, InPortal.AxisY), -InPortal.HalfSize.y, InPortal.HalfSize.y);
        return glm::length(InPoint - (InPortal.Center + InPortal.AxisX * x + InPortal.AxisY * y));
    }

    std::vector<glm::vec3> ClipPolygon(const std::vector<glm::vec3>& InPolygon, const std::vector<glm::vec4>& InPlanes) {
        std::vector<glm::vec3> polygon = InPolygon;
        std::vector<glm::vec3> clipped;
        for (const glm::vec4& plane : InPlanes) {
            if (polygon.empty())
                break;
            clipped.clear();
            const glm::vec3 normal(plane);
            for (size_t index = 0; index < polygon.size(); ++index) {
                const glm::vec3& a = polygon[index];
                const glm::vec3& b = polygon[(index + 1) % polygon.size()];
                const float da = glm::dot(normal, a) + plane.w;
                const float db = glm::dot(normal, b) + plane.w;
                if (da >= 0.0f)
                    clipped.push_back(a);
                if ((da >= 0.0f) != (db >= 0.0f))
                    clipped.push_back(a + (b - a) * (da / (da - db)));
            }
            polygon.swap(clipped);
        }
        return polygon;
    }

    bool AABBIntersectsVolume(const glm::vec3& InMin, const glm::vec3& InMax, const FViewVolume& InVolume) {
        for (const glm::vec4& plane : InVolume.Planes) {
            const glm::vec3 positive(plane.x >= 0.0f ? InMax.x : InMin.x, plane.y >= 0.0f ? InMax.y : InMin.y,
                                     plane.z >= 0.0f ? InMax.z : InMin.z);
            if (glm::dot(glm::vec3(plane), positive) + plane.w < 0.0f)
                return false;
        }
        return true;
    }

    namespace {

        /**
         * Follows the portals from InEye: the zones holding it are seen whole (through InVolume), the
         * others through the portals in turn, narrowed at each. InMaxPortalDistance leaves out portals
         * further than that from the eye (a light's radius). False when the eye is in no zone or the
         * level has more portal paths than are followed: nothing is known.
         */
        bool TraverseZones(const std::vector<FZoneBox>& InZones, const std::vector<FZonePortal>& InPortals,
                           const glm::vec3& InEye, const FViewVolume& InVolume, float InNearRadius,
                           float InMaxPortalDistance, FZoneVisibility& OutResult) {
            const size_t zoneCount = InZones.size();
            if (zoneCount == 0 || zoneCount > kMaxVisibilityZones)
                return false;
            OutResult.Visible.assign(zoneCount, 0);
            OutResult.Whole.assign(zoneCount, 0);
            OutResult.Views.assign(zoneCount, {});

            std::vector<uint8_t> start(zoneCount, 0);
            for (size_t index = 0; index < zoneCount; ++index)
                start[index] = BoxHolds(InZones[index], InEye, 0.0f) ? 1 : 0;
            if (std::find(start.begin(), start.end(), uint8_t(1)) == start.end())
                return false;
            std::vector<uint8_t> usable(InPortals.size(), 0);
            for (size_t index = 0; index < InPortals.size(); ++index) {
                const FZonePortal& portal = InPortals[index];
                const float distance = DistanceToPortal(portal, InEye);
                usable[index] = portal.bOpen && distance <= InMaxPortalDistance;
                // A portal the eye stands in (a camera's near plane may reach through it): both sides whole.
                if (!usable[index] || distance > InNearRadius)
                    continue;
                for (const int32_t zone : portal.Zones) {
                    if (zone >= 0)
                        start[static_cast<size_t>(zone)] = 1;
                }
                if ((portal.Zones[0] < 0) != (portal.Zones[1] < 0))
                    OutResult.bOutsideVisible = true;
            }

            std::vector<std::vector<uint32_t>> portalsOf(zoneCount);
            for (uint32_t index = 0; index < InPortals.size(); ++index) {
                for (const int32_t zone : InPortals[index].Zones) {
                    if (zone >= 0 && usable[index])
                        portalsOf[static_cast<size_t>(zone)].push_back(index);
                }
            }

            struct FStep {
                uint32_t Zone = 0;
                FViewVolume Volume;
                /** Portals already crossed on the way here: a path never crosses one twice. */
                std::vector<uint8_t> Crossed;
            };
            std::vector<FStep> pending;
            for (uint32_t zone = 0; zone < zoneCount; ++zone) {
                if (!start[zone])
                    continue;
                OutResult.Visible[zone] = 1;
                OutResult.Whole[zone] = 1;
                pending.push_back({zone, InVolume, std::vector<uint8_t>(InPortals.size(), 0)});
            }

            uint32_t steps = 0;
            std::vector<glm::vec3> corners(4);
            while (!pending.empty()) {
                FStep step = std::move(pending.back());
                pending.pop_back();
                for (const uint32_t portalIndex : portalsOf[step.Zone]) {
                    const FZonePortal& portal = InPortals[portalIndex];
                    if (step.Crossed[portalIndex])
                        continue;
                    glm::vec3 rectangle[4];
                    portal.Corners(rectangle);
                    corners.assign(std::begin(rectangle), std::end(rectangle));
                    const std::vector<glm::vec3> seen = ClipPolygon(corners, step.Volume.Planes);
                    if (seen.size() < 3)
                        continue;
                    const int32_t other =
                        portal.Zones[0] == static_cast<int32_t>(step.Zone) ? portal.Zones[1] : portal.Zones[0];
                    if (other < 0) {
                        OutResult.bOutsideVisible = true;
                        continue;
                    }
                    if (++steps > kMaxPortalSteps)
                        return false; // a maze of portals: draw everything rather than guess
                    const size_t next = static_cast<size_t>(other);
                    // A zone the eye stands in is followed from there with the whole volume, which holds
                    // whatever is seen through this portal.
                    if (start[next])
                        continue;
                    // Zones[0] is on the side the normal points to (ResolvePortalZones).
                    const glm::vec3 beyond = other == portal.Zones[0] ? portal.Normal() : -portal.Normal();
                    FViewVolume narrowed;
                    if (!NarrowThrough(InEye, seen, beyond, narrowed))
                        narrowed = step.Volume;
                    OutResult.Visible[next] = 1;
                    if (!OutResult.Whole[next]) {
                        if (OutResult.Views[next].size() < kMaxViewsPerZone) {
                            OutResult.Views[next].push_back(narrowed);
                        } else {
                            OutResult.Whole[next] = 1;
                            OutResult.Views[next].clear();
                        }
                    }
                    FStep further;
                    further.Zone = static_cast<uint32_t>(next);
                    further.Volume = std::move(narrowed);
                    further.Crossed = step.Crossed;
                    further.Crossed[portalIndex] = 1;
                    pending.push_back(std::move(further));
                }
            }
            OutResult.bActive = true;
            return true;
        }

        bool SeenThrough(const FZoneVisibility& InVisibility, size_t InZone, const glm::vec3& InMin, const glm::vec3& InMax) {
            if (!InVisibility.Visible[InZone])
                return false;
            if (InVisibility.Whole[InZone])
                return true;
            for (const FViewVolume& volume : InVisibility.Views[InZone]) {
                if (AABBIntersectsVolume(InMin, InMax, volume))
                    return true;
            }
            return false;
        }

    } // namespace

    FZoneVisibility ComputeZoneVisibility(const std::vector<FZoneBox>& InZones,
                                          const std::vector<FZonePortal>& InPortals,
                                          const glm::vec3& InCameraPosition, const FFrustumPlanes& InFrustum,
                                          float InNearRadius) {
        FZoneVisibility result;
        if (!TraverseZones(InZones, InPortals, InCameraPosition, FrustumVolume(InFrustum), InNearRadius,
                           std::numeric_limits<float>::max(), result))
            return FZoneVisibility{};
        return result;
    }

    uint64_t ZonesHolding(const std::vector<FZoneBox>& InZones, const glm::vec3& InMin, const glm::vec3& InMax) {
        uint64_t mask = 0;
        const size_t count = std::min<size_t>(InZones.size(), kMaxVisibilityZones);
        for (size_t index = 0; index < count; ++index) {
            if (BoxHolds(InZones[index], InMin, kZoneHoldSlack) && BoxHolds(InZones[index], InMax, kZoneHoldSlack))
                mask |= uint64_t(1) << index;
        }
        return mask;
    }

    uint64_t ZonesTouching(const std::vector<FZoneBox>& InZones, const glm::vec3& InMin, const glm::vec3& InMax) {
        uint64_t mask = 0;
        const size_t count = std::min<size_t>(InZones.size(), kMaxVisibilityZones);
        for (size_t index = 0; index < count; ++index) {
            const FZoneBox& box = InZones[index];
            if (glm::all(glm::lessThanEqual(InMin, box.Max)) && glm::all(glm::greaterThanEqual(InMax, box.Min)))
                mask |= uint64_t(1) << index;
        }
        return mask;
    }

    bool IsHeldBoundsVisible(const FZoneVisibility& InVisibility, uint64_t InHolding, const glm::vec3& InMin,
                             const glm::vec3& InMax) {
        if (!InVisibility.bActive || InHolding == 0)
            return true;
        for (size_t zone = 0; zone < InVisibility.Visible.size(); ++zone) {
            if ((InHolding & (uint64_t(1) << zone)) && SeenThrough(InVisibility, zone, InMin, InMax))
                return true;
        }
        return false;
    }

    bool IsCasterLit(const FZoneLightReach& InReach, uint64_t InTouching, const glm::vec3& InMin, const glm::vec3& InMax) {
        if (!InReach.bKnown || InReach.bOutside || InTouching == 0)
            return true;
        for (size_t zone = 0; zone < InReach.Lit.Visible.size(); ++zone) {
            if ((InTouching & (uint64_t(1) << zone)) && SeenThrough(InReach.Lit, zone, InMin, InMax))
                return true;
        }
        return false;
    }

    FZoneLightReach ComputeZoneLightReach(const std::vector<FZoneBox>& InZones,
                                          const std::vector<FZonePortal>& InPortals, const glm::vec3& InPosition,
                                          float InRadius) {
        FZoneLightReach reach;
        // Every way from the light, out to its radius: no planes to start with.
        if (!TraverseZones(InZones, InPortals, InPosition, FViewVolume{}, 0.05f, std::max(InRadius, 0.0f), reach.Lit))
            return FZoneLightReach{};
        reach.bKnown = true;
        reach.bOutside = reach.Lit.bOutsideVisible;
        for (size_t zone = 0; zone < reach.Lit.Visible.size(); ++zone) {
            if (reach.Lit.Visible[zone])
                reach.Zones |= uint64_t(1) << zone;
        }
        return reach;
    }

} // namespace Leon
