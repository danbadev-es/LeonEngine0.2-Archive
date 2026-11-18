#include "FD3D12Backend.hpp"

#include "RHID3D12/FD3D12RHI.hpp"

#include "Core/FLog.hpp"
#include "RHI/FRenderer.hpp"

#include <d3d12sdklayers.h>

#include <algorithm>
#include <cstring>
#include <string>

namespace Leon {

    namespace {

        FD3D12Device* GDevice = nullptr;
        uint64_t GValidationErrors = 0;

        std::string Narrow(const wchar_t* InText) {
            std::string out;
            for (const wchar_t* c = InText; c && *c; ++c)
                out.push_back(*c < 128 ? static_cast<char>(*c) : '?');
            return out;
        }

        std::wstring Widen(const std::string& InText) {
            return std::wstring(InText.begin(), InText.end());
        }

        void SetName(ID3D12Object* InObject, const std::string& InName) {
            if (InObject && !InName.empty())
                InObject->SetName(Widen(InName).c_str());
        }

        constexpr D3D12_RESOURCE_STATES kShaderRead =
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

        D3D12_HEAP_PROPERTIES HeapProperties(D3D12_HEAP_TYPE InType) {
            D3D12_HEAP_PROPERTIES properties{};
            properties.Type = InType;
            properties.CreationNodeMask = 1;
            properties.VisibleNodeMask = 1;
            return properties;
        }

        D3D12_RESOURCE_DESC BufferDesc(uint64_t InSize) {
            D3D12_RESOURCE_DESC desc{};
            desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            desc.Width = std::max<uint64_t>(InSize, 1);
            desc.Height = 1;
            desc.DepthOrArraySize = 1;
            desc.MipLevels = 1;
            desc.SampleDesc.Count = 1;
            desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            return desc;
        }

        D3D12_COMPARISON_FUNC ToD3D(ERHICompare InCompare) {
            switch (InCompare) {
            case ERHICompare::Never:
                return D3D12_COMPARISON_FUNC_NEVER;
            case ERHICompare::Less:
                return D3D12_COMPARISON_FUNC_LESS;
            case ERHICompare::Equal:
                return D3D12_COMPARISON_FUNC_EQUAL;
            case ERHICompare::LessEqual:
                return D3D12_COMPARISON_FUNC_LESS_EQUAL;
            case ERHICompare::Greater:
                return D3D12_COMPARISON_FUNC_GREATER;
            case ERHICompare::NotEqual:
                return D3D12_COMPARISON_FUNC_NOT_EQUAL;
            case ERHICompare::GreaterEqual:
                return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
            case ERHICompare::Always:
                return D3D12_COMPARISON_FUNC_ALWAYS;
            }
            return D3D12_COMPARISON_FUNC_LESS;
        }

        D3D12_BLEND ToD3D(ERHIBlend InBlend) {
            switch (InBlend) {
            case ERHIBlend::Zero:
                return D3D12_BLEND_ZERO;
            case ERHIBlend::One:
                return D3D12_BLEND_ONE;
            case ERHIBlend::SrcColor:
                return D3D12_BLEND_SRC_COLOR;
            case ERHIBlend::InvSrcColor:
                return D3D12_BLEND_INV_SRC_COLOR;
            case ERHIBlend::SrcAlpha:
                return D3D12_BLEND_SRC_ALPHA;
            case ERHIBlend::InvSrcAlpha:
                return D3D12_BLEND_INV_SRC_ALPHA;
            case ERHIBlend::DstAlpha:
                return D3D12_BLEND_DEST_ALPHA;
            case ERHIBlend::InvDstAlpha:
                return D3D12_BLEND_INV_DEST_ALPHA;
            case ERHIBlend::DstColor:
                return D3D12_BLEND_DEST_COLOR;
            case ERHIBlend::InvDstColor:
                return D3D12_BLEND_INV_DEST_COLOR;
            }
            return D3D12_BLEND_ONE;
        }

        DXGI_FORMAT ToD3D(ERHIVertexFormat InFormat) {
            switch (InFormat) {
            case ERHIVertexFormat::Float:
                return DXGI_FORMAT_R32_FLOAT;
            case ERHIVertexFormat::Float2:
                return DXGI_FORMAT_R32G32_FLOAT;
            case ERHIVertexFormat::Float3:
                return DXGI_FORMAT_R32G32B32_FLOAT;
            case ERHIVertexFormat::Float4:
                return DXGI_FORMAT_R32G32B32A32_FLOAT;
            case ERHIVertexFormat::Int4:
                return DXGI_FORMAT_R32G32B32A32_SINT;
            case ERHIVertexFormat::UByte4_UNorm:
                return DXGI_FORMAT_R8G8B8A8_UNORM;
            }
            return DXGI_FORMAT_UNKNOWN;
        }

    } // namespace

    FD3D12Device* GetD3D12Device() {
        return GDevice;
    }

    bool D3D12Check(HRESULT InResult, const char* InWhat) {
        if (SUCCEEDED(InResult))
            return true;
        LE_CORE_ERROR("D3D12: {0} failed (HRESULT 0x{1:08X})", InWhat, static_cast<uint32_t>(InResult));
        return false;
    }

    DXGI_FORMAT D3D12ResourceFormat(ERHIFormat InFormat) {
        switch (InFormat) {
        case ERHIFormat::R8_UNorm:
            return DXGI_FORMAT_R8_UNORM;
        case ERHIFormat::RGBA8_UNorm:
            return DXGI_FORMAT_R8G8B8A8_UNORM;
        case ERHIFormat::RGBA8_UNorm_sRGB:
            return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        case ERHIFormat::BGRA8_UNorm:
            return DXGI_FORMAT_B8G8R8A8_UNORM;
        case ERHIFormat::RG16_Float:
            return DXGI_FORMAT_R16G16_FLOAT;
        case ERHIFormat::RGBA16_Float:
            return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case ERHIFormat::RGBA32_Float:
            return DXGI_FORMAT_R32G32B32A32_FLOAT;
        case ERHIFormat::R32_Float:
            return DXGI_FORMAT_R32_FLOAT;
        case ERHIFormat::R32_SInt:
            return DXGI_FORMAT_R32_SINT;
        // Depth resources are typeless so the same memory can be a depth target and a shader input.
        case ERHIFormat::D32_Float:
            return DXGI_FORMAT_R32_TYPELESS;
        case ERHIFormat::D24_UNorm_S8_UInt:
            return DXGI_FORMAT_R24G8_TYPELESS;
        case ERHIFormat::Unknown:
            return DXGI_FORMAT_UNKNOWN;
        }
        return DXGI_FORMAT_UNKNOWN;
    }

    DXGI_FORMAT D3D12ViewFormat(ERHIFormat InFormat) {
        switch (InFormat) {
        case ERHIFormat::D32_Float:
            return DXGI_FORMAT_D32_FLOAT;
        case ERHIFormat::D24_UNorm_S8_UInt:
            return DXGI_FORMAT_D24_UNORM_S8_UINT;
        default:
            return D3D12ResourceFormat(InFormat);
        }
    }

    DXGI_FORMAT D3D12ShaderReadFormat(ERHIFormat InFormat) {
        switch (InFormat) {
        case ERHIFormat::D32_Float:
            return DXGI_FORMAT_R32_FLOAT;
        case ERHIFormat::D24_UNorm_S8_UInt:
            return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
        default:
            return D3D12ResourceFormat(InFormat);
        }
    }

    // ---------------------------------------------------------------------------------------------
    // Descriptor pool
    // ---------------------------------------------------------------------------------------------

    bool FD3D12DescriptorPool::Init(ID3D12Device* InDevice, D3D12_DESCRIPTOR_HEAP_TYPE InType, uint32_t InCapacity) {
        D3D12_DESCRIPTOR_HEAP_DESC desc{};
        desc.Type = InType;
        desc.NumDescriptors = InCapacity;
        if (!D3D12Check(InDevice->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&Heap)), "CreateDescriptorHeap"))
            return false;
        Start = Heap->GetCPUDescriptorHandleForHeapStart();
        Stride = InDevice->GetDescriptorHandleIncrementSize(InType);
        Capacity = InCapacity;
        return true;
    }

    uint32_t FD3D12DescriptorPool::Allocate() {
        if (!FreeList.empty()) {
            const uint32_t index = FreeList.back();
            FreeList.pop_back();
            return index;
        }
        if (Next >= Capacity) {
            LE_CORE_ERROR("D3D12: descriptor pool exhausted ({0} descriptors)", Capacity);
            return kD3D12InvalidDescriptor;
        }
        return Next++;
    }

    void FD3D12DescriptorPool::Free(uint32_t InIndex) {
        if (InIndex != kD3D12InvalidDescriptor)
            FreeList.push_back(InIndex);
    }

    D3D12_CPU_DESCRIPTOR_HANDLE FD3D12DescriptorPool::At(uint32_t InIndex) const {
        D3D12_CPU_DESCRIPTOR_HANDLE handle = Start;
        handle.ptr += static_cast<SIZE_T>(InIndex) * Stride;
        return handle;
    }

    // ---------------------------------------------------------------------------------------------
    // Resources: release through the device so the GPU never sees a freed object
    // ---------------------------------------------------------------------------------------------

    namespace {

        // Video memory by kind and by debug name, for the statistics overlay and the quality budget.
        // An estimate from the description: what the driver really reserves is not known here.

        EGPUMemoryCategory MemoryCategory(const FRHIBufferDesc& InDesc) {
            switch (InDesc.Usage) {
            case ERHIBufferUsage::Vertex:
                return EGPUMemoryCategory::VertexBuffer;
            case ERHIBufferUsage::Index:
                return EGPUMemoryCategory::IndexBuffer;
            case ERHIBufferUsage::Constant:
                break;
            }
            return EGPUMemoryCategory::UniformBuffer;
        }

        EGPUMemoryCategory MemoryCategory(const FRHITextureDesc& InDesc) {
            if ((InDesc.Usage & (RHITextureUsage_RenderTarget | RHITextureUsage_DepthStencil)) != 0)
                return EGPUMemoryCategory::Framebuffer;
            const bool bCube = InDesc.Dimension == ERHITextureDimension::TextureCube ||
                               InDesc.Dimension == ERHITextureDimension::TextureCubeArray;
            return bCube ? EGPUMemoryCategory::TextureCube : EGPUMemoryCategory::Texture2D;
        }

        uint64_t MemoryBytes(const FRHITextureDesc& InDesc) {
            uint64_t texels = 0;
            for (uint32_t mip = 0; mip < std::max(InDesc.MipLevels, 1u); ++mip)
                texels += static_cast<uint64_t>(std::max(InDesc.Width >> mip, 1u)) * std::max(InDesc.Height >> mip, 1u);
            return texels * RHIFormatBytesPerPixel(InDesc.Format) * std::max(InDesc.SliceCount(), 1u);
        }

        void TrackMemory(FD3D12Buffer& InBuffer) {
            InBuffer.TrackedBytes = InBuffer.Desc.Size;
            FRenderer::OnGPUAlloc(static_cast<size_t>(InBuffer.TrackedBytes), MemoryCategory(InBuffer.Desc),
                                  InBuffer.Desc.DebugName.c_str());
        }

        void TrackMemory(FD3D12Texture& InTexture) {
            InTexture.TrackedBytes = MemoryBytes(InTexture.Desc);
            FRenderer::OnGPUAlloc(static_cast<size_t>(InTexture.TrackedBytes), MemoryCategory(InTexture.Desc),
                                  InTexture.Desc.DebugName.c_str());
        }

    } // namespace

    FD3D12Buffer::~FD3D12Buffer() {
        if (TrackedBytes > 0)
            FRenderer::OnGPUFree(static_cast<size_t>(TrackedBytes), MemoryCategory(Desc), Desc.DebugName.c_str());
        if (FD3D12Device* device = GetD3D12Device())
            device->DeferRelease(Resource);
    }

    FD3D12Texture::~FD3D12Texture() {
        if (TrackedBytes > 0)
            FRenderer::OnGPUFree(static_cast<size_t>(TrackedBytes), MemoryCategory(Desc), Desc.DebugName.c_str());
        FD3D12Device* device = GetD3D12Device();
        if (!device)
            return;
        device->DeferRelease(Resource);
        device->DeferFree(device->GetSrvPool(), SrvIndex);
        for (const auto& [key, index] : RtvIndices)
            device->DeferFree(device->GetRtvPool(), index);
        for (const auto& [key, index] : DsvIndices)
            device->DeferFree(device->GetDsvPool(), index);
        for (const auto& [key, index] : MipSrvIndices)
            device->DeferFree(device->GetSrvPool(), index);
    }

    FD3D12Pipeline::~FD3D12Pipeline() {
        if (FD3D12Device* device = GetD3D12Device())
            device->DeferRelease(State);
    }

    // ---------------------------------------------------------------------------------------------
    // Device
    // ---------------------------------------------------------------------------------------------

    bool FD3D12Device::Init(const FRHIInitDesc& InDesc) {
        UINT factoryFlags = 0;
        if (InDesc.bEnableDebugLayer) {
            ComPtr<ID3D12Debug> debug;
            if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
                debug->EnableDebugLayer();
                factoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
            } else {
                LE_CORE_WARN("D3D12: the debug layer is not installed (Windows optional feature 'Graphics Tools')");
            }
        }
        if (!D3D12Check(CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&Factory)), "CreateDXGIFactory2")) {
            // The debug factory needs the same optional feature; retry without it.
            if (!D3D12Check(CreateDXGIFactory2(0, IID_PPV_ARGS(&Factory)), "CreateDXGIFactory2 (no debug)"))
                return false;
        }

        ComPtr<IDXGIAdapter1> adapter;
        if (!InDesc.bUseSoftwareAdapter) {
            ComPtr<IDXGIFactory6> factory6;
            if (SUCCEEDED(Factory.As(&factory6))) {
                for (UINT i = 0; factory6->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                                                      IID_PPV_ARGS(&adapter)) != DXGI_ERROR_NOT_FOUND;
                     ++i) {
                    DXGI_ADAPTER_DESC1 desc{};
                    adapter->GetDesc1(&desc);
                    if (!(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) &&
                        SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device),
                                                    nullptr)))
                        break;
                    adapter.Reset();
                }
            }
        }
        bool bSoftware = false;
        if (!adapter) {
            if (!InDesc.bUseSoftwareAdapter)
                LE_CORE_WARN("D3D12: no hardware adapter supports Direct3D 12; using the software adapter (WARP)");
            if (!D3D12Check(Factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)), "EnumWarpAdapter"))
                return false;
            bSoftware = true;
        }
        if (!D3D12Check(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&Device)),
                        "D3D12CreateDevice"))
            return false;

        DXGI_ADAPTER_DESC1 adapterDesc{};
        adapter->GetDesc1(&adapterDesc);
        Info.Backend = ERHIBackend::D3D12;
        Info.AdapterName = Narrow(adapterDesc.Description);
        Info.DedicatedVideoMemoryBytes = adapterDesc.DedicatedVideoMemory;
        Info.bSoftwareAdapter = bSoftware;

        if (InDesc.bEnableDebugLayer && SUCCEEDED(Device.As(&InfoQueue))) {
            // Clearing to a colour other than the one given at creation is legal, only slower.
            D3D12_MESSAGE_ID muted[] = {D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE,
                                        D3D12_MESSAGE_ID_CLEARDEPTHSTENCILVIEW_MISMATCHINGCLEARVALUE};
            D3D12_INFO_QUEUE_FILTER filter{};
            filter.DenyList.NumIDs = static_cast<UINT>(std::size(muted));
            filter.DenyList.pIDList = muted;
            InfoQueue->AddStorageFilterEntries(&filter);
        }

        D3D12_COMMAND_QUEUE_DESC queueDesc{};
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        if (!D3D12Check(Device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&Queue)), "CreateCommandQueue"))
            return false;
        if (!D3D12Check(Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&Fence)), "CreateFence"))
            return false;
        FenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!FenceEvent)
            return false;

        // GPU timers are a diagnostic: without them the device still works and reports zeros.
        GpuOpenInterval.fill(-1);
        const UINT timestampCount = kD3D12FrameCount * kD3D12MaxGpuIntervals * 2;
        D3D12_QUERY_HEAP_DESC timerHeapDesc{};
        timerHeapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
        timerHeapDesc.Count = timestampCount;
        const D3D12_HEAP_PROPERTIES timerReadbackHeap = HeapProperties(D3D12_HEAP_TYPE_READBACK);
        const D3D12_RESOURCE_DESC timerReadbackDesc = BufferDesc(timestampCount * sizeof(uint64_t));
        if (FAILED(Queue->GetTimestampFrequency(&GpuTimerFrequency)) || GpuTimerFrequency == 0 ||
            FAILED(Device->CreateQueryHeap(&timerHeapDesc, IID_PPV_ARGS(&GpuTimerHeap))) ||
            FAILED(Device->CreateCommittedResource(&timerReadbackHeap, D3D12_HEAP_FLAG_NONE, &timerReadbackDesc,
                                                   D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                   IID_PPV_ARGS(&GpuTimerReadback)))) {
            GpuTimerHeap.Reset();
            GpuTimerReadback.Reset();
            GpuTimerFrequency = 0;
        }

        if (!RtvPool.Init(Device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2048) ||
            !DsvPool.Init(Device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1024) ||
            !SrvPool.Init(Device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 8192))
            return false;

        D3D12_DESCRIPTOR_HEAP_DESC visibleDesc{};
        visibleDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        visibleDesc.NumDescriptors = kD3D12FrameCount * kD3D12DescriptorsPerFrame;
        visibleDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (!D3D12Check(Device->CreateDescriptorHeap(&visibleDesc, IID_PPV_ARGS(&VisibleHeap)),
                        "CreateDescriptorHeap (shader visible)"))
            return false;
        VisibleStride = Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        // What an unbound texture slot reads: black.
        NullSrvIndex = SrvPool.Allocate();
        D3D12_SHADER_RESOURCE_VIEW_DESC nullDesc{};
        nullDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        nullDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        nullDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        nullDesc.Texture2D.MipLevels = 1;
        Device->CreateShaderResourceView(nullptr, &nullDesc, SrvPool.At(NullSrvIndex));

        if (!CreateRootSignature())
            return false;

        for (FFrameContext& frame : Frames) {
            if (!D3D12Check(Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&frame.Allocator)),
                            "CreateCommandAllocator"))
                return false;
            if (!AddConstantPage(frame))
                return false;
        }
        if (!D3D12Check(Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, Frames[0].Allocator.Get(), nullptr,
                                                  IID_PPV_ARGS(&FrameList)),
                        "CreateCommandList"))
            return false;
        FrameList->Close();

        if (!D3D12Check(Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&ImmediateAllocator)),
                        "CreateCommandAllocator (immediate)") ||
            !D3D12Check(Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, ImmediateAllocator.Get(), nullptr,
                                                  IID_PPV_ARGS(&ImmediateList)),
                        "CreateCommandList (immediate)"))
            return false;
        ImmediateList->Close();

        CommandList = CreateScope<FD3D12CommandList>(*this);
        GValidationErrors = 0;
        GDevice = this;
        return true;
    }

    FD3D12Device::~FD3D12Device() {
        if (Queue && Fence)
            WaitIdle();
        for (FFrameContext& frame : Frames)
            FlushDeferred(frame);
        DrainMessages();
        GDevice = nullptr;
        if (FenceEvent)
            CloseHandle(FenceEvent);
    }

    bool FD3D12Device::CreateRootSignature() {
        // One layout for every pipeline: five constant buffers (b0..b4), one table of sixteen textures
        // (t0..t15) and five static samplers (s0..s4). See ERHIConstantSlot and ERHIStaticSampler.
        D3D12_ROOT_PARAMETER parameters[kRHIConstantSlots + 1]{};
        for (uint32_t i = 0; i < kRHIConstantSlots; ++i) {
            parameters[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
            parameters[i].Descriptor.ShaderRegister = i;
            parameters[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        }
        D3D12_DESCRIPTOR_RANGE range{};
        range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        range.NumDescriptors = kRHITextureSlots;
        range.BaseShaderRegister = 0;
        range.OffsetInDescriptorsFromTableStart = 0;
        parameters[kD3D12TextureTableRootIndex].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameters[kD3D12TextureTableRootIndex].DescriptorTable.NumDescriptorRanges = 1;
        parameters[kD3D12TextureTableRootIndex].DescriptorTable.pDescriptorRanges = &range;
        parameters[kD3D12TextureTableRootIndex].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_STATIC_SAMPLER_DESC samplers[5]{};
        for (uint32_t i = 0; i < 5; ++i) {
            samplers[i].ShaderRegister = i;
            samplers[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
            samplers[i].MaxLOD = D3D12_FLOAT32_MAX;
            samplers[i].MaxAnisotropy = 1;
            samplers[i].ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
            samplers[i].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
        }
        const auto address = [](D3D12_STATIC_SAMPLER_DESC& InSampler, D3D12_TEXTURE_ADDRESS_MODE InMode) {
            InSampler.AddressU = InSampler.AddressV = InSampler.AddressW = InMode;
        };
        samplers[0].Filter = D3D12_FILTER_ANISOTROPIC; // LinearWrap
        samplers[0].MaxAnisotropy = 8;
        address(samplers[0], D3D12_TEXTURE_ADDRESS_MODE_WRAP);
        samplers[1].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR; // LinearClamp
        address(samplers[1], D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
        samplers[2].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT; // PointClamp
        address(samplers[2], D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
        samplers[3].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT; // PointWrap
        address(samplers[3], D3D12_TEXTURE_ADDRESS_MODE_WRAP);
        samplers[4].Filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT; // ShadowCompare
        samplers[4].ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
        // Clamped, not bordered: shaders reject lookups outside the map themselves, and a lit border
        // would bleed into the filtered samples next to the map's edge.
        address(samplers[4], D3D12_TEXTURE_ADDRESS_MODE_CLAMP);

        D3D12_ROOT_SIGNATURE_DESC desc{};
        desc.NumParameters = static_cast<UINT>(std::size(parameters));
        desc.pParameters = parameters;
        desc.NumStaticSamplers = static_cast<UINT>(std::size(samplers));
        desc.pStaticSamplers = samplers;
        desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        ComPtr<ID3DBlob> blob;
        ComPtr<ID3DBlob> error;
        if (FAILED(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error))) {
            LE_CORE_ERROR("D3D12: root signature: {0}",
                          error ? static_cast<const char*>(error->GetBufferPointer()) : "serialization failed");
            return false;
        }
        return D3D12Check(Device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
                                                      IID_PPV_ARGS(&RootSignature)),
                          "CreateRootSignature");
    }

    bool FD3D12Device::AddConstantPage(FFrameContext& InFrame) {
        FConstantPage page;
        const D3D12_HEAP_PROPERTIES heap = HeapProperties(D3D12_HEAP_TYPE_UPLOAD);
        const D3D12_RESOURCE_DESC desc = BufferDesc(kD3D12ConstantPageSize);
        if (!D3D12Check(Device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                        IID_PPV_ARGS(&page.Resource)),
                        "CreateCommittedResource (constants)"))
            return false;
        void* mapped = nullptr;
        if (!D3D12Check(page.Resource->Map(0, nullptr, &mapped), "Map (constants)"))
            return false;
        page.Mapped = static_cast<uint8_t*>(mapped);
        page.Address = page.Resource->GetGPUVirtualAddress();
        InFrame.ConstantPages.push_back(std::move(page));
        return true;
    }

    D3D12_GPU_VIRTUAL_ADDRESS FD3D12Device::AllocateConstants(const void* InData, uint32_t InSize) {
        // Constant buffer views must be multiples of 256 bytes.
        const uint64_t aligned = (static_cast<uint64_t>(InSize) + 255ull) & ~255ull;
        if (aligned > kD3D12ConstantPageSize) {
            LE_CORE_ERROR("D3D12: constant block of {0} bytes is larger than a page", InSize);
            return 0;
        }
        FFrameContext& frame = CurrentFrame();
        if (frame.ConstantOffset + aligned > kD3D12ConstantPageSize) {
            ++frame.ConstantPage;
            frame.ConstantOffset = 0;
            if (frame.ConstantPage >= frame.ConstantPages.size() && !AddConstantPage(frame))
                return 0;
        }
        FConstantPage& page = frame.ConstantPages[frame.ConstantPage];
        std::memcpy(page.Mapped + frame.ConstantOffset, InData, InSize);
        if (aligned > InSize)
            std::memset(page.Mapped + frame.ConstantOffset + InSize, 0, static_cast<size_t>(aligned - InSize));
        const D3D12_GPU_VIRTUAL_ADDRESS address = page.Address + frame.ConstantOffset;
        frame.ConstantOffset += aligned;
        return address;
    }

    bool FD3D12Device::AllocateTextureTable(D3D12_CPU_DESCRIPTOR_HANDLE& OutCpu, D3D12_GPU_DESCRIPTOR_HANDLE& OutGpu) {
        FFrameContext& frame = CurrentFrame();
        if (frame.DescriptorCursor + kRHITextureSlots > kD3D12DescriptorsPerFrame) {
            LE_CORE_ERROR("D3D12: out of shader-visible descriptors this frame ({0})", kD3D12DescriptorsPerFrame);
            return false;
        }
        const uint64_t index = static_cast<uint64_t>(FrameIndex) * kD3D12DescriptorsPerFrame + frame.DescriptorCursor;
        frame.DescriptorCursor += kRHITextureSlots;
        OutCpu = VisibleHeap->GetCPUDescriptorHandleForHeapStart();
        OutCpu.ptr += static_cast<SIZE_T>(index * VisibleStride);
        OutGpu = VisibleHeap->GetGPUDescriptorHandleForHeapStart();
        OutGpu.ptr += index * VisibleStride;
        return true;
    }

    void FD3D12Device::Transition(ID3D12GraphicsCommandList* InList, FD3D12Texture& InTexture,
                                  D3D12_RESOURCE_STATES InState) {
        if (InTexture.State == InState || !InTexture.Resource)
            return;
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = InTexture.Resource.Get();
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = InTexture.State;
        barrier.Transition.StateAfter = InState;
        InList->ResourceBarrier(1, &barrier);
        InTexture.State = InState;
    }

    void FD3D12Device::WaitForFence(uint64_t InValue) {
        if (Fence->GetCompletedValue() >= InValue)
            return;
        if (SUCCEEDED(Fence->SetEventOnCompletion(InValue, FenceEvent)))
            WaitForSingleObject(FenceEvent, INFINITE);
    }

    void FD3D12Device::WaitIdle() {
        const uint64_t value = NextFenceValue++;
        if (SUCCEEDED(Queue->Signal(Fence.Get(), value)))
            WaitForFence(value);
    }

    void FD3D12Device::DeferRelease(ComPtr<IUnknown> InObject) {
        if (InObject)
            CurrentFrame().Releases.push_back(std::move(InObject));
    }

    void FD3D12Device::DeferFree(FD3D12DescriptorPool& InPool, uint32_t InIndex) {
        if (InIndex != kD3D12InvalidDescriptor)
            CurrentFrame().DescriptorFrees.emplace_back(&InPool, InIndex);
    }

    void FD3D12Device::FlushDeferred(FFrameContext& InFrame) {
        InFrame.Releases.clear();
        for (const auto& [pool, index] : InFrame.DescriptorFrees)
            pool->Free(index);
        InFrame.DescriptorFrees.clear();
    }

    ID3D12GraphicsCommandList* FD3D12Device::BeginImmediate() {
        ImmediateAllocator->Reset();
        ImmediateList->Reset(ImmediateAllocator.Get(), nullptr);
        return ImmediateList.Get();
    }

    void FD3D12Device::EndImmediate() {
        ImmediateList->Close();
        ID3D12CommandList* lists[] = {ImmediateList.Get()};
        Queue->ExecuteCommandLists(1, lists);
        WaitIdle();
    }

    // --- Buffers ---------------------------------------------------------------------------------

    TRef<IRHIBuffer> FD3D12Device::CreateBuffer(const FRHIBufferDesc& InDesc, const void* InInitialData) {
        auto buffer = CreateRef<FD3D12Buffer>();
        buffer->Desc = InDesc;
        const D3D12_RESOURCE_DESC desc = BufferDesc(InDesc.Size);

        if (InDesc.bDynamic) {
            const D3D12_HEAP_PROPERTIES heap = HeapProperties(D3D12_HEAP_TYPE_UPLOAD);
            if (!D3D12Check(Device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                            IID_PPV_ARGS(&buffer->Resource)),
                            "CreateCommittedResource (dynamic buffer)"))
                return nullptr;
            void* mapped = nullptr;
            if (!D3D12Check(buffer->Resource->Map(0, nullptr, &mapped), "Map (dynamic buffer)"))
                return nullptr;
            buffer->Mapped = static_cast<uint8_t*>(mapped);
            if (InInitialData)
                std::memcpy(buffer->Mapped, InInitialData, static_cast<size_t>(InDesc.Size));
            SetName(buffer->Resource.Get(), InDesc.DebugName);
            TrackMemory(*buffer);
            return buffer;
        }

        // Buffers start in COMMON and are promoted by their first use (copy destination, then vertex
        // or index buffer), decaying back when the command list finishes: no barriers needed.
        const D3D12_HEAP_PROPERTIES heap = HeapProperties(D3D12_HEAP_TYPE_DEFAULT);
        if (!D3D12Check(Device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                        D3D12_RESOURCE_STATE_COMMON, nullptr,
                                                        IID_PPV_ARGS(&buffer->Resource)),
                        "CreateCommittedResource (buffer)"))
            return nullptr;
        SetName(buffer->Resource.Get(), InDesc.DebugName);

        if (InInitialData) {
            ComPtr<ID3D12Resource> staging;
            const D3D12_HEAP_PROPERTIES uploadHeap = HeapProperties(D3D12_HEAP_TYPE_UPLOAD);
            if (!D3D12Check(Device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &desc,
                                                            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                            IID_PPV_ARGS(&staging)),
                            "CreateCommittedResource (buffer staging)"))
                return nullptr;
            void* mapped = nullptr;
            if (!D3D12Check(staging->Map(0, nullptr, &mapped), "Map (buffer staging)"))
                return nullptr;
            std::memcpy(mapped, InInitialData, static_cast<size_t>(InDesc.Size));
            staging->Unmap(0, nullptr);

            ID3D12GraphicsCommandList* list = BeginImmediate();
            list->CopyBufferRegion(buffer->Resource.Get(), 0, staging.Get(), 0, InDesc.Size);
            EndImmediate();
        }
        TrackMemory(*buffer);
        return buffer;
    }

    void FD3D12Device::WriteBuffer(IRHIBuffer& InBuffer, const void* InData, uint64_t InSize, uint64_t InOffset) {
        auto& buffer = static_cast<FD3D12Buffer&>(InBuffer);
        if (!buffer.Mapped) {
            LE_CORE_ERROR("D3D12: WriteBuffer on '{0}', which was not created with bDynamic", buffer.Desc.DebugName);
            return;
        }
        if (!InData || InOffset + InSize > buffer.Desc.Size)
            return;
        std::memcpy(buffer.Mapped + InOffset, InData, static_cast<size_t>(InSize));
    }

    // --- Textures --------------------------------------------------------------------------------

    TRef<IRHITexture> FD3D12Device::CreateTexture(const FRHITextureDesc& InDesc) {
        auto texture = CreateRef<FD3D12Texture>();
        texture->Desc = InDesc;
        const bool bDepth = (InDesc.Usage & RHITextureUsage_DepthStencil) != 0;
        const bool bTarget = (InDesc.Usage & RHITextureUsage_RenderTarget) != 0;
        const bool bSampled = (InDesc.Usage & RHITextureUsage_Sampled) != 0;

        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = std::max(InDesc.Width, 1u);
        desc.Height = std::max(InDesc.Height, 1u);
        desc.DepthOrArraySize = static_cast<UINT16>(std::max(InDesc.SliceCount(), 1u));
        desc.MipLevels = static_cast<UINT16>(std::max(InDesc.MipLevels, 1u));
        desc.Format = D3D12ResourceFormat(InDesc.Format);
        desc.SampleDesc.Count = 1;
        if (bTarget)
            desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        if (bDepth) {
            desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
            if (!bSampled)
                desc.Flags |= D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
        }

        D3D12_CLEAR_VALUE clear{};
        clear.Format = D3D12ViewFormat(InDesc.Format);
        if (bDepth) {
            clear.DepthStencil.Depth = InDesc.ClearDepth;
        } else {
            clear.Color[0] = InDesc.ClearColor.r;
            clear.Color[1] = InDesc.ClearColor.g;
            clear.Color[2] = InDesc.ClearColor.b;
            clear.Color[3] = InDesc.ClearColor.a;
        }

        // A depth-only texture that is never sampled rests writable; everything else rests readable.
        texture->RestState = (bDepth && !bSampled) ? D3D12_RESOURCE_STATE_DEPTH_WRITE : kShaderRead;
        texture->State = texture->RestState;
        const D3D12_HEAP_PROPERTIES heap = HeapProperties(D3D12_HEAP_TYPE_DEFAULT);
        if (!D3D12Check(Device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, texture->State,
                                                        (bTarget || bDepth) ? &clear : nullptr,
                                                        IID_PPV_ARGS(&texture->Resource)),
                        "CreateCommittedResource (texture)"))
            return nullptr;
        SetName(texture->Resource.Get(), InDesc.DebugName);

        if (bSampled) {
            texture->SrvIndex = SrvPool.Allocate();
            if (texture->SrvIndex == kD3D12InvalidDescriptor)
                return nullptr;
            D3D12_SHADER_RESOURCE_VIEW_DESC view{};
            view.Format = D3D12ShaderReadFormat(InDesc.Format);
            view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            switch (InDesc.Dimension) {
            case ERHITextureDimension::Texture2D:
                view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                view.Texture2D.MipLevels = desc.MipLevels;
                break;
            case ERHITextureDimension::Texture2DArray:
                view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
                view.Texture2DArray.MipLevels = desc.MipLevels;
                view.Texture2DArray.ArraySize = desc.DepthOrArraySize;
                break;
            case ERHITextureDimension::TextureCube:
                view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
                view.TextureCube.MipLevels = desc.MipLevels;
                break;
            case ERHITextureDimension::TextureCubeArray:
                view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
                view.TextureCubeArray.MipLevels = desc.MipLevels;
                view.TextureCubeArray.NumCubes = std::max(InDesc.ArraySize, 1u);
                break;
            }
            Device->CreateShaderResourceView(texture->Resource.Get(), &view, SrvPool.At(texture->SrvIndex));
        }
        TrackMemory(*texture);
        return texture;
    }

    TRef<FD3D12Texture> FD3D12Device::WrapBackBuffer(ComPtr<ID3D12Resource> InResource, uint32_t InWidth,
                                                     uint32_t InHeight) {
        auto texture = CreateRef<FD3D12Texture>();
        texture->Desc.Width = InWidth;
        texture->Desc.Height = InHeight;
        texture->Desc.Format = ERHIFormat::RGBA8_UNorm;
        texture->Desc.Usage = RHITextureUsage_RenderTarget;
        texture->Desc.DebugName = "BackBuffer";
        texture->Resource = std::move(InResource);
        texture->RestState = D3D12_RESOURCE_STATE_PRESENT;
        texture->State = D3D12_RESOURCE_STATE_PRESENT;
        SetName(texture->Resource.Get(), texture->Desc.DebugName);
        return texture;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE FD3D12Device::GetRenderTargetView(FD3D12Texture& InTexture, uint32_t InMip,
                                                                 uint32_t InSlice) {
        const uint32_t key = FD3D12Texture::ViewKey(InMip, InSlice);
        const auto found = InTexture.RtvIndices.find(key);
        if (found != InTexture.RtvIndices.end())
            return RtvPool.At(found->second);

        const uint32_t index = RtvPool.Allocate();
        D3D12_RENDER_TARGET_VIEW_DESC view{};
        view.Format = D3D12ViewFormat(InTexture.Desc.Format);
        if (InTexture.Desc.SliceCount() > 1) {
            view.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
            view.Texture2DArray.MipSlice = InMip;
            view.Texture2DArray.FirstArraySlice = InSlice;
            view.Texture2DArray.ArraySize = 1;
        } else {
            view.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
            view.Texture2D.MipSlice = InMip;
        }
        Device->CreateRenderTargetView(InTexture.Resource.Get(), &view, RtvPool.At(index));
        InTexture.RtvIndices[key] = index;
        return RtvPool.At(index);
    }

    D3D12_CPU_DESCRIPTOR_HANDLE FD3D12Device::GetMipShaderView(FD3D12Texture& InTexture, uint32_t InMip) {
        const auto found = InTexture.MipSrvIndices.find(InMip);
        if (found != InTexture.MipSrvIndices.end())
            return SrvPool.At(found->second);

        const uint32_t index = SrvPool.Allocate();
        D3D12_SHADER_RESOURCE_VIEW_DESC view{};
        view.Format = D3D12ShaderReadFormat(InTexture.Desc.Format);
        view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        view.Texture2D.MostDetailedMip = InMip;
        view.Texture2D.MipLevels = 1;
        Device->CreateShaderResourceView(InTexture.Resource.Get(), &view, SrvPool.At(index));
        InTexture.MipSrvIndices[InMip] = index;
        return SrvPool.At(index);
    }

    D3D12_CPU_DESCRIPTOR_HANDLE FD3D12Device::GetDepthStencilView(FD3D12Texture& InTexture, uint32_t InSlice) {
        const uint32_t key = FD3D12Texture::ViewKey(0, InSlice);
        const auto found = InTexture.DsvIndices.find(key);
        if (found != InTexture.DsvIndices.end())
            return DsvPool.At(found->second);

        const uint32_t index = DsvPool.Allocate();
        D3D12_DEPTH_STENCIL_VIEW_DESC view{};
        view.Format = D3D12ViewFormat(InTexture.Desc.Format);
        if (InTexture.Desc.SliceCount() > 1) {
            view.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
            view.Texture2DArray.FirstArraySlice = InSlice;
            view.Texture2DArray.ArraySize = 1;
        } else {
            view.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        }
        Device->CreateDepthStencilView(InTexture.Resource.Get(), &view, DsvPool.At(index));
        InTexture.DsvIndices[key] = index;
        return DsvPool.At(index);
    }

    void FD3D12Device::UploadTexture(IRHITexture& InTexture, uint32_t InMipLevel, uint32_t InArraySlice,
                                     const void* InData, uint32_t InRowPitch) {
        auto& texture = static_cast<FD3D12Texture&>(InTexture);
        const FRHITextureDesc& textureDesc = texture.Desc;
        if (!InData || InMipLevel >= textureDesc.MipLevels || InArraySlice >= textureDesc.SliceCount())
            return;

        const UINT subresource = InMipLevel + InArraySlice * textureDesc.MipLevels;
        const D3D12_RESOURCE_DESC resourceDesc = texture.Resource->GetDesc();
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
        UINT rows = 0;
        UINT64 rowBytes = 0;
        UINT64 totalBytes = 0;
        Device->GetCopyableFootprints(&resourceDesc, subresource, 1, 0, &footprint, &rows, &rowBytes, &totalBytes);

        ComPtr<ID3D12Resource> staging;
        const D3D12_HEAP_PROPERTIES heap = HeapProperties(D3D12_HEAP_TYPE_UPLOAD);
        const D3D12_RESOURCE_DESC stagingDesc = BufferDesc(totalBytes);
        if (!D3D12Check(Device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &stagingDesc,
                                                        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                        IID_PPV_ARGS(&staging)),
                        "CreateCommittedResource (texture staging)"))
            return;
        void* mapped = nullptr;
        if (!D3D12Check(staging->Map(0, nullptr, &mapped), "Map (texture staging)"))
            return;
        const size_t copyBytes = static_cast<size_t>(std::min<UINT64>(rowBytes, InRowPitch));
        for (UINT y = 0; y < rows; ++y)
            std::memcpy(static_cast<uint8_t*>(mapped) + footprint.Offset + static_cast<size_t>(y) * footprint.Footprint.RowPitch,
                        static_cast<const uint8_t*>(InData) + static_cast<size_t>(y) * InRowPitch, copyBytes);
        staging->Unmap(0, nullptr);

        ID3D12GraphicsCommandList* list = BeginImmediate();
        Transition(list, texture, D3D12_RESOURCE_STATE_COPY_DEST);
        D3D12_TEXTURE_COPY_LOCATION source{};
        source.pResource = staging.Get();
        source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        source.PlacedFootprint = footprint;
        D3D12_TEXTURE_COPY_LOCATION destination{};
        destination.pResource = texture.Resource.Get();
        destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        destination.SubresourceIndex = subresource;
        list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
        Transition(list, texture, texture.RestState);
        EndImmediate();
    }

    bool FD3D12Device::ReadTexture(IRHITexture& InTexture, uint32_t InMipLevel, uint32_t InArraySlice,
                                   std::vector<uint8_t>& OutPixels) {
        auto& texture = static_cast<FD3D12Texture&>(InTexture);
        const FRHITextureDesc& textureDesc = texture.Desc;
        if (InMipLevel >= textureDesc.MipLevels || InArraySlice >= textureDesc.SliceCount())
            return false;
        if (bFrameOpen) {
            LE_CORE_ERROR("D3D12: ReadTexture called between BeginFrame and EndFrame");
            return false;
        }

        const UINT subresource = InMipLevel + InArraySlice * textureDesc.MipLevels;
        const D3D12_RESOURCE_DESC resourceDesc = texture.Resource->GetDesc();
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
        UINT rows = 0;
        UINT64 rowBytes = 0;
        UINT64 totalBytes = 0;
        Device->GetCopyableFootprints(&resourceDesc, subresource, 1, 0, &footprint, &rows, &rowBytes, &totalBytes);

        ComPtr<ID3D12Resource> staging;
        const D3D12_HEAP_PROPERTIES heap = HeapProperties(D3D12_HEAP_TYPE_READBACK);
        const D3D12_RESOURCE_DESC stagingDesc = BufferDesc(totalBytes);
        if (!D3D12Check(Device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &stagingDesc,
                                                        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&staging)),
                        "CreateCommittedResource (readback)"))
            return false;

        ID3D12GraphicsCommandList* list = BeginImmediate();
        Transition(list, texture, D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION source{};
        source.pResource = texture.Resource.Get();
        source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        source.SubresourceIndex = subresource;
        D3D12_TEXTURE_COPY_LOCATION destination{};
        destination.pResource = staging.Get();
        destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        destination.PlacedFootprint = footprint;
        list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
        Transition(list, texture, texture.RestState);
        EndImmediate();

        void* mapped = nullptr;
        const D3D12_RANGE readRange{0, static_cast<SIZE_T>(totalBytes)};
        if (!D3D12Check(staging->Map(0, &readRange, &mapped), "Map (readback)"))
            return false;
        OutPixels.resize(static_cast<size_t>(rowBytes) * rows);
        for (UINT y = 0; y < rows; ++y)
            std::memcpy(OutPixels.data() + static_cast<size_t>(y) * rowBytes,
                        static_cast<const uint8_t*>(mapped) + footprint.Offset +
                            static_cast<size_t>(y) * footprint.Footprint.RowPitch,
                        static_cast<size_t>(rowBytes));
        const D3D12_RANGE noWrite{0, 0};
        staging->Unmap(0, &noWrite);
        return true;
    }

    // --- Shaders and pipelines -------------------------------------------------------------------

    TRef<IRHIShader> FD3D12Device::CreateShader(const FRHIShaderDesc& InDesc) {
        if (!InDesc.Bytecode || InDesc.BytecodeSize == 0) {
            LE_CORE_ERROR("D3D12: shader '{0}' has no bytecode", InDesc.DebugName);
            return nullptr;
        }
        auto shader = CreateRef<FD3D12Shader>();
        shader->Stage = InDesc.Stage;
        shader->Bytecode.assign(static_cast<const uint8_t*>(InDesc.Bytecode),
                                static_cast<const uint8_t*>(InDesc.Bytecode) + InDesc.BytecodeSize);
        return shader;
    }

    TRef<IRHIPipeline> FD3D12Device::CreateGraphicsPipeline(const FRHIGraphicsPipelineDesc& InDesc) {
        if (!InDesc.VertexShader) {
            LE_CORE_ERROR("D3D12: pipeline '{0}' has no vertex shader", InDesc.DebugName);
            return nullptr;
        }
        const auto* vertexShader = static_cast<const FD3D12Shader*>(InDesc.VertexShader);
        const auto* pixelShader = static_cast<const FD3D12Shader*>(InDesc.PixelShader);

        std::vector<D3D12_INPUT_ELEMENT_DESC> elements;
        elements.reserve(InDesc.VertexLayout.Attributes.size());
        for (size_t i = 0; i < InDesc.VertexLayout.Attributes.size(); ++i) {
            const FRHIVertexAttribute& attribute = InDesc.VertexLayout.Attributes[i];
            D3D12_INPUT_ELEMENT_DESC element{};
            element.SemanticName = "ATTRIBUTE";
            element.SemanticIndex = static_cast<UINT>(i);
            element.Format = ToD3D(attribute.Format);
            element.AlignedByteOffset = attribute.Offset;
            element.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
            elements.push_back(element);
        }

        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
        desc.pRootSignature = RootSignature.Get();
        desc.VS = {vertexShader->Bytecode.data(), vertexShader->Bytecode.size()};
        if (pixelShader)
            desc.PS = {pixelShader->Bytecode.data(), pixelShader->Bytecode.size()};
        desc.InputLayout = {elements.data(), static_cast<UINT>(elements.size())};
        desc.SampleMask = 0xFFFFFFFFu;
        desc.SampleDesc.Count = 1;

        desc.RasterizerState.FillMode = InDesc.Raster.bWireframe ? D3D12_FILL_MODE_WIREFRAME : D3D12_FILL_MODE_SOLID;
        desc.RasterizerState.CullMode = InDesc.Raster.Cull == ERHICull::None
                                            ? D3D12_CULL_MODE_NONE
                                            : (InDesc.Raster.Cull == ERHICull::Front ? D3D12_CULL_MODE_FRONT
                                                                                    : D3D12_CULL_MODE_BACK);
        desc.RasterizerState.FrontCounterClockwise = InDesc.Raster.bFrontCounterClockwise ? TRUE : FALSE;
        desc.RasterizerState.DepthBias = InDesc.Raster.DepthBias;
        desc.RasterizerState.SlopeScaledDepthBias = InDesc.Raster.SlopeScaledDepthBias;
        desc.RasterizerState.DepthClipEnable = InDesc.Raster.bDepthClip ? TRUE : FALSE;

        const bool bHasDepth = InDesc.DepthFormat != ERHIFormat::Unknown;
        desc.DepthStencilState.DepthEnable = (bHasDepth && InDesc.Depth.bTest) ? TRUE : FALSE;
        desc.DepthStencilState.DepthWriteMask =
            (bHasDepth && InDesc.Depth.bWrite) ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
        desc.DepthStencilState.DepthFunc = ToD3D(InDesc.Depth.Compare);
        desc.DSVFormat = bHasDepth ? D3D12ViewFormat(InDesc.DepthFormat) : DXGI_FORMAT_UNKNOWN;

        desc.NumRenderTargets = std::min(InDesc.NumColorTargets, kRHIMaxColorTargets);
        for (uint32_t i = 0; i < desc.NumRenderTargets; ++i) {
            desc.RTVFormats[i] = D3D12ViewFormat(InDesc.ColorFormats[i]);
            D3D12_RENDER_TARGET_BLEND_DESC& blend = desc.BlendState.RenderTarget[i];
            blend.BlendEnable = InDesc.Blend.bEnabled ? TRUE : FALSE;
            blend.SrcBlend = ToD3D(InDesc.Blend.SrcColor);
            blend.DestBlend = ToD3D(InDesc.Blend.DstColor);
            blend.BlendOp = D3D12_BLEND_OP_ADD;
            blend.SrcBlendAlpha = ToD3D(InDesc.Blend.SrcAlpha);
            blend.DestBlendAlpha = ToD3D(InDesc.Blend.DstAlpha);
            blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
            blend.LogicOp = D3D12_LOGIC_OP_NOOP;
            blend.RenderTargetWriteMask = InDesc.Blend.bWriteColor ? D3D12_COLOR_WRITE_ENABLE_ALL : 0;
        }
        desc.BlendState.IndependentBlendEnable = desc.NumRenderTargets > 1 ? TRUE : FALSE;

        const bool bLines = InDesc.Topology == ERHITopology::LineList;
        desc.PrimitiveTopologyType = bLines ? D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE : D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

        auto pipeline = CreateRef<FD3D12Pipeline>();
        pipeline->Topology = bLines ? D3D_PRIMITIVE_TOPOLOGY_LINELIST : D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        if (FAILED(Device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pipeline->State)))) {
            LE_CORE_ERROR("D3D12: could not create pipeline '{0}'", InDesc.DebugName);
            DrainMessages();
            return nullptr;
        }
        SetName(pipeline->State.Get(), InDesc.DebugName);
        return pipeline;
    }

    TScope<IRHISwapchain> FD3D12Device::CreateSwapchain(void* InWindowHandle, uint32_t InWidth, uint32_t InHeight) {
        auto swapchain = CreateScope<FD3D12Swapchain>(*this);
        if (!swapchain->Init(InWindowHandle, InWidth, InHeight))
            return nullptr;
        return swapchain;
    }

    // --- Frames ----------------------------------------------------------------------------------

    IRHICommandList& FD3D12Device::BeginFrame() {
        if (bFrameOpen) {
            LE_CORE_ERROR("D3D12: BeginFrame called twice without EndFrame");
            return *CommandList;
        }
        FFrameContext& frame = CurrentFrame();
        // This slot's previous use must be finished before its memory is reused.
        WaitForFence(frame.FenceValue);
        CollectGpuTimes(frame, FrameIndex);
        FlushDeferred(frame);
        frame.Allocator->Reset();
        frame.ConstantPage = 0;
        frame.ConstantOffset = 0;
        frame.DescriptorCursor = 0;

        FrameList->Reset(frame.Allocator.Get(), nullptr);
        ID3D12DescriptorHeap* heaps[] = {VisibleHeap.Get()};
        FrameList->SetDescriptorHeaps(1, heaps);
        FrameList->SetGraphicsRootSignature(RootSignature.Get());
        CommandList->ResetForFrame(FrameList.Get());
        bFrameOpen = true;
        return *CommandList;
    }

    void FD3D12Device::EndFrame() {
        if (!bFrameOpen)
            return;
        CommandList->EndRenderPass(); // tolerate a pass left open
        if (GpuTimerHeap && CurrentFrame().GpuIntervalCount > 0) {
            const UINT first = FrameIndex * kD3D12MaxGpuIntervals * 2;
            FrameList->ResolveQueryData(GpuTimerHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, first,
                                        CurrentFrame().GpuIntervalCount * 2, GpuTimerReadback.Get(),
                                        first * sizeof(uint64_t));
        }
        GpuOpenInterval.fill(-1);
        FrameList->Close();
        ID3D12CommandList* lists[] = {FrameList.Get()};
        Queue->ExecuteCommandLists(1, lists);

        FFrameContext& frame = CurrentFrame();
        frame.FenceValue = NextFenceValue++;
        Queue->Signal(Fence.Get(), frame.FenceValue);
        FrameIndex = (FrameIndex + 1) % kD3D12FrameCount;
        bFrameOpen = false;
        DrainMessages();
    }

    void FD3D12Device::CollectGpuTimes(FFrameContext& InFrame, uint32_t InFrameIndex) {
        if (!GpuTimerHeap || InFrame.GpuIntervalCount == 0) {
            InFrame.GpuIntervalCount = 0;
            return;
        }
        const size_t first = static_cast<size_t>(InFrameIndex) * kD3D12MaxGpuIntervals * 2;
        const D3D12_RANGE readRange{first * sizeof(uint64_t),
                                    (first + InFrame.GpuIntervalCount * 2) * sizeof(uint64_t)};
        void* mapped = nullptr;
        if (SUCCEEDED(GpuTimerReadback->Map(0, &readRange, &mapped)) && mapped) {
            const auto* ticks = static_cast<const uint64_t*>(mapped) + first;
            GpuTimesMs.fill(0.0f);
            for (uint32_t i = 0; i < InFrame.GpuIntervalCount; ++i) {
                const int32_t slot = InFrame.GpuIntervalSlots[i];
                const uint64_t begin = ticks[i * 2];
                const uint64_t end = ticks[i * 2 + 1];
                // An interval left open keeps slot -1; timestamps can also come back out of order
                // when the adapter changes its clock, which is not a time to report.
                if (slot < 0 || end < begin)
                    continue;
                GpuTimesMs[static_cast<size_t>(slot)] +=
                    static_cast<float>(static_cast<double>(end - begin) * 1000.0 / static_cast<double>(GpuTimerFrequency));
            }
            const D3D12_RANGE nothingWritten{0, 0};
            GpuTimerReadback->Unmap(0, &nothingWritten);
        }
        InFrame.GpuIntervalCount = 0;
    }

    void FD3D12Device::DrainMessages() {
        if (!InfoQueue)
            return;
        const UINT64 count = InfoQueue->GetNumStoredMessages();
        std::vector<uint8_t> storage;
        for (UINT64 i = 0; i < count; ++i) {
            SIZE_T length = 0;
            if (FAILED(InfoQueue->GetMessage(i, nullptr, &length)) || length == 0)
                continue;
            storage.resize(length);
            auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
            if (FAILED(InfoQueue->GetMessage(i, message, &length)))
                continue;
            if (message->Severity == D3D12_MESSAGE_SEVERITY_ERROR ||
                message->Severity == D3D12_MESSAGE_SEVERITY_CORRUPTION) {
                ++GValidationErrors;
                LE_CORE_ERROR("D3D12 validation: {0}", message->pDescription);
            } else if (message->Severity == D3D12_MESSAGE_SEVERITY_WARNING) {
                LE_CORE_WARN("D3D12 validation: {0}", message->pDescription);
            }
        }
        InfoQueue->ClearStoredMessages();
    }

    // ---------------------------------------------------------------------------------------------
    // Registration
    // ---------------------------------------------------------------------------------------------

    void FD3D12RHI::Register() {
        if (FRHI::IsBackendRegistered(ERHIBackend::D3D12))
            return;
        FRHI::RegisterBackend(ERHIBackend::D3D12, [](const FRHIInitDesc& InDesc) -> TScope<IRHIDevice> {
            auto device = CreateScope<FD3D12Device>();
            if (!device->Init(InDesc))
                return nullptr;
            return device;
        });
    }

    uint64_t FD3D12RHI::GetValidationErrorCount() {
        if (FD3D12Device* device = GetD3D12Device())
            device->DrainMessages();
        return GValidationErrors;
    }

} // namespace Leon
