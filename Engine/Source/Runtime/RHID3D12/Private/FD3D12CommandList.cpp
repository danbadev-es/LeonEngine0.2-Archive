#include "FD3D12Backend.hpp"

#include "Core/FLog.hpp"

#include <algorithm>
#include <cstring>

namespace Leon {

    void FD3D12CommandList::ResetForFrame(ID3D12GraphicsCommandList* InList) {
        List = InList;
        bInPass = false;
        bPipelineSet = false;
        bTexturesDirty = true;
        bPassMarker = false;
        Textures.fill(nullptr);
        PassTargets.clear();
    }

    void FD3D12CommandList::BeginRenderPass(const FRHIRenderPassDesc& InPass) {
        if (!List)
            return;
        if (bInPass)
            EndRenderPass();
        bPassMarker = InPass.Name && InPass.Name[0];
        if (bPassMarker)
            PushMarker(InPass.Name);

        D3D12_CPU_DESCRIPTOR_HANDLE colorViews[kRHIMaxColorTargets]{};
        const uint32_t colorCount = std::min(InPass.NumColorTargets, kRHIMaxColorTargets);
        uint32_t width = 0;
        uint32_t height = 0;

        // Make every target writable before any of them is touched.
        for (uint32_t i = 0; i < colorCount; ++i) {
            auto* texture = static_cast<FD3D12Texture*>(InPass.Color[i].Texture);
            if (!texture) {
                LE_CORE_ERROR("D3D12: render pass '{0}' has a null colour target", InPass.Name ? InPass.Name : "");
                continue;
            }
            FD3D12Device::Transition(List, *texture, D3D12_RESOURCE_STATE_RENDER_TARGET);
            PassTargets.push_back(texture);
            colorViews[i] = Device.GetRenderTargetView(*texture, InPass.Color[i].MipLevel, InPass.Color[i].ArraySlice);
            width = std::max(texture->Desc.Width >> InPass.Color[i].MipLevel, 1u);
            height = std::max(texture->Desc.Height >> InPass.Color[i].MipLevel, 1u);
        }
        auto* depth = static_cast<FD3D12Texture*>(InPass.Depth.Texture);
        D3D12_CPU_DESCRIPTOR_HANDLE depthView{};
        if (depth) {
            FD3D12Device::Transition(List, *depth, D3D12_RESOURCE_STATE_DEPTH_WRITE);
            PassTargets.push_back(depth);
            depthView = Device.GetDepthStencilView(*depth, InPass.Depth.ArraySlice);
            if (colorCount == 0) {
                width = depth->Desc.Width;
                height = depth->Desc.Height;
            }
        }

        List->OMSetRenderTargets(colorCount, colorCount ? colorViews : nullptr, FALSE, depth ? &depthView : nullptr);

        for (uint32_t i = 0; i < colorCount; ++i) {
            if (InPass.Color[i].Texture && InPass.Color[i].LoadOp == ERHILoadOp::Clear) {
                const float color[4] = {InPass.Color[i].ClearColor.r, InPass.Color[i].ClearColor.g,
                                        InPass.Color[i].ClearColor.b, InPass.Color[i].ClearColor.a};
                List->ClearRenderTargetView(colorViews[i], color, 0, nullptr);
            }
        }
        if (depth && InPass.Depth.LoadOp == ERHILoadOp::Clear) {
            D3D12_CLEAR_FLAGS flags = D3D12_CLEAR_FLAG_DEPTH;
            if (depth->Desc.Format == ERHIFormat::D24_UNorm_S8_UInt)
                flags |= D3D12_CLEAR_FLAG_STENCIL;
            List->ClearDepthStencilView(depthView, flags, InPass.Depth.ClearDepth, 0, 0, nullptr);
        }

        bInPass = true;
        SetViewport({0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f});
        SetScissor({0, 0, static_cast<int32_t>(width), static_cast<int32_t>(height)});
    }

    void FD3D12CommandList::EndRenderPass() {
        if (!List || !bInPass)
            return;
        for (FD3D12Texture* target : PassTargets)
            FD3D12Device::Transition(List, *target, target->RestState);
        PassTargets.clear();
        bInPass = false;
        if (bPassMarker)
            List->EndEvent();
        bPassMarker = false;
    }

    void FD3D12CommandList::SetViewport(const FRHIViewport& InViewport) {
        if (!List)
            return;
        const D3D12_VIEWPORT viewport{InViewport.X,     InViewport.Y,        InViewport.Width,
                                      InViewport.Height, InViewport.MinDepth, InViewport.MaxDepth};
        List->RSSetViewports(1, &viewport);
    }

    void FD3D12CommandList::SetScissor(const FRHIRect& InRect) {
        if (!List)
            return;
        const D3D12_RECT rect{InRect.Left, InRect.Top, InRect.Right, InRect.Bottom};
        List->RSSetScissorRects(1, &rect);
    }

    void FD3D12CommandList::SetPipeline(IRHIPipeline* InPipeline) {
        auto* pipeline = static_cast<FD3D12Pipeline*>(InPipeline);
        if (!List || !pipeline || !pipeline->State) {
            bPipelineSet = false;
            return;
        }
        List->SetPipelineState(pipeline->State.Get());
        List->IASetPrimitiveTopology(pipeline->Topology);
        bPipelineSet = true;
    }

    void FD3D12CommandList::SetVertexBuffer(IRHIBuffer* InBuffer) {
        auto* buffer = static_cast<FD3D12Buffer*>(InBuffer);
        if (!List || !buffer || !buffer->Resource)
            return;
        D3D12_VERTEX_BUFFER_VIEW view{};
        view.BufferLocation = buffer->Resource->GetGPUVirtualAddress();
        view.SizeInBytes = static_cast<UINT>(buffer->Desc.Size);
        view.StrideInBytes = buffer->Desc.Stride;
        List->IASetVertexBuffers(0, 1, &view);
    }

    void FD3D12CommandList::SetIndexBuffer(IRHIBuffer* InBuffer) {
        auto* buffer = static_cast<FD3D12Buffer*>(InBuffer);
        if (!List || !buffer || !buffer->Resource)
            return;
        D3D12_INDEX_BUFFER_VIEW view{};
        view.BufferLocation = buffer->Resource->GetGPUVirtualAddress();
        view.SizeInBytes = static_cast<UINT>(buffer->Desc.Size);
        view.Format = buffer->Desc.Stride == 2 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
        List->IASetIndexBuffer(&view);
    }

    void FD3D12CommandList::SetConstants(ERHIConstantSlot InSlot, const void* InData, uint32_t InSize) {
        if (!List || !InData || InSize == 0)
            return;
        const D3D12_GPU_VIRTUAL_ADDRESS address = Device.AllocateConstants(InData, InSize);
        if (address)
            List->SetGraphicsRootConstantBufferView(static_cast<UINT>(InSlot), address);
    }

    void FD3D12CommandList::SetTexture(uint32_t InSlot, IRHITexture* InTexture) {
        if (InSlot >= kRHITextureSlots)
            return;
        auto* texture = static_cast<FD3D12Texture*>(InTexture);
        if (Textures[InSlot] != texture) {
            Textures[InSlot] = texture;
            bTexturesDirty = true;
        }
    }

    void FD3D12CommandList::FlushTextures() {
        if (!bTexturesDirty)
            return;
        D3D12_CPU_DESCRIPTOR_HANDLE cpu{};
        D3D12_GPU_DESCRIPTOR_HANDLE gpu{};
        if (!Device.AllocateTextureTable(cpu, gpu))
            return;
        const UINT stride = Device.GetD3D()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        for (uint32_t slot = 0; slot < kRHITextureSlots; ++slot) {
            const FD3D12Texture* texture = Textures[slot];
            D3D12_CPU_DESCRIPTOR_HANDLE source = Device.GetNullSrv();
            if (texture) {
                if (texture->SrvIndex == kD3D12InvalidDescriptor) {
                    LE_CORE_ERROR("D3D12: texture '{0}' bound at slot {1} was not created as Sampled",
                                  texture->Desc.DebugName, slot);
                } else if ((texture->State & D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) == 0) {
                    LE_CORE_ERROR("D3D12: texture '{0}' bound at slot {1} is a target of the current pass",
                                  texture->Desc.DebugName, slot);
                } else {
                    source = Device.GetSrvPool().At(texture->SrvIndex);
                }
            }
            D3D12_CPU_DESCRIPTOR_HANDLE destination = cpu;
            destination.ptr += static_cast<SIZE_T>(slot) * stride;
            Device.GetD3D()->CopyDescriptorsSimple(1, destination, source, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        }
        List->SetGraphicsRootDescriptorTable(kD3D12TextureTableRootIndex, gpu);
        bTexturesDirty = false;
    }

    void FD3D12CommandList::Draw(uint32_t InVertexCount, uint32_t InFirstVertex) {
        if (!CanDraw())
            return;
        FlushTextures();
        List->DrawInstanced(InVertexCount, 1, InFirstVertex, 0);
    }

    void FD3D12CommandList::DrawIndexed(uint32_t InIndexCount, uint32_t InFirstIndex, int32_t InBaseVertex,
                                        uint32_t InInstanceCount) {
        if (!CanDraw())
            return;
        FlushTextures();
        List->DrawIndexedInstanced(InIndexCount, std::max(InInstanceCount, 1u), InFirstIndex, InBaseVertex, 0);
    }

    void FD3D12CommandList::PushMarker(const char* InName) {
        if (!List || !InName)
            return;
        // Metadata 1 = ANSI string, the encoding PIX and RenderDoc expect from BeginEvent.
        List->BeginEvent(1, InName, static_cast<UINT>(std::strlen(InName) + 1));
    }

    void FD3D12CommandList::PopMarker() {
        if (List)
            List->EndEvent();
    }

    // ---------------------------------------------------------------------------------------------
    // Swapchain
    // ---------------------------------------------------------------------------------------------

    FD3D12Swapchain::~FD3D12Swapchain() {
        Device.WaitIdle();
        ReleaseBuffers();
    }

    bool FD3D12Swapchain::Init(void* InWindowHandle, uint32_t InWidth, uint32_t InHeight) {
        if (!InWindowHandle) {
            LE_CORE_ERROR("D3D12: a swapchain needs a window handle");
            return false;
        }
        Width = std::max(InWidth, 1u);
        Height = std::max(InHeight, 1u);

        DXGI_SWAP_CHAIN_DESC1 desc{};
        desc.Width = Width;
        desc.Height = Height;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 2;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        desc.Scaling = DXGI_SCALING_STRETCH;
        desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

        const HWND window = static_cast<HWND>(InWindowHandle);
        ComPtr<IDXGISwapChain1> created;
        if (!D3D12Check(Device.GetFactory()->CreateSwapChainForHwnd(Device.GetQueue(), window, &desc, nullptr, nullptr,
                                                                    &created),
                        "CreateSwapChainForHwnd"))
            return false;
        // The engine handles fullscreen itself.
        Device.GetFactory()->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER);
        if (!D3D12Check(created.As(&SwapChain), "IDXGISwapChain3"))
            return false;
        return AcquireBuffers();
    }

    bool FD3D12Swapchain::AcquireBuffers() {
        BackBuffers.clear();
        for (UINT i = 0; i < 2; ++i) {
            ComPtr<ID3D12Resource> resource;
            if (!D3D12Check(SwapChain->GetBuffer(i, IID_PPV_ARGS(&resource)), "IDXGISwapChain::GetBuffer"))
                return false;
            BackBuffers.push_back(Device.WrapBackBuffer(std::move(resource), Width, Height));
        }
        return true;
    }

    void FD3D12Swapchain::ReleaseBuffers() {
        // The swapchain can only resize or die once nothing else references its images: drop the
        // wrappers and force the deferred releases through.
        for (TRef<FD3D12Texture>& buffer : BackBuffers) {
            for (const auto& [key, index] : buffer->RtvIndices)
                Device.GetRtvPool().Free(index);
            buffer->RtvIndices.clear();
            buffer->Resource.Reset();
        }
        BackBuffers.clear();
    }

    IRHITexture* FD3D12Swapchain::GetBackBuffer() {
        if (!SwapChain || BackBuffers.empty())
            return nullptr;
        return BackBuffers[SwapChain->GetCurrentBackBufferIndex()].get();
    }

    void FD3D12Swapchain::Resize(uint32_t InWidth, uint32_t InHeight) {
        if (!SwapChain || InWidth == 0 || InHeight == 0 || (InWidth == Width && InHeight == Height))
            return;
        Device.WaitIdle();
        ReleaseBuffers();
        if (!D3D12Check(SwapChain->ResizeBuffers(2, InWidth, InHeight, DXGI_FORMAT_R8G8B8A8_UNORM, 0),
                        "IDXGISwapChain::ResizeBuffers"))
            return;
        Width = InWidth;
        Height = InHeight;
        AcquireBuffers();
    }

    void FD3D12Swapchain::Present(bool bInVSync) {
        if (SwapChain)
            SwapChain->Present(bInVSync ? 1 : 0, 0);
    }

} // namespace Leon
