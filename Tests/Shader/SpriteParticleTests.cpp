#include <doctest/doctest.h>

#include "Engine/FParticleSimulation.hpp"
#include "GPU/WorldRenderFixture.hpp"
#include "RHI/FFrameProfiler.hpp"
#include "RenderCore/FLightProbeGrid.hpp"
#include "Scene/FParticleTypes.hpp"

#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

// Sprite particles in whole frames: blended over the HDR scene after its pass, the farthest first, lit
// by the light probes and the lamps, fogged, faded where they meet a surface (SpriteParticles.hlsl).
// Looked at in the HDR target, where light adds up linearly; nothing else lights the stage.

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

        struct FHdrImage {
            std::vector<glm::vec3> Pixels;
        };

        struct FSpriteFixture : FWorldRenderFixture {
            FSkyboxComponent* Sky = nullptr;
            uint32_t Seed = 1;

            FSpriteFixture() {
                if (!bReady)
                    return;
                UsePlainPostProcess();
                SwitchFixtureLightsOff();
                World->FindActorByName("Box")->GetComponent<FMeshComponent>().bVisible = false;
                World->FindActorByName("Floor")->GetComponent<FMeshComponent>().bVisible = false;
                World->GetWorldRenderer()->SetPlanarReflectionEnabled(false);
                // No sky light: a lit sprite shows what the probes and lamps give it, nothing more.
                AActor* environment = World->SpawnActor<AActor>("Environment");
                Sky = &environment->AddComponent<FSkyboxComponent>();
                Sky->bEnabled = false;
                Sky->EnvironmentIntensity = 0.0f;
                Camera.SetPosition({0.0f, 1.0f, 6.0f});
                Camera.SetRotation(0.0f, -90.0f);
            }

            /** One sprite, standing still at InPosition, as old as InAge seconds. */
            FSpriteEmitterComponent& AddSprite(const glm::vec3& InPosition, const glm::vec4& InColor, float InSize,
                                               bool bInLit, float InAge = 0.0f, float InFadeIn = 0.0f) {
                AActor* actor = World->SpawnActor<AActor>("Sprite");
                actor->SetActorLocation(InPosition);
                FSpriteEmitterSettings settings;
                settings.Burst = 1;
                settings.SpawnRate = 0.0f;
                settings.Lifetime = 2.0f;
                settings.LifetimeSpread = 0.0f;
                settings.SpawnExtent = glm::vec3(0.0f);
                settings.ColorStart = settings.ColorEnd = InColor;
                settings.FadeIn = InFadeIn;
                settings.FadeOut = 0.0f;
                settings.SizeStart = settings.SizeEnd = InSize;
                settings.bLit = bInLit;
                settings.bRandomRotation = false;
                settings.DepthFade = 0.5f;
                auto& emitter = actor->AddComponent<FSpriteEmitterComponent>(settings);
                TickSpriteEmitter(emitter, actor->GetActorWorldMatrix(), 0.0f, Seed++);
                for (float age = 0.0f; age < InAge - 1e-4f; age += 0.1f)
                    TickSpriteEmitter(emitter, actor->GetActorWorldMatrix(), std::min(0.1f, InAge - age), Seed);
                return emitter;
            }

            FHdrImage RenderHdr() {
                Render(1);
                IRHIDevice* device = FRHI::GetDevice();
                IRHITexture* scene = World->GetWorldRenderer()->GetSceneColorTarget();
                REQUIRE(scene != nullptr);
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

            /** Mean light of the 3 x 3 pixels around where a point of the world lands, InOffset metres to the right. */
            glm::vec3 LightAt(const FHdrImage& InImage, const glm::vec3& InWorldPoint) const {
                const glm::vec4 clip = Camera.GetViewProjectionMatrix() * glm::vec4(InWorldPoint, 1.0f);
                REQUIRE(clip.w > 0.0f);
                const int centerX = static_cast<int>((clip.x / clip.w * 0.5f + 0.5f) * static_cast<float>(kWidth));
                const int centerY = static_cast<int>((0.5f - clip.y / clip.w * 0.5f) * static_cast<float>(kHeight));
                REQUIRE(centerX >= 1);
                REQUIRE(centerX < static_cast<int>(kWidth) - 1);
                REQUIRE(centerY >= 1);
                REQUIRE(centerY < static_cast<int>(kHeight) - 1);
                glm::vec3 sum(0.0f);
                for (int y = centerY - 1; y <= centerY + 1; ++y) {
                    for (int x = centerX - 1; x <= centerX + 1; ++x)
                        sum += InImage.Pixels[static_cast<size_t>(y) * kWidth + static_cast<size_t>(x)];
                }
                return sum / 9.0f;
            }

            void Remove(FSpriteEmitterComponent& InEmitter) {
                InEmitter.Settings.bEnabled = false;
                InEmitter.Particles.clear();
            }
        };

    } // namespace

    TEST_SUITE("Shader GPU - Sprite particles") {

        TEST_CASE_FIXTURE(FSpriteFixture, "Sprites are blended from the farthest to the nearest, whatever order they were made in") {
            REQUIRE(bReady);
            const glm::vec3 nearPoint(0.0f, 1.0f, 2.0f);
            const glm::vec3 farPoint(0.0f, 1.0f, 0.0f);
            const glm::vec4 red(4.0f, 0.0f, 0.0f, 0.9f);
            const glm::vec4 blue(0.0f, 0.0f, 4.0f, 0.9f);
            // Made far first, then near first: the picture is the same, the near one on top.
            FSpriteEmitterComponent& far1 = AddSprite(farPoint, blue, 1.0f, false);
            FSpriteEmitterComponent& near1 = AddSprite(nearPoint, red, 1.0f, false);
            const glm::vec3 first = LightAt(RenderHdr(), nearPoint);
            Remove(far1);
            Remove(near1);
            AddSprite(nearPoint, red, 1.0f, false);
            AddSprite(farPoint, blue, 1.0f, false);
            const glm::vec3 second = LightAt(RenderHdr(), nearPoint);
            CAPTURE(first.r);
            CAPTURE(first.b);
            // Over: 0.9 of the near one, a tenth of what is behind it.
            CHECK(first.r == doctest::Approx(3.6f).epsilon(0.08));
            CHECK(first.b < 0.6f);
            CHECK(first.b > 0.2f);
            CHECK(second.r == doctest::Approx(first.r).epsilon(0.01));
            CHECK(second.b == doctest::Approx(first.b).epsilon(0.01));
            CHECK(FFrameProfiler::Working().SpriteParticleCount == 2);
            CHECK(NewValidationErrors() == 0);
        }

        TEST_CASE_FIXTURE(FSpriteFixture, "A puff is round: the corners of its square are clear") {
            REQUIRE(bReady);
            const glm::vec3 center(0.0f, 1.0f, 0.0f);
            AddSprite(center, glm::vec4(2.0f, 2.0f, 2.0f, 1.0f), 2.0f, false);
            const FHdrImage image = RenderHdr();
            CHECK(LightAt(image, center).g > 1.9f);
            // Most of the way to a corner of the 2 m square: outside the circle.
            CHECK(LightAt(image, center + glm::vec3(0.85f, 0.85f, 0.0f)).g < 0.05f);
            CHECK(NewValidationErrors() == 0);
        }

        TEST_CASE_FIXTURE(FSpriteFixture, "A sprite fades out as it nears the surface behind it, and hides behind one") {
            REQUIRE(bReady);
            // A black wall whose face is at z = -0.9.
            AActor* wall = World->SpawnActor<AActor>("Wall");
            wall->SetActorLocation({0.0f, 1.0f, -1.0f});
            auto& mesh = wall->AddComponent<FMeshComponent>(FMeshPrimitives::CreateBox(8.0f, 4.0f, 0.2f));
            mesh.MeshType = "Box";
            mesh.MeshWidth = 8.0f;
            mesh.MeshHeight = 4.0f;
            mesh.MeshDepth = 0.2f;
            wall->AddComponent<FMaterialComponent>(FloorMaterial);
            const glm::vec4 white(2.0f, 2.0f, 2.0f, 1.0f);
            const glm::vec3 freePoint(-1.5f, 1.0f, 1.0f);
            const glm::vec3 nearPoint(1.5f, 1.0f, -0.8f);
            const glm::vec3 behindPoint(0.0f, 1.0f, -1.5f);
            const FHdrImage empty = RenderHdr();
            AddSprite(freePoint, white, 0.6f, false);
            AddSprite(nearPoint, white, 0.6f, false);
            AddSprite(behindPoint, white, 0.6f, false);
            const FHdrImage image = RenderHdr();
            const float free = LightAt(image, freePoint).g - LightAt(empty, freePoint).g;
            const float near = LightAt(image, nearPoint).g - LightAt(empty, nearPoint).g;
            const float behind = LightAt(image, behindPoint).g - LightAt(empty, behindPoint).g;
            CAPTURE(free);
            CAPTURE(near);
            CAPTURE(behind);
            CHECK(free > 1.8f);
            // 0.1 m in front of the wall with a fade of 0.5 m: a fifth of it.
            CHECK(near / free == doctest::Approx(0.2f).epsilon(0.25));
            CHECK(std::abs(behind) < 0.01f);
            CHECK(NewValidationErrors() == 0);
        }

        TEST_CASE_FIXTURE(FSpriteFixture, "A lit sprite takes the light of the probes where it is; an unlit one keeps its colour") {
            REQUIRE(bReady);
            const glm::vec3 point(0.0f, 1.0f, 0.0f);
            // Red light from every side.
            FLightProbeGrid grid;
            grid.Origin = point;
            grid.Resize({1, 1, 1});
            for (const glm::vec3& direction : {glm::vec3(1, 0, 0), glm::vec3(-1, 0, 0), glm::vec3(0, 1, 0),
                                               glm::vec3(0, -1, 0), glm::vec3(0, 0, 1), glm::vec3(0, 0, -1)})
                grid.Probes[0].AddRadiance(direction, {2.0f, 0.2f, 0.2f});
            World->SetLightProbes(grid);
            FSpriteEmitterComponent& lit = AddSprite(point, glm::vec4(1.0f), 1.0f, true);
            const glm::vec3 litLight = LightAt(RenderHdr(), point);
            Remove(lit);
            AddSprite(point, glm::vec4(1.0f), 1.0f, false);
            const glm::vec3 unlitLight = LightAt(RenderHdr(), point);
            CAPTURE(litLight.r);
            CAPTURE(litLight.g);
            // Irradiance 3 (red), 0.3 (green, blue) on average; a white sprite sends back that over pi.
            CHECK(litLight.r == doctest::Approx(3.0f / 3.14159265f).epsilon(0.08));
            CHECK(litLight.g == doctest::Approx(0.3f / 3.14159265f).epsilon(0.15));
            CHECK(litLight.r > litLight.g * 5.0f);
            CHECK(unlitLight.r == doctest::Approx(1.0f).epsilon(0.05));
            CHECK(unlitLight.g == doctest::Approx(1.0f).epsilon(0.05));
            CHECK(NewValidationErrors() == 0);
        }

        TEST_CASE_FIXTURE(FSpriteFixture, "Dust in a lamp's cone is lit, less so at its edge, and not at all outside it") {
            REQUIRE(bReady);
            AddSpotLight("Lamp", {0.0f, 4.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 20.0f, 10.0f, 15.0f, 25.0f);
            const glm::vec3 inside(0.0f, 1.0f, 0.0f);
            const glm::vec3 edge(3.0f * std::tan(glm::radians(20.0f)), 1.0f, 0.0f);
            const glm::vec3 outside(-3.0f * std::tan(glm::radians(35.0f)), 1.0f, 0.0f);
            for (const glm::vec3& point : {inside, edge, outside})
                AddSprite(point, glm::vec4(1.0f), 0.4f, true);
            const FHdrImage image = RenderHdr();
            const float lit = LightAt(image, inside).r;
            const float atEdge = LightAt(image, edge).r;
            const float dark = LightAt(image, outside).r;
            CAPTURE(lit);
            CAPTURE(atEdge);
            CAPTURE(dark);
            // 20 x the falloff 3 m from a lamp of radius 10, over pi.
            const float expected = 20.0f * (std::pow(1.0f - std::pow(0.3f, 4.0f), 2.0f) / 10.0f) / 3.14159265f;
            CHECK(lit == doctest::Approx(expected).epsilon(0.1));
            CHECK(atEdge > 0.15f * lit);
            CHECK(atEdge < 0.8f * lit);
            CHECK(dark < 0.01f);
            // Taking none of the lamps' light: dark, the sky being black.
            World->GetRegistry().view<FSpriteEmitterComponent>().each(
                [](FSpriteEmitterComponent& InEmitter) { InEmitter.Settings.LightFraction = 0.0f; });
            CHECK(LightAt(RenderHdr(), inside).r < 0.01f);
            CHECK(NewValidationErrors() == 0);
        }

        TEST_CASE_FIXTURE(FSpriteFixture, "A point lamp lights a sprite as it lights a surface facing it") {
            REQUIRE(bReady);
            AActor* bulb = World->SpawnActor<AActor>("Bulb");
            bulb->SetActorLocation({0.0f, 3.0f, 0.0f});
            FPointLight point;
            point.Color = {1.0f, 1.0f, 1.0f};
            point.Intensity = 10.0f;
            point.Radius = 6.0f;
            bulb->AddComponent<FPointLightComponent>(point);
            const glm::vec3 near(0.0f, 1.0f, 0.0f);
            const glm::vec3 beyond(4.0f, 1.0f, -5.0f); // 6.7 m away: past the radius
            AddSprite(near, glm::vec4(1.0f), 0.4f, true);
            AddSprite(beyond, glm::vec4(1.0f), 0.4f, true);
            const FHdrImage image = RenderHdr();
            // 10 x the falloff 2 m from a lamp of radius 6, over pi.
            const float expected = 10.0f * (std::pow(1.0f - std::pow(2.0f / 6.0f, 4.0f), 2.0f) / 5.0f) / 3.14159265f;
            CHECK(LightAt(image, near).r == doctest::Approx(expected).epsilon(0.1));
            // Black: only the clear colour (0.04) shows a little through its soft edge.
            CHECK(LightAt(image, beyond).r < 0.03f);
            CHECK(NewValidationErrors() == 0);
        }

        TEST_CASE_FIXTURE(FSpriteFixture, "The alpha follows the particle's life") {
            REQUIRE(bReady);
            const glm::vec3 young(-1.0f, 1.0f, 0.0f);
            const glm::vec3 grown(1.0f, 1.0f, 0.0f);
            // Fading in over the first half of two seconds.
            AddSprite(young, glm::vec4(2.0f, 2.0f, 2.0f, 1.0f), 0.8f, false, 0.1f, 0.5f);
            AddSprite(grown, glm::vec4(2.0f, 2.0f, 2.0f, 1.0f), 0.8f, false, 1.5f, 0.5f);
            const FHdrImage image = RenderHdr();
            const float youngLight = LightAt(image, young).g;
            const float grownLight = LightAt(image, grown).g;
            CAPTURE(youngLight);
            CAPTURE(grownLight);
            CHECK(grownLight > 1.8f);
            CHECK(youngLight / grownLight == doctest::Approx(0.1f).epsilon(0.3));
            CHECK(NewValidationErrors() == 0);
        }

        TEST_CASE_FIXTURE(FSpriteFixture, "Sprites are fogged like surfaces") {
            REQUIRE(bReady);
            const glm::vec3 point(0.0f, 1.0f, 0.0f);
            AddSprite(point, glm::vec4(1.0f, 0.0f, 0.0f, 1.0f), 1.0f, false);
            const glm::vec3 clear = LightAt(RenderHdr(), point);
            Sky->bFogEnabled = true;
            Sky->FogColor = {0.0f, 1.0f, 0.0f};
            Sky->FogDensity = 0.2f;
            Sky->FogStartDistance = 0.0f;
            Sky->FogHeightFalloff = 0.0f;
            Sky->FogMaxOpacity = 1.0f;
            const glm::vec3 fogged = LightAt(RenderHdr(), point);
            // Six metres of fog at 0.2: 70 % fog colour.
            const float fog = 1.0f - std::exp(-6.0f * 0.2f);
            CHECK(clear.r == doctest::Approx(1.0f).epsilon(0.05));
            CHECK(fogged.g == doctest::Approx(fog).epsilon(0.08));
            CHECK(fogged.r == doctest::Approx(1.0f - fog).epsilon(0.15));
            CHECK(NewValidationErrors() == 0);
        }

        TEST_CASE_FIXTURE(FSpriteFixture, "Nothing that is not a finite number reaches the scene, whatever the sprites ask for") {
            REQUIRE(bReady);
            AddSpotLight("Lamp", {0.0f, 3.0f, 2.0f}, {0.0f, 0.0f, 2.0f}, 1.0e6f, 10.0f, 30.0f, 40.0f);
            FSpriteEmitterComponent& huge = AddSprite({0.0f, 1.0f, 2.0f}, glm::vec4(1.0e30f, 1.0f, 0.0f, 1.0f), 1.0e6f, true);
            huge.Settings.LightFraction = 1.0e30f;
            huge.Settings.DepthFade = 0.0f;
            AddSprite(Camera.GetPosition(), glm::vec4(5.0f), 0.5f, false);
            FSpriteEmitterComponent& broken = AddSprite({0.5f, 1.0f, 0.0f}, glm::vec4(1.0f), 1.0f, true);
            broken.Settings.ColorStart = glm::vec4(std::numeric_limits<float>::quiet_NaN());
            broken.Settings.DepthFade = std::numeric_limits<float>::infinity();
            const FHdrImage image = RenderHdr();
            uint32_t bad = 0;
            for (const glm::vec3& pixel : image.Pixels)
                bad += (std::isfinite(pixel.r) && std::isfinite(pixel.g) && std::isfinite(pixel.b)) ? 0 : 1;
            CHECK(bad == 0);
            CHECK(NewValidationErrors() == 0);
        }
    }

} // namespace Leon
