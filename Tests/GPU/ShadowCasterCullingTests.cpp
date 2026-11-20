#include <doctest/doctest.h>

#include "GPU/WorldRenderFixture.hpp"

#include "RHI/FFrameProfiler.hpp"
#include "RenderCore/FShadowTypes.hpp"

#include <cstring>
#include <string>

// A spot or point light's shadow map draws only the casters the light can reach and, for the camera's
// maps, whose shadow can fall where the camera looks (FShadowCasterCulling.hpp). Counted with the
// frame profiler's shadow draw calls, and checked against frames drawn with the culling off.

namespace Leon {

    namespace {

        struct FShadowCullFixture : FWorldRenderFixture {
            FShadowCullFixture() {
                if (!bReady)
                    return;
                SwitchFixtureLightsOff();
                UsePlainPostProcess();
            }

            FShadowSettings& Shadows() { return World->GetWorldRenderer()->GetShadowSettings(); }

            AActor* AddBox(const std::string& InName, const glm::vec3& InCenter, float InSize) {
                AActor* box = World->SpawnActor<AActor>(InName);
                box->SetActorLocation(InCenter);
                auto& mesh = box->AddComponent<FMeshComponent>(FMeshPrimitives::CreateCube(InSize));
                mesh.MeshSize = InSize;
                mesh.Mobility = EComponentMobility::Static;
                box->AddComponent<FMaterialComponent>(FloorMaterial);
                return box;
            }

            FPointLightComponent& AddPointLight(const glm::vec3& InPosition, float InRadius) {
                AActor* lamp = World->SpawnActor<AActor>("Bulb");
                lamp->SetActorLocation(InPosition);
                FPointLight point;
                point.Color = {1.0f, 1.0f, 1.0f};
                point.Intensity = 30.0f;
                point.Radius = InRadius;
                FPointLightComponent& component = lamp->AddComponent<FPointLightComponent>(point);
                component.bCastShadows = true;
                return component;
            }

            /** Shadow draw calls of a frame, with the culling on or off. */
            uint32_t ShadowDraws(bool bInCull) {
                Shadows().bCullShadowCasters = bInCull;
                Render(1);
                return FFrameProfiler::Working().ShadowDrawCalls;
            }

            /** Looking straight down on the floor from high up: every shadow on it is in view. */
            void LookDown() {
                Camera.SetPosition({0.0f, 14.0f, 0.01f});
                Camera.SetRotation(-89.9f, -90.0f);
            }
        };

    } // namespace

    TEST_SUITE("World renderer - shadow casters") {

        // Procedural boxes are culled by a cube of at least a metre around them (ProceduralMeshLocalExtent),
        // whatever their size: the boxes below are placed for those bounds. A box near the edge of a
        // cube face can be in two faces' frustums, so counts are compared, not added up.

        TEST_CASE_FIXTURE(FShadowCullFixture, "A caster outside a spot light's cone is not drawn into its map; one inside is") {
            REQUIRE(bReady);
            LookDown();
            // Straight down from 10 m, 20 degrees: the floor is lit, the fixture's box stands in the cone.
            AddSpotLight("Down", {0.0f, 10.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 200.0f, 14.0f, 15.0f, 20.0f);
            const uint32_t baseOff = ShadowDraws(false);
            const uint32_t baseOn = ShadowDraws(true);
            CHECK(baseOn == baseOff); // the floor and the box: both in the cone

            // In a corner of the square shadow projection, about 28 degrees off the axis: outside the cone.
            AActor* corner = AddBox("Corner", {3.5f, 0.5f, 3.5f}, 0.5f);
            CHECK(ShadowDraws(false) > baseOff);
            CHECK(ShadowDraws(true) == baseOn);
            corner->GetComponent<FMeshComponent>().bVisible = false;

            // In the cone.
            AddBox("InCone", {0.8f, 3.0f, 0.8f}, 0.5f);
            CHECK(ShadowDraws(false) > baseOff);
            CHECK(ShadowDraws(true) > baseOn);
            CHECK(NewValidationErrors() == 0);
        }

        TEST_CASE_FIXTURE(FShadowCullFixture, "A caster past a point light's radius is not drawn into its cube; one inside is") {
            REQUIRE(bReady);
            LookDown();
            AddPointLight({0.0f, 3.0f, 0.0f}, 4.0f);
            const uint32_t baseOff = ShadowDraws(false);
            const uint32_t baseOn = ShadowDraws(true);

            // In the frustum of the +X face (it reaches 4 m along X, and as far sideways) but 4.7 m
            // from the light at its nearest.
            AActor* far = AddBox("Far", {4.3f, 3.0f, 3.3f}, 0.5f);
            CHECK(ShadowDraws(false) > baseOff);
            CHECK(ShadowDraws(true) == baseOn);
            far->GetComponent<FMeshComponent>().bVisible = false;

            AddBox("Near", {2.0f, 2.0f, 1.0f}, 0.5f);
            CHECK(ShadowDraws(false) > baseOff);
            CHECK(ShadowDraws(true) > baseOn);
            CHECK(NewValidationErrors() == 0);
        }

        TEST_CASE_FIXTURE(FShadowCullFixture, "A caster whose shadow cannot fall in view is not drawn; one out of view whose shadow falls in it is") {
            REQUIRE(bReady);
            World->FindActorByName("Box")->GetComponent<FMeshComponent>().bVisible = false;
            Camera.SetPosition({0.0f, 1.0f, 3.0f});
            Camera.SetRotation(-7.0f, -90.0f);
            AddPointLight({0.0f, 5.0f, -1.0f}, 10.0f);
            const uint32_t baseOff = ShadowDraws(false);
            const uint32_t baseOn = ShadowDraws(true);
            const FImage empty = Render(1);

            // Behind the camera: the light throws its shadow further back.
            AActor* behind = AddBox("Behind", {0.0f, 1.0f, 6.0f}, 0.5f);
            CHECK(ShadowDraws(false) > baseOff);
            CHECK(ShadowDraws(true) == baseOn);
            behind->GetComponent<FMeshComponent>().bVisible = false;

            // Above the top of the picture, right under the light: its shadow lies on the floor in view.
            AddBox("Above", {0.0f, 3.5f, -1.0f}, 0.5f);
            CHECK(ShadowDraws(false) > baseOff);
            CHECK(ShadowDraws(true) > baseOn);
            const FImage shadowed = Render(1);
            CHECK(LumaAt(shadowed, {0.0f, 0.0f, -1.0f}) < LumaAt(empty, {0.0f, 0.0f, -1.0f}) - 0.1f);
            CHECK(NewValidationErrors() == 0);
        }

        TEST_CASE_FIXTURE(FShadowCullFixture, "Culling the casters changes no picture") {
            REQUIRE(bReady);
            // A spot light and two shadowed bulbs, and boxes all around: in view, behind the camera, to
            // the sides, beyond the lights' reach.
            AddSpotLight("Spot", {-1.5f, 3.0f, 1.0f}, {0.0f, 0.0f, -0.5f}, 40.0f, 8.0f, 15.0f, 25.0f);
            AddPointLight({2.5f, 2.5f, 0.0f}, 6.0f);
            AddPointLight({-3.0f, 2.0f, -3.0f}, 5.0f);
            for (int x = -5; x <= 5; x += 2) {
                for (int z = -5; z <= 9; z += 2)
                    AddBox("Crate", {static_cast<float>(x) + 0.3f, 0.4f + 0.1f * static_cast<float>(x & 3),
                                     static_cast<float>(z) - 0.2f},
                           0.6f);
            }
            const auto compare = [&](const char* InView) {
                Shadows().bCullShadowCasters = false;
                const uint32_t drawsOff = ShadowDraws(false);
                const FImage off = Render(2);
                Shadows().bCullShadowCasters = true;
                const uint32_t drawsOn = ShadowDraws(true);
                const FImage on = Render(2);
                CAPTURE(InView);
                CAPTURE(drawsOff);
                CAPTURE(drawsOn);
                CHECK(drawsOn < drawsOff);
                CHECK(std::memcmp(off.Pixels.data(), on.Pixels.data(), off.Pixels.size()) == 0);
            };
            compare("the fixture's view");
            Camera.SetRotation(-20.0f, -40.0f);
            compare("to the right, down");
            Camera.SetPosition({0.0f, 1.6f, -1.0f});
            Camera.SetRotation(-10.0f, 90.0f);
            compare("from the middle, back towards +Z");
            CHECK(NewValidationErrors() == 0);
        }
    }

} // namespace Leon
