#include <doctest/doctest.h>

#include "Engine/FGraphicsQuality.hpp"
#include "Engine/FMapSerializer.hpp"
#include "GPU/WorldRenderFixture.hpp"
#include "RHI/FFrameProfiler.hpp"
#include "RenderCore/FPostProcessConstants.hpp"
#include "RenderCore/FVolumetricFogSettings.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

// Volumetric fog: the light of the shadowed lamps in the air of a map. Whole frames of the small scene
// of FWorldRenderFixture, looked at in the HDR target the fog is laid over, where light adds up
// linearly (the final image has been through tone mapping).

namespace Leon {

    namespace {

        float HalfToFloat(uint16_t InHalf) {
            const uint32_t sign = (InHalf >> 15) & 1u;
            const uint32_t exponent = (InHalf >> 10) & 0x1Fu;
            const uint32_t mantissa = InHalf & 0x3FFu;
            float value;
            if (exponent == 0)
                value = std::ldexp(static_cast<float>(mantissa), -24);
            else if (exponent == 31)
                value = mantissa == 0 ? std::numeric_limits<float>::infinity() : std::numeric_limits<float>::quiet_NaN();
            else
                value = std::ldexp(static_cast<float>(mantissa | 0x400u), static_cast<int>(exponent) - 25);
            return sign ? -value : value;
        }

        /** The HDR scene of a frame: linear light, three numbers per pixel, rows from the top. */
        struct FHdrImage {
            std::vector<glm::vec3> Pixels;
            const glm::vec3& At(uint32_t InX, uint32_t InY) const { return Pixels[InY * kWidth + InX]; }
        };

        struct FFogFixture : FWorldRenderFixture {
            FVolumetricFogSettings* Fog = nullptr;

            FFogFixture() {
                if (!bReady)
                    return;
                // Without ambient occlusion the fog is laid over the scene's own HDR target, which
                // the tests read; bloom and anti-aliasing only touch the final image.
                UsePlainPostProcess();
                World->GetWorldRenderer()->SetPlanarReflectionEnabled(false);
                World->GetWorldRenderer()->GetShadowSettings().bEnableShadows = true;
                // A night without a sky: what is seen in the air is what the lamps put there.
                AActor* environment = World->SpawnActor<AActor>("Environment");
                auto& sky = environment->AddComponent<FSkyboxComponent>();
                sky.bEnabled = false;
                sky.SkyZenithColor = sky.HorizonColor = sky.GroundColor = glm::vec3(0.01f);
                Fog = &environment->AddComponent<FVolumetricFogComponent>().Settings;
                Fog->bEnabled = true;
                Fog->Density = 0.05f;
                Fog->Anisotropy = 0.0f;
            }

            /**
             * An empty stage: the fixture's lights off, its box and floor hidden, the camera level.
             * Behind the air there is only the colour the scene is cleared to, which no lamp lights.
             */
            void ClearStage() {
                SwitchFixtureLightsOff();
                World->FindActorByName("Box")->GetComponent<FMeshComponent>().bVisible = false;
                World->FindActorByName("Floor")->GetComponent<FMeshComponent>().bVisible = false;
                Camera.SetRotation(0.0f, -90.0f);
            }

            /** A narrow beam at the camera's height, from the left to the right across what it sees. */
            FSpotLightComponent& AddBeam() {
                ClearStage();
                return AddSpotLight("Beam", {-4.0f, 2.0f, 2.0f}, {4.0f, 2.0f, 2.0f}, 60.0f, 12.0f, 8.0f, 12.0f);
            }

            AActor* AddBlock(const char* InName, const glm::vec3& InPosition, float InWidth, float InHeight,
                             float InDepth) {
                AActor* block = World->SpawnActor<AActor>(InName);
                block->SetActorLocation(InPosition);
                auto& mesh = block->AddComponent<FMeshComponent>(FMeshPrimitives::CreateBox(InWidth, InHeight, InDepth));
                mesh.MeshType = "Box";
                mesh.MeshWidth = InWidth;
                mesh.MeshHeight = InHeight;
                mesh.MeshDepth = InDepth;
                mesh.Mobility = EComponentMobility::Movable;
                block->AddComponent<FMaterialComponent>(FloorMaterial);
                return block;
            }

            /** Draws frames and returns the HDR scene of the last, fog included. */
            FHdrImage RenderHdr(uint32_t InFrames = 2) {
                Render(InFrames);
                IRHIDevice* device = FRHI::GetDevice();
                IRHITexture* scene = World->GetWorldRenderer()->GetSceneColorTarget();
                REQUIRE(scene != nullptr);
                REQUIRE(scene->GetDesc().Format == ERHIFormat::RGBA16_Float);
                std::vector<uint8_t> bytes;
                REQUIRE(device->ReadTexture(*scene, 0, 0, bytes));
                REQUIRE(bytes.size() == static_cast<size_t>(kWidth) * kHeight * 8);
                FHdrImage image;
                image.Pixels.resize(static_cast<size_t>(kWidth) * kHeight);
                for (size_t i = 0; i < image.Pixels.size(); ++i) {
                    uint16_t texel[4];
                    std::memcpy(texel, bytes.data() + i * 8, 8);
                    image.Pixels[i] = {HalfToFloat(texel[0]), HalfToFloat(texel[1]), HalfToFloat(texel[2])};
                }
                return image;
            }

            /** Mean light of the few pixels around where a point of the world lands in the picture. */
            float LightAt(const FHdrImage& InImage, const glm::vec3& InWorldPoint) const {
                const glm::vec4 clip = Camera.GetViewProjectionMatrix() * glm::vec4(InWorldPoint, 1.0f);
                REQUIRE(clip.w > 0.0f);
                const int centerX = static_cast<int>((clip.x / clip.w * 0.5f + 0.5f) * static_cast<float>(kWidth));
                const int centerY = static_cast<int>((0.5f - clip.y / clip.w * 0.5f) * static_cast<float>(kHeight));
                REQUIRE(centerX >= 2);
                REQUIRE(centerX < static_cast<int>(kWidth) - 2);
                REQUIRE(centerY >= 2);
                REQUIRE(centerY < static_cast<int>(kHeight) - 2);
                float sum = 0.0f;
                for (int y = centerY - 1; y <= centerY + 1; ++y) {
                    for (int x = centerX - 1; x <= centerX + 1; ++x) {
                        const glm::vec3& pixel = InImage.At(static_cast<uint32_t>(x), static_cast<uint32_t>(y));
                        sum += (pixel.r + pixel.g + pixel.b) / 3.0f;
                    }
                }
                return sum / 9.0f;
            }

            /**
             * The light one lamp puts into the air at each point: the frame with the lamp, less the
             * frame without it. Both have the same air in front of the same things, so what the air
             * hides cancels out; on an empty stage what is left is the lamp's light in the air.
             */
            template <typename FLampComponent>
            std::vector<float> AddedLight(FLampComponent& InLamp, std::initializer_list<glm::vec3> InPoints) {
                InLamp.bEnabled = true;
                const FHdrImage with = RenderHdr();
                InLamp.bEnabled = false;
                const FHdrImage without = RenderHdr();
                InLamp.bEnabled = true;
                std::vector<float> added;
                for (const glm::vec3& point : InPoints)
                    added.push_back(LightAt(with, point) - LightAt(without, point));
                return added;
            }
        };

        // On the axis of AddBeam's light, four metres along it and straight ahead of the camera; and
        // a point as far ahead but above the beam, whose view ray passes over the cone.
        const glm::vec3 kInBeam(0.0f, 2.0f, 2.0f);
        const glm::vec3 kAboveBeam(0.0f, 3.3f, 2.0f);

    } // namespace

    TEST_SUITE("Shader GPU - Volumetric fog") {

        TEST_CASE_FIXTURE(FWorldRenderFixture, "A map without fog, with it switched off or with air of no density is drawn exactly as before") {
            REQUIRE(bReady);
            // The whole chain as a game runs it: ambient occlusion, bloom and anti-aliasing on.
            const FImage plain = Render();
            const auto sameAsPlain = [&plain](const FImage& InImage) {
                size_t different = 0;
                for (size_t i = 0; i < plain.Pixels.size(); ++i)
                    different += plain.Pixels[i] != InImage.Pixels[i] ? 1 : 0;
                return different == 0;
            };

            AActor* environment = World->SpawnActor<AActor>("Environment");
            FVolumetricFogSettings& fog = environment->AddComponent<FVolumetricFogComponent>().Settings;
            // The block is there and says nothing: off is the default.
            CHECK_FALSE(fog.bEnabled);
            CHECK(sameAsPlain(Render()));

            // On in the map, off in the renderer (the Low quality preset).
            fog.bEnabled = true;
            World->GetWorldRenderer()->SetVolumetricFogEnabled(false);
            CHECK(sameAsPlain(Render()));
            World->GetWorldRenderer()->SetVolumetricFogEnabled(true);

            // Air without density scatters nothing and hides nothing.
            fog.Density = 0.0f;
            CHECK(sameAsPlain(Render()));

            // And the comparison does notice fog when there is some.
            fog.Density = 0.05f;
            CHECK_FALSE(sameAsPlain(Render()));
            fog.bEnabled = false;
            CHECK(sameAsPlain(Render()));
            CHECK(NewValidationErrors() == 0);
        }

        TEST_CASE_FIXTURE(FFogFixture, "A spot light pointing across the view lights the air inside its cone and not outside it") {
            REQUIRE(bReady);
            FSpotLightComponent& beam = AddBeam();
            const glm::vec3 nearLamp(-2.5f, 2.0f, 2.0f);
            const std::vector<float> added = AddedLight(beam, {kInBeam, kAboveBeam, nearLamp});
            const float inBeam = added[0];
            const float aboveBeam = added[1];
            CAPTURE(inBeam);
            CAPTURE(aboveBeam);
            CHECK(inBeam > 0.005f);
            // Outside the cone the lamp adds nothing to the air.
            CHECK(std::abs(aboveBeam) < inBeam * 0.02f);
            // Nearer the lamp the beam is narrower and brighter.
            CHECK(added[2] > inBeam * 1.5f);

            // And the air is not lit at all where the fog is off: the comparison sees only the air.
            Fog->bEnabled = false;
            CHECK(std::abs(AddedLight(beam, {kInBeam})[0]) < inBeam * 0.02f);
            Fog->bEnabled = true;

            // A light without a shadow map would shine through walls: it is not followed through the air.
            beam.bCastShadows = false;
            const float unshadowed = AddedLight(beam, {kInBeam})[0];
            CAPTURE(unshadowed);
            CHECK(std::abs(unshadowed) < inBeam * 0.02f);
            CHECK(NewValidationErrors() == 0);
        }

        TEST_CASE_FIXTURE(FFogFixture, "Something standing in a beam leaves a dark shaft in the air behind it") {
            REQUIRE(bReady);
            FSpotLightComponent& beam = AddBeam();
            // Between the lamp and where the block will stand; its view ray passes in front of the block.
            const glm::vec3 beforeBlock(-2.8f, 2.0f, 2.0f);
            const std::vector<float> open = AddedLight(beam, {kInBeam, beforeBlock});

            // A block two and a half metres along the beam, wider than the beam is there.
            AddBlock("Block", {-1.5f, 2.0f, 2.0f}, 0.9f, 0.9f, 0.9f);
            const std::vector<float> blocked = AddedLight(beam, {kInBeam, beforeBlock});

            const float openBeam = open[0];
            const float shaft = blocked[0];
            CAPTURE(openBeam);
            CAPTURE(shaft);
            CHECK(openBeam > 0.005f);
            CHECK(std::abs(shaft) < openBeam * 0.1f);

            // Between the lamp and the block the air is lit as before.
            CAPTURE(open[1]);
            CAPTURE(blocked[1]);
            CHECK(open[1] > 0.005f);
            CHECK(blocked[1] > open[1] * 0.8f);
            CHECK(NewValidationErrors() == 0);
        }

        TEST_CASE_FIXTURE(FFogFixture, "The light in the air grows with the density, the scattering and the direction it is seen from") {
            REQUIRE(bReady);
            FSpotLightComponent& beam = AddBeam();
            const auto scattered = [this, &beam]() { return AddedLight(beam, {kInBeam})[0]; };

            Fog->Density = 0.02f;
            const float thin = scattered();
            Fog->Density = 0.04f;
            const float thick = scattered();
            CAPTURE(thin);
            CAPTURE(thick);
            CHECK(thin > 0.002f);
            // Twice the air sends on twice the light, less the little more it takes back on the four
            // metres to the camera.
            CHECK(thick > thin * 1.7f);
            CHECK(thick < thin * 2.0f);

            // No density, no light: the frame is the clear one.
            Fog->Density = 0.0f;
            CHECK(scattered() == 0.0f);

            Fog->Density = 0.04f;
            Fog->Scattering = 2.0f;
            CHECK(scattered() == doctest::Approx(thick * 2.0f).epsilon(0.03));
            Fog->Scattering = 0.0f;
            CHECK(std::abs(scattered()) < thick * 0.01f);
            Fog->Scattering = 1.0f;

            // The beam is seen from the side. Air that sends light on forwards shows less of it there...
            Fog->Anisotropy = 0.8f;
            const float forward = scattered();
            CAPTURE(forward);
            CHECK(forward < thick * 0.3f);
            CHECK(forward > thick * 0.05f);
            // ...and the same amount for forwards as for backwards, at a right angle.
            Fog->Anisotropy = -0.8f;
            CHECK(scattered() == doctest::Approx(forward).epsilon(0.1));

            // Seen at an angle: on the lamp's side of the view the camera looks partly into the beam,
            // on the other side partly along it. Forward scattering keeps most of the light on the
            // first side and takes most of it from the second, relative to air that scatters evenly.
            const glm::vec3 towardsLamp(-2.5f, 2.0f, 2.0f);
            const glm::vec3 awayFromLamp(2.5f, 2.0f, 2.0f);
            Fog->Anisotropy = 0.0f;
            const std::vector<float> even = AddedLight(beam, {towardsLamp, awayFromLamp});
            Fog->Anisotropy = 0.6f;
            const std::vector<float> ahead = AddedLight(beam, {towardsLamp, awayFromLamp});
            const float keptTowards = ahead[0] / even[0];
            const float keptAway = ahead[1] / even[1];
            CAPTURE(keptTowards);
            CAPTURE(keptAway);
            CHECK(even[1] > 0.0005f);
            CHECK(keptTowards > 0.8f);
            CHECK(keptAway < 0.5f);
            Fog->Anisotropy = 0.0f;

            // More steps along the ray give the same beam, not a brighter one.
            Fog->StepCount = 48;
            CHECK(scattered() == doctest::Approx(thick).epsilon(0.1));
            Fog->StepCount = 0;

            // Beyond MaxDistance the air is not looked at: the beam is four metres away.
            Fog->MaxDistance = 2.0f;
            CHECK(std::abs(scattered()) < thick * 0.02f);
            CHECK(NewValidationErrors() == 0);
        }

        TEST_CASE_FIXTURE(FFogFixture, "Something between the camera and a beam shows only the air in front of it, up to its edges") {
            REQUIRE(bReady);
            FSpotLightComponent& beam = AddBeam();
            // A post two metres from the camera, straight ahead: the air in front of it is short of the beam.
            constexpr float kPostWidth = 0.3f;
            AddBlock("Post", {0.0f, 2.0f, 4.0f}, kPostWidth, 4.0f, 0.1f);
            Fog->bEnabled = true;
            beam.bEnabled = true;
            const FHdrImage with = RenderHdr();
            beam.bEnabled = false;
            const FHdrImage without = RenderHdr();

            // The columns the post covers, from its two edges on its front face, less one on each side.
            const auto columnOf = [this](const glm::vec3& InPoint) {
                const glm::vec4 clip = Camera.GetViewProjectionMatrix() * glm::vec4(InPoint, 1.0f);
                return static_cast<int>((clip.x / clip.w * 0.5f + 0.5f) * static_cast<float>(kWidth));
            };
            const int left = columnOf({-kPostWidth * 0.5f, 2.0f, 4.05f}) + 1;
            const int right = columnOf({kPostWidth * 0.5f, 2.0f, 4.05f}) - 1;
            REQUIRE(right - left > 10);
            const auto added = [&](int InX, int InY) {
                const glm::vec3 a = with.At(static_cast<uint32_t>(InX), static_cast<uint32_t>(InY));
                const glm::vec3 b = without.At(static_cast<uint32_t>(InX), static_cast<uint32_t>(InY));
                return (a.r + a.g + a.b - b.r - b.g - b.b) / 3.0f;
            };
            const int row = static_cast<int>(kHeight / 2);
            float onPost = 0.0f;
            for (int y = row - 4; y <= row + 4; ++y) {
                for (int x = left; x <= right; ++x)
                    onPost = std::max(onPost, std::abs(added(x, y)));
            }
            // Beside the post, the beam as usual.
            const float beside = added(right + 12, row);
            CAPTURE(onPost);
            CAPTURE(beside);
            CHECK(beside > 0.005f);
            CHECK(onPost < beside * 0.05f);
            CHECK(NewValidationErrors() == 0);
        }

        TEST_CASE_FIXTURE(FFogFixture, "A shadowed point light glows in the air around it, and what stands in its light cuts into the glow") {
            REQUIRE(bReady);
            ClearStage();
            AActor* lamp = World->SpawnActor<AActor>("Bulb");
            lamp->SetActorLocation({-4.0f, 2.0f, 2.0f});
            FPointLight point;
            point.Color = {1.0f, 1.0f, 1.0f};
            point.Intensity = 40.0f;
            point.Radius = 12.0f;
            auto& bulb = lamp->AddComponent<FPointLightComponent>(point);
            bulb.bCastShadows = true;

            const glm::vec3 nearBulb(-2.5f, 2.0f, 2.0f);
            const std::vector<float> open = AddedLight(bulb, {nearBulb, kInBeam});
            const float glowNear = open[0];
            const float glowFar = open[1];
            CAPTURE(glowNear);
            CAPTURE(glowFar);
            CHECK(glowFar > 0.002f);
            CHECK(glowNear > glowFar * 1.5f);

            // A screen between the bulb and the middle of the view: the air behind it is in its shadow.
            AActor* screen = AddBlock("Screen", {-2.0f, 2.0f, 2.0f}, 0.2f, 2.0f, 2.0f);
            const float behindScreen = AddedLight(bulb, {kInBeam})[0];
            CAPTURE(behindScreen);
            CHECK(behindScreen < glowFar * 0.8f);
            screen->GetComponent<FMeshComponent>().bVisible = false;

            // Without a cube of its own the bulb lights the surfaces and not the air.
            bulb.bCastShadows = false;
            const float unshadowed = AddedLight(bulb, {kInBeam})[0];
            CAPTURE(unshadowed);
            CHECK(std::abs(unshadowed) < glowFar * 0.05f);
            CHECK(NewValidationErrors() == 0);
        }

        TEST_CASE_FIXTURE(FFogFixture, "The air hides what is far and glows with its ambient light, unless the map's distance fog already does") {
            REQUIRE(bReady);
            // The fixture's own lamps on its floor and box, and no lamp followed through the air.
            World->FindActorByName("Lamp")->GetComponent<FPointLightComponent>().bCastShadows = false;
            World->FindActorByName("Spot")->GetComponent<FSpotLightComponent>().bCastShadows = false;
            // The box stands in front of the far end of the floor.
            World->FindActorByName("Box")->GetComponent<FMeshComponent>().bVisible = false;
            const glm::vec3 nearFloor(0.0f, 0.0f, 2.5f);
            const glm::vec3 farFloor(0.0f, 0.0f, -4.5f);

            Fog->bEnabled = false;
            const FHdrImage clear = RenderHdr();
            Fog->bEnabled = true;
            Fog->Density = 0.1f;
            const FHdrImage dimmed = RenderHdr();
            // exp(-density * distance): the far end of the floor loses more than the near one.
            const float nearLeft = LightAt(dimmed, nearFloor) / LightAt(clear, nearFloor);
            const float farLeft = LightAt(dimmed, farFloor) / LightAt(clear, farFloor);
            CAPTURE(nearLeft);
            CAPTURE(farLeft);
            const auto leftAfter = [this](const glm::vec3& InPoint) {
                return std::exp(-0.1f * glm::length(InPoint - Camera.GetPosition()));
            };
            CHECK(nearLeft == doctest::Approx(leftAfter(nearFloor)).epsilon(0.03));
            CHECK(farLeft == doctest::Approx(leftAfter(farFloor)).epsilon(0.03));

            // Ambient light fills in what the air took, in its own colour: thick air ends up that colour.
            Fog->Density = 2.0f;
            Fog->AmbientColor = {0.0f, 0.5f, 0.0f};
            Fog->AmbientIntensity = 1.0f;
            const FHdrImage glowing = RenderHdr();
            const glm::vec4 clip = Camera.GetViewProjectionMatrix() * glm::vec4(farFloor, 1.0f);
            const glm::vec3 thickAir = glowing.At(static_cast<uint32_t>((clip.x / clip.w * 0.5f + 0.5f) * kWidth),
                                                  static_cast<uint32_t>((0.5f - clip.y / clip.w * 0.5f) * kHeight));
            CHECK(thickAir.g == doctest::Approx(0.5f).epsilon(0.02));
            CHECK(thickAir.r < 0.01f);

            // With the map's distance fog on, the surfaces have been through fog once: the air of
            // this pass leaves them alone and adds no glow of its own.
            auto& sky = World->FindActorByName("Environment")->GetComponent<FSkyboxComponent>();
            sky.bFogEnabled = true;
            sky.FogDensity = 0.05f;
            // A shadowed spot light behind the camera, shining away from what it sees: the fog passes
            // run (they have a lamp to follow) but no view ray crosses its beam.
            AddSpotLight("Behind", {0.0f, 2.0f, 8.0f}, {0.0f, 2.0f, 16.0f}, 30.0f, 12.0f, 10.0f, 15.0f);
            Fog->bEnabled = false;
            const FHdrImage distanceFog = RenderHdr();
            Fog->bEnabled = true;
            // Enough frames for the GPU timer of the fog to come back.
            const FHdrImage both = RenderHdr(6);
            CHECK(FRHI::GetDevice()->GetGpuTimeMs(EGPUTimerSlot::VolumetricFog) > 0.0f);
            bool bSame = true;
            for (size_t i = 0; i < both.Pixels.size(); ++i)
                bSame = bSame && both.Pixels[i] == distanceFog.Pixels[i];
            CHECK(bSame);
            CHECK(NewValidationErrors() == 0);
        }

        TEST_CASE_FIXTURE(FFogFixture, "Nothing that is not a number reaches the HDR scene, whatever the settings and wherever the camera") {
            REQUIRE(bReady);
            // The fixture's lamp (a point light with a cube) and its spot light, both shadowed.
            AddSpotLight("Beam", {-4.0f, 2.0f, 2.0f}, {4.0f, 2.0f, 2.0f}, 60.0f, 12.0f, 8.0f, 12.0f);
            const auto checkFinite = [this](const char* InWhat) {
                const FHdrImage image = RenderHdr();
                size_t bad = 0;
                float brightest = 0.0f;
                for (const glm::vec3& pixel : image.Pixels) {
                    for (int channel = 0; channel < 3; ++channel) {
                        if (!std::isfinite(pixel[channel]) || pixel[channel] < 0.0f)
                            ++bad;
                        else
                            brightest = std::max(brightest, pixel[channel]);
                    }
                }
                CAPTURE(InWhat);
                CAPTURE(brightest);
                CHECK(bad == 0);
                // A half float holds 65504 at most: brighter than this and the target has overflowed somewhere.
                CHECK(brightest < 60000.0f);
            };

            checkFinite("as the fixture is");
            Fog->Anisotropy = 1.0f; // all the light in one direction: the phase function has no limit there
            checkFinite("g = 1");
            Fog->Anisotropy = -1.0f;
            checkFinite("g = -1");
            Fog->Anisotropy = 0.5f;
            Fog->Density = 1.0e6f;
            checkFinite("air as thick as a wall");
            Fog->Density = 1.0e-7f;
            Fog->Scattering = 1.0e6f;
            checkFinite("almost no air, scattering everything");
            Fog->Density = 0.05f;
            Fog->Scattering = 1.0f;
            Fog->MaxDistance = 1.0e9f;
            Fog->StepCount = 1000;
            Fog->AmbientIntensity = 3.0f;
            checkFinite("no limit of distance, more steps than allowed");
            Fog->MaxDistance = 30.0f;
            Fog->StepCount = 1;

            // The camera inside the lamp, on the axis of the beam and looking along it, then into it.
            Camera.SetPosition({-4.0f, 2.0f, 2.0f});
            Camera.SetRotation(0.0f, 0.0f);
            checkFinite("in the spot light, looking along its beam");
            Camera.SetPosition({2.0f, 2.0f, 2.0f});
            Camera.SetRotation(0.0f, 180.0f);
            checkFinite("in the beam, looking into the light");
            Camera.SetPosition({2.5f, 3.0f, 1.0f});
            Camera.SetRotation(-89.0f, -90.0f);
            checkFinite("in the point light, looking down");
            // A light without reach or without a cone to speak of.
            World->FindActorByName("Beam")->GetComponent<FSpotLightComponent>().Light.Radius = 0.0f;
            World->FindActorByName("Spot")->GetComponent<FSpotLightComponent>().Light.OuterCutOff = 89.9f;
            World->FindActorByName("Lamp")->GetComponent<FPointLightComponent>().Light.Radius = 0.0f;
            Camera.SetPosition({0.0f, 2.0f, 6.0f});
            Camera.SetRotation(-15.0f, -90.0f);
            checkFinite("lights of no radius and a cone of a half space");
            CHECK(NewValidationErrors() == 0);
        }

        TEST_CASE_FIXTURE(FFogFixture, "The fog is timed on its own, inside post-processing") {
            REQUIRE(bReady);
            AddBeam();
            IRHIDevice* device = FRHI::GetDevice();
            // Results lag by the frames the device keeps in flight: draw a few.
            Render(6);
            const float fog = device->GetGpuTimeMs(EGPUTimerSlot::VolumetricFog);
            const float post = device->GetGpuTimeMs(EGPUTimerSlot::PostProcess);
            CAPTURE(fog);
            CAPTURE(post);
            CHECK(fog > 0.0f);
            CHECK(fog <= post);
            Fog->bEnabled = false;
            Render(6);
            CHECK(device->GetGpuTimeMs(EGPUTimerSlot::VolumetricFog) == 0.0f);
            CHECK(NewValidationErrors() == 0);
        }

        TEST_CASE_FIXTURE(FFogFixture, "The fog of a room with every shadowed light of the High preset costs well under 1.5 ms at 1080p") {
            REQUIRE(bReady);
            // A measurement of the graphics card: WARP's times say nothing about it.
            if (std::getenv("LEON_TEST_GPU") == nullptr) {
                MESSAGE("Run with LEON_TEST_GPU=1 to time the fog on the graphics card.");
                return;
            }
            IRHIDevice* device = FRHI::GetDevice();
            FWorldRenderer* renderer = World->GetWorldRenderer();
            // The game's chain at High: ambient occlusion, bloom, anti-aliasing, 12 steps, 4 + 4 shadow maps.
            FPostProcessSettings& post = renderer->GetPostProcessSettings();
            post.bSSAOEnabled = true;
            post.bBloomEnabled = true;
            post.bFXAAEnabled = true;
            const FGraphicsPreset high = FGraphicsQuality::GetPreset(EGraphicsQuality::High);
            renderer->SetVolumetricFogSteps(high.VolumetricFogSteps);
            renderer->GetShadowSettings().MaxShadowedSpotLights = high.MaxShadowedSpotLights;
            renderer->GetShadowSettings().MaxShadowedPointLights = high.MaxShadowedPointLights;

            // A room: the fixture's floor, a ceiling at three metres, a wall at the back.
            AddBlock("Ceiling", {0.0f, 3.1f, 0.0f}, 12.0f, 0.2f, 12.0f);
            AddBlock("BackWall", {0.0f, 1.5f, -6.0f}, 12.0f, 3.0f, 0.2f);
            SwitchFixtureLightsOff();
            // The flashlight in the player's hand, three more spot lights and four bulbs under the ceiling.
            AddSpotLight("Flashlight", {0.3f, 1.4f, 4.5f}, {0.0f, 0.8f, -2.0f}, 80.0f, 15.0f, 12.0f, 20.0f);
            for (int i = 0; i < 3; ++i) {
                const float x = -3.0f + 3.0f * static_cast<float>(i);
                AddSpotLight(("Ceiling Spot " + std::to_string(i)).c_str(), {x, 2.9f, -2.0f}, {x, 0.0f, -2.5f}, 40.0f,
                             6.0f, 25.0f, 35.0f);
            }
            for (int i = 0; i < 4; ++i) {
                AActor* bulb = World->SpawnActor<AActor>("Bulb " + std::to_string(i));
                bulb->SetActorLocation({-4.5f + 3.0f * static_cast<float>(i), 2.7f, 0.5f});
                FPointLight point;
                point.Color = {1.0f, 0.9f, 0.7f};
                point.Intensity = 20.0f;
                point.Radius = 8.0f;
                bulb->AddComponent<FPointLightComponent>(point).bCastShadows = true;
            }
            Fog->Density = 0.04f;
            Fog->AmbientIntensity = 0.02f;
            Camera.SetPosition({0.0f, 1.6f, 5.0f});
            Camera.SetRotation(-5.0f, -90.0f);

            FRHITextureDesc desc;
            desc.Width = 1920;
            desc.Height = 1080;
            desc.Format = ERHIFormat::RGBA8_UNorm;
            desc.Usage = RHITextureUsage_Sampled | RHITextureUsage_RenderTarget;
            desc.DebugName = "Output1080p";
            Target = device->CreateTexture(desc);
            REQUIRE(Target != nullptr);
            renderer->OnViewportResize(desc.Width, desc.Height);

            // GPU times arrive a few frames late: the first frames are left out of the average.
            const auto timeFrames = [&](float& OutFog, float& OutPost) {
                constexpr uint32_t kWarmUp = 8;
                constexpr uint32_t kTimed = 60;
                double fogSum = 0.0;
                double postSum = 0.0;
                for (uint32_t frame = 0; frame < kWarmUp + kTimed; ++frame) {
                    IRHICommandList& commands = device->BeginFrame();
                    FRHIFrame::Begin(commands, *Target);
                    renderer->Render(Camera);
                    FRHIFrame::End();
                    device->EndFrame();
                    FRHIFrame::RunAfterFrameTasks();
                    if (frame >= kWarmUp) {
                        fogSum += device->GetGpuTimeMs(EGPUTimerSlot::VolumetricFog);
                        postSum += device->GetGpuTimeMs(EGPUTimerSlot::PostProcess);
                    }
                }
                device->WaitIdle();
                OutFog = static_cast<float>(fogSum / kTimed);
                OutPost = static_cast<float>(postSum / kTimed);
            };
            float fogMs = 0.0f;
            float postMs = 0.0f;
            timeFrames(fogMs, postMs);
            Fog->bEnabled = false;
            float fogOffMs = 0.0f;
            float postOffMs = 0.0f;
            timeFrames(fogOffMs, postOffMs);
            MESSAGE("Volumetric fog at 1920x1080, 4 shadowed spot + 4 shadowed point lights, ",
                    high.VolumetricFogSteps, " steps: ", fogMs, " ms (post-processing ", postMs, " ms with it, ",
                    postOffMs, " ms without)");
            CHECK(fogMs > 0.0f);
            // The budget. Runs on the laptop's RTX 5060 measured 0.6 to 1.0 ms: clocks vary between runs.
            CHECK(fogMs < 1.5f);
            CHECK(fogOffMs == 0.0f);
            CHECK(NewValidationErrors() == 0);
        }
    }

    TEST_SUITE("Volumetric fog settings") {

        TEST_CASE("the fog block of a map is written and read back, and a map without it has none") {
            const auto findFog = [](const TRef<UWorld>& InWorld) -> const FVolumetricFogComponent* {
                for (const auto& actor : InWorld->GetAllActors()) {
                    if (actor && actor->HasComponent<FVolumetricFogComponent>())
                        return &actor->GetComponent<FVolumetricFogComponent>();
                }
                return nullptr;
            };

            auto world = UWorld::Create("Foggy");
            AActor* environment = world->SpawnActor<AActor>("Environment");
            environment->AddComponent<FSkyboxComponent>();
            // An actor after the block: the block must end where the actors begin.
            world->SpawnActor<AActor>("Crate")->SetActorLocation({1.0f, 0.0f, 2.0f});
            std::string plain;
            REQUIRE(FMapSerializer(world).SerializeText(plain));
            CHECK(plain.find("VolumetricFog") == std::string::npos);
            auto plainLoaded = UWorld::Create("PlainLoaded");
            REQUIRE(FMapSerializer(plainLoaded).DeserializeText(plain));
            CHECK(findFog(plainLoaded) == nullptr);

            FVolumetricFogSettings& fog = environment->AddComponent<FVolumetricFogComponent>().Settings;
            fog.bEnabled = true;
            fog.Density = 0.045f;
            fog.Scattering = 1.5f;
            fog.Anisotropy = 0.6f;
            fog.AmbientColor = {0.2f, 0.3f, 0.4f};
            fog.AmbientIntensity = 0.25f;
            fog.MaxDistance = 18.0f;
            fog.StepCount = 16;
            std::string text;
            REQUIRE(FMapSerializer(world).SerializeText(text));
            CHECK(text.find("  VolumetricFog:\n") != std::string::npos);
            // The environment actor is not written among the actors of the map.
            CHECK(text.find("Name: \"Environment\"") == std::string::npos);

            auto loaded = UWorld::Create("FoggyLoaded");
            REQUIRE(FMapSerializer(loaded).DeserializeText(text));
            const FVolumetricFogComponent* read = findFog(loaded);
            REQUIRE(read != nullptr);
            CHECK(read->Settings.bEnabled);
            CHECK(read->Settings.Density == doctest::Approx(0.045f));
            CHECK(read->Settings.Scattering == doctest::Approx(1.5f));
            CHECK(read->Settings.Anisotropy == doctest::Approx(0.6f));
            CHECK(read->Settings.AmbientColor.b == doctest::Approx(0.4f));
            CHECK(read->Settings.AmbientIntensity == doctest::Approx(0.25f));
            CHECK(read->Settings.MaxDistance == doctest::Approx(18.0f));
            CHECK(read->Settings.StepCount == 16);
            std::string again;
            REQUIRE(FMapSerializer(loaded).SerializeText(again));
            CHECK(again == text);
            CHECK(loaded->FindActorByName("Crate") != nullptr);

            // A block that only switches the fog on takes the defaults for the rest.
            auto brief = UWorld::Create("Brief");
            REQUIRE(FMapSerializer(brief).DeserializeText(
                "Map:\n  Name: \"Brief\"\n\nEnvironment:\n  VolumetricFog:\n    Enabled: true\n\nActors:\n"));
            const FVolumetricFogComponent* briefFog = findFog(brief);
            REQUIRE(briefFog != nullptr);
            CHECK(briefFog->Settings.bEnabled);
            CHECK(briefFog->Settings.Density == doctest::Approx(FVolumetricFogSettings().Density));
            CHECK(briefFog->Settings.StepCount == 0);
        }

        TEST_CASE("the quality presets switch the fog off on Low and choose its steps") {
            CHECK_FALSE(FGraphicsQuality::GetPreset(EGraphicsQuality::Low).bEnableVolumetricFog);
            CHECK(FGraphicsQuality::GetPreset(EGraphicsQuality::Medium).bEnableVolumetricFog);
            CHECK(FGraphicsQuality::GetPreset(EGraphicsQuality::High).bEnableVolumetricFog);
            CHECK(FGraphicsQuality::GetPreset(EGraphicsQuality::Medium).VolumetricFogSteps <
                  FGraphicsQuality::GetPreset(EGraphicsQuality::High).VolumetricFogSteps);

            auto world = UWorld::Create("Quality");
            CHECK(world->GetPendingVolumetricFogEnabled());
            FGraphicsQuality::ApplyToWorld(*world, EGraphicsQuality::Low);
            CHECK_FALSE(world->GetPendingVolumetricFogEnabled());
            CHECK_FALSE(world->GetWorldRenderer()->IsVolumetricFogEnabled());
            FGraphicsQuality::ApplyToWorld(*world, EGraphicsQuality::High);
            CHECK(world->GetWorldRenderer()->IsVolumetricFogEnabled());
            CHECK(world->GetWorldRenderer()->GetVolumetricFogSteps() ==
                  FGraphicsQuality::GetPreset(EGraphicsQuality::High).VolumetricFogSteps);
        }

        TEST_CASE("the constants of the pass hold the lights that have a shadow map, and nothing is drawn without need") {
            FVolumetricFogSettings settings;
            settings.bEnabled = true;
            FLitPassConstants lighting;
            lighting.LightCounts = glm::ivec4(2, 3, 0, 0);
            lighting.ShadowFlags = glm::ivec4(0, 1, 1, 0);
            for (int i = 0; i < 3; ++i) {
                lighting.SpotLights[i].Position = glm::vec4(static_cast<float>(i), 2.0f, 0.0f, 1.0f);
                lighting.SpotLights[i].Direction = glm::vec4(0.0f, -1.0f, 0.0f, 0.9f);
                lighting.SpotLights[i].Color = glm::vec4(1.0f, 0.5f, 0.25f, 0.8f);
                lighting.SpotLights[i].Params = glm::vec4(6.0f, 4.0f, -1.0f, 0.0f);
            }
            // The first and the third spot light have a layer of the shadow map; the third has layer 0.
            lighting.SpotLights[0].Params.z = 1.0f;
            lighting.SpotLights[2].Params.z = 0.0f;
            lighting.SpotLightSpaceMatrices[0] = glm::mat4(3.0f);
            lighting.SpotLightSpaceMatrices[1] = glm::mat4(5.0f);
            for (int i = 0; i < 2; ++i) {
                lighting.PointLights[i].Position = glm::vec4(0.0f, 3.0f, static_cast<float>(i), 1.0f);
                lighting.PointLights[i].Color = glm::vec4(1.0f, 1.0f, 1.0f, 8.0f);
                lighting.PointLights[i].Params = glm::vec4(5.0f, -1.0f, 0.0f, 0.0f);
            }
            lighting.PointLights[1].Params.y = 2.0f;

            const glm::mat4 projection =
                ClipDepthZeroToOne() * glm::perspective(glm::radians(45.0f), 16.0f / 9.0f, 0.1f, 100.0f);
            const auto make = [&](bool bInDistanceFog) {
                return MakeVolumetricFogConstants(settings, lighting, projection, projection, glm::vec3(1.0f, 2.0f, 3.0f),
                                                  bInDistanceFog, 12);
            };
            FVolumetricFogConstants constants = make(false);
            REQUIRE(constants.LightCounts.x == 2);
            REQUIRE(constants.LightCounts.y == 1);
            CHECK(constants.SpotLights[0].Position == glm::vec4(0.0f, 2.0f, 0.0f, 6.0f));
            CHECK(constants.SpotLights[0].Color == glm::vec4(4.0f, 2.0f, 1.0f, 0.8f));
            CHECK(constants.SpotLights[0].Shadow.x == 1.0f);
            CHECK(constants.SpotMatrices[0] == glm::mat4(5.0f));
            CHECK(constants.SpotLights[1].Position.x == 2.0f);
            CHECK(constants.SpotMatrices[1] == glm::mat4(3.0f));
            CHECK(constants.PointLights[0].Position == glm::vec4(0.0f, 3.0f, 1.0f, 5.0f));
            CHECK(constants.PointLights[0].Color == glm::vec4(8.0f, 8.0f, 8.0f, 2.0f));
            CHECK(constants.FogParams.w == 12.0f);
            CHECK(constants.FogAmbient.a == 1.0f);
            // A depth-buffer value back to metres in front of the camera: the near and the far plane.
            CHECK(constants.DepthParams.x / (constants.DepthParams.y - 0.0f) == doctest::Approx(0.1f));
            CHECK(constants.DepthParams.x / (constants.DepthParams.y - 1.0f) == doctest::Approx(100.0f).epsilon(0.01));
            CHECK(IsVolumetricFogDrawn(constants));

            // The map's distance fog on: the air only adds the light of the lamps.
            constants = make(true);
            CHECK(constants.FogAmbient.a == 0.0f);
            CHECK(IsVolumetricFogDrawn(constants));
            // ...so without a lamp to follow there is nothing to draw.
            lighting.ShadowFlags = glm::ivec4(0);
            CHECK(make(true).LightCounts == glm::ivec4(0));
            CHECK_FALSE(IsVolumetricFogDrawn(make(true)));
            CHECK(IsVolumetricFogDrawn(make(false)));
            settings.Density = 0.0f;
            CHECK_FALSE(IsVolumetricFogDrawn(make(false)));

            // What a map asks for is held to what the shader can do.
            settings.Density = 0.03f;
            settings.StepCount = 1000;
            settings.Anisotropy = 4.0f;
            CHECK(make(false).FogParams.w == static_cast<float>(kMaxVolumetricFogSteps));
            CHECK(make(false).FogParams.z == doctest::Approx(0.95f));
            settings.StepCount = 1;
            CHECK(make(false).FogParams.w == static_cast<float>(kMinVolumetricFogSteps));
        }
    }

} // namespace Leon
