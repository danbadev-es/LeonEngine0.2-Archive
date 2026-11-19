#include <doctest/doctest.h>

#include "Engine/FGraphicsQuality.hpp"
#include "Engine/UWorld.hpp"
#include "RenderCore/FLightSelection.hpp"
#include "RenderCore/FPerspectiveCamera.hpp"
#include "Renderer/FWorldRenderer.hpp"

#include <algorithm>
#include <vector>

// Which lights get the renderer's slots when a world has more than fit. No device needed: the choice
// is plain arithmetic on positions, radii and intensities.

namespace Leon {

    namespace {

        /** A camera at the origin looking down -Z, and what it sees. */
        struct FSelectionView {
            FPerspectiveCamera Camera{60.0f, 16.0f / 9.0f, 0.1f, 200.0f};
            glm::vec3 Position{0.0f};
            FFrustumPlanes Frustum;

            FSelectionView() {
                Camera.SetPosition(Position);
                Camera.SetRotation(0.0f, -90.0f);
                Frustum = ExtractFrustumPlanes(Camera.GetViewProjectionMatrix());
            }

            std::vector<uint32_t> Select(const std::vector<FLightCandidate>& InLights, uint32_t InMaxCount,
                                         bool bInViewOnly = false) const {
                return SelectLightsByImportance(InLights, Position, Frustum, InMaxCount, bInViewOnly);
            }
        };

        FLightCandidate MakeLight(uint32_t InId, const glm::vec3& InPosition, float InRadius = 6.0f,
                                  float InStrength = 10.0f) {
            FLightCandidate light;
            light.Id = InId;
            light.Position = InPosition;
            light.Radius = InRadius;
            light.Strength = InStrength;
            return light;
        }

        bool Contains(const std::vector<uint32_t>& InList, uint32_t InValue) {
            return std::find(InList.begin(), InList.end(), InValue) != InList.end();
        }

    } // namespace

    TEST_SUITE("Light selection") {

        TEST_CASE("the view looks down -Z, so the tests below place lights where they mean to") {
            const FSelectionView view;
            CHECK(SphereIntersectsFrustum({0.0f, 0.0f, -10.0f}, 1.0f, view.Frustum));
            CHECK_FALSE(SphereIntersectsFrustum({0.0f, 0.0f, 10.0f}, 1.0f, view.Frustum));
            // Behind the camera, but large enough to reach in front of it.
            CHECK(SphereIntersectsFrustum({0.0f, 0.0f, 3.0f}, 5.0f, view.Frustum));
        }

        TEST_CASE("with more lights than slots the nearest stay, whatever order they are listed in") {
            const FSelectionView view;
            // Twelve equal lights ahead of the camera, listed far and near interleaved. Some reach the
            // camera (within 6 m) and some do not: both ways of ranking have to agree on "nearest".
            const float depths[12] = {20.0f, 2.0f, 14.0f, 4.0f, 18.0f, 3.0f, 16.0f, 5.0f, 9.0f, 7.0f, 11.0f, 8.0f};
            std::vector<FLightCandidate> lights;
            for (uint32_t i = 0; i < 12; ++i)
                lights.push_back(MakeLight(100 + i, {0.0f, 0.0f, -depths[i]}));

            const std::vector<uint32_t> selected = view.Select(lights, 8);
            REQUIRE(selected.size() == 8);
            for (uint32_t i = 0; i < 12; ++i) {
                CAPTURE(depths[i]);
                CHECK(Contains(selected, i) == (depths[i] <= 11.0f));
            }
            // Most important first.
            CHECK(selected.front() == 1);
            CHECK(selected.back() == 10);

            // The same lights listed backwards give the same choice.
            std::vector<FLightCandidate> reversed(lights.rbegin(), lights.rend());
            const std::vector<uint32_t> selectedReversed = view.Select(reversed, 8);
            REQUIRE(selectedReversed.size() == 8);
            for (size_t i = 0; i < 8; ++i)
                CHECK(reversed[selectedReversed[i]].Id == lights[selected[i]].Id);
        }

        TEST_CASE("everything fits: every light is selected") {
            const FSelectionView view;
            const std::vector<FLightCandidate> lights = {MakeLight(1, {0.0f, 0.0f, -50.0f}), MakeLight(2, {0.0f, 0.0f, 50.0f})};
            CHECK(view.Select(lights, 8).size() == 2);
        }

        TEST_CASE("a movable light keeps its slot against stronger level lights") {
            const FSelectionView view;
            std::vector<FLightCandidate> lights;
            for (uint32_t i = 0; i < 8; ++i)
                lights.push_back(MakeLight(i, {0.0f, 0.0f, -1.0f - 0.1f * static_cast<float>(i)}, 10.0f, 100.0f));
            // The flashlight: weak, and listed last.
            FLightCandidate flashlight = MakeLight(50, {0.2f, -0.2f, -0.8f}, 26.0f, 1.0f);
            flashlight.bMovable = true;
            lights.push_back(flashlight);

            const std::vector<uint32_t> selected = view.Select(lights, 8);
            REQUIRE(selected.size() == 8);
            CHECK(selected.front() == 8);
            // The level light that made room is the least important one.
            CHECK_FALSE(Contains(selected, 7));
            // With one shadow map to give, it goes to the flashlight too.
            CHECK(view.Select(lights, 1, true) == std::vector<uint32_t>{8});
        }

        TEST_CASE("lights that reach nothing on screen come last and get no shadow map") {
            const FSelectionView view;
            // Behind the camera and too small to reach in front of it, but very near and very strong.
            const FLightCandidate behind = MakeLight(1, {0.0f, 0.0f, 4.0f}, 2.0f, 1000.0f);
            const FLightCandidate ahead = MakeLight(2, {0.0f, 0.0f, -30.0f}, 5.0f, 1.0f);

            CHECK(view.Select({behind, ahead}, 1) == std::vector<uint32_t>{1});
            // Free slots still go to it: it may light what a mirror shows.
            CHECK(view.Select({behind, ahead}, 2) == std::vector<uint32_t>{1, 0});
            CHECK(view.Select({behind, ahead}, 2, true) == std::vector<uint32_t>{1});
        }

        TEST_CASE("a light that holds a slot keeps it against one that is only slightly better") {
            const FSelectionView view;
            FLightCandidate holder = MakeLight(1, {0.0f, 0.0f, -3.2f});
            const FLightCandidate challenger = MakeLight(2, {0.0f, 0.0f, -3.0f});
            CHECK(view.Select({holder, challenger}, 1) == std::vector<uint32_t>{1});
            holder.bWasSelected = true;
            CHECK(view.Select({holder, challenger}, 1) == std::vector<uint32_t>{0});
            // Not against one that is clearly better.
            const FLightCandidate close = MakeLight(3, {0.0f, 0.0f, -1.0f});
            CHECK(view.Select({holder, close}, 1) == std::vector<uint32_t>{1});

            // The same for lights that do not reach the camera and are ranked by distance.
            FLightCandidate farHolder = MakeLight(4, {0.0f, 0.0f, -21.0f});
            const FLightCandidate farChallenger = MakeLight(5, {0.0f, 0.0f, -20.0f});
            CHECK(view.Select({farHolder, farChallenger}, 1) == std::vector<uint32_t>{1});
            farHolder.bWasSelected = true;
            CHECK(view.Select({farHolder, farChallenger}, 1) == std::vector<uint32_t>{0});
        }

        TEST_CASE("equal lights are told apart by identity, not by where they are listed") {
            const FSelectionView view;
            const FLightCandidate first = MakeLight(7, {1.0f, 0.0f, -5.0f});
            const FLightCandidate second = MakeLight(9, {-1.0f, 0.0f, -5.0f});
            CHECK(view.Select({first, second}, 1) == std::vector<uint32_t>{0});
            CHECK(view.Select({second, first}, 1) == std::vector<uint32_t>{1});
        }

        TEST_CASE("quality presets say how many spot lights are shadowed, and the renderer follows") {
            CHECK(FGraphicsQuality::GetPreset(EGraphicsQuality::Low).MaxShadowedSpotLights == 1);
            CHECK(FGraphicsQuality::GetPreset(EGraphicsQuality::Medium).MaxShadowedSpotLights == 2);
            CHECK(FGraphicsQuality::GetPreset(EGraphicsQuality::High).MaxShadowedSpotLights == 4);

            const TRef<UWorld> world = UWorld::Create("ShadowBudget");
            CHECK(world->GetWorldRenderer()->GetShadowSettings().MaxShadowedSpotLights == 4);
            const FGraphicsPreset medium = FGraphicsQuality::GetPreset(EGraphicsQuality::Medium);
            world->SetProjectOmniShadowDefaults(medium.SpotResolution, medium.PointShadowResolution,
                                                medium.MaxShadowedPointLights, medium.MaxShadowedSpotLights);
            CHECK(world->GetPendingMaxShadowedSpotLights() == 2);
            CHECK(world->GetWorldRenderer()->GetShadowSettings().MaxShadowedSpotLights == 2);
            // Never more than the shadow map has layers.
            world->SetProjectOmniShadowDefaults(1024, 512, 4, 99);
            CHECK(world->GetWorldRenderer()->GetShadowSettings().MaxShadowedSpotLights ==
                  FShadowSettings::kMaxShadowedSpotLights);
        }
    }

} // namespace Leon
