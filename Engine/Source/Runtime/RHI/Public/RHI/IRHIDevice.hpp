#pragma once

#include "RHI/RHITypes.hpp"

#include <functional>
#include <vector>

namespace Leon {

    // Resources are reference counted (TRef). A backend keeps a released resource alive until the
    // GPU has finished the frames that used it, so dropping the last reference is always safe.

    class IRHIBuffer {
    public:
        virtual ~IRHIBuffer() = default;
        virtual const FRHIBufferDesc& GetDesc() const = 0;
    };

    class IRHITexture {
    public:
        virtual ~IRHITexture() = default;
        virtual const FRHITextureDesc& GetDesc() const = 0;
    };

    class IRHIShader {
    public:
        virtual ~IRHIShader() = default;
        virtual ERHIShaderStage GetStage() const = 0;
    };

    class IRHIPipeline {
    public:
        virtual ~IRHIPipeline() = default;
    };

    /**
     * Records the GPU work of one frame. Drawing is only valid between BeginRenderPass and
     * EndRenderPass; everything set on the list (pipeline, buffers, constants, textures) stays set
     * until changed or until the pass ends.
     */
    class IRHICommandList {
    public:
        virtual ~IRHICommandList() = default;

        /**
         * Starts drawing into the given targets and sets the viewport and scissor to cover them.
         * Targets are made writable here and readable again in EndRenderPass, so a texture drawn in
         * one pass can simply be sampled in the next.
         */
        virtual void BeginRenderPass(const FRHIRenderPassDesc& InPass) = 0;
        virtual void EndRenderPass() = 0;

        virtual void SetViewport(const FRHIViewport& InViewport) = 0;
        virtual void SetScissor(const FRHIRect& InRect) = 0;

        virtual void SetPipeline(IRHIPipeline* InPipeline) = 0;
        virtual void SetVertexBuffer(IRHIBuffer* InBuffer) = 0;
        virtual void SetIndexBuffer(IRHIBuffer* InBuffer) = 0;

        /**
         * Copies InSize bytes of vertices into this frame's memory and binds them as the vertex buffer.
         * For geometry rebuilt every frame (UI, debug lines, particles): there is no buffer to create
         * or keep, and the data is captured at the call like SetConstants.
         */
        virtual void SetVertexData(const void* InData, uint32_t InSize, uint32_t InStride) = 0;
        /** The same for indices; InStride is 2 or 4 bytes per index. Use with DrawIndexed. */
        virtual void SetIndexData(const void* InData, uint32_t InSize, uint32_t InStride) = 0;

        /**
         * Copies InSize bytes into this frame's constant memory and binds them at the slot. The data
         * is captured at the call: each draw sees exactly what was set before it, however many times
         * a slot is rewritten in a frame.
         */
        virtual void SetConstants(ERHIConstantSlot InSlot, const void* InData, uint32_t InSize) = 0;

        /** Binds a texture at register t<InSlot>. Null binds a 1x1 black texture. */
        virtual void SetTexture(uint32_t InSlot, IRHITexture* InTexture) = 0;

        virtual void Draw(uint32_t InVertexCount, uint32_t InFirstVertex = 0) = 0;
        virtual void DrawIndexed(uint32_t InIndexCount, uint32_t InFirstIndex = 0, int32_t InBaseVertex = 0,
                                 uint32_t InInstanceCount = 1) = 0;

        /**
         * Fills every mip level of a 2D render target below the first, each drawn from the one above
         * with InDownsample: a pipeline that draws a full-screen triangle (3 vertices, no vertex
         * buffer, no constants) reading its source at t0, built for the texture's format with no
         * depth. Call outside a render pass; the pipeline and textures set on the list are forgotten.
         */
        virtual void GenerateMips(IRHITexture& InTexture, IRHIPipeline* InDownsample) = 0;

        /** Named regions in GPU captures (PIX, RenderDoc). */
        virtual void PushMarker(const char* InName) = 0;
        virtual void PopMarker() = 0;

        /**
         * Brackets GPU work to be timed under a slot (EGPUTimerSlot). A slot may be opened several
         * times in a frame: the times add up. Different slots may nest. The result arrives a few
         * frames later through IRHIDevice::GetGpuTimeMs. Backends without timers ignore both calls.
         */
        virtual void BeginGpuTimer(uint32_t /*InSlot*/) {}
        virtual void EndGpuTimer(uint32_t /*InSlot*/) {}
    };

    /** The images a window shows. */
    class IRHISwapchain {
    public:
        virtual ~IRHISwapchain() = default;
        virtual uint32_t GetWidth() const = 0;
        virtual uint32_t GetHeight() const = 0;
        /** The image to draw into this frame. Valid until Present. */
        virtual IRHITexture* GetBackBuffer() = 0;
        /** Call when the window changes size. Width or height of zero (minimised) is ignored. */
        virtual void Resize(uint32_t InWidth, uint32_t InHeight) = 0;
        /** Shows the back buffer. Call after IRHIDevice::EndFrame. */
        virtual void Present(bool bInVSync) = 0;
    };

    class IRHIDevice {
    public:
        virtual ~IRHIDevice() = default;

        virtual const FRHIDeviceInfo& GetInfo() const = 0;

        /** InInitialData may be null; when given it must hold InDesc.Size bytes. */
        virtual TRef<IRHIBuffer> CreateBuffer(const FRHIBufferDesc& InDesc, const void* InInitialData = nullptr) = 0;
        /** Rewrites part of a buffer created with bDynamic. */
        virtual void WriteBuffer(IRHIBuffer& InBuffer, const void* InData, uint64_t InSize, uint64_t InOffset = 0) = 0;

        virtual TRef<IRHITexture> CreateTexture(const FRHITextureDesc& InDesc) = 0;
        /**
         * Fills one mip of one slice. InData holds Height rows of InRowPitch bytes; rows start at the
         * top of the image.
         */
        virtual void UploadTexture(IRHITexture& InTexture, uint32_t InMipLevel, uint32_t InArraySlice,
                                   const void* InData, uint32_t InRowPitch) = 0;
        /**
         * Copies one mip of one slice back to memory, tightly packed, rows from the top. Waits for the
         * GPU: for tests and tools, not for per-frame use. Call outside BeginFrame/EndFrame.
         */
        virtual bool ReadTexture(IRHITexture& InTexture, uint32_t InMipLevel, uint32_t InArraySlice,
                                 std::vector<uint8_t>& OutPixels) = 0;

        virtual TRef<IRHIShader> CreateShader(const FRHIShaderDesc& InDesc) = 0;
        virtual TRef<IRHIPipeline> CreateGraphicsPipeline(const FRHIGraphicsPipelineDesc& InDesc) = 0;

        /** InWindowHandle is the OS window (an HWND on Windows). */
        virtual TScope<IRHISwapchain> CreateSwapchain(void* InWindowHandle, uint32_t InWidth, uint32_t InHeight) = 0;

        /**
         * Milliseconds the GPU spent in a timer slot, in the most recent frame it has finished
         * (0 when the slot was not used in that frame, or the backend has no timers).
         */
        virtual float GetGpuTimeMs(uint32_t /*InSlot*/) const { return 0.0f; }

        /** One frame of GPU work: record on the returned list, then EndFrame submits it. */
        virtual IRHICommandList& BeginFrame() = 0;
        virtual void EndFrame() = 0;

        /** Blocks until the GPU has finished everything submitted. */
        virtual void WaitIdle() = 0;
    };

    /** Access to the one device of the process, and the list of backends that can create it. */
    class FRHI {
    public:
        using FDeviceFactory = std::function<TScope<IRHIDevice>(const FRHIInitDesc&)>;

        /** Called by a backend module so Init can find it. */
        static void RegisterBackend(ERHIBackend InBackend, FDeviceFactory InFactory);
        static bool IsBackendRegistered(ERHIBackend InBackend);

        /** Creates the device. Returns false when the backend is not registered or cannot start. */
        static bool Init(ERHIBackend InBackend, const FRHIInitDesc& InDesc = {});
        static void Shutdown();

        static bool IsInitialized();
        /** Null before Init. */
        static IRHIDevice* GetDevice();
    };

} // namespace Leon
