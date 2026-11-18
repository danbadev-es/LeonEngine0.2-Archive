#pragma once

#include "Core/Base.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace Leon {

    // The render hardware interface. It is shaped like an explicit API (Direct3D 12): state lives in
    // pipeline objects, drawing happens inside render passes on a command list, and nothing is "bound"
    // globally. See IRHIDevice.hpp for the objects; this header holds the plain data that describes them.

    /** Fixed limits of the binding model, shared by every backend and every shader. */
    inline constexpr uint32_t kRHIMaxColorTargets = 4;
    inline constexpr uint32_t kRHIConstantSlots = 5;
    inline constexpr uint32_t kRHITextureSlots = 16;

    /**
     * Constant buffer slots (HLSL registers b0..b4), ordered by how often they change. A shader
     * declares only the ones it uses.
     */
    enum class ERHIConstantSlot : uint8_t {
        Frame = 0,    ///< Camera and time: once per view.
        Pass = 1,     ///< Lights, shadows, fog: once per pass.
        Material = 2, ///< Surface parameters: when the material changes.
        Draw = 3,     ///< Object transform: every draw.
        Extra = 4,    ///< Bone palette or instance transforms.
    };

    /**
     * Samplers every shader gets for free (HLSL registers s0..s4). There is no sampler object to
     * create or bind: a texture is sampled with whichever of these fits.
     */
    enum class ERHIStaticSampler : uint8_t {
        LinearWrap = 0,    ///< Trilinear, anisotropic, repeat. Material textures.
        LinearClamp = 1,   ///< Trilinear, clamp. Render targets, lookup tables, cubemaps.
        PointClamp = 2,    ///< Nearest, clamp. Exact texel reads.
        PointWrap = 3,     ///< Nearest, repeat. Noise.
        ShadowCompare = 4, ///< Linear comparison (less-equal), clamp. Shadow maps.
    };

    enum class ERHIFormat : uint8_t {
        Unknown = 0,
        R8_UNorm,
        RGBA8_UNorm,
        RGBA8_UNorm_sRGB,
        BGRA8_UNorm,
        RG16_Float,
        RGBA16_Float,
        RGBA32_Float,
        R32_Float,
        R32_SInt,
        D32_Float,
        D24_UNorm_S8_UInt,
    };

    /** Bytes of one pixel; 0 for Unknown. */
    uint32_t RHIFormatBytesPerPixel(ERHIFormat InFormat);
    bool RHIFormatIsDepth(ERHIFormat InFormat);

    // --- Buffers --------------------------------------------------------------------------------

    enum class ERHIBufferUsage : uint8_t { Vertex, Index, Constant };

    struct FRHIBufferDesc {
        uint64_t Size = 0;
        ERHIBufferUsage Usage = ERHIBufferUsage::Vertex;
        /** Bytes per vertex for vertex buffers; 2 or 4 for index buffers; unused for constants. */
        uint32_t Stride = 0;
        /** True when the contents are rewritten after creation (IRHIDevice::WriteBuffer). */
        bool bDynamic = false;
        std::string DebugName;
    };

    // --- Textures -------------------------------------------------------------------------------

    enum class ERHITextureDimension : uint8_t { Texture2D, Texture2DArray, TextureCube, TextureCubeArray };

    enum ERHITextureUsage : uint8_t {
        RHITextureUsage_Sampled = 1 << 0,
        RHITextureUsage_RenderTarget = 1 << 1,
        RHITextureUsage_DepthStencil = 1 << 2,
    };

    struct FRHITextureDesc {
        uint32_t Width = 1;
        uint32_t Height = 1;
        /** Layers of an array, or number of cubes for cube types (a cube array of 4 has 24 slices). */
        uint32_t ArraySize = 1;
        uint32_t MipLevels = 1;
        ERHIFormat Format = ERHIFormat::RGBA8_UNorm;
        ERHITextureDimension Dimension = ERHITextureDimension::Texture2D;
        uint8_t Usage = RHITextureUsage_Sampled;
        /** Value the texture is most often cleared to; clearing to it is the fast path. */
        glm::vec4 ClearColor{0.0f, 0.0f, 0.0f, 1.0f};
        float ClearDepth = 1.0f;
        std::string DebugName;

        /** Number of 2D slices: ArraySize, times six for cube types. */
        uint32_t SliceCount() const {
            const bool bCube =
                Dimension == ERHITextureDimension::TextureCube || Dimension == ERHITextureDimension::TextureCubeArray;
            return ArraySize * (bCube ? 6u : 1u);
        }
    };

    // --- Shaders and pipelines ------------------------------------------------------------------

    enum class ERHIShaderStage : uint8_t { Vertex, Pixel };

    struct FRHIShaderDesc {
        ERHIShaderStage Stage = ERHIShaderStage::Vertex;
        /** Compiled code in the backend's format (DXIL for Direct3D 12). Copied by the device. */
        const void* Bytecode = nullptr;
        size_t BytecodeSize = 0;
        std::string DebugName;
    };

    enum class ERHIVertexFormat : uint8_t { Float, Float2, Float3, Float4, Int4, UByte4_UNorm };

    /**
     * One vertex attribute. Attributes are matched to shader inputs by position: the first is
     * `ATTRIBUTE0` in HLSL, the second `ATTRIBUTE1`, and so on.
     */
    struct FRHIVertexAttribute {
        ERHIVertexFormat Format = ERHIVertexFormat::Float3;
        uint32_t Offset = 0;
    };

    struct FRHIVertexLayout {
        std::vector<FRHIVertexAttribute> Attributes;
        uint32_t Stride = 0;
    };

    enum class ERHICompare : uint8_t { Never, Less, Equal, LessEqual, Greater, NotEqual, GreaterEqual, Always };
    enum class ERHICull : uint8_t { None, Front, Back };
    enum class ERHITopology : uint8_t { TriangleList, LineList };
    enum class ERHIBlend : uint8_t {
        Zero,
        One,
        SrcColor,
        InvSrcColor,
        SrcAlpha,
        InvSrcAlpha,
        DstAlpha,
        InvDstAlpha,
        DstColor,
        InvDstColor,
    };

    struct FRHIRasterState {
        ERHICull Cull = ERHICull::Back;
        /** The engine's meshes are wound counter-clockwise. */
        bool bFrontCounterClockwise = true;
        bool bWireframe = false;
        int32_t DepthBias = 0;
        float SlopeScaledDepthBias = 0.0f;
        /** False clamps depth instead of clipping (shadow casters behind the near plane). */
        bool bDepthClip = true;
    };

    struct FRHIDepthState {
        bool bTest = true;
        bool bWrite = true;
        ERHICompare Compare = ERHICompare::Less;
    };

    struct FRHIBlendState {
        bool bEnabled = false;
        ERHIBlend SrcColor = ERHIBlend::SrcAlpha;
        ERHIBlend DstColor = ERHIBlend::InvSrcAlpha;
        ERHIBlend SrcAlpha = ERHIBlend::One;
        ERHIBlend DstAlpha = ERHIBlend::InvSrcAlpha;
        /** False draws to depth only. */
        bool bWriteColor = true;
    };

    class IRHIShader;

    /**
     * Everything that decides how triangles become pixels. Immutable once created: a draw that needs
     * different state uses a different pipeline.
     */
    struct FRHIGraphicsPipelineDesc {
        IRHIShader* VertexShader = nullptr;
        /** May be null for depth-only passes. */
        IRHIShader* PixelShader = nullptr;
        FRHIVertexLayout VertexLayout;
        FRHIRasterState Raster;
        FRHIDepthState Depth;
        /** Applied to every colour target. */
        FRHIBlendState Blend;
        ERHITopology Topology = ERHITopology::TriangleList;
        uint32_t NumColorTargets = 1;
        ERHIFormat ColorFormats[kRHIMaxColorTargets] = {ERHIFormat::RGBA8_UNorm, ERHIFormat::Unknown,
                                                        ERHIFormat::Unknown, ERHIFormat::Unknown};
        /** Unknown when the pass has no depth target. */
        ERHIFormat DepthFormat = ERHIFormat::Unknown;
        std::string DebugName;
    };

    // --- Render passes --------------------------------------------------------------------------

    class IRHITexture;

    enum class ERHILoadOp : uint8_t {
        Load,     ///< Keep what the target holds.
        Clear,    ///< Clear to the given value.
        DontCare, ///< Contents undefined; every pixel will be overwritten.
    };

    struct FRHIColorAttachment {
        IRHITexture* Texture = nullptr;
        uint32_t MipLevel = 0;
        /** Layer of an array, or face index (cube * 6 + face) of a cube. */
        uint32_t ArraySlice = 0;
        ERHILoadOp LoadOp = ERHILoadOp::Clear;
        glm::vec4 ClearColor{0.0f, 0.0f, 0.0f, 1.0f};
    };

    struct FRHIDepthAttachment {
        IRHITexture* Texture = nullptr;
        uint32_t ArraySlice = 0;
        ERHILoadOp LoadOp = ERHILoadOp::Clear;
        float ClearDepth = 1.0f;
    };

    struct FRHIRenderPassDesc {
        FRHIColorAttachment Color[kRHIMaxColorTargets];
        uint32_t NumColorTargets = 0;
        /** Texture null when the pass has no depth. */
        FRHIDepthAttachment Depth;
        /** Shown in GPU captures. */
        const char* Name = "";
    };

    struct FRHIViewport {
        float X = 0.0f;
        float Y = 0.0f;
        float Width = 0.0f;
        float Height = 0.0f;
        float MinDepth = 0.0f;
        float MaxDepth = 1.0f;
    };

    struct FRHIRect {
        int32_t Left = 0;
        int32_t Top = 0;
        int32_t Right = 0;
        int32_t Bottom = 0;
    };

    // --- Device ---------------------------------------------------------------------------------

    enum class ERHIBackend : uint8_t { Null, D3D12 };

    struct FRHIInitDesc {
        /** Validation by the API itself. Slow; on by default in Debug builds. */
        bool bEnableDebugLayer = false;
        /** Use the software adapter (WARP): no GPU needed, deterministic. Used by tests and CI. */
        bool bUseSoftwareAdapter = false;
    };

    struct FRHIDeviceInfo {
        ERHIBackend Backend = ERHIBackend::Null;
        std::string AdapterName;
        uint64_t DedicatedVideoMemoryBytes = 0;
        bool bSoftwareAdapter = false;
    };

} // namespace Leon
