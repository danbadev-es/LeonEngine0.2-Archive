#include <doctest/doctest.h>

#include "Scene/Components.hpp"
#include "Engine/UWorld.hpp"
#include "Gameplay/AActor.hpp"
#include "RHI/FFrameProfiler.hpp"
#include "RHI/FLegacyRenderDriver.hpp"
#include "RHI/FRHIFrame.hpp"
#include "RHI/FRenderer.hpp"
#include "RHI/IRHIDevice.hpp"
#include "RHI/IRenderDriver.hpp"
#include "RHID3D12/FD3D12RHI.hpp"
#include "Renderer/FImmediateDrawRHI.hpp"
#include "Assets/FMaterial.hpp"
#include "Assets/FMaterialInstance.hpp"
#include "RenderCore/FMeshPrimitives.hpp"
#include "RenderCore/FPerspectiveCamera.hpp"
#include "RenderCore/FShaderLibrary.hpp"
#include "Renderer/FWorldRenderer.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

// The world renderer, drawing real frames of a small scene on WARP (the software Direct3D 12
// adapter) with the debug layer on. Every case also requires that the debug layer stayed silent:
// that alone covers the shaders, pipelines, render passes and resource states of each stage.
//
// Set LEON_TEST_GPU=1 to run on the machine's graphics card instead.

namespace Leon {

    namespace {

        constexpr uint32_t kWidth = 320;
        constexpr uint32_t kHeight = 180;

        struct FImage {
            std::vector<uint8_t> Pixels; // RGBA8, rows from the top
            const uint8_t* At(uint32_t InX, uint32_t InY) const { return &Pixels[(InY * kWidth + InX) * 4]; }
            float Luma(uint32_t InX, uint32_t InY) const {
                const uint8_t* pixel = At(InX, InY);
                return (0.299f * pixel[0] + 0.587f * pixel[1] + 0.114f * pixel[2]) / 255.0f;
            }
        };

        /**
         * A room-sized scene: a grey floor, a red box on it, a point light above and to the side (so
         * the box throws a shadow on the floor) and a spot light pointing down at the box.
         */
        struct FWorldRenderFixture {
            TRef<UWorld> World;
            TRef<IRHITexture> Target;
            TRef<FMaterialInstance> FloorMaterial;
            FPerspectiveCamera Camera{45.0f, static_cast<float>(kWidth) / static_cast<float>(kHeight), 0.1f, 100.0f};
            uint64_t ErrorsAtStart = 0;
            bool bReady = false;

            FWorldRenderFixture() {
                FD3D12RHI::Register();
                FRHIInitDesc init;
                init.bEnableDebugLayer = true;
                init.bUseSoftwareAdapter = std::getenv("LEON_TEST_GPU") == nullptr;
                if (!FRHI::Init(ERHIBackend::D3D12, init))
                    return;
                ErrorsAtStart = FD3D12RHI::GetValidationErrorCount();
                // Meshes and textures created from here on are real device resources.
                FLegacyRenderDriver::Register();

                World = UWorld::Create("RenderTestWorld");
                FWorldRenderer* renderer = World->GetWorldRenderer();
                // Building the image-based lighting environment takes seconds on the CPU; the
                // procedural fallback is enough here.
                renderer->SetImageBasedLightingEnabled(false);
                renderer->OnViewportResize(kWidth, kHeight);

                const TRef<FMaterial> material = FMaterial::Create("TestSurface");
                FloorMaterial = material->CreateInstance("Floor");
                FloorMaterial->SetAlbedoColor({0.5f, 0.5f, 0.5f});
                FloorMaterial->SetRoughness(0.9f);
                const TRef<FMaterialInstance> boxMaterial = material->CreateInstance("Box");
                boxMaterial->SetAlbedoColor({0.9f, 0.05f, 0.05f});
                boxMaterial->SetRoughness(0.8f);

                AActor* floor = World->SpawnActor<AActor>("Floor");
                floor->SetActorLocation({0.0f, -0.1f, 0.0f});
                auto& floorMesh = floor->AddComponent<FMeshComponent>(FMeshPrimitives::CreateBox(12.0f, 0.2f, 12.0f));
                floorMesh.MeshWidth = 12.0f;
                floorMesh.MeshHeight = 0.2f;
                floorMesh.MeshDepth = 12.0f;
                floorMesh.Mobility = EComponentMobility::Static;
                floor->AddComponent<FMaterialComponent>(FloorMaterial);

                AActor* box = World->SpawnActor<AActor>("Box");
                box->SetActorLocation({0.0f, 0.5f, 0.0f});
                auto& boxMesh = box->AddComponent<FMeshComponent>(FMeshPrimitives::CreateCube(1.0f));
                boxMesh.MeshSize = 1.0f;
                boxMesh.Mobility = EComponentMobility::Static;
                box->AddComponent<FMaterialComponent>(boxMaterial);

                AActor* lamp = World->SpawnActor<AActor>("Lamp");
                lamp->SetActorLocation({2.5f, 3.0f, 1.0f});
                FPointLight point;
                point.Color = {1.0f, 1.0f, 1.0f};
                point.Intensity = 40.0f;
                point.Radius = 12.0f;
                lamp->AddComponent<FPointLightComponent>(point).bCastShadows = true;

                AActor* spotActor = World->SpawnActor<AActor>("Spot");
                spotActor->SetActorLocation({0.0f, 4.0f, 0.0f});
                FSpotLight spot;
                spot.Color = {1.0f, 1.0f, 1.0f};
                spot.Intensity = 30.0f;
                spot.Radius = 10.0f;
                spotActor->AddComponent<FSpotLightComponent>().Light = spot; // aims down by default

                Camera.SetPosition({0.0f, 2.0f, 6.0f});
                Camera.SetRotation(-15.0f, -90.0f);

                FRHITextureDesc target;
                target.Width = kWidth;
                target.Height = kHeight;
                target.Format = ERHIFormat::RGBA8_UNorm;
                target.Usage = RHITextureUsage_Sampled | RHITextureUsage_RenderTarget;
                target.DebugName = "TestOutput";
                Target = FRHI::GetDevice()->CreateTexture(target);
                bReady = Target != nullptr;
            }

            ~FWorldRenderFixture() {
                // Everything that holds device resources goes before the device.
                World.reset();
                FloorMaterial.reset();
                Target.reset();
                ShutdownImmediateDrawRHI();
                FShaderLibrary::Clear();
                FRenderDriverRegistry::RegisterDriver(nullptr);
                FRHI::Shutdown();
            }

            /** Draws frames of the world and returns the last one. InOverlay runs after the world, inside the frame. */
            template <typename FOverlay> FImage Render(uint32_t InFrames, FOverlay&& InOverlay) {
                IRHIDevice* device = FRHI::GetDevice();
                for (uint32_t frame = 0; frame < InFrames; ++frame) {
                    IRHICommandList& commands = device->BeginFrame();
                    FRHIFrame::Begin(commands, *Target);
                    World->GetWorldRenderer()->Render(Camera);
                    InOverlay();
                    FRHIFrame::End();
                    device->EndFrame();
                }
                device->WaitIdle();
                FImage image;
                REQUIRE(device->ReadTexture(*Target, 0, 0, image.Pixels));
                REQUIRE(image.Pixels.size() == static_cast<size_t>(kWidth) * kHeight * 4);
                return image;
            }

            FImage Render(uint32_t InFrames = 2) {
                return Render(InFrames, [] {});
            }

            uint64_t NewValidationErrors() const { return FD3D12RHI::GetValidationErrorCount() - ErrorsAtStart; }

            /** Switches the fixture's own lamp and spot light off, for tests that bring their lights. */
            void SwitchFixtureLightsOff() {
                World->FindActorByName("Lamp")->GetComponent<FPointLightComponent>().bEnabled = false;
                World->FindActorByName("Spot")->GetComponent<FSpotLightComponent>().bEnabled = false;
            }

            /** Bloom, ambient occlusion and anti-aliasing off: each pixel shows its own surface only. */
            void UsePlainPostProcess() {
                FPostProcessSettings& post = World->GetWorldRenderer()->GetPostProcessSettings();
                post.bSSAOEnabled = false;
                post.bBloomEnabled = false;
                post.bFXAAEnabled = false;
            }

            /** A white spot light at InPosition aimed at InTarget. */
            FSpotLightComponent& AddSpotLight(const char* InName, const glm::vec3& InPosition, const glm::vec3& InTarget,
                                              float InIntensity, float InRadius, float InInnerDegrees,
                                              float InOuterDegrees) {
                AActor* actor = World->SpawnActor<AActor>(InName);
                actor->SetActorLocation(InPosition);
                actor->SetActorRotation(SpotLightEulerFromWorldDirection(InTarget - InPosition));
                FSpotLight spot;
                spot.Color = {1.0f, 1.0f, 1.0f};
                spot.Intensity = InIntensity;
                spot.Radius = InRadius;
                spot.CutOff = InInnerDegrees;
                spot.OuterCutOff = InOuterDegrees;
                auto& component = actor->AddComponent<FSpotLightComponent>();
                component.Light = spot;
                return component;
            }

            /** Mean luma of the few pixels around where a point of the world lands in the picture. */
            float LumaAt(const FImage& InImage, const glm::vec3& InWorldPoint) const {
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
                    for (int x = centerX - 1; x <= centerX + 1; ++x)
                        sum += InImage.Luma(static_cast<uint32_t>(x), static_cast<uint32_t>(y));
                }
                return sum / 9.0f;
            }
        };

    } // namespace

    TEST_SUITE("World renderer on Direct3D 12") {

        TEST_CASE_FIXTURE(FWorldRenderFixture, "A lit scene comes out lit, coloured and without validation errors") {
            REQUIRE(bReady);
            const FImage image = Render();

            // The red box is in the middle of the picture.
            const uint8_t* box = image.At(kWidth / 2, kHeight / 2);
            CHECK(box[0] > 60);
            CHECK(box[0] > box[1] * 2);
            CHECK(box[0] > box[2] * 2);

            // The floor in front of the box is lit, and grey rather than red (the sky tints it slightly).
            const uint8_t* floor = image.At(kWidth / 2, kHeight - 20);
            CHECK(floor[0] > 25);
            CHECK(floor[0] < floor[2] * 2);

            // Above the horizon there is nothing: the clear colour, dark.
            CHECK(image.Luma(kWidth / 2, 4) < 0.35f);
            CHECK(NewValidationErrors() == 0);
        }

        TEST_CASE_FIXTURE(FWorldRenderFixture, "Shadows darken part of the floor and nothing else") {
            REQUIRE(bReady);
            FShadowSettings& shadows = World->GetWorldRenderer()->GetShadowSettings();
            shadows.bEnableShadows = true;
            const FImage shadowed = Render();
            shadows.bEnableShadows = false;
            const FImage unshadowed = Render();

            uint32_t darker = 0;
            uint32_t brighter = 0;
            for (uint32_t y = 0; y < kHeight; ++y) {
                for (uint32_t x = 0; x < kWidth; ++x) {
                    const float difference = unshadowed.Luma(x, y) - shadowed.Luma(x, y);
                    if (difference > 0.04f)
                        ++darker;
                    else if (difference < -0.04f)
                        ++brighter;
                }
            }
            // The box blocks the lamp over a patch of floor; a shadow never adds light.
            CHECK(darker > 150);
            CHECK(brighter == 0);
            CHECK(NewValidationErrors() == 0);
        }

        TEST_CASE_FIXTURE(FWorldRenderFixture, "With more spot lights than slots, the ones nearest the camera light the scene") {
            REQUIRE(bReady);
            SwitchFixtureLightsOff();
            UsePlainPostProcess();
            World->FindActorByName("Box")->GetComponent<FMeshComponent>().bVisible = false;
            const FImage dark = Render();

            // Twelve lamps hanging over the floor in three rows of four, each lighting the floor
            // under it. The far row is created first and last, so it is at both ends of the registry.
            struct FLamp {
                glm::vec3 FloorPoint;
                bool bExpectLit;
            };
            std::vector<FLamp> lamps;
            const auto addRow = [&](float InZ, int InFirst, int InLast, bool bInExpectLit) {
                for (int column = InFirst; column <= InLast; ++column) {
                    const glm::vec3 floorPoint(-1.5f + static_cast<float>(column), 0.0f, InZ);
                    AddSpotLight("Hanging", floorPoint + glm::vec3(0.0f, 1.5f, 0.0f), floorPoint, 30.0f, 4.0f, 12.5f, 17.5f);
                    lamps.push_back({floorPoint, bInExpectLit});
                }
            };
            addRow(-2.0f, 0, 1, false);
            addRow(2.5f, 0, 3, true); // near enough to reach the camera
            addRow(0.5f, 0, 3, true); // beyond their radius, but nearer than the far row
            addRow(-2.0f, 2, 3, false);
            REQUIRE(lamps.size() == 12);

            const FImage lit = Render();
            for (const FLamp& lamp : lamps) {
                const float gained = LumaAt(lit, lamp.FloorPoint) - LumaAt(dark, lamp.FloorPoint);
                CAPTURE(lamp.FloorPoint.x);
                CAPTURE(lamp.FloorPoint.z);
                CAPTURE(gained);
                if (lamp.bExpectLit)
                    CHECK(gained > 0.15f);
                else
                    CHECK(gained < 0.03f);
            }
            CHECK(NewValidationErrors() == 0);
        }

        TEST_CASE_FIXTURE(FWorldRenderFixture, "Two spot lights each cast their own shadow, and one told not to casts none") {
            REQUIRE(bReady);
            SwitchFixtureLightsOff();
            UsePlainPostProcess();
            // Hardly any light from the sky, so a shadow is as dark as the other light leaves it.
            AActor* environment = World->SpawnActor<AActor>("Environment");
            auto& sky = environment->AddComponent<FSkyboxComponent>();
            sky.bEnabled = false;
            sky.EnvironmentIntensity = 0.05f;
            // One narrow light on each side of the box: each throws the box's shadow on the far side of it,
            // where the other light does not reach.
            FSpotLightComponent& left = AddSpotLight("Left", {-1.8f, 2.0f, 0.0f}, {0.3f, 0.3f, 0.0f}, 40.0f, 8.0f, 12.0f, 16.0f);
            AddSpotLight("Right", {1.8f, 2.0f, 0.0f}, {-0.3f, 0.3f, 0.0f}, 40.0f, 8.0f, 12.0f, 16.0f);
            const glm::vec3 shadowOfLeft(1.3f, 0.0f, 0.0f);
            const glm::vec3 shadowOfRight(-1.3f, 0.0f, 0.0f);

            FShadowSettings& shadows = World->GetWorldRenderer()->GetShadowSettings();
            shadows.bEnableShadows = false;
            const FImage unshadowed = Render();
            shadows.bEnableShadows = true;
            const FImage both = Render();

            const float leftDrop = LumaAt(unshadowed, shadowOfLeft) - LumaAt(both, shadowOfLeft);
            const float rightDrop = LumaAt(unshadowed, shadowOfRight) - LumaAt(both, shadowOfRight);
            CAPTURE(leftDrop);
            CAPTURE(rightDrop);
            CHECK(leftDrop > 0.2f);
            CHECK(rightDrop > 0.2f);

            // The left light stops casting: its shadow goes, the other stays where it was.
            left.bCastShadows = false;
            const FImage rightOnly = Render();
            CHECK(std::abs(LumaAt(unshadowed, shadowOfLeft) - LumaAt(rightOnly, shadowOfLeft)) < 0.01f);
            CHECK(LumaAt(rightOnly, shadowOfRight) == doctest::Approx(LumaAt(both, shadowOfRight)).epsilon(0.02));

            // A budget of one shadowed spot light: one shadow, not two (which one is the ranking's business).
            left.bCastShadows = true;
            shadows.MaxShadowedSpotLights = 1;
            const FImage budget = Render();
            const bool bLeftShadow = LumaAt(unshadowed, shadowOfLeft) - LumaAt(budget, shadowOfLeft) > 0.2f;
            const bool bRightShadow = LumaAt(unshadowed, shadowOfRight) - LumaAt(budget, shadowOfRight) > 0.2f;
            CHECK(bLeftShadow != bRightShadow);
            CHECK(NewValidationErrors() == 0);
        }

        TEST_CASE_FIXTURE(FWorldRenderFixture, "A point light told not to cast shadows casts none") {
            REQUIRE(bReady);
            // The fixture's spot light hangs straight above the box; only the lamp throws a shadow on the floor.
            World->FindActorByName("Spot")->GetComponent<FSpotLightComponent>().bCastShadows = false;
            FPointLightComponent& lamp = World->FindActorByName("Lamp")->GetComponent<FPointLightComponent>();
            FShadowSettings& shadows = World->GetWorldRenderer()->GetShadowSettings();
            const auto darkerPixels = [&] {
                shadows.bEnableShadows = true;
                const FImage shadowed = Render();
                shadows.bEnableShadows = false;
                const FImage unshadowed = Render();
                uint32_t darker = 0;
                for (uint32_t y = 0; y < kHeight; ++y) {
                    for (uint32_t x = 0; x < kWidth; ++x) {
                        if (unshadowed.Luma(x, y) - shadowed.Luma(x, y) > 0.04f)
                            ++darker;
                    }
                }
                return darker;
            };

            REQUIRE(lamp.bCastShadows); // the fixture asked for it
            CHECK(darkerPixels() > 150);
            lamp.bCastShadows = false;
            CHECK(darkerPixels() == 0);
            // What a point light does when nobody says: no shadows.
            CHECK_FALSE(FPointLightComponent().bCastShadows);
            CHECK(FSpotLightComponent().bCastShadows);
            CHECK(NewValidationErrors() == 0);
        }

        TEST_CASE_FIXTURE(FWorldRenderFixture, "A shadowed point light in a closed room of single-sided walls lights every wall") {
            REQUIRE(bReady);
            SwitchFixtureLightsOff();
            UsePlainPostProcess();
            World->FindActorByName("Box")->GetComponent<FMeshComponent>().bVisible = false;
            World->FindActorByName("Floor")->GetComponent<FMeshComponent>().bVisible = false;
            AActor* environment = World->SpawnActor<AActor>("Environment");
            auto& sky = environment->AddComponent<FSkyboxComponent>();
            sky.bEnabled = false;
            sky.EnvironmentIntensity = 0.0f;

            // A room 8 x 2.8 x 2.4 m around the camera, each side one quad that faces inwards (a quad is
            // in the XY plane and faces +Z).
            const glm::vec3 center(0.0f, 1.4f, 4.8f);
            const TRef<FMaterialInstance> paint = FloorMaterial->GetParent()->CreateInstance("Paint");
            paint->SetAlbedoColor({0.7f, 0.7f, 0.7f});
            paint->SetRoughness(0.9f);
            const auto addWall = [&](const char* InName, const glm::vec3& InOffset, const glm::vec3& InRotation,
                                     float InWidth, float InHeight) {
                AActor* wall = World->SpawnActor<AActor>(InName);
                wall->SetActorLocation(center + InOffset);
                wall->SetActorRotation(InRotation);
                auto& mesh = wall->AddComponent<FMeshComponent>(FMeshPrimitives::CreateQuad(InWidth, InHeight));
                mesh.MeshType = "Quad";
                mesh.MeshWidth = InWidth;
                mesh.MeshHeight = InHeight;
                mesh.Mobility = EComponentMobility::Static;
                wall->AddComponent<FMaterialComponent>(paint);
            };
            addWall("Back", {0.0f, 0.0f, -1.2f}, {0.0f, 0.0f, 0.0f}, 8.0f, 2.8f);
            addWall("Front", {0.0f, 0.0f, 1.2f}, {0.0f, 180.0f, 0.0f}, 8.0f, 2.8f);
            addWall("Left", {-4.0f, 0.0f, 0.0f}, {0.0f, 90.0f, 0.0f}, 2.4f, 2.8f);
            addWall("Right", {4.0f, 0.0f, 0.0f}, {0.0f, -90.0f, 0.0f}, 2.4f, 2.8f);
            addWall("RoomFloor", {0.0f, -1.4f, 0.0f}, {-90.0f, 0.0f, 0.0f}, 8.0f, 2.4f);
            addWall("Ceiling", {0.0f, 1.4f, 0.0f}, {90.0f, 0.0f, 0.0f}, 8.0f, 2.4f);

            // Six ceiling lamps, more than there are shadow cubes, all asking for shadows.
            for (int i = 0; i < 6; ++i) {
                AActor* lamp = World->SpawnActor<AActor>("CeilingLamp");
                lamp->SetActorLocation(center + glm::vec3(-3.25f + 1.3f * static_cast<float>(i), 1.15f, 0.0f));
                FPointLight point;
                point.Color = {1.0f, 1.0f, 1.0f};
                point.Intensity = 3.0f;
                point.Radius = 6.5f;
                auto& component = lamp->AddComponent<FPointLightComponent>(point);
                component.bCastShadows = true;
                component.Mobility = ELightMobility::Stationary;
            }

            Camera.SetPosition(center + glm::vec3(0.0f, 0.0f, 1.0f));
            FShadowSettings& shadows = World->GetWorldRenderer()->GetShadowSettings();
            // Looking at the back wall, then at each end, the floor and the ceiling.
            const glm::vec2 views[5] = {{0.0f, -90.0f}, {0.0f, 180.0f}, {0.0f, 0.0f}, {-60.0f, -90.0f}, {60.0f, -90.0f}};
            for (const glm::vec2& view : views) {
                Camera.SetRotation(view.x, view.y);
                shadows.bEnableShadows = false;
                const FImage unshadowed = Render();
                shadows.bEnableShadows = true;
                const FImage shadowed = Render();
                // Nothing stands in the room: no wall is in shadow, and every pixel is lit. (A few pixels
                // along the far corners do darken: the bias gives out where the light grazes a wall.)
                uint32_t darker = 0;
                uint32_t unlit = 0;
                for (uint32_t y = 0; y < kHeight; ++y) {
                    for (uint32_t x = 0; x < kWidth; ++x) {
                        if (unshadowed.Luma(x, y) - shadowed.Luma(x, y) > 0.04f)
                            ++darker;
                        if (shadowed.Luma(x, y) < 0.1f)
                            ++unlit;
                    }
                }
                CAPTURE(view.x);
                CAPTURE(view.y);
                CHECK(darker < kWidth * kHeight / 500);
                CHECK(unlit == 0);
            }
            CHECK(NewValidationErrors() == 0);
        }

        TEST_CASE_FIXTURE(FWorldRenderFixture, "The post-process settings of the map are applied, and a neutral map changes nothing") {
            REQUIRE(bReady);
            FWorldRenderer* renderer = World->GetWorldRenderer();
            const FImage plain = Render();

            // A map that has the block but says nothing in it: the same picture, bit for bit.
            AActor* environment = World->SpawnActor<AActor>("Environment");
            auto& mapPost = environment->AddComponent<FPostProcessComponent>();
            const FImage neutral = Render();
            CHECK(neutral.Pixels == plain.Pixels);

            // The red box in the middle of the picture, in black and white.
            mapPost.Look.Saturation = 0.0f;
            const FImage grey = Render();
            const uint8_t* box = grey.At(kWidth / 2, kHeight / 2);
            CHECK(std::abs(static_cast<int>(box[0]) - static_cast<int>(box[1])) <= 2);
            CHECK(std::abs(static_cast<int>(box[1]) - static_cast<int>(box[2])) <= 2);
            CHECK(plain.At(kWidth / 2, kHeight / 2)[0] > plain.At(kWidth / 2, kHeight / 2)[1] * 2);
            mapPost.Look.Saturation = 1.0f;

            // Its exposure replaces the one of the sky, and the tools see the value in use.
            mapPost.bOverrideExposure = true;
            mapPost.Exposure = 0.25f;
            const FImage dim = Render();
            CHECK(dim.Luma(kWidth / 2, kHeight - 20) < plain.Luma(kWidth / 2, kHeight - 20) * 0.8f);
            CHECK(renderer->GetPostProcessSettings().Exposure == 0.25f);
            mapPost.bOverrideExposure = false;

            // Vignette and grain reach the final image: dark corners, and a still pattern that repeats.
            mapPost.Look.VignetteIntensity = 1.0f;
            mapPost.Look.VignetteRadius = 0.2f;
            mapPost.Look.VignetteSoftness = 0.3f;
            const FImage vignetted = Render();
            CHECK(vignetted.Luma(2, kHeight - 3) < 0.02f);
            CHECK(plain.Luma(2, kHeight - 3) > 0.1f);
            CHECK(vignetted.Luma(kWidth / 2, kHeight / 2) == plain.Luma(kWidth / 2, kHeight / 2));
            mapPost.Look.VignetteIntensity = 0.0f;

            mapPost.Look.FilmGrainIntensity = 0.2f;
            mapPost.Look.bFilmGrainAnimated = false;
            const FImage grain = Render();
            CHECK(grain.Pixels != plain.Pixels);
            CHECK(Render().Pixels == grain.Pixels);
            mapPost.Look.bFilmGrainAnimated = true;
            CHECK(Render().Pixels != Render().Pixels);

            // The settings of the renderer are not written over by the map, and come back with it gone.
            CHECK(renderer->GetPostProcessSettings().Look.FilmGrainIntensity == 0.0f);
            environment->RemoveComponent<FPostProcessComponent>();
            CHECK(Render().Pixels == plain.Pixels);
            CHECK(NewValidationErrors() == 0);
        }

        TEST_CASE_FIXTURE(FWorldRenderFixture, "Each post-process effect can be switched off") {
            REQUIRE(bReady);
            FPostProcessSettings& post = World->GetWorldRenderer()->GetPostProcessSettings();
            const FImage full = Render();
            post.bSSAOEnabled = false;
            post.bBloomEnabled = false;
            post.bFXAAEnabled = false;
            const FImage plain = Render();
            post.bEnabled = false;
            const FImage off = Render();

            // The scene is still there in every case.
            for (const FImage* image : {&full, &plain, &off}) {
                const uint8_t* box = image->At(kWidth / 2, kHeight / 2);
                CHECK(box[0] > 60);
                CHECK(box[0] > box[1] * 2);
            }
            CHECK(NewValidationErrors() == 0);
        }

        TEST_CASE_FIXTURE(FWorldRenderFixture, "A floor with planar reflections mirrors the box") {
            REQUIRE(bReady);
            FWorldRenderer* renderer = World->GetWorldRenderer();
            renderer->SetPlanarReflectionEnabled(true);
            renderer->AddPlanarReflectionPlane({0.0f, 1.0f, 0.0f}, 0.0f);
            FloorMaterial->SetUsePlanarReflection(true);
            FloorMaterial->SetRoughness(0.05f);
            Render();
            CHECK(NewValidationErrors() == 0);

            // The view that shows the capture itself on every surface: how much of it reaches the
            // eye depends on the material, what it contains does not.
            renderer->SetDebugMode(13);
            const FImage capture = Render();

            // On the floor in front of the box, where a mirror would show it upside down, it is red.
            const uint32_t x = kWidth / 2;
            uint32_t red = 0;
            for (uint32_t y = kHeight / 2 + 20; y < kHeight - 10; ++y) {
                const uint8_t* pixel = capture.At(x, y);
                if (pixel[0] > pixel[1] + 25 && pixel[0] > pixel[2] + 25)
                    ++red;
            }
            CHECK(red > 5);
            CHECK(NewValidationErrors() == 0);
        }

        TEST_CASE_FIXTURE(FWorldRenderFixture, "2D drawing and world-space batches land on the frame") {
            REQUIRE(bReady);
            const glm::mat4 pixels =
                glm::ortho(0.0f, static_cast<float>(kWidth), static_cast<float>(kHeight), 0.0f, -1.0f, 1.0f);
            const FImage image = Render(2, [&] {
                // A white square in the top-left corner, drawn like the HUD draws its panels.
                const glm::vec4 white(1.0f);
                const FCanvasVertex quad[6] = {{{0.0f, 0.0f}, {0.0f, 0.0f}, white},   {{20.0f, 0.0f}, {1.0f, 0.0f}, white},
                                               {{20.0f, 20.0f}, {1.0f, 1.0f}, white}, {{0.0f, 0.0f}, {0.0f, 0.0f}, white},
                                               {{20.0f, 20.0f}, {1.0f, 1.0f}, white}, {{0.0f, 20.0f}, {0.0f, 1.0f}, white}};
                FCanvasRHI::DrawTriangles(quad, 6, pixels);

                // A debug line through the scene; it is drawn with the next frame's scene pass.
                struct FLineVertex {
                    glm::vec3 Position;
                    glm::vec4 Color;
                };
                const FLineVertex line[2] = {{{-3.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 1.0f}},
                                             {{3.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 1.0f}}};
                FSceneBatchesRHI::Submit(ESceneBatch::LinesOnTop, line, 2);
            });

            const uint8_t* corner = image.At(10, 10);
            CHECK(corner[0] > 240);
            CHECK(corner[1] > 240);
            CHECK(corner[2] > 240);
            // Outside the square the frame is untouched by it.
            CHECK(image.Luma(60, 10) < 0.5f);
            CHECK(NewValidationErrors() == 0);
        }
        TEST_CASE_FIXTURE(FWorldRenderFixture, "Copies of one mesh are drawn in one call and look the same") {
            REQUIRE(bReady);
            // A row of small boxes sharing one mesh and one material, each turned a little more.
            const TRef<FVertexArray> cube = FMeshPrimitives::CreateCube(0.35f);
            const TRef<FMaterialInstance> blue = FloorMaterial->GetParent()->CreateInstance("Copies");
            blue->SetAlbedoColor({0.1f, 0.2f, 0.9f});
            const int copies = 12;
            for (int i = 0; i < copies; ++i) {
                AActor* copy = World->SpawnActor<AActor>("Copy");
                copy->SetActorLocation({-2.75f + 0.5f * static_cast<float>(i), 0.25f, 2.0f});
                copy->SetActorRotation({0.0f, 7.0f * static_cast<float>(i), 0.0f});
                auto& mesh = copy->AddComponent<FMeshComponent>(cube);
                mesh.MeshSize = 0.35f;
                mesh.Mobility = EComponentMobility::Static;
                copy->AddComponent<FMaterialComponent>(blue);
            }
            FWorldRenderer* renderer = World->GetWorldRenderer();

            REQUIRE(renderer->IsInstancingEnabled());
            const FImage batched = Render();
            const uint32_t batchedCalls = FRenderer::GetStats().DrawCalls;
            const uint32_t batchedTriangles = FRenderer::GetStats().TriangleCount;

            renderer->SetInstancingEnabled(false);
            const FImage separate = Render();
            const uint32_t separateCalls = FRenderer::GetStats().DrawCalls;

            // The twelve boxes cost one call instead of twelve, and nothing else changed.
            CHECK(separateCalls - batchedCalls == static_cast<uint32_t>(copies - 1));
            CHECK(FRenderer::GetStats().TriangleCount == batchedTriangles);

            // The boxes are there (blue, in front of the red one) and the two frames are the same picture.
            const uint8_t* box = batched.At(kWidth / 2 + 6, kHeight * 3 / 4);
            CAPTURE(static_cast<int>(box[0]));
            CAPTURE(static_cast<int>(box[2]));
            int worst = 0;
            bool bAnyBlue = false;
            for (size_t i = 0; i < batched.Pixels.size(); i += 4) {
                for (int channel = 0; channel < 3; ++channel)
                    worst = std::max(worst, std::abs(static_cast<int>(batched.Pixels[i + channel]) -
                                                     static_cast<int>(separate.Pixels[i + channel])));
                bAnyBlue = bAnyBlue || (batched.Pixels[i + 2] > 90 && batched.Pixels[i + 2] > 2 * batched.Pixels[i]);
            }
            CHECK(bAnyBlue);
            CHECK(worst <= 1);
            CHECK(NewValidationErrors() == 0);
        }

        TEST_CASE_FIXTURE(FWorldRenderFixture, "The GPU time of each stage reaches the frame profiler") {
            REQUIRE(bReady);
            // Results lag by the frames the device keeps in flight: draw a few.
            Render(6);
            IRHIDevice* device = FRHI::GetDevice();
            const float shadow = device->GetGpuTimeMs(EGPUTimerSlot::Shadow);
            const float scene = device->GetGpuTimeMs(EGPUTimerSlot::Opaque);
            const float post = device->GetGpuTimeMs(EGPUTimerSlot::PostProcess);
            CHECK(shadow > 0.0f);
            CHECK(scene > 0.0f);
            CHECK(post > 0.0f);
            // A software rasteriser is slow, not that slow: a second per stage is a broken unit.
            CHECK(shadow + scene + post < 3000.0f);
            // A slot nothing used in the frame reads zero.
            CHECK(device->GetGpuTimeMs(EGPUTimerSlot::Particles) == 0.0f);

            const FFrameTiming& working = FFrameProfiler::Working();
            CHECK(working.GPUOpaqueMs > 0.0f);
            CHECK(working.GPUMs >= working.GPUOpaqueMs);
            CHECK(NewValidationErrors() == 0);
        }
    }

} // namespace Leon
