#pragma once

// The world renderer drawing real frames of a small scene on WARP (the software Direct3D 12 adapter)
// with the debug layer on, for the tests that look at whole frames. One fixture per test case: it
// creates the device and destroys it. Set LEON_TEST_GPU=1 to run on the machine's graphics card.

#include <doctest/doctest.h>

#include "Assets/FMaterial.hpp"
#include "Assets/FMaterialInstance.hpp"
#include "Engine/UWorld.hpp"
#include "Gameplay/AActor.hpp"
#include "RHI/FLegacyRenderDriver.hpp"
#include "RHI/FRHIFrame.hpp"
#include "RHI/IRHIDevice.hpp"
#include "RHI/IRenderDriver.hpp"
#include "RHID3D12/FD3D12RHI.hpp"
#include "RenderCore/FMeshPrimitives.hpp"
#include "RenderCore/FPerspectiveCamera.hpp"
#include "RenderCore/FShaderLibrary.hpp"
#include "Renderer/FImmediateDrawRHI.hpp"
#include "Renderer/FWorldRenderer.hpp"
#include "Scene/Components.hpp"

#include <glm/glm.hpp>

#include <cstdlib>
#include <vector>

namespace Leon {

    inline constexpr uint32_t kWidth = 320;
    inline constexpr uint32_t kHeight = 180;

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

} // namespace Leon
