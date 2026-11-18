#include "Application/FApplication.hpp"
#include "Platform/FInput.hpp"
#include "Core/FLog.hpp"
#include "RHI/FFrameProfiler.hpp"
#include "Core/events/FKeyEvent.hpp"
#include "Assets/UAssetManager.hpp"
#include "Renderer/FDebugOverlay.hpp"
#include "Renderer/FDebugRenderer.hpp"
#include "Renderer/FParticleRenderer.hpp"
#include "RHI/FRenderer.hpp"
#include "RHI/FRenderCommand.hpp"
#include "Renderer/FTextRenderer.hpp"
#include "Renderer/FShaderLibrary.hpp"
#include "Physics/IPhysicsScene.hpp"

#include <algorithm>
#include "Platform/FPlatformTime.hpp"

namespace Leon {

    FApplication* FApplication::Instance = nullptr;

    FApplication::FApplication(const FApplicationProps& InProps) {
        if (Instance) {
            LE_CORE_ERROR("FApplication already exists!");
            return;
        }
        Instance = this;

        FLog::Init();
        LE_CORE_INFO("Initializing LeonEngine FApplication: {0}", InProps.Name);

        FWindowProps WindowProps(InProps.Name, InProps.WindowWidth, InProps.WindowHeight, true,
                                 InProps.bHdClientPolicy, true, InProps.bMaximized);
        AppWindow = FWindow::Create(WindowProps);
        GraphicsContext = IGraphicsContext::Create(AppWindow->GetNativeHandle());
        if (GraphicsContext) {
            GraphicsContext->Init();
            AppWindow->SetVSync(AppWindow->IsVSync());
        } else {
            LE_CORE_ERROR("Failed to create IGraphicsContext! No active RenderDriver is registered.");
        }
        // The GPU device, when a backend was registered by the executable (the launcher does).
        if (!FRHI::IsInitialized() && FRHI::IsBackendRegistered(ERHIBackend::D3D12)) {
            FRHIInitDesc rhiDesc;
#ifndef NDEBUG
            rhiDesc.bEnableDebugLayer = true;
#endif
            for (int i = 1; i < InProps.CommandLineArgs.Count; ++i) {
                const std::string argument = InProps.CommandLineArgs[i] ? InProps.CommandLineArgs[i] : "";
                if (argument == "--warp")
                    rhiDesc.bUseSoftwareAdapter = true;
                else if (argument == "--gpu-validation")
                    rhiDesc.bEnableDebugLayer = true;
                else if (argument == "--no-gpu-validation")
                    rhiDesc.bEnableDebugLayer = false;
            }
            FRHI::Init(ERHIBackend::D3D12, rhiDesc);
        }
        if (IRHIDevice* device = FRHI::GetDevice()) {
            Swapchain = device->CreateSwapchain(AppWindow->GetOSHandle(), AppWindow->GetWidth(), AppWindow->GetHeight());
            if (!Swapchain)
                LE_CORE_ERROR("FApplication: could not create the swapchain; the window will stay empty");
        }
        WindowCreationTime = std::chrono::high_resolution_clock::now();
        AppWindow->SetEventCallback(LE_BIND_EVENT_FN(FApplication::OnEvent));

        FRenderer::Init();
        UAssetManager::Init();
        FDebugRenderer::Init();
        FParticleRenderer::Init();
        FTextRenderer::Init();
        FDebugOverlay::Init();
    }

    FApplication::~FApplication() {
        FDebugOverlay::Shutdown();
        FTextRenderer::Shutdown();
        FParticleRenderer::Shutdown();
        FDebugRenderer::Shutdown();
        UAssetManager::Shutdown();
        FRenderer::Shutdown();
        FShaderLibrary::Clear();
        // The swapchain's images must be gone before the device.
        if (IRHIDevice* device = FRHI::GetDevice())
            device->WaitIdle();
        Swapchain.reset();
        FRHI::Shutdown();
        Instance = nullptr;
    }

    void FApplication::PushLayer(FLayer* InLayer) {
        LayerStack.PushLayer(InLayer);
    }

    void FApplication::PushOverlay(FLayer* InOverlay) {
        LayerStack.PushOverlay(InOverlay);
    }

    void FApplication::Close() {
        bRunning = false;
    }

    void FApplication::OnEvent(FEvent& InEvent) {
        FEventDispatcher dispatcher(InEvent);
        dispatcher.Dispatch<FWindowCloseEvent>(LE_BIND_EVENT_FN(FApplication::OnWindowClose));
        dispatcher.Dispatch<FWindowResizeEvent>(LE_BIND_EVENT_FN(FApplication::OnWindowResize));
        dispatcher.Dispatch<FKeyPressedEvent>(LE_BIND_EVENT_FN(FApplication::OnKeyPressed));

        for (auto it = LayerStack.rbegin(); it != LayerStack.rend(); ++it) {
            if (InEvent.bHandled)
                break;
            (*it)->OnEvent(InEvent);
        }
    }

    bool FApplication::OnWindowClose(FWindowCloseEvent& /*InEvent*/) {
        bRunning = false;
        return true;
    }

    bool FApplication::OnWindowResize(FWindowResizeEvent& InEvent) {
        if (InEvent.GetWidth() == 0 || InEvent.GetHeight() == 0) {
            bMinimized = true;
            return false;
        }

        bMinimized = false;
        if (Swapchain)
            Swapchain->Resize(InEvent.GetWidth(), InEvent.GetHeight());
        FRenderer::OnWindowResize(InEvent.GetWidth(), InEvent.GetHeight());
        return false;
    }

    bool FApplication::OnKeyPressed(FKeyPressedEvent& InEvent) {
        bool bShift = FInput::IsKeyPressed(Key::LeftShift) || FInput::IsKeyPressed(Key::RightShift);
        if (bShift) {
            if (InEvent.GetKeyCode() == Key::F1 && !InEvent.IsRepeat()) {
                bShowGameplayDebug = !bShowGameplayDebug;
                bDebugPhysics = bShowGameplayDebug;
                bDebugCharacter = false;
                FDebugRenderer::SetTraceCaptureEnabled(bShowGameplayDebug);
                LE_CORE_INFO("Gameplay debug (colliders + weapon traces): {0}  [Shift+F1]",
                             bShowGameplayDebug ? "ENABLED" : "DISABLED");
                return true;
            }
            if (InEvent.GetKeyCode() == Key::F5 && !InEvent.IsRepeat()) {
                ToggleDebugAI();
                LE_CORE_INFO("AI debug: {0}  [Shift+F5]", bDebugAI ? "ENABLED" : "DISABLED");
                return true;
            }
            if (InEvent.GetKeyCode() == Key::F6 && !InEvent.IsRepeat()) {
                ToggleDebugPhysics();
                LE_CORE_INFO("Physics debug: {0}  [Shift+F6]", bDebugPhysics ? "ENABLED" : "DISABLED");
                return true;
            }
            if (InEvent.GetKeyCode() == Key::F7 && !InEvent.IsRepeat()) {
                ToggleDebugCharacter();
                return true;
            }
            if (InEvent.GetKeyCode() == Key::F8 && !InEvent.IsRepeat()) {
                ToggleDebugNetwork();
                return true;
            }
        } else {
            if (InEvent.GetKeyCode() == Key::F1 && !InEvent.IsRepeat()) {
                bShowHUD = !bShowHUD;
                LE_CORE_INFO("Diagnostics HUD: {0}", bShowHUD ? "ENABLED" : "DISABLED");
                return true;
            }

            if (InEvent.GetKeyCode() == Key::F2 && !InEvent.IsRepeat()) {
                bShowLightGizmos = !bShowLightGizmos;
                LE_CORE_INFO("Light Debug Gizmos: {0}", bShowLightGizmos ? "ENABLED" : "DISABLED");
                return true;
            }
        }

        return false;
    }

    void FApplication::Run() {
        OnInit();

        while (bRunning) {
            float time = static_cast<float>(FPlatformTime::Seconds());
            const float rawDt = time - LastFrameTime;
            LastFrameTime = time;
            FTimestep timestep = std::min(rawDt, kPhysicsMaxFrameDeltaSeconds);

            IRHIDevice* device = FRHI::GetDevice();
            const bool bRenderFrame = device && Swapchain && !bMinimized;
            if (bRenderFrame) {
                FrameCommands = &device->BeginFrame();
                bBackBufferUntouched = true;
            }

            if (!bMinimized) {
                OnUpdate(timestep);

                for (FLayer* layer : LayerStack) {
                    layer->OnUpdate(timestep);
                }

                // Render Top-Level Diagnostics HUD Overlay if enabled
                if (bShowHUD) {
                    FDebugOverlay::Render(AppWindow->GetWidth(), AppWindow->GetHeight(), timestep,
                                          FRenderer::GetStats(), bShowLightGizmos);
                }
            }

            {
                const auto presentStart = std::chrono::high_resolution_clock::now();
                if (bRenderFrame) {
                    // Nothing drew the final image (no world, or the renderer is not on this RHI yet):
                    // show a cleared frame rather than whatever the back buffer held.
                    if (bBackBufferUntouched) {
                        FRHIRenderPassDesc pass;
                        pass.NumColorTargets = 1;
                        pass.Color[0].Texture = Swapchain->GetBackBuffer();
                        pass.Color[0].ClearColor = {0.02f, 0.03f, 0.05f, 1.0f};
                        pass.Name = "ClearBackBuffer";
                        FrameCommands->BeginRenderPass(pass);
                        FrameCommands->EndRenderPass();
                    }
                    FrameCommands = nullptr;
                    device->EndFrame();
                }
                AppWindow->OnUpdate();
                if (bRenderFrame)
                    Swapchain->Present(AppWindow->IsVSync());
                if (GraphicsContext)
                    GraphicsContext->SwapBuffers();
                const auto presentEnd = std::chrono::high_resolution_clock::now();
                FFrameProfiler::SetPresentMs(
                    std::chrono::duration<float, std::milli>(presentEnd - presentStart).count());
            }
            FRenderCommand::ResolveGPUTimeQueries();
            auto& last = FFrameProfiler::LastMutable();
            last.GPUShadowMs = FRenderCommand::GetGPUTimeMs(EGPUTimerSlot::Shadow);
            last.GPUOpaqueMs = FRenderCommand::GetGPUTimeMs(EGPUTimerSlot::Opaque);
            last.GPUPostProcessMs = FRenderCommand::GetGPUTimeMs(EGPUTimerSlot::PostProcess);
            last.GPUPlanarMs = FRenderCommand::GetGPUTimeMs(EGPUTimerSlot::Planar);
            last.GPUMs = last.GPUShadowMs + last.GPUOpaqueMs + last.GPUPostProcessMs + last.GPUPlanarMs +
                         FRenderCommand::GetGPUTimeMs(EGPUTimerSlot::Sky) +
                         FRenderCommand::GetGPUTimeMs(EGPUTimerSlot::Transparent) +
                         FRenderCommand::GetGPUTimeMs(EGPUTimerSlot::Particles);
            FFrameStatsCollector::Capture(last);
        }

        OnShutdown();
    }

    float FApplication::GetTimeSinceWindowOpenMs() const {
        auto now = std::chrono::high_resolution_clock::now();
        return std::chrono::duration<float, std::milli>(now - WindowCreationTime).count();
    }

} // namespace Leon
