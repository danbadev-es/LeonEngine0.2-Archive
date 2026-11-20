#include <doctest/doctest.h>

#include "RenderCore/FShadowCasterCulling.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <random>

// Which casters a spot or point light's shadow map needs (FShadowCasterCulling.hpp). Every test must
// be conservative: dropping a caster that shadows something seen is a visible error.

namespace Leon {

    namespace {

        glm::vec3 Corner(const glm::vec3& InMin, const glm::vec3& InMax, uint32_t InIndex) {
            return {(InIndex & 1) ? InMax.x : InMin.x, (InIndex & 2) ? InMax.y : InMin.y,
                    (InIndex & 4) ? InMax.z : InMin.z};
        }

        bool Inside(const glm::vec3& InPoint, const glm::vec3& InMin, const glm::vec3& InMax, float InSlack = 1e-3f) {
            return glm::all(glm::greaterThanEqual(InPoint, InMin - InSlack)) &&
                   glm::all(glm::lessThanEqual(InPoint, InMax + InSlack));
        }

        FFrustumPlanes CameraFrustum(const glm::vec3& InPosition, const glm::vec3& InTarget) {
            const glm::mat4 view = glm::lookAt(InPosition, InTarget, glm::vec3(0.0f, 1.0f, 0.0f));
            return ExtractFrustumPlanes(glm::perspective(glm::radians(45.0f), 16.0f / 9.0f, 0.1f, 100.0f) * view);
        }

    } // namespace

    TEST_SUITE("Shadow caster culling") {

        TEST_CASE("a box touches a sphere when its nearest point is within the radius") {
            CHECK(AABBIntersectsSphere({1.0f, -1.0f, -1.0f}, {2.0f, 1.0f, 1.0f}, glm::vec3(0.0f), 1.01f));
            CHECK_FALSE(AABBIntersectsSphere({1.0f, -1.0f, -1.0f}, {2.0f, 1.0f, 1.0f}, glm::vec3(0.0f), 0.99f));
            // Across a corner: the nearest point is the corner, sqrt(3) away.
            CHECK_FALSE(AABBIntersectsSphere(glm::vec3(1.0f), glm::vec3(2.0f), glm::vec3(0.0f), 1.7f));
            CHECK(AABBIntersectsSphere(glm::vec3(1.0f), glm::vec3(2.0f), glm::vec3(0.0f), 1.74f));
            // The centre inside the box.
            CHECK(AABBIntersectsSphere(glm::vec3(-1.0f), glm::vec3(1.0f), glm::vec3(0.0f), 0.0f));
        }

        TEST_CASE("a sphere meets a cone inside it and near its side, not far outside or behind it") {
            const glm::vec3 apex(0.0f);
            const glm::vec3 axis(0.0f, -1.0f, 0.0f);
            const float half = glm::radians(20.0f);
            const auto meets = [&](const glm::vec3& InCenter, float InRadius) {
                return SphereIntersectsCone(InCenter, InRadius, apex, axis, std::cos(half), std::sin(half));
            };
            CHECK(meets({0.0f, -3.0f, 0.0f}, 0.1f));
            // At 30 degrees off the axis, 3 m away: about 0.5 m from the side.
            const glm::vec3 off(3.0f * std::sin(glm::radians(30.0f)), -3.0f * std::cos(glm::radians(30.0f)), 0.0f);
            CHECK_FALSE(meets(off, 0.4f));
            CHECK(meets(off, 0.6f));
            CHECK_FALSE(meets({0.0f, 3.0f, 0.0f}, 0.5f));
        }

        TEST_CASE("a spot light reaches casters in its cone and sphere only") {
            const FLightReach reach =
                SpotLightReach({0.0f, 4.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, 6.0f, 20.0f, 42.0f, 1024, 0.055f);
            REQUIRE(reach.bCone);
            const glm::vec3 half(0.05f);
            // On the axis, and just inside the cut-off.
            CHECK(CasterInLightReach(glm::vec3(0.0f, 2.0f, 0.0f) - half, glm::vec3(0.0f, 2.0f, 0.0f) + half, reach));
            const glm::vec3 edge(3.0f * std::sin(glm::radians(19.5f)), 4.0f - 3.0f * std::cos(glm::radians(19.5f)), 0.0f);
            CHECK(CasterInLightReach(edge - half, edge + half, reach));
            // In a corner of the square shadow projection, 27 degrees off the axis: outside the cone.
            const glm::vec3 corner(1.26f, 0.5f, 1.26f);
            CHECK_FALSE(CasterInLightReach(corner - half, corner + half, reach));
            // On the axis, past the radius.
            CHECK_FALSE(CasterInLightReach({-0.05f, -2.5f, -0.05f}, {0.05f, -2.4f, 0.05f}, reach));
            // A cone of 85 degrees or more is not a cone worth testing: the sphere alone decides.
            CHECK_FALSE(SpotLightReach(glm::vec3(0.0f), {0.0f, -1.0f, 0.0f}, 6.0f, 84.0f, 160.0f, 1024, 0.0f).bCone);
        }

        TEST_CASE("a point light reaches casters in its sphere, not in the corners of its cube faces") {
            const FLightReach reach = PointLightReach({0.0f, 3.0f, 0.0f}, 4.0f, 0.055f);
            CHECK(CasterInLightReach({2.5f, 2.95f, 2.5f}, {2.6f, 3.05f, 2.6f}, reach));
            // In the frustum of the +X face (x beyond |z|, short of the far plane at 4 m) but 4.6 m away.
            CHECK_FALSE(CasterInLightReach({3.65f, 2.95f, 2.85f}, {3.75f, 3.05f, 2.95f}, reach));
        }

        TEST_CASE("the bounds of a caster's shadow hold every point it can shadow") {
            std::mt19937 random(7);
            std::uniform_real_distribution<float> unit(0.0f, 1.0f);
            std::uniform_real_distribution<float> spread(-6.0f, 6.0f);
            uint32_t checked = 0;
            for (int trial = 0; trial < 300; ++trial) {
                const glm::vec3 light(spread(random), spread(random), spread(random));
                const glm::vec3 center(spread(random), spread(random), spread(random));
                const glm::vec3 size(0.05f + 2.0f * unit(random), 0.05f + 2.0f * unit(random), 0.05f + 2.0f * unit(random));
                const glm::vec3 boxMin = center - size * 0.5f;
                const glm::vec3 boxMax = center + size * 0.5f;
                const float radius = 1.0f + 12.0f * unit(random);
                glm::vec3 shadowMin;
                glm::vec3 shadowMax;
                if (!CasterShadowBounds(boxMin, boxMax, light, radius, shadowMin, shadowMax))
                    continue;
                for (uint32_t corner = 0; corner < 8; ++corner)
                    CHECK(Inside(Corner(boxMin, boxMax, corner), shadowMin, shadowMax));
                // Points of the box, pushed away from the light as far as the light reaches.
                for (int sample = 0; sample < 200; ++sample) {
                    const glm::vec3 point = boxMin + size * glm::vec3(unit(random), unit(random), unit(random));
                    const glm::vec3 toPoint = point - light;
                    const float distance = glm::length(toPoint);
                    if (distance >= radius)
                        continue;
                    const float along = distance + (radius - distance) * unit(random);
                    const glm::vec3 shadowed = light + toPoint / distance * along;
                    ++checked;
                    if (!Inside(shadowed, shadowMin, shadowMax)) {
                        CAPTURE(trial);
                        CHECK(Inside(shadowed, shadowMin, shadowMax));
                    }
                }
            }
            CHECK(checked > 1000);
            // The light inside the box: the shadow can be anywhere.
            glm::vec3 unusedMin;
            glm::vec3 unusedMax;
            CHECK_FALSE(CasterShadowBounds(glm::vec3(-1.0f), glm::vec3(1.0f), glm::vec3(0.0f), 5.0f, unusedMin, unusedMax));
        }

        TEST_CASE("a caster counts for the camera's map only when its shadow can fall in view") {
            // A camera at z = 3 looking down -Z; a point light above the middle of the room.
            const FFrustumPlanes view = CameraFrustum({0.0f, 1.0f, 3.0f}, {0.0f, 0.5f, -1.0f});
            const FLightReach reach = PointLightReach({0.0f, 5.0f, -1.0f}, 10.0f, 0.055f);
            const glm::vec3 half(0.25f);
            // Above the view: its shadow falls on the floor in front of the camera.
            const glm::vec3 above(0.0f, 3.5f, -1.0f);
            CHECK_FALSE(AABBIntersectsFrustum(above - half, above + half, view));
            CHECK(CasterCanShadowView(above - half, above + half, reach, view));
            // Behind the camera: its shadow goes further behind.
            const glm::vec3 behind(0.0f, 1.0f, 6.0f);
            CHECK_FALSE(CasterCanShadowView(behind - half, behind + half, reach, view));
            // In view: always.
            const glm::vec3 seen(0.0f, 0.5f, -1.0f);
            CHECK(CasterCanShadowView(seen - half, seen + half, reach, view));
            // Around the light: it can shadow anywhere.
            CHECK(CasterCanShadowView({-0.5f, 4.5f, -1.5f}, {0.5f, 5.5f, -0.5f}, reach, view));
        }
    }

} // namespace Leon
