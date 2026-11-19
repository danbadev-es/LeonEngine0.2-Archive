#pragma once

// Test harness for shader-level GPU tests: a Direct3D 12 device on WARP (the software adapter) with
// the debug layer on, a float render target, a unit quad and helpers to feed the engine's shaders.
//
// The tests it serves were written against the OpenGL renderer. They address shader inputs by the
// names the GLSL shaders used ("u_Roughness", "u_UseShadows"...) and fill the two uniform blocks the
// old renderer had. Those shapes are kept here, as the vocabulary of the tests, and translated to the
// constant buffers of the HLSL shaders when a quad is drawn. The maths the tests check is unchanged.
//
// One FHeadlessDevice per test case, declared first: it creates the device and destroys it, so tests
// do not depend on each other. Set LEON_TEST_GPU=1 to run on the machine's graphics card.

#include "RHI/FLegacyRenderDriver.hpp"
#include "RHI/IRHIDevice.hpp"
#include "RHI/IRenderDriver.hpp"
#include "RHID3D12/FD3D12RHI.hpp"
#include "RenderCore/FShaderConstants.hpp"
#include "RenderCore/FShaderLibrary.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace Leon::TestGPU {

    // --- The uniform blocks of the old renderer, as the tests fill them ----------------------------

    /** Camera and shadow matrices. Matrices are in the engine's clip convention (depth -1..1). */
    struct FCameraBufferData {
        glm::mat4 ViewProjection{1.0f};
        glm::mat4 LightSpaceMatrices[4]{glm::mat4(1.0f), glm::mat4(1.0f), glm::mat4(1.0f), glm::mat4(1.0f)};
        /** The map of the shadowed spot light (ShadowSettings.y); it is layer 0 of the spot shadow map. */
        glm::mat4 SpotLightSpaceMatrix{1.0f};
        /** More shadowed spot lights: the light that owns each of layers 1 to 3 (-1 = nobody) and its matrix. */
        glm::ivec3 ExtraShadowedSpots{-1};
        glm::mat4 ExtraSpotLightSpaceMatrices[3]{glm::mat4(1.0f), glm::mat4(1.0f), glm::mat4(1.0f)};
        glm::vec4 CameraPosition{0.0f};
        glm::vec4 CameraForward{0.0f, 0.0f, -1.0f, 0.0f};
        glm::vec4 CascadeSplits{0.0f};
        glm::vec4 ShadowParams{0.0010f, 0.0035f, 0.040f, 0.25f}; // constant bias, slope bias, normal bias, blend width
        glm::ivec4 ShadowSettings{1, 0, 0, 0};                   // filter, shadowed spot index, cascade count, unused
    };

    struct FGpuDirectionalLight {
        glm::vec4 Direction{0.0f, -1.0f, 0.0f, 0.0f}; // xyz = direction, w = enabled
        glm::vec4 Color{1.0f, 1.0f, 1.0f, 3.0f};      // rgb = colour, a = intensity
    };

    struct FGpuPointLight {
        glm::vec4 Position{0.0f};
        glm::vec4 Color{1.0f, 1.0f, 1.0f, 8.0f};
        glm::vec4 Params{10.0f, -1.0f, 0.0f, 0.0f}; // x = radius, y = shadow cube index
    };

    struct FGpuSpotLight {
        glm::vec4 Position{0.0f};
        glm::vec4 Direction{0.0f, -1.0f, 0.0f, 0.9f};
        glm::vec4 Color{1.0f, 1.0f, 1.0f, 0.8f};
        glm::vec4 Params{10.0f, 10.0f, 0.0f, 0.0f};
    };

    struct FLightingBufferData {
        FGpuDirectionalLight DirLight;
        FGpuPointLight PointLights[16];
        FGpuSpotLight SpotLights[8];
        glm::ivec4 LightCounts{0, 0, 0, 0};
        glm::vec4 EnvSkyColor{0.18f, 0.44f, 0.88f, 1.2f};
        glm::vec4 EnvHorizonColor{0.78f, 0.84f, 0.95f, 0.0f};
        glm::vec4 EnvGroundColor{0.22f, 0.24f, 0.28f, 0.0f};
        glm::vec4 FogColorDensity{0.0f};
        glm::vec4 FogParams{0.0f, 0.0f, 0.0f, 1.0f};
    };

    struct FTestVertex {
        glm::vec3 Position;
        glm::vec3 Normal;
        glm::vec2 TexCoord;
        glm::vec4 Tangent;
        glm::vec3 Color{1.0f};
        glm::vec2 LightmapUV{0.0f};
    };

    /** A texture made by a test. Keep it while it is bound. */
    using FTestTexture = TRef<IRHITexture>;

    // --- Shaders addressed by the old uniform names ------------------------------------------------

    /**
     * One of the engine's shaders, with its inputs set by name. Values are remembered and turned into
     * the shader's constant buffers when the device draws. A name that was never set has the default
     * the GLSL shader gave it.
     */
    class FTestShader : public std::enable_shared_from_this<FTestShader> {
    public:
        enum class EKind {
            Surface,       // PBR_Lit
            ShadowDepth,   // ShadowDepth
            Sky,           // Skybox
            BloomBright,   // BloomBrightPass
            BloomDown,     // BloomDownsample
            BloomUp,       // BloomUpsample
            ToneMapping,   // ToneMapping
            FXAA,          // FXAA
            SSAO,          // SSAO
            SSAOComposite, // SSAOComposite
            Unknown,
        };

        /** InPath names the shader as the tests always did ("PBR_Lit", "BloomUpsample"); a folder or extension is ignored. */
        static TRef<FTestShader> Create(const std::string& InPath) {
            static const std::pair<const char*, EKind> kinds[] = {
                {"PBR_Lit", EKind::Surface},           {"ShadowDepth", EKind::ShadowDepth},
                {"Skybox", EKind::Sky},                {"BloomBrightPass", EKind::BloomBright},
                {"BloomDownsample", EKind::BloomDown}, {"BloomUpsample", EKind::BloomUp},
                {"ToneMapping", EKind::ToneMapping},   {"FXAA", EKind::FXAA},
                {"SSAOComposite", EKind::SSAOComposite}, {"SSAO", EKind::SSAO},
            };
            const size_t slash = InPath.find_last_of("/\\");
            std::string name = InPath.substr(slash == std::string::npos ? 0 : slash + 1);
            name = name.substr(0, name.find('.'));
            auto shader = CreateRef<FTestShader>();
            for (const auto& [kindName, kind] : kinds) {
                if (name == kindName) {
                    shader->Kind = kind;
                    break;
                }
            }
            return shader->Kind == EKind::Unknown ? nullptr : shader;
        }

        void Bind();

        void SetInt(const std::string& InName, int InValue) { Values[InName] = {static_cast<float>(InValue)}; }
        void SetFloat(const std::string& InName, float InValue) { Values[InName] = {InValue}; }
        void SetFloat2(const std::string& InName, float InX, float InY) { Values[InName] = {InX, InY}; }
        void SetFloat3(const std::string& InName, float InX, float InY, float InZ) { Values[InName] = {InX, InY, InZ}; }
        void SetFloat4(const std::string& InName, float InX, float InY, float InZ, float InW) {
            Values[InName] = {InX, InY, InZ, InW};
        }
        void SetMat3(const std::string& InName, const float* InValues) { Values[InName].assign(InValues, InValues + 9); }
        void SetMat4(const std::string& InName, const float* InValues) { Values[InName].assign(InValues, InValues + 16); }

        EKind GetKind() const { return Kind; }

        // Reading back, with the default used when the name was never set.
        float Float(const std::string& InName, float InDefault) const {
            const auto found = Values.find(InName);
            return found != Values.end() && !found->second.empty() ? found->second[0] : InDefault;
        }
        int Int(const std::string& InName, int InDefault) const {
            return static_cast<int>(Float(InName, static_cast<float>(InDefault)));
        }
        glm::vec2 Vec2(const std::string& InName, const glm::vec2& InDefault) const {
            const auto found = Values.find(InName);
            return found != Values.end() && found->second.size() >= 2 ? glm::vec2(found->second[0], found->second[1])
                                                                      : InDefault;
        }
        glm::vec3 Vec3(const std::string& InName, const glm::vec3& InDefault) const {
            const auto found = Values.find(InName);
            return found != Values.end() && found->second.size() >= 3
                       ? glm::vec3(found->second[0], found->second[1], found->second[2])
                       : InDefault;
        }
        glm::vec4 Vec4(const std::string& InName, const glm::vec4& InDefault) const {
            const auto found = Values.find(InName);
            return found != Values.end() && found->second.size() >= 4
                       ? glm::vec4(found->second[0], found->second[1], found->second[2], found->second[3])
                       : InDefault;
        }
        glm::mat4 Mat4(const std::string& InName, const glm::mat4& InDefault) const {
            const auto found = Values.find(InName);
            return found != Values.end() && found->second.size() >= 16 ? glm::make_mat4(found->second.data()) : InDefault;
        }
        glm::mat3 Mat3(const std::string& InName, const glm::mat3& InDefault) const {
            const auto found = Values.find(InName);
            return found != Values.end() && found->second.size() >= 9 ? glm::make_mat3(found->second.data()) : InDefault;
        }

        std::map<std::string, TRef<IRHIPipeline>> Pipelines;

    private:
        EKind Kind = EKind::Unknown;
        std::map<std::string, std::vector<float>> Values;
    };

    inline void SetMat4(const TRef<FTestShader>& InShader, const std::string& InName, const glm::mat4& InValue) {
        InShader->SetMat4(InName, glm::value_ptr(InValue));
    }
    inline void SetMat3(const TRef<FTestShader>& InShader, const std::string& InName, const glm::mat3& InValue) {
        InShader->SetMat3(InName, glm::value_ptr(InValue));
    }
    inline void SetFloat3(const TRef<FTestShader>& InShader, const std::string& InName, const glm::vec3& InValue) {
        InShader->SetFloat3(InName, InValue.x, InValue.y, InValue.z);
    }
    inline void SetFloat2(const TRef<FTestShader>& InShader, const std::string& InName, const glm::vec2& InValue) {
        InShader->SetFloat2(InName, InValue.x, InValue.y);
    }

    /** Normal matrix that turns the quad's +Z normal into +Y: the quad shades as a floor. */
    inline glm::mat3 FloorFacingNormalMatrix() {
        glm::mat3 m(1.0f);
        m[1] = glm::vec3(0.0f, 0.0f, 1.0f);
        m[2] = glm::vec3(0.0f, 1.0f, 0.0f);
        return m;
    }

    inline void ApplyFloorView(FCameraBufferData& InOutCamera) {
        InOutCamera.CameraPosition = glm::vec4(0.0f, 1.0f, 0.0f, 0.0f);
        InOutCamera.CameraForward = glm::vec4(0.0f, -1.0f, 0.0f, 0.0f);
    }

    // --- The device --------------------------------------------------------------------------------

    class FHeadlessDevice {
    public:
        FHeadlessDevice() {
            Current() = this;
            FD3D12RHI::Register();
            FRHIInitDesc init;
            init.bEnableDebugLayer = true;
            init.bUseSoftwareAdapter = std::getenv("LEON_TEST_GPU") == nullptr;
            if (!FRHI::Init(ERHIBackend::D3D12, init))
                return;
            Device = FRHI::GetDevice();
            ErrorsAtStart = FD3D12RHI::GetValidationErrorCount();

            const FTestVertex vertices[4] = {
                {{-1.0f, -1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
                {{1.0f, -1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
                {{1.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
                {{-1.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}, {1.0f, 0.0f, 0.0f, 1.0f}}};
            SetQuadVertices(vertices);
            const uint32_t indices[6] = {0, 1, 2, 2, 3, 0};
            FRHIBufferDesc indexDesc;
            indexDesc.Size = sizeof(indices);
            indexDesc.Usage = ERHIBufferUsage::Index;
            indexDesc.Stride = 4;
            QuadIndices = Device->CreateBuffer(indexDesc, indices);

            White = Create1x1Texture(255, 255, 255, 255);
            Black = Create1x1Texture(0, 0, 0, 255);
            FlatNormal = Create1x1Texture(128, 128, 255, 255);
            // Split-sum fallback: scale 1, bias 0. White would blow up specular image-based lighting.
            BRDFLut = Create1x1FloatTexture(1.0f, 0.0f, 0.0f, 1.0f);
            BlackCube = CreateCube(1, ERHIFormat::RGBA8_UNorm, std::vector<uint8_t>{0, 0, 0, 255});
            ShadowMap = CreateDepth(ERHITextureDimension::Texture2DArray, 1, 1, FShadowSettings::kMaxShadowedSpotLights);
            ShadowArray = CreateDepth(ERHITextureDimension::Texture2DArray, 1, 1, 4);
            PointShadowCubes = CreateDepth(ERHITextureDimension::TextureCubeArray, 1, 1, 1);
            BindFramebuffer(1, 1);
            bInitialized = QuadVertices && QuadIndices && White && ShadowArray && PointShadowCubes && Color;
        }

        ~FHeadlessDevice() {
            Slots.fill(nullptr);
            Shader.reset();
            Color.reset();
            Depth.reset();
            QuadVertices.reset();
            QuadIndices.reset();
            White.reset();
            Black.reset();
            FlatNormal.reset();
            BRDFLut.reset();
            BlackCube.reset();
            ShadowMap.reset();
            ShadowArray.reset();
            PointShadowCubes.reset();
            FShaderLibrary::Clear();
            if (bEngineResources)
                FRenderDriverRegistry::RegisterDriver(nullptr);
            if (Device)
                FRHI::Shutdown();
            if (Current() == this)
                Current() = nullptr;
        }

        FHeadlessDevice(const FHeadlessDevice&) = delete;
        FHeadlessDevice& operator=(const FHeadlessDevice&) = delete;

        /** The device of the running test case; null outside one. */
        static FHeadlessDevice*& Current() {
            static FHeadlessDevice* current = nullptr;
            return current;
        }

        bool IsValid() const { return bInitialized; }
        /**
         * Lets engine objects (meshes, textures) create their resources on this device. Whatever they
         * create must be destroyed before the device: declare it after this object.
         */
        void UseEngineResources() {
            if (Device && !bEngineResources) {
                FLegacyRenderDriver::Register();
                bEngineResources = true;
            }
        }
        IRHIDevice* GetDevice() const { return Device; }
        /** Messages the Direct3D debug layer has raised since this device was created. */
        uint64_t ValidationErrors() const { return FD3D12RHI::GetValidationErrorCount() - ErrorsAtStart; }

        /** Sizes the render target and clears it to transparent black before the next draw. */
        void BindFramebuffer(int InWidth = 1, int InHeight = 1) {
            if (!Device)
                return;
            const uint32_t width = static_cast<uint32_t>(std::max(InWidth, 1));
            const uint32_t height = static_cast<uint32_t>(std::max(InHeight, 1));
            if (!Color || Color->GetDesc().Width != width || Color->GetDesc().Height != height) {
                FRHITextureDesc desc;
                desc.Width = width;
                desc.Height = height;
                desc.Format = ERHIFormat::RGBA32_Float;
                desc.Usage = RHITextureUsage_Sampled | RHITextureUsage_RenderTarget;
                desc.ClearColor = {0.0f, 0.0f, 0.0f, 0.0f};
                desc.DebugName = "TestColor";
                Color = Device->CreateTexture(desc);
                desc.Format = ERHIFormat::D32_Float;
                desc.Usage = RHITextureUsage_DepthStencil;
                desc.DebugName = "TestDepth";
                Depth = Device->CreateTexture(desc);
            }
            bClearPending = true;
        }

        /** Clears the render target before the next draw, keeping its size. */
        void Clear() { bClearPending = true; }

        uint32_t GetWidth() const { return Color ? Color->GetDesc().Width : 0; }
        uint32_t GetHeight() const { return Color ? Color->GetDesc().Height : 0; }
        /** The render target, to feed one pass into the next. Copy it with ResolveColor first. */
        const FTestTexture& GetColorTexture() const { return Color; }

        void UpdateCameraUBO(const FCameraBufferData& InData) { Camera = InData; }
        void UpdateLightingUBO(const FLightingBufferData& InData) { Lighting = InData; }

        /** Replaces the quad's four vertices (counter-clockwise from the bottom left). */
        void SetQuadVertices(const FTestVertex InVertices[4]) {
            FRHIBufferDesc desc;
            desc.Size = sizeof(FTestVertex) * 4;
            desc.Usage = ERHIBufferUsage::Vertex;
            desc.Stride = sizeof(FTestVertex);
            QuadVertices = Device->CreateBuffer(desc, InVertices);
        }

        // --- Textures -----------------------------------------------------------------------------

        /** Puts a texture in a register of the next draws. The registers are those of PBR_Lit.hlsl. */
        void BindTexture(uint32_t InUnit, const FTestTexture& InTexture) {
            if (InUnit < Slots.size())
                Slots[InUnit] = InTexture;
        }

        /** Neutral textures in every register: white material maps, flat normal, no shadows, black environment. */
        void BindDefaultTextures() {
            for (uint32_t unit = 0; unit < 6; ++unit)
                Slots[unit] = unit == 1 ? FlatNormal : White;
            Slots[kSlotBRDFLUT] = BRDFLut;
            Slots[kSlotIrradiance] = BlackCube;
            Slots[kSlotPrefilter] = BlackCube;
            Slots[kSlotEmissive] = Black;
            Slots[kSlotCascadeShadow] = ShadowArray;
            Slots[kSlotSpotShadow] = ShadowMap;
            Slots[kSlotLightmap] = Black;
            Slots[kSlotPlanarWall] = Black;
            Slots[kSlotPointShadow] = PointShadowCubes;
            // A planar capture must not default to white: slot 5 is also read as one.
            Slots[kSlotPlanarFloor] = Black;
        }

        FTestTexture Create2D(uint32_t InWidth, uint32_t InHeight, ERHIFormat InFormat, const void* InPixels,
                              uint32_t InMipLevels = 1) {
            FRHITextureDesc desc;
            desc.Width = InWidth;
            desc.Height = InHeight;
            desc.MipLevels = InMipLevels;
            desc.Format = InFormat;
            desc.DebugName = "TestTexture";
            FTestTexture texture = Device->CreateTexture(desc);
            if (texture && InPixels)
                Device->UploadTexture(*texture, 0, 0, InPixels, RHIFormatRowBytes(InFormat, InWidth));
            return texture;
        }

        /** A texture to be filled later with Upload. */
        FTestTexture CreateEmpty2D(uint32_t InWidth, uint32_t InHeight, ERHIFormat InFormat, uint32_t InMipLevels = 1) {
            return Create2D(InWidth, InHeight, InFormat, nullptr, InMipLevels);
        }

        /** Replaces one mip level of a texture; InPixels holds tightly packed rows of the texture's format. */
        void Upload(const FTestTexture& InTexture, const void* InPixels, uint32_t InMipLevel = 0, uint32_t InSlice = 0) {
            if (!InTexture || !InPixels)
                return;
            const uint32_t width = std::max(InTexture->GetDesc().Width >> InMipLevel, 1u);
            Device->WaitIdle();
            Device->UploadTexture(*InTexture, InMipLevel, InSlice, InPixels,
                                  RHIFormatRowBytes(InTexture->GetDesc().Format, width));
        }

        FTestTexture Create1x1Texture(uint8_t InR, uint8_t InG, uint8_t InB, uint8_t InA = 255) {
            const uint8_t pixel[4] = {InR, InG, InB, InA};
            return Create2D(1, 1, ERHIFormat::RGBA8_UNorm, pixel);
        }
        FTestTexture Create1x1SRGBTexture(uint8_t InR, uint8_t InG, uint8_t InB, uint8_t InA = 255) {
            const uint8_t pixel[4] = {InR, InG, InB, InA};
            return Create2D(1, 1, ERHIFormat::RGBA8_UNorm_sRGB, pixel);
        }
        FTestTexture Create1x1FloatTexture(float InR, float InG, float InB, float InA = 1.0f) {
            const float pixel[4] = {InR, InG, InB, InA};
            return Create2D(1, 1, ERHIFormat::RGBA32_Float, pixel);
        }
        /** Four RGBA8 pixels; the first two are the row sampled at V = 0. */
        FTestTexture Create2x2Texture(const uint32_t InPixels[4]) {
            return Create2D(2, 2, ERHIFormat::RGBA8_UNorm, InPixels);
        }

        /** A cube whose six faces are filled with one pixel value (InPixel holds one pixel of the format). */
        template <typename T> FTestTexture CreateCube(uint32_t InSize, ERHIFormat InFormat, const std::vector<T>& InPixel) {
            FRHITextureDesc desc;
            desc.Width = InSize;
            desc.Height = InSize;
            desc.Format = InFormat;
            desc.Dimension = ERHITextureDimension::TextureCube;
            desc.DebugName = "TestCube";
            FTestTexture texture = Device->CreateTexture(desc);
            if (!texture)
                return nullptr;
            std::vector<T> face;
            for (uint32_t i = 0; i < InSize * InSize; ++i)
                face.insert(face.end(), InPixel.begin(), InPixel.end());
            for (uint32_t slice = 0; slice < 6; ++slice)
                Device->UploadTexture(*texture, 0, slice, face.data(), InSize * RHIFormatBytesPerPixel(InFormat));
            return texture;
        }
        FTestTexture CreateFloatCube(float InR, float InG, float InB, float InA = 1.0f) {
            return CreateCube(1, ERHIFormat::RGBA32_Float, std::vector<float>{InR, InG, InB, InA});
        }

        /** A shadow-map stand-in: every texel of every slice holds depth 1 (nothing in the way). */
        FTestTexture CreateDepth(ERHITextureDimension InDimension, uint32_t InWidth, uint32_t InHeight, uint32_t InArraySize) {
            FRHITextureDesc desc;
            desc.Width = InWidth;
            desc.Height = InHeight;
            desc.ArraySize = InArraySize;
            desc.Format = ERHIFormat::R32_Float;
            desc.Dimension = InDimension;
            desc.DebugName = "TestShadowMap";
            FTestTexture texture = Device->CreateTexture(desc);
            if (texture)
                FillDepth(texture, 1.0f);
            return texture;
        }

        /** Sets every texel of one slice of a shadow-map stand-in (or of all slices: InSlice = -1). */
        void FillDepth(const FTestTexture& InTexture, float InDepth, int InSlice = -1) {
            if (!InTexture)
                return;
            const FRHITextureDesc& desc = InTexture->GetDesc();
            const std::vector<float> texels(static_cast<size_t>(desc.Width) * desc.Height, InDepth);
            for (uint32_t slice = 0; slice < desc.SliceCount(); ++slice) {
                if (InSlice < 0 || static_cast<uint32_t>(InSlice) == slice)
                    Device->UploadTexture(*InTexture, 0, slice, texels.data(), desc.Width * 4);
            }
        }

        const FTestTexture& GetDefaultShadowArrayTex() const { return ShadowArray; }
        const FTestTexture& GetDefaultShadowTex() const { return ShadowMap; }
        const FTestTexture& GetDefaultPointCubeArrayTex() const { return PointShadowCubes; }

        void DestroyTexture(FTestTexture& InOutTexture) {
            for (FTestTexture& slot : Slots) {
                if (slot == InOutTexture)
                    slot = nullptr;
            }
            InOutTexture.reset();
        }

        /** The state a surface test starts from: no textures in use, no shadows, white dielectric. */
        void ResetShaderUniforms(const TRef<FTestShader>& InShader) {
            if (!InShader)
                return;
            InShader->Bind();
            for (const char* name : {"u_AlphaMode", "u_UseAlbedoMap", "u_UseNormalMap", "u_UseMetallicMap", "u_UseAOMap",
                                     "u_UseRoughnessMap", "u_UseEmissiveMap", "u_UsePlanarReflection",
                                     "u_UsePlanarReflection1", "u_UseIBL", "u_UseShadows", "u_UseSpotShadows",
                                     "u_UsePointShadows", "u_DebugMode"})
                InShader->SetInt(name, 0);
            InShader->SetFloat("u_AlphaCutoff", 0.5f);
            InShader->SetFloat3("u_AlbedoColor", 1.0f, 1.0f, 1.0f);
            InShader->SetFloat("u_Metallic", 0.0f);
            InShader->SetFloat("u_Roughness", 0.5f);
            InShader->SetFloat("u_AO", 1.0f);
            InShader->SetFloat("u_NormalScale", 1.0f);
            InShader->SetFloat("u_OcclusionStrength", 1.0f);
            InShader->SetFloat3("u_EmissiveColor", 0.0f, 0.0f, 0.0f);
            InShader->SetFloat("u_EmissiveIntensity", 0.0f);
            InShader->SetFloat2("u_UVTiling", 1.0f, 1.0f);
            InShader->SetFloat2("u_UVOffset", 0.0f, 0.0f);
        }

        void SetShader(const TRef<FTestShader>& InShader) { Shader = InShader; }

        // --- Drawing and reading ------------------------------------------------------------------

        /** Draws with the bound shader into the render target: the quad, or the whole target for post-process shaders. */
        void DrawQuad() {
            if (!Device || !Shader || !Color)
                return;
            const FTestShader::EKind kind = Shader->GetKind();
            const bool bQuad = kind == FTestShader::EKind::Surface || kind == FTestShader::EKind::ShadowDepth;
            IRHIPipeline* pipeline = PipelineFor(*Shader, bQuad);
            if (!pipeline)
                return;

            IRHICommandList& commands = Device->BeginFrame();
            FRHIRenderPassDesc pass;
            pass.NumColorTargets = 1;
            pass.Color[0].Texture = Color.get();
            pass.Color[0].LoadOp = bClearPending ? ERHILoadOp::Clear : ERHILoadOp::Load;
            pass.Color[0].ClearColor = {0.0f, 0.0f, 0.0f, 0.0f};
            pass.Depth.Texture = Depth.get();
            pass.Name = "TestDraw";
            commands.BeginRenderPass(pass);
            commands.SetPipeline(pipeline);
            SetConstants(commands, *Shader);
            for (uint32_t unit = 0; unit < Slots.size(); ++unit)
                commands.SetTexture(TextureRegister(kind, unit), nullptr);
            for (uint32_t unit = 0; unit < Slots.size(); ++unit) {
                if (Slots[unit])
                    commands.SetTexture(TextureRegister(kind, unit), Slots[unit].get());
            }
            if (bQuad) {
                commands.SetVertexBuffer(QuadVertices.get());
                commands.SetIndexBuffer(QuadIndices.get());
                commands.DrawIndexed(6);
            } else {
                commands.Draw(3);
            }
            commands.EndRenderPass();
            Device->EndFrame();
            bClearPending = false;
            // The quad's (0,0) corner is at the bottom of the target; full-screen shaders put their
            // (0,0) at the top. ReadPixel keeps the tests' convention: y = 0 is where V = 0 is drawn.
            bReadFlipped = bQuad;
        }

        /** The value of a pixel. Y counts from the row where texture coordinate V = 0 was drawn. */
        glm::vec4 ReadPixel(int InX = 0, int InY = 0) {
            glm::vec4 pixel(0.0f);
            if (!Device || !Color)
                return pixel;
            Device->WaitIdle();
            std::vector<uint8_t> bytes;
            if (!Device->ReadTexture(*Color, 0, 0, bytes))
                return pixel;
            const uint32_t width = Color->GetDesc().Width;
            const uint32_t height = Color->GetDesc().Height;
            const uint32_t x = static_cast<uint32_t>(std::clamp(InX, 0, static_cast<int>(width) - 1));
            uint32_t y = static_cast<uint32_t>(std::clamp(InY, 0, static_cast<int>(height) - 1));
            if (bReadFlipped)
                y = height - 1 - y;
            std::memcpy(&pixel, bytes.data() + (static_cast<size_t>(y) * width + x) * 16, 16);
            return pixel;
        }

        /** Every pixel of the render target, four floats each, rows in the order ReadPixel counts them. */
        std::vector<float> ReadPixels() {
            std::vector<float> pixels;
            if (!Device || !Color)
                return pixels;
            Device->WaitIdle();
            std::vector<uint8_t> bytes;
            if (!Device->ReadTexture(*Color, 0, 0, bytes))
                return pixels;
            const size_t width = Color->GetDesc().Width;
            const size_t height = Color->GetDesc().Height;
            pixels.resize(width * height * 4);
            for (size_t y = 0; y < height; ++y) {
                const size_t source = bReadFlipped ? height - 1 - y : y;
                std::memcpy(pixels.data() + y * width * 4, bytes.data() + source * width * 16, width * 16);
            }
            return pixels;
        }

        /** ReadPixels as an 8-bit image would store it: clamped to 0..1 and rounded to 0..255. */
        std::vector<uint8_t> ReadPixelsRGBA8() {
            const std::vector<float> pixels = ReadPixels();
            std::vector<uint8_t> bytes(pixels.size());
            for (size_t i = 0; i < pixels.size(); ++i)
                bytes[i] = static_cast<uint8_t>(std::clamp(pixels[i], 0.0f, 1.0f) * 255.0f + 0.5f);
            return bytes;
        }

        /** A copy of the render target as a texture, to feed the next pass. */
        FTestTexture ResolveColor() {
            if (!Device || !Color)
                return nullptr;
            Device->WaitIdle();
            std::vector<uint8_t> bytes;
            if (!Device->ReadTexture(*Color, 0, 0, bytes))
                return nullptr;
            return Create2D(Color->GetDesc().Width, Color->GetDesc().Height, ERHIFormat::RGBA32_Float, bytes.data());
        }

    private:
        /** Post-process shaders were written with their inputs in units 0 and 1; the HLSL ones differ. */
        static uint32_t TextureRegister(FTestShader::EKind InKind, uint32_t InUnit) {
            if (InKind == FTestShader::EKind::SSAOComposite)
                return InUnit == 0 ? 2u : InUnit; // scene: unit 0 -> t2; occlusion: unit 1 -> t1
            return InUnit;
        }

        IRHIPipeline* PipelineFor(FTestShader& InShader, bool bInQuad) {
            const char* file = "";
            const char* pixelEntry = "PSMain";
            switch (InShader.GetKind()) {
            case FTestShader::EKind::Surface:
                file = "PBR_Lit.hlsl";
                break;
            case FTestShader::EKind::ShadowDepth:
                file = "ShadowDepth.hlsl";
                pixelEntry = InShader.Float("u_PointShadowFarPlane", 0.0f) > 0.0f ? "PSMainLinear" : "PSMain";
                break;
            case FTestShader::EKind::Sky:
                file = "Skybox.hlsl";
                break;
            case FTestShader::EKind::BloomBright:
                file = "Bloom.hlsl";
                pixelEntry = "PSBrightPass";
                break;
            case FTestShader::EKind::BloomDown:
                file = "Bloom.hlsl";
                pixelEntry = "PSDownsample";
                break;
            case FTestShader::EKind::BloomUp:
                file = "Bloom.hlsl";
                pixelEntry = "PSUpsample";
                break;
            case FTestShader::EKind::ToneMapping:
                file = "ToneMapping.hlsl";
                break;
            case FTestShader::EKind::FXAA:
                file = "FXAA.hlsl";
                break;
            case FTestShader::EKind::SSAO:
                file = "SSAO.hlsl";
                pixelEntry = "PSOcclusion";
                break;
            case FTestShader::EKind::SSAOComposite:
                file = "SSAO.hlsl";
                pixelEntry = "PSComposite";
                break;
            case FTestShader::EKind::Unknown:
                return nullptr;
            }
            const std::string key = pixelEntry;
            const auto found = InShader.Pipelines.find(key);
            if (found != InShader.Pipelines.end())
                return found->second.get();

            TRef<IRHIPipeline> pipeline;
            const TRef<IRHIShader> vertexShader = FShaderLibrary::Get(file, "VSMain", ERHIShaderStage::Vertex);
            const TRef<IRHIShader> pixelShader = FShaderLibrary::Get(file, pixelEntry, ERHIShaderStage::Pixel);
            if (vertexShader && pixelShader) {
                FRHIGraphicsPipelineDesc desc;
                desc.VertexShader = vertexShader.get();
                desc.PixelShader = pixelShader.get();
                if (bInQuad) {
                    desc.VertexLayout.Stride = sizeof(FTestVertex);
                    desc.VertexLayout.Attributes = {{ERHIVertexFormat::Float3, offsetof(FTestVertex, Position)},
                                                    {ERHIVertexFormat::Float3, offsetof(FTestVertex, Normal)},
                                                    {ERHIVertexFormat::Float2, offsetof(FTestVertex, TexCoord)},
                                                    {ERHIVertexFormat::Float4, offsetof(FTestVertex, Tangent)},
                                                    {ERHIVertexFormat::Float3, offsetof(FTestVertex, Color)},
                                                    {ERHIVertexFormat::Float2, offsetof(FTestVertex, LightmapUV)}};
                }
                // As the tests always drew: no culling, no depth test, no blending.
                desc.Raster.Cull = ERHICull::None;
                desc.Depth.bTest = false;
                desc.Depth.bWrite = InShader.GetKind() == FTestShader::EKind::ShadowDepth;
                desc.Depth.Compare = ERHICompare::Always;
                if (InShader.GetKind() == FTestShader::EKind::ShadowDepth)
                    desc.Depth.bTest = true;
                desc.ColorFormats[0] = ERHIFormat::RGBA32_Float;
                desc.DepthFormat = ERHIFormat::D32_Float;
                desc.DebugName = "TestPipeline";
                pipeline = Device->CreateGraphicsPipeline(desc);
            }
            InShader.Pipelines[key] = pipeline;
            return pipeline.get();
        }

        void SetConstants(IRHICommandList& InCommands, const FTestShader& InShader) {
            switch (InShader.GetKind()) {
            case FTestShader::EKind::Surface:
                SetSurfaceConstants(InCommands, InShader);
                break;
            case FTestShader::EKind::ShadowDepth: {
                FShadowPassConstants pass;
                pass.LightViewProjection = ClipDepthZeroToOne() * InShader.Mat4("u_LightSpaceMatrix", glm::mat4(1.0f));
                pass.PointLight = glm::vec4(InShader.Vec3("u_PointLightWorldPosition", glm::vec3(0.0f)),
                                            InShader.Float("u_PointShadowFarPlane", 0.0f));
                FShadowMaterialConstants material;
                material.UVTransform = glm::vec4(InShader.Vec2("u_UVTiling", glm::vec2(1.0f)),
                                                 InShader.Vec2("u_UVOffset", glm::vec2(0.0f)));
                const bool bAlphaTest = InShader.Int("u_AlphaMode", 0) == 1 && InShader.Int("u_UseAlbedoMap", 0) == 1;
                material.AlphaTest = glm::vec4(bAlphaTest ? 1.0f : 0.0f, InShader.Float("u_AlphaCutoff", 0.5f), 0.0f, 0.0f);
                const glm::mat4 model = InShader.Mat4("u_Model", glm::mat4(1.0f));
                InCommands.SetConstants(ERHIConstantSlot::Pass, &pass, sizeof(pass));
                InCommands.SetConstants(ERHIConstantSlot::Material, &material, sizeof(material));
                InCommands.SetConstants(ERHIConstantSlot::Draw, &model, sizeof(model));
                break;
            }
            case FTestShader::EKind::Sky: {
                FSkyConstants sky;
                const glm::mat4 view = InShader.Mat4("u_View", glm::mat4(1.0f));
                const glm::mat4 projection = InShader.Mat4("u_Projection", glm::mat4(1.0f));
                sky.ClipToDirection = glm::inverse(ClipDepthZeroToOne() * projection * glm::mat4(glm::mat3(view)));
                sky.SunDirection = glm::vec4(InShader.Vec3("u_SunDir", glm::vec3(0.0f, 1.0f, 0.0f)), 0.0f);
                sky.SkyColor = glm::vec4(InShader.Vec3("u_SkyColor", glm::vec3(0.0f)),
                                         InShader.Float("u_EnvironmentIntensity", 1.0f));
                sky.HorizonColor = glm::vec4(InShader.Vec3("u_HorizonColor", glm::vec3(0.0f)), 0.0f);
                sky.GroundColor = glm::vec4(InShader.Vec3("u_GroundColor", glm::vec3(0.0f)), 0.0f);
                sky.SunColor = glm::vec4(InShader.Vec3("u_SunColor", glm::vec3(0.0f)), InShader.Float("u_SunIntensity", 0.0f));
                sky.SkyFlags = glm::ivec4(InShader.Int("u_UseHDREnvironmentMap", 0), 0, 0, 0);
                InCommands.SetConstants(ERHIConstantSlot::Material, &sky, sizeof(sky));
                break;
            }
            case FTestShader::EKind::BloomBright:
            case FTestShader::EKind::BloomDown:
            case FTestShader::EKind::BloomUp: {
                FBloomConstants bloom;
                bloom.BloomParams = glm::vec4(InShader.Float("u_Threshold", 1.0f), InShader.Float("u_SoftKnee", 0.5f),
                                              InShader.Float("u_FilterRadius", 1.0f),
                                              InShader.Int("u_MipLevel", 0) == 0 ? 1.0f : 0.0f);
                bloom.TexelSize = glm::vec4(InShader.Vec2("u_TexelSize", glm::vec2(0.0f)), 0.0f, 0.0f);
                InCommands.SetConstants(ERHIConstantSlot::Pass, &bloom, sizeof(bloom));
                break;
            }
            case FTestShader::EKind::ToneMapping: {
                FToneMappingConstants tone;
                tone.ToneParams = glm::vec4(InShader.Float("u_Exposure", 1.0f), InShader.Float("u_BloomIntensity", 0.05f),
                                            InShader.Float("u_Gamma", 2.2f), 0.0f);
                tone.ToneFlags = glm::ivec4(InShader.Int("u_ToneMapper", 0), InShader.Int("u_UseBloom", 1),
                                            InShader.Int("u_DebugMode", 0), 0);
                InCommands.SetConstants(ERHIConstantSlot::Pass, &tone, sizeof(tone));
                break;
            }
            case FTestShader::EKind::FXAA: {
                FFXAAConstants fxaa;
                fxaa.FXAAParams = glm::vec4(InShader.Vec2("u_InverseScreenSize", glm::vec2(1.0f)),
                                            static_cast<float>(InShader.Int("u_FXAAEnabled", 1)), 0.0f);
                InCommands.SetConstants(ERHIConstantSlot::Pass, &fxaa, sizeof(fxaa));
                break;
            }
            case FTestShader::EKind::SSAO:
            case FTestShader::EKind::SSAOComposite: {
                FSSAOConstants ssao;
                // The tests give the engine's projection; the shader works in Direct3D's clip space.
                ssao.Projection = ClipDepthZeroToOne() * InShader.Mat4("u_Projection", glm::mat4(1.0f));
                ssao.InverseProjection = glm::inverse(ssao.Projection);
                for (int i = 0; i < 16; ++i)
                    ssao.Samples[i] = glm::vec4(InShader.Vec3("u_Samples[" + std::to_string(i) + "]", glm::vec3(0.0f)), 0.0f);
                ssao.SSAOParams = glm::vec4(InShader.Float("u_Radius", 0.5f), InShader.Float("u_Bias", 0.025f),
                                            static_cast<float>(InShader.Int("u_KernelSize", 16)),
                                            InShader.Float("u_Intensity", 1.0f));
                ssao.BlurParams = glm::vec4(0.0f, 0.0f, 0.0f, static_cast<float>(InShader.Int("u_DebugAO", 0)));
                InCommands.SetConstants(ERHIConstantSlot::Pass, &ssao, sizeof(ssao));
                break;
            }
            case FTestShader::EKind::Unknown:
                break;
            }
        }

        void SetSurfaceConstants(IRHICommandList& InCommands, const FTestShader& InShader) {
            FFrameConstants frame;
            frame.ViewProjection = ClipDepthZeroToOne() * Camera.ViewProjection;
            frame.View = glm::mat4(1.0f);
            frame.ViewPosition = Camera.CameraPosition;
            frame.CameraForward = Camera.CameraForward;
            frame.ViewportSize = {static_cast<float>(GetWidth()), static_cast<float>(GetHeight()),
                                  1.0f / static_cast<float>(GetWidth()), 1.0f / static_cast<float>(GetHeight())};

            FLitPassConstants pass;
            pass.DirLight.Direction = Lighting.DirLight.Direction;
            pass.DirLight.Color = Lighting.DirLight.Color;
            for (int i = 0; i < 16; ++i) {
                pass.PointLights[i].Position = Lighting.PointLights[i].Position;
                pass.PointLights[i].Color = Lighting.PointLights[i].Color;
                pass.PointLights[i].Params = Lighting.PointLights[i].Params;
            }
            for (int i = 0; i < 8; ++i) {
                pass.SpotLights[i].Position = Lighting.SpotLights[i].Position;
                pass.SpotLights[i].Direction = Lighting.SpotLights[i].Direction;
                pass.SpotLights[i].Color = Lighting.SpotLights[i].Color;
                pass.SpotLights[i].Params = Lighting.SpotLights[i].Params;
                // The tests name the shadowed lights in the camera block; the shader reads the layer here.
                pass.SpotLights[i].Params.z = -1.0f;
                if (i == Camera.ShadowSettings.y)
                    pass.SpotLights[i].Params.z = 0.0f;
                for (int extra = 0; extra < 3; ++extra) {
                    if (i == Camera.ExtraShadowedSpots[extra])
                        pass.SpotLights[i].Params.z = static_cast<float>(extra + 1);
                }
            }
            pass.LightCounts = Lighting.LightCounts;
            pass.EnvSkyColor = Lighting.EnvSkyColor;
            pass.EnvHorizonColor = Lighting.EnvHorizonColor;
            pass.EnvGroundColor = Lighting.EnvGroundColor;
            pass.FogColorDensity = Lighting.FogColorDensity;
            pass.FogParams = Lighting.FogParams;
            pass.PassFlags = glm::ivec4(InShader.Int("u_UseIBL", 0), InShader.Int("u_DebugMode", 0), 0, 0);
            for (int i = 0; i < 4; ++i)
                pass.LightSpaceMatrices[i] = ClipToShadowMap() * Camera.LightSpaceMatrices[i];
            pass.SpotLightSpaceMatrices[0] = ClipToShadowMap() * Camera.SpotLightSpaceMatrix;
            for (int extra = 0; extra < 3; ++extra)
                pass.SpotLightSpaceMatrices[extra + 1] = ClipToShadowMap() * Camera.ExtraSpotLightSpaceMatrices[extra];
            pass.CascadeSplits = Camera.CascadeSplits;
            pass.ShadowParams = Camera.ShadowParams;
            pass.ShadowSettings = Camera.ShadowSettings;
            pass.ShadowFlags = glm::ivec4(InShader.Int("u_UseShadows", 0), InShader.Int("u_UseSpotShadows", 0),
                                          InShader.Int("u_UsePointShadows", 0), 0);
            const bool bPlanar = InShader.Int("u_UsePlanarReflection", 0) == 1;
            const bool bPlanarWall = InShader.Int("u_UsePlanarReflection1", 0) == 1;
            pass.PlanarMatrix[0] = ClipToShadowMap() * InShader.Mat4("u_PlanarViewProjection", glm::mat4(1.0f));
            pass.PlanarMatrix[1] = ClipToShadowMap() * InShader.Mat4("u_PlanarViewProjection1", glm::mat4(1.0f));
            pass.PlanarPlane[0] = glm::vec4(InShader.Vec3("u_PlanarPlaneNormal", glm::vec3(0.0f, 1.0f, 0.0f)),
                                            InShader.Float("u_PlanarPlaneDistance", 0.0f));
            pass.PlanarPlane[1] = glm::vec4(InShader.Vec3("u_PlanarPlaneNormal1", glm::vec3(0.0f, 0.0f, 1.0f)),
                                            InShader.Float("u_PlanarPlaneDistance1", 0.0f));
            pass.ClipPlane = InShader.Vec4("u_ClipPlane", glm::vec4(0.0f, 1.0f, 0.0f, 0.0f));
            pass.PlanarFlags = glm::ivec4(bPlanar ? 1 : 0, bPlanarWall ? 1 : 0, InShader.Int("u_EnableClipPlane", 0), 0);

            FMaterialConstants material;
            int textureBits = 0;
            const char* textureFlags[6] = {"u_UseAlbedoMap", "u_UseNormalMap", "u_UseMetallicMap",
                                           "u_UseAOMap",     "u_UseRoughnessMap", "u_UseEmissiveMap"};
            for (int i = 0; i < 6; ++i) {
                if (InShader.Int(textureFlags[i], 0) == 1)
                    textureBits |= 1 << i;
            }
            material.AlbedoColor = glm::vec4(InShader.Vec3("u_AlbedoColor", glm::vec3(0.0f)), InShader.Float("u_Opacity", 1.0f));
            material.EmissiveColor = glm::vec4(InShader.Vec3("u_EmissiveColor", glm::vec3(0.0f)),
                                               InShader.Float("u_EmissiveIntensity", 0.0f));
            material.SurfaceParams = glm::vec4(InShader.Float("u_Metallic", 0.0f), InShader.Float("u_Roughness", 0.0f),
                                               InShader.Float("u_AO", 0.0f), InShader.Float("u_NormalScale", 1.0f));
            material.SurfaceParams2 =
                glm::vec4(InShader.Float("u_OcclusionStrength", 1.0f), InShader.Float("u_AlphaCutoff", 0.5f), 0.0f, 0.0f);
            material.UVTransform = glm::vec4(InShader.Vec2("u_UVTiling", glm::vec2(1.0f)),
                                             InShader.Vec2("u_UVOffset", glm::vec2(0.0f)));
            material.MaterialFlags =
                glm::ivec4(textureBits, InShader.Int("u_AlphaMode", 0), InShader.Int("u_DoubleSided", 0),
                           InShader.Int("u_PackedORM", 0));

            FDrawConstants draw;
            draw.Model = InShader.Mat4("u_Model", glm::mat4(1.0f));
            draw.NormalMatrix = glm::mat4(InShader.Mat3("u_NormalMatrix", glm::mat3(1.0f)));
            draw.LightmapScaleBias = glm::vec4(InShader.Vec2("u_LightmapScale", glm::vec2(1.0f)),
                                               InShader.Vec2("u_LightmapBias", glm::vec2(0.0f)));
            // The old shader took shadows and planar reflections per shader; here they are per draw.
            draw.DrawFlags = glm::ivec4(InShader.Int("u_UseLightmap", 0), InShader.Int("u_LightmapUseTexCoord", 0), 1,
                                        bPlanar ? 1 : 0);

            InCommands.SetConstants(ERHIConstantSlot::Frame, &frame, sizeof(frame));
            InCommands.SetConstants(ERHIConstantSlot::Pass, &pass, sizeof(pass));
            InCommands.SetConstants(ERHIConstantSlot::Material, &material, sizeof(material));
            InCommands.SetConstants(ERHIConstantSlot::Draw, &draw, sizeof(draw));
        }

        IRHIDevice* Device = nullptr;
        bool bInitialized = false;
        bool bEngineResources = false;
        uint64_t ErrorsAtStart = 0;

        FTestTexture Color;
        FTestTexture Depth;
        bool bClearPending = true;
        bool bReadFlipped = true;

        TRef<IRHIBuffer> QuadVertices;
        TRef<IRHIBuffer> QuadIndices;
        TRef<FTestShader> Shader;
        std::array<FTestTexture, kRHITextureSlots> Slots{};

        FCameraBufferData Camera;
        FLightingBufferData Lighting;

        FTestTexture White;
        FTestTexture Black;
        FTestTexture FlatNormal;
        FTestTexture BRDFLut;
        FTestTexture BlackCube;
        FTestTexture ShadowMap;
        FTestTexture ShadowArray;
        FTestTexture PointShadowCubes;
    };

    inline void FTestShader::Bind() {
        if (FHeadlessDevice* device = FHeadlessDevice::Current())
            device->SetShader(shared_from_this());
    }

} // namespace Leon::TestGPU
