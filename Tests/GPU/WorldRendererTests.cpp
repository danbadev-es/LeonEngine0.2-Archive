#include <doctest/doctest.h>

#include "Scene/Components.hpp"
#include "Engine/UWorld.hpp"
#include "Gameplay/AActor.hpp"
#include "Gameplay/AReflectionProbe.hpp"
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
#include <filesystem>
#include <system_error>
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
            /** Draws the frames instead of the world's own renderer when set. */
            FWorldRenderer* Renderer = nullptr;

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
                    (Renderer ? Renderer : World->GetWorldRenderer())->Render(Camera);
                    InOverlay();
                    FRHIFrame::End();
                    device->EndFrame();
                    FRHIFrame::RunAfterFrameTasks();
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


        /**
         * A closed room of six glowing walls, each its own colour, with a reflection probe in it and a
         * mirror standing in the middle that faces the camera. On the wall behind the camera there is a
         * small green square; where it shows up in the mirror says whether the probe is read as a box.
         */
        struct FProbeRoomFixture : FWorldRenderFixture {
            /** The debug view of PBR_Lit.hlsl that shows the reflection read from the probes. */
            static constexpr int kProbeReflectionView = 41;
            /** The one that shows what the surface ends up reflecting: probes, sky and planar captures. */
            static constexpr int kReflectionView = 42;
            const glm::vec3 Center{30.0f, 1.5f, 0.0f};
            const glm::vec3 Feature{31.0f, 2.0f, 2.95f};
            TRef<FMaterialInstance> MirrorMaterial;
            AActor* Probe = nullptr;

            TRef<FMaterialInstance> Glow(const char* InName, const glm::vec3& InColor, float InAlbedo = 0.0f) {
                const TRef<FMaterialInstance> material = FloorMaterial->GetParent()->CreateInstance(InName);
                material->SetAlbedoColor(glm::vec3(InAlbedo));
                material->SetRoughness(1.0f);
                material->SetEmissiveColor(InColor);
                material->SetEmissiveIntensity(1.0f);
                return material;
            }

            void AddQuad(const char* InName, const glm::vec3& InPosition, const glm::vec3& InRotation, float InWidth,
                         float InHeight, const TRef<FMaterialInstance>& InMaterial) {
                AActor* wall = World->SpawnActor<AActor>(InName);
                wall->SetActorLocation(InPosition);
                wall->SetActorRotation(InRotation);
                auto& mesh = wall->AddComponent<FMeshComponent>(FMeshPrimitives::CreateQuad(InWidth, InHeight));
                mesh.MeshType = "Quad";
                mesh.MeshWidth = InWidth;
                mesh.MeshHeight = InHeight;
                mesh.Mobility = EComponentMobility::Static;
                wall->AddComponent<FMaterialComponent>(InMaterial);
            }

            /** A polished metal plate facing +Z, a metre behind InRoomCenter. Movable: it is in no capture. */
            void AddMirror(const char* InName, const glm::vec3& InRoomCenter) {
                AActor* mirror = World->SpawnActor<AActor>(InName);
                mirror->SetActorLocation(InRoomCenter + glm::vec3(0.0f, 0.0f, -1.0f));
                auto& mesh = mirror->AddComponent<FMeshComponent>(FMeshPrimitives::CreateBox(2.0f, 2.0f, 0.1f));
                mesh.MeshType = "Box";
                mesh.MeshWidth = 2.0f;
                mesh.MeshHeight = 2.0f;
                mesh.MeshDepth = 0.1f;
                mesh.Mobility = EComponentMobility::Movable;
                mirror->AddComponent<FMaterialComponent>(MirrorMaterial);
            }

            FProbeRoomFixture() {
                if (!bReady)
                    return;
                SwitchFixtureLightsOff();
                UsePlainPostProcess();
                FWorldRenderer* renderer = World->GetWorldRenderer();
                // Every surface shows what its probes give it and nothing else. (The finished image would
                // need the environment, which takes ten seconds and more to build: reflections go through
                // its lookup table.)
                renderer->SetDebugMode(kProbeReflectionView);
                renderer->SetPlanarReflectionEnabled(false);
                renderer->SetReflectionProbeResolution(64);

                // A sky that is blue in every direction: what a mirror shows where no probe reaches.
                AActor* environment = World->SpawnActor<AActor>("Environment");
                auto& sky = environment->AddComponent<FSkyboxComponent>();
                sky.SkyZenithColor = sky.HorizonColor = sky.GroundColor = {0.05f, 0.1f, 1.0f};
                sky.SunIntensity = 0.0f;
                sky.EnvironmentIntensity = 1.0f;

                // The wall behind the camera also takes light, so a lamp near it shows in the capture.
                AddQuad("Back", Center + glm::vec3(0.0f, 0.0f, -3.0f), {0.0f, 0.0f, 0.0f}, 6.0f, 3.0f, Glow("Yellow", {1.0f, 1.0f, 0.0f}));
                AddQuad("Front", Center + glm::vec3(0.0f, 0.0f, 3.0f), {0.0f, 180.0f, 0.0f}, 6.0f, 3.0f,
                        Glow("Magenta", {1.0f, 0.0f, 1.0f}, 1.0f));
                AddQuad("Left", Center + glm::vec3(-3.0f, 0.0f, 0.0f), {0.0f, 90.0f, 0.0f}, 6.0f, 3.0f, Glow("Red", {1.0f, 0.0f, 0.0f}));
                AddQuad("Right", Center + glm::vec3(3.0f, 0.0f, 0.0f), {0.0f, -90.0f, 0.0f}, 6.0f, 3.0f, Glow("Green", {0.0f, 1.0f, 0.0f}));
                AddQuad("RoomFloor", Center + glm::vec3(0.0f, -1.5f, 0.0f), {-90.0f, 0.0f, 0.0f}, 6.0f, 6.0f, Glow("Grey", glm::vec3(0.2f)));
                AddQuad("Ceiling", Center + glm::vec3(0.0f, 1.5f, 0.0f), {90.0f, 0.0f, 0.0f}, 6.0f, 6.0f, Glow("White", glm::vec3(0.6f)));
                AddQuad("Feature", Feature, {0.0f, 180.0f, 0.0f}, 0.5f, 0.5f, Glow("Feature", {0.0f, 3.0f, 0.0f}));

                MirrorMaterial = FloorMaterial->GetParent()->CreateInstance("Mirror");
                MirrorMaterial->SetAlbedoColor({1.0f, 1.0f, 1.0f});
                MirrorMaterial->SetMetallic(1.0f);
                MirrorMaterial->SetRoughness(0.0f);
                AddMirror("Mirror", Center);

                Probe = World->SpawnActor<AReflectionProbe>("Probe");
                Probe->SetActorLocation(Center);
                auto& probe = Probe->AddOrReplaceComponent<FReflectionProbeComponent>();
                probe.Extent = {3.0f, 1.5f, 3.0f};
                // Off centre: a lookup that ignored where the cube was captured would put things elsewhere.
                probe.CaptureOffset = {-1.0f, 0.0f, 0.5f};

                LookAtMirror(Center);
            }

            /** Every surface shows what it reflects, whether that comes from probes or from the sky. */
            void ShowReflections() { World->GetWorldRenderer()->SetDebugMode(kReflectionView); }

            void LookAtMirror(const glm::vec3& InRoomCenter) {
                Camera.SetPosition(InRoomCenter + glm::vec3(0.0f, 0.0f, 2.0f));
                Camera.SetRotation(0.0f, -90.0f);
            }

            /** Where a point of the world lands in the picture, in pixels. */
            glm::vec2 PixelOf(const glm::vec3& InWorldPoint) const {
                const glm::vec4 clip = Camera.GetViewProjectionMatrix() * glm::vec4(InWorldPoint, 1.0f);
                return {(clip.x / clip.w * 0.5f + 0.5f) * static_cast<float>(kWidth),
                        (0.5f - clip.y / clip.w * 0.5f) * static_cast<float>(kHeight)};
            }

            /** The pixels of the mirror that are clearly green, and where their middle is. */
            uint32_t GreenPixels(const FImage& InImage, glm::vec2& OutCenter) const {
                const glm::vec2 low = PixelOf(Camera.GetPosition() + glm::vec3(-0.95f, 0.95f, -2.95f));
                const glm::vec2 high = PixelOf(Camera.GetPosition() + glm::vec3(0.95f, -0.95f, -2.95f));
                uint32_t count = 0;
                glm::vec2 sum(0.0f);
                for (uint32_t y = static_cast<uint32_t>(low.y); y < static_cast<uint32_t>(high.y); ++y) {
                    for (uint32_t x = static_cast<uint32_t>(low.x); x < static_cast<uint32_t>(high.x); ++x) {
                        const uint8_t* pixel = InImage.At(x, y);
                        if (pixel[1] > 80 && pixel[1] > pixel[0] * 2) {
                            ++count;
                            sum += glm::vec2(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f);
                        }
                    }
                }
                OutCenter = count > 0 ? sum / static_cast<float>(count) : glm::vec2(0.0f);
                return count;
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

        TEST_CASE_FIXTURE(FProbeRoomFixture, "A mirror in a room with a reflection probe shows the room, each thing where it is") {
            REQUIRE(bReady);
            FWorldRenderer* renderer = World->GetWorldRenderer();
            // Things a capture must not hold: a lamp that gameplay moves, right at the wall the mirror
            // faces, and a glowing crate that is not part of the level.
            AActor* lamp = World->SpawnActor<AActor>("Flashlight");
            lamp->SetActorLocation(Center + glm::vec3(0.0f, 0.0f, 2.5f));
            FPointLight point;
            point.Color = {0.0f, 1.0f, 0.0f};
            point.Intensity = 6.0f;
            point.Radius = 2.0f; // reaches the wall, not the mirror
            auto& lampLight = lamp->AddComponent<FPointLightComponent>(point);
            lampLight.Mobility = ELightMobility::Movable;
            AActor* crate = World->SpawnActor<AActor>("Crate");
            crate->SetActorLocation(Center + glm::vec3(0.0f, 0.0f, 2.6f));
            auto& crateMesh = crate->AddComponent<FMeshComponent>(FMeshPrimitives::CreateCube(0.6f));
            crateMesh.MeshSize = 0.6f;
            crateMesh.Mobility = EComponentMobility::Movable;
            crate->AddComponent<FMaterialComponent>(Glow("Cyan", {0.0f, 1.0f, 1.0f}));

            const FImage image = Render();
            CHECK(renderer->GetReflectionProbeCaptureCount() == 1);

            // The middle of the mirror looks straight back at the magenta wall behind the camera: not
            // the blue sky, not the crate in front of that wall, and without the flashlight's green.
            const uint8_t* middle = image.At(kWidth / 2, kHeight / 2);
            CAPTURE(static_cast<int>(middle[0]));
            CAPTURE(static_cast<int>(middle[1]));
            CAPTURE(static_cast<int>(middle[2]));
            CHECK(middle[0] > 100);
            CHECK(middle[2] > 100);
            CHECK(middle[0] > middle[1] * 2);
            CHECK(middle[2] > middle[1] * 2);

            // The green square is seen where a real mirror would show it: on the line from the camera's
            // mirror image to the square. A cube read without its box would put it some forty pixels away.
            const float mirrorZ = Center.z - 0.95f;
            const glm::vec3 imageOfCamera(Camera.GetPosition().x, Camera.GetPosition().y, 2.0f * mirrorZ - Camera.GetPosition().z);
            const float along = (mirrorZ - imageOfCamera.z) / (Feature.z - imageOfCamera.z);
            const glm::vec2 expected = PixelOf(imageOfCamera + (Feature - imageOfCamera) * along);
            glm::vec2 found(0.0f);
            const uint32_t sharpPixels = GreenPixels(image, found);
            CAPTURE(expected.x);
            CAPTURE(expected.y);
            CAPTURE(found.x);
            CAPTURE(found.y);
            CHECK(sharpPixels > 40);
            CHECK(glm::length(found - expected) < 4.0f);

            // Nothing is captured again while nothing changes.
            Render();
            CHECK(renderer->GetReflectionProbeCaptureCount() == 1);

            // A rough plate reads the blurred levels of the cube: the square melts into the wall.
            MirrorMaterial->SetRoughness(1.0f);
            glm::vec2 unused(0.0f);
            CHECK(GreenPixels(Render(), unused) == 0);
            MirrorMaterial->SetRoughness(0.0f);

            // A sun that belongs to the level shines down at the wall behind the camera, and the ceiling
            // is in its way: the capture has the level's shadows, so the wall stays as it was.
            renderer->GetShadowSettings().CascadeResolution = 512;
            AActor* sun = World->SpawnActor<AActor>("Sun");
            FDirectionalLight sunLight;
            sunLight.Direction = {0.0f, -1.0f, 1.0f};
            sunLight.Color = {1.0f, 1.0f, 1.0f};
            sunLight.Intensity = 5.0f;
            sun->AddComponent<FDirectionalLightComponent>(sunLight).Mobility = ELightMobility::Stationary;
            renderer->InvalidateReflectionProbes();
            const FImage withSun = Render();
            CHECK(renderer->GetReflectionProbeCaptureCount() == 2);
            CHECK(withSun.At(kWidth / 2, kHeight / 2)[1] < middle[1] + 25);

            // The lamp becomes part of the level too, with shadows of its own: now it is in the
            // capture, and the wall behind the camera is no longer plain magenta.
            lampLight.Mobility = ELightMobility::Stationary;
            lampLight.bCastShadows = true;
            renderer->InvalidateReflectionProbes();
            const FImage withLamp = Render();
            CHECK(renderer->GetReflectionProbeCaptureCount() == 3);
            CHECK(withLamp.At(kWidth / 2, kHeight / 2)[1] > middle[1] + 40);
            CHECK(NewValidationErrors() == 0);
        }

        TEST_CASE_FIXTURE(FProbeRoomFixture, "Outside every reflection probe a surface reflects the sky, and between two it blends") {
            REQUIRE(bReady);
            ShowReflections();
            // The same plate far from the room, under the open sky.
            const glm::vec3 outside(60.0f, 1.5f, 0.0f);
            AddMirror("OutsideMirror", outside);
            LookAtMirror(outside);
            const FImage open = Render();
            const uint8_t* sky = open.At(kWidth / 2, kHeight / 2);
            CAPTURE(static_cast<int>(sky[0]));
            CAPTURE(static_cast<int>(sky[2]));
            CHECK(sky[2] > 60);
            CHECK(sky[2] > sky[0] * 2);

            // Half a fade away from the room's box the probe and the sky share the reflection.
            const glm::vec3 fringe = Center + glm::vec3(0.0f, 0.0f, 3.0f + 0.25f + 0.95f);
            AddMirror("FringeMirror", fringe + glm::vec3(0.0f, 0.0f, 0.05f));
            Camera.SetPosition(fringe + glm::vec3(0.0f, 0.0f, 3.0f));
            const FImage fringeImage = Render();
            const uint8_t* shared = fringeImage.At(kWidth / 2, kHeight / 2);
            CAPTURE(static_cast<int>(shared[0]));
            CAPTURE(static_cast<int>(shared[2]));
            // Red comes only from the room, and there is less of it than inside.
            LookAtMirror(Center);
            const FImage insideImage = Render();
            const uint8_t* inside = insideImage.At(kWidth / 2, kHeight / 2);
            CAPTURE(static_cast<int>(inside[0]));
            CHECK(shared[0] > sky[0] + 20);
            CHECK(shared[0] < inside[0] - 20);
            CHECK(inside[0] > inside[1] * 2);

            // With probes off the plate in the room reflects the sky too, as before probes existed.
            World->GetWorldRenderer()->SetReflectionProbesEnabled(false);
            const FImage withoutProbes = Render();
            const uint8_t* plain = withoutProbes.At(kWidth / 2, kHeight / 2);
            CHECK(plain[2] > plain[0] * 2);
            CHECK(NewValidationErrors() == 0);
        }

        TEST_CASE_FIXTURE(FProbeRoomFixture, "The capture of a reflection probe is kept on disk and used by the next run") {
            REQUIRE(bReady);
            namespace fs = std::filesystem;
            const fs::path cache = fs::path("build") / "ReflectionProbeCacheTest";
            std::error_code error;
            fs::remove_all(cache, error);
            const auto filesIn = [&cache]() {
                std::error_code ignored;
                size_t count = 0;
                if (fs::exists(cache, ignored)) {
                    for (const auto& entry : fs::directory_iterator(cache, ignored))
                        count += entry.path().extension() == ".lprobecube" ? 1 : 0;
                }
                return count;
            };

            // Without a bake nothing identifies the level: captures are made but not kept.
            FWorldRenderer* renderer = World->GetWorldRenderer();
            renderer->SetReflectionProbeCacheDirectory(cache.string());
            Render();
            CHECK(renderer->GetReflectionProbeCaptureCount() == 1);
            CHECK(filesIn() == 0);

            // A baked level has a hash; its captures go to disk under it.
            auto& settings = World->FindActorByName("Environment")->AddComponent<FWorldSettingsComponent>();
            settings.bStaticLighting = true;
            settings.LightmapBakeHash = 0x1234ABCDull;
            const FImage captured = Render();
            CHECK(renderer->GetReflectionProbeCaptureCount() == 2);
            CHECK(renderer->GetReflectionProbeCacheLoadCount() == 0);
            CHECK(filesIn() == 1);

            // "The next run": another renderer for the same world finds the file and captures nothing.
            {
                FWorldRenderer second(World.get());
                second.SetImageBasedLightingEnabled(false);
                second.SetDebugMode(kProbeReflectionView);
                second.SetPlanarReflectionEnabled(false);
                second.SetReflectionProbeResolution(64);
                second.SetReflectionProbeCacheDirectory(cache.string());
                second.OnViewportResize(kWidth, kHeight);
                second.GetPostProcessSettings() = renderer->GetPostProcessSettings();
                Renderer = &second;
                const FImage loaded = Render();
                CHECK(second.GetReflectionProbeCaptureCount() == 0);
                CHECK(second.GetReflectionProbeCacheLoadCount() == 1);
                // And what it loaded is what was captured: the mirror shows the same thing.
                uint32_t different = 0;
                for (uint32_t y = 0; y < kHeight; ++y) {
                    for (uint32_t x = 0; x < kWidth; ++x) {
                        for (uint32_t channel = 0; channel < 3; ++channel)
                            different += std::abs(captured.At(x, y)[channel] - loaded.At(x, y)[channel]) > 3 ? 1 : 0;
                    }
                }
                CHECK(different == 0);

                // A probe that moves is another capture; the bake hash changing is another one again.
                Probe->GetComponent<FReflectionProbeComponent>().CaptureOffset = {0.0f, 0.0f, 0.0f};
                Render();
                CHECK(second.GetReflectionProbeCaptureCount() == 1);
                CHECK(filesIn() == 2);
                settings.LightmapBakeHash = 0x5678ull;
                Render();
                CHECK(second.GetReflectionProbeCaptureCount() == 2);
                CHECK(filesIn() == 3);
                // A bake that is not trusted identifies nothing: captured, not kept.
                World->SetLightmapsTrusted(false);
                Render();
                CHECK(second.GetReflectionProbeCaptureCount() == 3);
                CHECK(filesIn() == 3);
                Renderer = nullptr;
            }
            fs::remove_all(cache, error);
            CHECK(NewValidationErrors() == 0);
        }

        TEST_CASE_FIXTURE(FWorldRenderFixture, "Things without a lightmap take their ambient light from the light probes around them") {
            REQUIRE(bReady);
            SwitchFixtureLightsOff();
            UsePlainPostProcess();
            World->FindActorByName("Box")->GetComponent<FMeshComponent>().bVisible = false;
            World->FindActorByName("Floor")->GetComponent<FMeshComponent>().bVisible = false;
            // A grey sky, the same everywhere: what lights these things when the map has no probes.
            AActor* environment = World->SpawnActor<AActor>("Environment");
            auto& sky = environment->AddComponent<FSkyboxComponent>();
            sky.bEnabled = false;
            sky.SkyZenithColor = sky.HorizonColor = sky.GroundColor = glm::vec3(0.25f);
            sky.EnvironmentIntensity = 1.0f;

            // Two white balls that move, sharing one mesh and one material, three metres apart.
            const TRef<FMaterialInstance> white = FloorMaterial->GetParent()->CreateInstance("White");
            white->SetAlbedoColor({1.0f, 1.0f, 1.0f});
            white->SetRoughness(1.0f);
            const TRef<FVertexArray> ball = FMeshPrimitives::CreateSphere(0.5f);
            const glm::vec3 leftBall(-1.5f, 1.0f, 3.0f);
            const glm::vec3 rightBall(1.5f, 1.0f, 3.0f);
            for (const glm::vec3& position : {leftBall, rightBall}) {
                AActor* actor = World->SpawnActor<AActor>("Ball");
                actor->SetActorLocation(position);
                auto& mesh = actor->AddComponent<FMeshComponent>(ball);
                mesh.MeshType = "Sphere";
                mesh.MeshRadius = 0.5f;
                mesh.Mobility = EComponentMobility::Movable;
                actor->AddComponent<FMaterialComponent>(white);
            }
            Camera.SetPosition({0.0f, 1.0f, 8.0f});
            Camera.SetRotation(0.0f, -90.0f);

            // The colour of a ball a third of a metre to one side of its middle, on the half that faces us.
            const auto sideOf = [this](const FImage& InImage, const glm::vec3& InBall, float InSide) {
                const glm::vec4 clip =
                    Camera.GetViewProjectionMatrix() * glm::vec4(InBall + glm::vec3(0.35f * InSide, 0.0f, 0.35f), 1.0f);
                const uint32_t x = static_cast<uint32_t>((clip.x / clip.w * 0.5f + 0.5f) * static_cast<float>(kWidth));
                const uint32_t y = static_cast<uint32_t>((0.5f - clip.y / clip.w * 0.5f) * static_cast<float>(kHeight));
                const uint8_t* pixel = InImage.At(x, y);
                return glm::ivec3(pixel[0], pixel[1], pixel[2]);
            };

            // No probes: the sky, grey, the same on both sides of both balls.
            const FImage plain = Render();
            for (const glm::vec3& position : {leftBall, rightBall}) {
                const glm::ivec3 left = sideOf(plain, position, -1.0f);
                const glm::ivec3 right = sideOf(plain, position, 1.0f);
                CHECK(left.r > 40);
                CHECK(std::abs(left.r - left.b) < 6);
                CHECK(std::abs(left.r - right.r) < 6);
            }

            // Probes: where the left ball is, red light comes from the left; where the right one is,
            // blue light from the right. One probe each, exactly where the balls are.
            FLightProbeGrid grid;
            grid.Origin = leftBall;
            grid.Spacing = {3.0f, 1.0f, 1.0f};
            grid.Resize({2, 1, 1});
            grid.Probes[0].AddRadiance({-1.0f, 0.0f, 0.0f}, {3.0f, 0.2f, 0.2f});
            grid.Probes[1].AddRadiance({1.0f, 0.0f, 0.0f}, {0.2f, 0.2f, 3.0f});
            World->SetLightProbes(grid);
            const FImage probed = Render();
            const glm::ivec3 redSide = sideOf(probed, leftBall, -1.0f);
            const glm::ivec3 darkSide = sideOf(probed, leftBall, 1.0f);
            CAPTURE(redSide.r);
            CAPTURE(redSide.g);
            CAPTURE(darkSide.r);
            CHECK(redSide.r > 120);
            CHECK(redSide.r > redSide.g * 2);
            CHECK(redSide.r > darkSide.r + 60);
            // The other ball has its own light, although it is the same mesh with the same material:
            // the two are not drawn as copies of one.
            const glm::ivec3 blueSide = sideOf(probed, rightBall, 1.0f);
            const glm::ivec3 otherSide = sideOf(probed, rightBall, -1.0f);
            CAPTURE(blueSide.b);
            CAPTURE(blueSide.r);
            CHECK(blueSide.b > 120);
            CHECK(blueSide.b > blueSide.r * 2);
            CHECK(blueSide.b > otherSide.b + 60);

            // A bake that is not trusted takes its probes with it: the sky again, as without them.
            World->SetLightmapsTrusted(false);
            const FImage untrusted = Render();
            uint32_t different = 0;
            for (size_t i = 0; i < plain.Pixels.size(); ++i)
                different += plain.Pixels[i] != untrusted.Pixels[i] ? 1 : 0;
            CHECK(different == 0);
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
