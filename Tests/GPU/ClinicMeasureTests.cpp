#include <doctest/doctest.h>

// What a frame of LeonHorror's clinic costs the renderer, measured on the graphics card: draw calls,
// shadow-map draw calls and CPU time of FWorldRenderer::Render at a few places of the map. Not a
// check: it prints numbers, and only when asked.
//
//   LEON_TEST_GPU=1 LEON_MEASURE_CLINIC=1 RendererTests.exe -tc="*clinic*"
//
// The map is loaded by the engine alone: the game's classes are not registered, so its actors are
// plain actors with their meshes and lights (no characters, no enemies, doors as the map places
// them). The player's flashlight is added: a Movable spot light at the camera, as the game's.

#include "Assets/FImageFileLoader.hpp"
#include "Assets/UAssetManager.hpp"
#include "Core/FProjectPaths.hpp"
#include "Engine/FGraphicsQuality.hpp"
#include "Engine/FLightmapBakeInputs.hpp"
#include "Engine/FMapSerializer.hpp"
#include "Engine/UWorld.hpp"
#include "Gameplay/AActor.hpp"
#include "RHI/FFrameProfiler.hpp"
#include "RHI/FLegacyRenderDriver.hpp"
#include "RHI/FRHIFrame.hpp"
#include "RHI/FRenderer.hpp"
#include "RHI/IRHIDevice.hpp"
#include "RHI/IRenderDriver.hpp"
#include "RHID3D12/FD3D12RHI.hpp"
#include "RenderCore/FPerspectiveCamera.hpp"
#include "RenderCore/FShaderLibrary.hpp"
#include "Renderer/FImmediateDrawRHI.hpp"
#include "Renderer/FWorldRenderer.hpp"
#include "Scene/Components.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <string>
#include <vector>

namespace Leon {

    namespace {

        struct FClinicView {
            const char* Name;
            glm::vec3 Position;
            /** Yaw in degrees: -90 looks down -Z (plan north). */
            float Yaw;
        };

        float Median(std::vector<float> InValues) {
            if (InValues.empty())
                return 0.0f;
            std::sort(InValues.begin(), InValues.end());
            return InValues[InValues.size() / 2];
        }

    } // namespace

    TEST_SUITE("Renderer measurements") {

        TEST_CASE("the clinic: draw calls, shadow draws and CPU time of a frame at a few places") {
            if (std::getenv("LEON_TEST_GPU") == nullptr || std::getenv("LEON_MEASURE_CLINIC") == nullptr) {
                MESSAGE("Set LEON_TEST_GPU=1 and LEON_MEASURE_CLINIC=1 to measure the clinic on the graphics card.");
                return;
            }
            const std::string project = FProjectPaths::LocateProjectFile("Games/LeonHorror/LeonHorror.lproject");
            REQUIRE_FALSE(project.empty());

            UAssetManager::Clear();
            // The cooked textures (.ltex) are read by the loader UAssetManager::Init installs.
            InstallImageFileLoader();
            FD3D12RHI::Register();
            FRHIInitDesc init;
            // The debug layer costs CPU time on every call: off for a measurement.
            init.bEnableDebugLayer = false;
            init.bUseSoftwareAdapter = false;
            REQUIRE(FRHI::Init(ERHIBackend::D3D12, init));
            FLegacyRenderDriver::Register();
            FProjectPaths::SetProjectRoot(project);
            UAssetManager::SetContentRoot(FProjectPaths::ProjectContentDir());
            {
                TRef<UWorld> world = UWorld::Create("ClinicMeasure");
                std::string map = FProjectPaths::ResolveVirtualPath("/Game/Maps/Clinic");
                if (!map.ends_with(".lmap"))
                    map += ".lmap";
                // The game's settings (its DefaultEngine.ini): High, without planar reflections, one
                // cascade, PCF 5x5. The preset goes first: applying it reloads every texture.
                FGraphicsQuality::ApplyToWorld(*world, EGraphicsQuality::High);
                REQUIRE(FMapSerializer(world).Deserialize(map));
                FLightmapBakeInputs::RefreshRuntimeTrust(*world);
                world->InitWorld();

                FWorldRenderer* renderer = world->GetWorldRenderer();
                renderer->SetImageBasedLightingEnabled(false);
                renderer->SetPlanarReflectionEnabled(false);
                renderer->GetShadowSettings().CascadeCount = 1;
                renderer->GetShadowSettings().ShadowDistance = 30.0f;
                renderer->GetShadowSettings().FilterMode = EShadowFilterMode::PCF5x5;
                const uint32_t width = 1920;
                const uint32_t height = 1080;
                renderer->OnViewportResize(width, height);

                // The flashlight: as ALeonHorrorFlashlight sets it up.
                AActor* flashlight = world->SpawnActor<AActor>("Flashlight");
                FSpotLight spot;
                spot.Color = {1.0f, 0.96f, 0.88f};
                spot.Intensity = 40.0f;
                spot.Radius = 26.0f;
                spot.CutOff = 11.0f;
                spot.OuterCutOff = 26.0f;
                auto& spotComponent = flashlight->AddComponent<FSpotLightComponent>();
                spotComponent.Light = spot;
                spotComponent.Mobility = ELightMobility::Movable;
                spotComponent.bCastShadows = true;

                IRHIDevice* device = FRHI::GetDevice();
                FRHITextureDesc desc;
                desc.Width = width;
                desc.Height = height;
                desc.Format = ERHIFormat::RGBA8_UNorm;
                desc.Usage = RHITextureUsage_Sampled | RHITextureUsage_RenderTarget;
                desc.DebugName = "ClinicOutput";
                TRef<IRHITexture> target = device->CreateTexture(desc);
                REQUIRE(target != nullptr);

                FPerspectiveCamera camera(45.0f, static_cast<float>(width) / static_cast<float>(height), 0.1f, 1000.0f);
                const auto frame = [&]() {
                    FFrameProfiler::BeginFrame();
                    IRHICommandList& commands = device->BeginFrame();
                    FRHIFrame::Begin(commands, *target);
                    const auto start = std::chrono::steady_clock::now();
                    renderer->Render(camera);
                    const float ms =
                        std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start).count();
                    FRHIFrame::End();
                    device->EndFrame();
                    FRHIFrame::RunAfterFrameTasks();
                    return ms;
                };

                // Plan (x, y) is engine (x, 1.6, -y); the plan's north is -Z.
                const FClinicView views[] = {
                    {"corridor (7.2, 8.6) looking north", {7.2f, 1.6f, -8.6f}, -90.0f},
                    {"ward (11.4, 9.0) looking north", {11.4f, 1.6f, -9.0f}, -90.0f},
                    {"reception (2.2, 2.3) looking north", {2.2f, 1.6f, -2.3f}, -90.0f},
                    {"reception (2.2, 2.3) looking east", {2.2f, 1.6f, -2.3f}, 0.0f},
                };
                // Reflection probes are captured over the first frames of a run (or loaded from disk).
                camera.SetPosition(views[0].Position);
                for (int i = 0; i < 80; ++i)
                    frame();

                for (const FClinicView& view : views) {
                    camera.SetPosition(view.Position);
                    camera.SetRotation(-5.0f, view.Yaw);
                    const glm::vec3 forward = camera.GetForwardDirection();
                    flashlight->SetActorLocation(view.Position + glm::vec3(0.2f, -0.3f, 0.0f));
                    flashlight->SetActorRotation(SpotLightEulerFromWorldDirection(forward));
                    // Each place with shadow casters culled as before (the frustum of each shadow view
                    // only) and by the lights' reach and the camera's view, in alternating blocks of
                    // frames: the laptop's clocks drift over a run, and both see the same drift. GPU
                    // times come back a few frames late: the first frames of a block are left out.
                    struct FSamples {
                        std::vector<float> Render, Shadow, Scene, Post, Gpu;
                        uint32_t DrawCalls = 0;
                        uint32_t ShadowDraws = 0;
                    } samples[2];
                    for (int i = 0; i < 40; ++i)
                        frame();
                    constexpr int kBlock = 20;
                    for (int block = 0; block < 20; ++block) {
                        const int mode = block % 2;
                        renderer->GetShadowSettings().bCullShadowCasters = mode == 1;
                        FSamples& out = samples[mode];
                        for (int i = 0; i < kBlock; ++i) {
                            out.Render.push_back(frame());
                            const FFrameTiming& timing = FFrameProfiler::Working();
                            out.Shadow.push_back(timing.ShadowMs);
                            out.Scene.push_back(timing.OpaqueMs);
                            out.Post.push_back(timing.PostProcessMs);
                            if (i >= 5)
                                out.Gpu.push_back(timing.GPUMs);
                            out.DrawCalls = FRenderer::GetStats().DrawCalls;
                            out.ShadowDraws = timing.ShadowDrawCalls;
                        }
                    }
                    for (int mode = 0; mode < 2; ++mode) {
                        const FSamples& in = samples[mode];
                        const std::string name =
                            std::string(view.Name) + (mode == 1 ? " [casters culled]" : " [frustum only]");
                        MESSAGE(name, ": ", in.DrawCalls, " draw calls, ", in.ShadowDraws,
                                " of them shadow maps; CPU render ", Median(in.Render), " ms (shadow maps ",
                                Median(in.Shadow), ", scene pass ", Median(in.Scene), ", post ", Median(in.Post),
                                "); GPU ", Median(in.Gpu), " ms (medians of ", in.Render.size(), " frames)");
                    }
                }
                device->WaitIdle();
                target.reset();
                world.reset();
            }
            UAssetManager::Clear();
            ShutdownImmediateDrawRHI();
            FShaderLibrary::Clear();
            FRenderDriverRegistry::RegisterDriver(nullptr);
            FRHI::Shutdown();
        }
    }

} // namespace Leon
