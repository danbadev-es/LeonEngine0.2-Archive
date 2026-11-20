#include <doctest/doctest.h>

#include "RenderCore/FZoneVisibility.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <chrono>

// Rooms and doorways (FZoneVisibility.hpp). The level of these tests, seen from above (x right, z down):
//
//   z=0 +-------+-------+-------+
//       |   A   |   B   |   C   |      doorways at x = 4 and x = 8, z = 1.5..2.5
//   z=4 +-------+---+---+-------+
//               |   D   |              doorway at z = 4, x = 5.5..6.5
//   z=8         +-------+
//
// Rooms are 3 m high. A camera 1.5 m up.

namespace Leon {

    namespace {

        struct FLevel {
            std::vector<FZoneBox> Zones;
            std::vector<FZonePortal> Portals;
            enum { A, B, C, D };
            enum { AB, BC, BD };

            FLevel() {
                Zones = {{{0.0f, 0.0f, 0.0f}, {4.0f, 3.0f, 4.0f}},
                         {{4.0f, 0.0f, 0.0f}, {8.0f, 3.0f, 4.0f}},
                         {{8.0f, 0.0f, 0.0f}, {12.0f, 3.0f, 4.0f}},
                         {{4.0f, 0.0f, 4.0f}, {8.0f, 3.0f, 8.0f}}};
                Portals.push_back(Doorway({4.0f, 1.0f, 2.0f}, {0.0f, 0.0f, 1.0f}));
                Portals.push_back(Doorway({8.0f, 1.0f, 2.0f}, {0.0f, 0.0f, 1.0f}));
                Portals.push_back(Doorway({6.0f, 1.0f, 4.0f}, {1.0f, 0.0f, 0.0f}));
                ResolvePortalZones(Zones, Portals);
            }

            /** A doorway 1 m wide and 2 m high, its width along InAcross. */
            static FZonePortal Doorway(const glm::vec3& InCenter, const glm::vec3& InAcross) {
                FZonePortal portal;
                portal.Center = InCenter;
                portal.AxisX = InAcross;
                portal.AxisY = {0.0f, 1.0f, 0.0f};
                portal.HalfSize = {0.5f, 1.0f};
                return portal;
            }

            FZoneVisibility See(const glm::vec3& InEye, const glm::vec3& InTarget) const {
                const glm::mat4 view = glm::lookAt(InEye, InTarget, glm::vec3(0.0f, 1.0f, 0.0f));
                const glm::mat4 projection = glm::perspective(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 100.0f);
                return ComputeZoneVisibility(Zones, Portals, InEye, ExtractFrustumPlanes(projection * view), 0.2f);
            }

            bool Seen(const FZoneVisibility& InVisibility, const glm::vec3& InCenter, float InHalf = 0.2f) const {
                const glm::vec3 min = InCenter - glm::vec3(InHalf);
                const glm::vec3 max = InCenter + glm::vec3(InHalf);
                return IsHeldBoundsVisible(InVisibility, ZonesHolding(Zones, min, max), min, max);
            }
        };

    } // namespace

    TEST_SUITE("Zone visibility") {

        TEST_CASE("a portal joins the zones on its two sides; one inside a zone joins nothing") {
            const FLevel level;
            const auto joins = [&](int InPortal, int InFirst, int InSecond) {
                const FZonePortal& portal = level.Portals[static_cast<size_t>(InPortal)];
                return (portal.Zones[0] == InFirst && portal.Zones[1] == InSecond) ||
                       (portal.Zones[0] == InSecond && portal.Zones[1] == InFirst);
            };
            CHECK(joins(FLevel::AB, FLevel::A, FLevel::B));
            CHECK(joins(FLevel::BC, FLevel::B, FLevel::C));
            CHECK(joins(FLevel::BD, FLevel::B, FLevel::D));
            std::vector<FZonePortal> inside = {FLevel::Doorway({2.0f, 1.0f, 2.0f}, {0.0f, 0.0f, 1.0f})};
            ResolvePortalZones(level.Zones, inside);
            CHECK(inside[0].Zones[0] == -1);
            CHECK(inside[0].Zones[1] == -1);
            // The edge of the level: one side outside every zone.
            std::vector<FZonePortal> edge = {FLevel::Doorway({0.0f, 1.0f, 2.0f}, {0.0f, 0.0f, 1.0f})};
            ResolvePortalZones(level.Zones, edge);
            CHECK((edge[0].Zones[0] == FLevel::A) != (edge[0].Zones[1] == FLevel::A));
            CHECK((edge[0].Zones[0] == -1) != (edge[0].Zones[1] == -1));
        }

        TEST_CASE("the camera sees its zone and the zones through the doorways in view, through them only") {
            const FLevel level;
            // In A, looking down the line of doorways towards C.
            const FZoneVisibility along = level.See({1.0f, 1.5f, 2.0f}, {12.0f, 1.5f, 2.0f});
            REQUIRE(along.bActive);
            CHECK(along.Visible[FLevel::A]);
            CHECK(along.Visible[FLevel::B]);
            CHECK(along.Visible[FLevel::C]);
            CHECK_FALSE(along.Visible[FLevel::D]); // its doorway is behind B's wall from here
            CHECK(along.Whole[FLevel::A]);
            CHECK_FALSE(along.Whole[FLevel::B]);
            // In B, straight beyond the doorway; and in B, in front of the camera but behind A's wall.
            CHECK(level.Seen(along, {6.0f, 1.5f, 2.0f}));
            CHECK_FALSE(level.Seen(along, {6.0f, 1.5f, 3.6f}));
            CHECK(level.Seen(along, {10.0f, 1.5f, 2.0f}));
            CHECK_FALSE(level.Seen(along, {6.0f, 1.5f, 6.0f})); // in D

            // Looking back, away from every doorway: A alone.
            const FZoneVisibility back = level.See({3.0f, 1.5f, 2.0f}, {-5.0f, 1.5f, 2.0f});
            REQUIRE(back.bActive);
            CHECK(back.VisibleCount() == 1);
            CHECK_FALSE(level.Seen(back, {6.0f, 1.5f, 2.0f}));

            // In B, looking at D's doorway: D, through it.
            const FZoneVisibility down = level.See({6.0f, 1.5f, 1.0f}, {6.0f, 1.0f, 8.0f});
            CHECK(down.Visible[FLevel::D]);
            CHECK(level.Seen(down, {6.0f, 1.0f, 6.0f}));
        }

        TEST_CASE("a closed portal lets nothing through") {
            FLevel level;
            level.Portals[FLevel::AB].bOpen = false;
            const FZoneVisibility along = level.See({1.0f, 1.5f, 2.0f}, {12.0f, 1.5f, 2.0f});
            REQUIRE(along.bActive);
            CHECK(along.VisibleCount() == 1);
            CHECK_FALSE(level.Seen(along, {6.0f, 1.5f, 2.0f}));
            CHECK_FALSE(level.Seen(along, {10.0f, 1.5f, 2.0f}));
            // Closing the far one only cuts what is beyond it.
            level.Portals[FLevel::AB].bOpen = true;
            level.Portals[FLevel::BC].bOpen = false;
            const FZoneVisibility half = level.See({1.0f, 1.5f, 2.0f}, {12.0f, 1.5f, 2.0f});
            CHECK(half.Visible[FLevel::B]);
            CHECK_FALSE(half.Visible[FLevel::C]);
        }

        TEST_CASE("what no single zone holds is always seen, and so is everything with the camera outside the zones") {
            const FLevel level;
            const FZoneVisibility back = level.See({3.0f, 1.5f, 2.0f}, {-5.0f, 1.5f, 2.0f});
            // Across the wall between B and C; beyond every zone.
            CHECK(level.Seen(back, {8.0f, 1.5f, 1.0f}, 0.3f));
            CHECK(level.Seen(back, {20.0f, 1.5f, 2.0f}));
            CHECK(ZonesHolding(level.Zones, {7.7f, 1.0f, 1.0f}, {8.3f, 2.0f, 2.0f}) == 0);
            // Not in any zone: nothing to cull with.
            const FZoneVisibility outside = level.See({20.0f, 1.5f, 2.0f}, {0.0f, 1.5f, 2.0f});
            CHECK_FALSE(outside.bActive);
            CHECK(level.Seen(outside, {6.0f, 1.5f, 6.0f}));
        }

        TEST_CASE("a doorway the camera stands in shows both rooms whole") {
            const FLevel level;
            // At the doorway between A and B, looking along the wall into A: B is behind the camera's
            // shoulder, but its near plane may reach into it.
            const FZoneVisibility at = level.See({3.95f, 1.5f, 2.0f}, {3.95f, 1.5f, -5.0f});
            CHECK(at.Whole[FLevel::A]);
            CHECK(at.Whole[FLevel::B]);
        }

        TEST_CASE("a light reaches its zone and the zones behind open doorways within its radius") {
            FLevel level;
            const FZoneLightReach inC = ComputeZoneLightReach(level.Zones, level.Portals, {10.0f, 2.0f, 2.0f}, 3.0f);
            REQUIRE(inC.bKnown);
            CHECK(inC.Zones == ((uint64_t(1) << FLevel::C) | (uint64_t(1) << FLevel::B)));
            // From B, with a radius that reaches the doorways of A and D, past B into A and D but not
            // beyond a doorway out of range.
            const FZoneLightReach inB = ComputeZoneLightReach(level.Zones, level.Portals, {6.0f, 2.0f, 2.0f}, 2.5f);
            CHECK(inB.Zones == 0b1111);
            level.Portals[FLevel::BD].bOpen = false;
            CHECK(ComputeZoneLightReach(level.Zones, level.Portals, {6.0f, 2.0f, 2.0f}, 2.5f).Zones == 0b0111);
            // A small light in D.
            CHECK(ComputeZoneLightReach(level.Zones, level.Portals, {6.0f, 2.0f, 7.0f}, 1.0f).Zones == 0b1000);
            // Outside every zone: unknown.
            CHECK_FALSE(ComputeZoneLightReach(level.Zones, level.Portals, {20.0f, 2.0f, 2.0f}, 5.0f).bKnown);
        }

        TEST_CASE("a level with a great many doorways is followed in bounded time") {
            // 8 x 8 rooms of 4 m, every neighbour joined by a doorway in the middle of the wall.
            std::vector<FZoneBox> zones;
            std::vector<FZonePortal> portals;
            for (int z = 0; z < 8; ++z) {
                for (int x = 0; x < 8; ++x) {
                    const glm::vec3 min(4.0f * static_cast<float>(x), 0.0f, 4.0f * static_cast<float>(z));
                    zones.push_back({min, min + glm::vec3(4.0f, 3.0f, 4.0f)});
                    if (x > 0)
                        portals.push_back(FLevel::Doorway(min + glm::vec3(0.0f, 1.0f, 2.0f), {0.0f, 0.0f, 1.0f}));
                    if (z > 0)
                        portals.push_back(FLevel::Doorway(min + glm::vec3(2.0f, 1.0f, 0.0f), {1.0f, 0.0f, 0.0f}));
                }
            }
            ResolvePortalZones(zones, portals);
            const glm::vec3 eye(1.0f, 1.5f, 1.0f);
            const glm::mat4 view = glm::lookAt(eye, glm::vec3(32.0f, 1.0f, 32.0f), glm::vec3(0.0f, 1.0f, 0.0f));
            const FFrustumPlanes frustum =
                ExtractFrustumPlanes(glm::perspective(glm::radians(90.0f), 16.0f / 9.0f, 0.1f, 100.0f) * view);
            const auto start = std::chrono::steady_clock::now();
            const FZoneVisibility visibility = ComputeZoneVisibility(zones, portals, eye, frustum, 0.2f);
            const float ms = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start).count();
            MESSAGE("64 rooms, 112 doorways: ", ms, " ms, ", visibility.bActive ? visibility.VisibleCount() : 64u,
                    " rooms seen");
            // Either a result, or the admission that it gave up: both draw what is seen.
            if (visibility.bActive)
                CHECK(visibility.Visible[1]);
            CHECK(ms < 2000.0f);
        }
    }

} // namespace Leon
