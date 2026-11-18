#pragma once

// Private declarations of the Direct3D 12 backend. Nothing outside this module includes this file.

#include "RHI/IRHIDevice.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <array>
#include <unordered_map>
#include <vector>

namespace Leon {

    using Microsoft::WRL::ComPtr;

    class FD3D12Device;

    /** Frames the CPU may record ahead of the GPU. Per-frame memory exists once per slot. */
    inline constexpr uint32_t kD3D12FrameCount = 2;
    /** Shader-visible descriptors available to one frame (16 are used per texture-table change). */
    inline constexpr uint32_t kD3D12DescriptorsPerFrame = 32768;
    /** Size of one page of per-frame constant memory. More pages are added when a frame needs them. */
    inline constexpr uint64_t kD3D12ConstantPageSize = 4ull * 1024ull * 1024ull;
    inline constexpr uint32_t kD3D12TextureTableRootIndex = kRHIConstantSlots;
    inline constexpr uint32_t kD3D12InvalidDescriptor = 0xFFFFFFFFu;

    DXGI_FORMAT D3D12ResourceFormat(ERHIFormat InFormat);
    DXGI_FORMAT D3D12ViewFormat(ERHIFormat InFormat);       // render target / depth stencil view
    DXGI_FORMAT D3D12ShaderReadFormat(ERHIFormat InFormat); // shader resource view

    /** Fixed-size descriptor heap that is not visible to shaders, with a free list. */
    class FD3D12DescriptorPool {
    public:
        bool Init(ID3D12Device* InDevice, D3D12_DESCRIPTOR_HEAP_TYPE InType, uint32_t InCapacity);
        uint32_t Allocate();
        void Free(uint32_t InIndex);
        D3D12_CPU_DESCRIPTOR_HANDLE At(uint32_t InIndex) const;

    private:
        ComPtr<ID3D12DescriptorHeap> Heap;
        D3D12_CPU_DESCRIPTOR_HANDLE Start{};
        uint32_t Stride = 0;
        uint32_t Capacity = 0;
        uint32_t Next = 0;
        std::vector<uint32_t> FreeList;
    };

    class FD3D12Buffer final : public IRHIBuffer {
    public:
        ~FD3D12Buffer() override;
        const FRHIBufferDesc& GetDesc() const override { return Desc; }

        FRHIBufferDesc Desc;
        ComPtr<ID3D12Resource> Resource;
        /** Non-null for dynamic buffers: they live in an upload heap and stay mapped. */
        uint8_t* Mapped = nullptr;
    };

    class FD3D12Texture final : public IRHITexture {
    public:
        ~FD3D12Texture() override;
        const FRHITextureDesc& GetDesc() const override { return Desc; }

        static uint32_t ViewKey(uint32_t InMip, uint32_t InSlice) { return (InSlice << 8) | InMip; }

        FRHITextureDesc Desc;
        ComPtr<ID3D12Resource> Resource;
        /** State the resource is in right now, as recorded so far. */
        D3D12_RESOURCE_STATES State = D3D12_RESOURCE_STATE_COMMON;
        /** State it rests in between uses: shader-readable, or PRESENT for a back buffer. */
        D3D12_RESOURCE_STATES RestState = D3D12_RESOURCE_STATE_COMMON;
        uint32_t SrvIndex = kD3D12InvalidDescriptor;
        std::unordered_map<uint32_t, uint32_t> RtvIndices;
        std::unordered_map<uint32_t, uint32_t> DsvIndices;
    };

    class FD3D12Shader final : public IRHIShader {
    public:
        ERHIShaderStage GetStage() const override { return Stage; }
        ERHIShaderStage Stage = ERHIShaderStage::Vertex;
        std::vector<uint8_t> Bytecode;
    };

    class FD3D12Pipeline final : public IRHIPipeline {
    public:
        ~FD3D12Pipeline() override;
        ComPtr<ID3D12PipelineState> State;
        D3D_PRIMITIVE_TOPOLOGY Topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    };

    class FD3D12CommandList final : public IRHICommandList {
    public:
        explicit FD3D12CommandList(FD3D12Device& InDevice) : Device(InDevice) {}

        /** Called by the device when a frame starts: forget everything set in the previous one. */
        void ResetForFrame(ID3D12GraphicsCommandList* InList);

        void BeginRenderPass(const FRHIRenderPassDesc& InPass) override;
        void EndRenderPass() override;
        void SetViewport(const FRHIViewport& InViewport) override;
        void SetScissor(const FRHIRect& InRect) override;
        void SetPipeline(IRHIPipeline* InPipeline) override;
        void SetVertexBuffer(IRHIBuffer* InBuffer) override;
        void SetIndexBuffer(IRHIBuffer* InBuffer) override;
        void SetVertexData(const void* InData, uint32_t InSize, uint32_t InStride) override;
        void SetConstants(ERHIConstantSlot InSlot, const void* InData, uint32_t InSize) override;
        void SetTexture(uint32_t InSlot, IRHITexture* InTexture) override;
        void Draw(uint32_t InVertexCount, uint32_t InFirstVertex) override;
        void DrawIndexed(uint32_t InIndexCount, uint32_t InFirstIndex, int32_t InBaseVertex,
                         uint32_t InInstanceCount) override;
        void PushMarker(const char* InName) override;
        void PopMarker() override;

    private:
        /** Uploads the texture table if a slot changed since the last draw. */
        void FlushTextures();
        bool CanDraw() const { return List && bInPass && bPipelineSet; }

        FD3D12Device& Device;
        ID3D12GraphicsCommandList* List = nullptr;
        bool bInPass = false;
        bool bPipelineSet = false;
        bool bTexturesDirty = true;
        /** True when the pass in progress opened a capture marker that EndRenderPass must close. */
        bool bPassMarker = false;
        std::array<FD3D12Texture*, kRHITextureSlots> Textures{};
        /** Targets of the pass in progress, returned to their rest state when it ends. */
        std::vector<FD3D12Texture*> PassTargets;
    };

    class FD3D12Swapchain final : public IRHISwapchain {
    public:
        FD3D12Swapchain(FD3D12Device& InDevice) : Device(InDevice) {}
        ~FD3D12Swapchain() override;
        bool Init(void* InWindowHandle, uint32_t InWidth, uint32_t InHeight);

        uint32_t GetWidth() const override { return Width; }
        uint32_t GetHeight() const override { return Height; }
        IRHITexture* GetBackBuffer() override;
        void Resize(uint32_t InWidth, uint32_t InHeight) override;
        void Present(bool bInVSync) override;

    private:
        bool AcquireBuffers();
        void ReleaseBuffers();

        FD3D12Device& Device;
        ComPtr<IDXGISwapChain3> SwapChain;
        std::vector<TRef<FD3D12Texture>> BackBuffers;
        uint32_t Width = 0;
        uint32_t Height = 0;
    };

    class FD3D12Device final : public IRHIDevice {
    public:
        FD3D12Device() = default;
        ~FD3D12Device() override;
        bool Init(const FRHIInitDesc& InDesc);

        const FRHIDeviceInfo& GetInfo() const override { return Info; }
        TRef<IRHIBuffer> CreateBuffer(const FRHIBufferDesc& InDesc, const void* InInitialData) override;
        void WriteBuffer(IRHIBuffer& InBuffer, const void* InData, uint64_t InSize, uint64_t InOffset) override;
        TRef<IRHITexture> CreateTexture(const FRHITextureDesc& InDesc) override;
        void UploadTexture(IRHITexture& InTexture, uint32_t InMipLevel, uint32_t InArraySlice, const void* InData,
                           uint32_t InRowPitch) override;
        bool ReadTexture(IRHITexture& InTexture, uint32_t InMipLevel, uint32_t InArraySlice,
                         std::vector<uint8_t>& OutPixels) override;
        TRef<IRHIShader> CreateShader(const FRHIShaderDesc& InDesc) override;
        TRef<IRHIPipeline> CreateGraphicsPipeline(const FRHIGraphicsPipelineDesc& InDesc) override;
        TScope<IRHISwapchain> CreateSwapchain(void* InWindowHandle, uint32_t InWidth, uint32_t InHeight) override;
        IRHICommandList& BeginFrame() override;
        void EndFrame() override;
        void WaitIdle() override;

        // --- Used by the other classes of the backend ---
        ID3D12Device* GetD3D() const { return Device.Get(); }
        IDXGIFactory4* GetFactory() const { return Factory.Get(); }
        ID3D12CommandQueue* GetQueue() const { return Queue.Get(); }

        /** Keeps a COM object alive until the GPU has finished the frames recorded so far. */
        void DeferRelease(ComPtr<IUnknown> InObject);
        /** Returns a descriptor to its pool once the GPU can no longer be using it. */
        void DeferFree(FD3D12DescriptorPool& InPool, uint32_t InIndex);

        FD3D12DescriptorPool& GetRtvPool() { return RtvPool; }
        FD3D12DescriptorPool& GetDsvPool() { return DsvPool; }
        FD3D12DescriptorPool& GetSrvPool() { return SrvPool; }
        D3D12_CPU_DESCRIPTOR_HANDLE GetNullSrv() const { return SrvPool.At(NullSrvIndex); }

        /** View of one mip of one slice, created on first use. */
        D3D12_CPU_DESCRIPTOR_HANDLE GetRenderTargetView(FD3D12Texture& InTexture, uint32_t InMip, uint32_t InSlice);
        D3D12_CPU_DESCRIPTOR_HANDLE GetDepthStencilView(FD3D12Texture& InTexture, uint32_t InSlice);

        /** Constant memory valid until the current frame has been executed. */
        D3D12_GPU_VIRTUAL_ADDRESS AllocateConstants(const void* InData, uint32_t InSize);
        /** Room for a 16-entry texture table in the shader-visible heap. False when the frame ran out. */
        bool AllocateTextureTable(D3D12_CPU_DESCRIPTOR_HANDLE& OutCpu, D3D12_GPU_DESCRIPTOR_HANDLE& OutGpu);

        /** Records a state change on InList if the texture is not already in InState. */
        static void Transition(ID3D12GraphicsCommandList* InList, FD3D12Texture& InTexture,
                               D3D12_RESOURCE_STATES InState);

        /** Runs GPU work right now and waits for it: uploads and readbacks. */
        ID3D12GraphicsCommandList* BeginImmediate();
        void EndImmediate();

        /** Wraps a swapchain image. */
        TRef<FD3D12Texture> WrapBackBuffer(ComPtr<ID3D12Resource> InResource, uint32_t InWidth, uint32_t InHeight);

        void DrainMessages();
        bool IsFrameOpen() const { return bFrameOpen; }

    private:
        struct FConstantPage {
            ComPtr<ID3D12Resource> Resource;
            uint8_t* Mapped = nullptr;
            D3D12_GPU_VIRTUAL_ADDRESS Address = 0;
        };

        struct FFrameContext {
            ComPtr<ID3D12CommandAllocator> Allocator;
            uint64_t FenceValue = 0;
            std::vector<FConstantPage> ConstantPages;
            uint32_t ConstantPage = 0;
            uint64_t ConstantOffset = 0;
            uint32_t DescriptorCursor = 0;
            std::vector<ComPtr<IUnknown>> Releases;
            std::vector<std::pair<FD3D12DescriptorPool*, uint32_t>> DescriptorFrees;
        };

        bool CreateRootSignature();
        bool AddConstantPage(FFrameContext& InFrame);
        void WaitForFence(uint64_t InValue);
        void FlushDeferred(FFrameContext& InFrame);
        FFrameContext& CurrentFrame() { return Frames[FrameIndex]; }

        FRHIDeviceInfo Info;
        ComPtr<IDXGIFactory4> Factory;
        ComPtr<ID3D12Device> Device;
        ComPtr<ID3D12InfoQueue> InfoQueue;
        ComPtr<ID3D12CommandQueue> Queue;
        ComPtr<ID3D12Fence> Fence;
        HANDLE FenceEvent = nullptr;
        uint64_t NextFenceValue = 1;

        ComPtr<ID3D12RootSignature> RootSignature;
        ComPtr<ID3D12DescriptorHeap> VisibleHeap;
        uint32_t VisibleStride = 0;

        FD3D12DescriptorPool RtvPool;
        FD3D12DescriptorPool DsvPool;
        FD3D12DescriptorPool SrvPool;
        uint32_t NullSrvIndex = kD3D12InvalidDescriptor;

        std::array<FFrameContext, kD3D12FrameCount> Frames;
        uint32_t FrameIndex = 0;
        bool bFrameOpen = false;

        ComPtr<ID3D12GraphicsCommandList> FrameList;
        TScope<FD3D12CommandList> CommandList;

        ComPtr<ID3D12CommandAllocator> ImmediateAllocator;
        ComPtr<ID3D12GraphicsCommandList> ImmediateList;

        friend class FD3D12CommandList;
    };

    /** The device, for destructors of resources that may outlive it. Null after shutdown. */
    FD3D12Device* GetD3D12Device();

    /** Logs a failed HRESULT with the call that produced it. Returns true when the call succeeded. */
    bool D3D12Check(HRESULT InResult, const char* InWhat);

} // namespace Leon
