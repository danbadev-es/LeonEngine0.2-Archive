#include "RHI/FLegacyRenderDriver.hpp"

#include "RHI/FTextureCube.hpp"
#include "RHI/IRHIDevice.hpp"
#include "RHI/IRenderDriver.hpp"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace Leon {

    namespace {

        class FNullGraphicsContext final : public IGraphicsContext {
        public:
            void Init() override {}
            void SwapBuffers() override {}
        };

        class FNullRenderAPI final : public IRenderAPI {
        public:
            void Init() override {}
            void SetViewport(unsigned int, unsigned int, unsigned int, unsigned int) override {}
            void SetClearColor(float, float, float, float) override {}
            void Clear() override {}
            void SetDepthTesting(bool) override {}
            void SetDepthMask(bool) override {}
            void SetDepthFunc(EDepthFunc) override {}
            void SetCulling(bool, ECullMode) override {}
            void SetWireframe(bool) override {}
            void SetBlendState(bool) override {}
            void SetBlendFunc(EBlendFactor, EBlendFactor) override {}
            void SetClipDistance(bool) override {}
            void SetPolygonOffset(bool, float, float) override {}
            uint32_t GetFramebufferBinding() override { return 0; }
            void BindFramebuffer(uint32_t) override {}
            void DrawArrays(const TRef<FVertexArray>&, unsigned int) override {}
            void DrawIndexed(const TRef<FVertexArray>&, unsigned int) override {}
            void DrawIndexedOffset(const TRef<FVertexArray>&, unsigned int, unsigned int) override {}
            void DrawIndexedInstanced(const TRef<FVertexArray>&, unsigned int, unsigned int) override {}
            void DrawIndexedOffsetInstanced(const TRef<FVertexArray>&, unsigned int, unsigned int,
                                            unsigned int) override {}
            void DrawLines(const TRef<FVertexArray>&, unsigned int) override {}
            void SetLineWidth(float) override {}
            FGPUInfo GetGPUInfo() override {
                FGPUInfo info;
                info.Vendor = "None";
                info.Renderer = "Null render driver";
                info.Version = "0";
                return info;
            }
            FGPUVRAMStats GetGPUVRAMStats() override { return {}; }
        };

        /** Vertex data kept in memory until the first draw needs it on the device. */
        class FLegacyVertexBuffer final : public FVertexBuffer {
        public:
            explicit FLegacyVertexBuffer(unsigned int InSize) : Bytes(InSize), bDynamic(true) {}
            FLegacyVertexBuffer(const void* InData, unsigned int InSize)
                : Bytes(static_cast<const uint8_t*>(InData), static_cast<const uint8_t*>(InData) + InSize) {}

            void Bind() const override {}
            void Unbind() const override {}
            void SetData(const void* InData, unsigned int InSize) override {
                if (!InData)
                    return;
                if (InSize > Bytes.size())
                    Bytes.resize(InSize);
                std::memcpy(Bytes.data(), InData, InSize);
                if (Buffer && bDynamic && InSize <= Buffer->GetDesc().Size) {
                    if (IRHIDevice* device = FRHI::GetDevice())
                        device->WriteBuffer(*Buffer, InData, InSize);
                } else {
                    Buffer.reset(); // grew, or was static: recreate on next use
                }
            }
            const FBufferLayout& GetLayout() const override { return Layout; }
            void SetLayout(const FBufferLayout& InLayout) override { Layout = InLayout; }

            IRHIBuffer* GetOrCreate() {
                IRHIDevice* device = FRHI::GetDevice();
                if (!device || Bytes.empty())
                    return nullptr;
                if (!Buffer) {
                    FRHIBufferDesc desc;
                    desc.Size = Bytes.size();
                    desc.Usage = ERHIBufferUsage::Vertex;
                    desc.Stride = Layout.GetStride();
                    desc.bDynamic = bDynamic;
                    desc.DebugName = "LegacyVertexBuffer";
                    Buffer = device->CreateBuffer(desc, Bytes.data());
                }
                return Buffer.get();
            }

        private:
            FBufferLayout Layout;
            std::vector<uint8_t> Bytes;
            TRef<IRHIBuffer> Buffer;
            bool bDynamic = false;
        };

        class FLegacyIndexBuffer final : public FIndexBuffer {
        public:
            FLegacyIndexBuffer(const uint32_t* InIndices, unsigned int InCount)
                : Indices(InIndices ? std::vector<uint32_t>(InIndices, InIndices + InCount) : std::vector<uint32_t>()),
                  Count(InCount) {}
            void Bind() const override {}
            void Unbind() const override {}
            unsigned int GetCount() const override { return Count; }

            IRHIBuffer* GetOrCreate() {
                IRHIDevice* device = FRHI::GetDevice();
                if (!device || Indices.empty())
                    return nullptr;
                if (!Buffer) {
                    FRHIBufferDesc desc;
                    desc.Size = Indices.size() * sizeof(uint32_t);
                    desc.Usage = ERHIBufferUsage::Index;
                    desc.Stride = sizeof(uint32_t);
                    desc.DebugName = "LegacyIndexBuffer";
                    Buffer = device->CreateBuffer(desc, Indices.data());
                }
                return Buffer.get();
            }

        private:
            std::vector<uint32_t> Indices;
            unsigned int Count;
            TRef<IRHIBuffer> Buffer;
        };

        class FNullUniformBuffer final : public FUniformBuffer {
        public:
            explicit FNullUniformBuffer(unsigned int InBinding) : Binding(InBinding) {}
            void SetData(const void*, unsigned int, unsigned int) override {}
            unsigned int GetBinding() const override { return Binding; }

        private:
            unsigned int Binding;
        };

        class FNullVertexArray final : public FVertexArray {
        public:
            void Bind() const override {}
            void Unbind() const override {}
            void AddVertexBuffer(const TRef<FVertexBuffer>& InVertexBuffer) override {
                VertexBuffers.push_back(InVertexBuffer);
            }
            void SetIndexBuffer(const TRef<FIndexBuffer>& InIndexBuffer) override { IndexBuffer = InIndexBuffer; }
            const std::vector<TRef<FVertexBuffer>>& GetVertexBuffers() const override { return VertexBuffers; }
            const TRef<FIndexBuffer>& GetIndexBuffer() const override { return IndexBuffer; }

        private:
            std::vector<TRef<FVertexBuffer>> VertexBuffers;
            TRef<FIndexBuffer> IndexBuffer;
        };

        class FNullShader final : public FShader {
        public:
            explicit FNullShader(std::string InName) : Name(std::move(InName)) {}
            void Bind() const override {}
            void Unbind() const override {}
            void SetInt(const std::string&, int) override {}
            void SetFloat(const std::string&, float) override {}
            void SetFloat2(const std::string&, float, float) override {}
            void SetFloat3(const std::string&, float, float, float) override {}
            void SetFloat4(const std::string&, float, float, float, float) override {}
            void SetMat3(const std::string&, const float*) override {}
            void SetMat4(const std::string&, const float*) override {}
            const std::string& GetName() const override { return Name; }

        private:
            std::string Name;
        };

        uint32_t NextNullId() {
            static uint32_t next = 1;
            return next++;
        }

        class FNullTexture2D final : public FTexture2D {
        public:
            FNullTexture2D(uint32_t InWidth, uint32_t InHeight, std::string InPath = {})
                : Width(std::max(InWidth, 1u)), Height(std::max(InHeight, 1u)), Path(std::move(InPath)),
                  Id(NextNullId()) {}
            uint32_t GetWidth() const override { return Width; }
            uint32_t GetHeight() const override { return Height; }
            uint32_t GetRendererID() const override { return Id; }
            const std::string& GetPath() const override { return Path; }
            void SetData(void*, uint32_t) override {}
            void Bind(uint32_t) const override {}
            bool IsLoaded() const override { return true; }
            bool operator==(const FTexture& InOther) const override { return Id == InOther.GetRendererID(); }

        private:
            uint32_t Width;
            uint32_t Height;
            std::string Path;
            uint32_t Id;
        };

        class FNullTextureCube final : public FTextureCube {
        public:
            FNullTextureCube(uint32_t InWidth, uint32_t InHeight)
                : Width(std::max(InWidth, 1u)), Height(std::max(InHeight, 1u)), Id(NextNullId()) {}
            uint32_t GetWidth() const override { return Width; }
            uint32_t GetHeight() const override { return Height; }
            uint32_t GetRendererID() const override { return Id; }
            void Bind(uint32_t) const override {}
            void Unbind() const override {}
            bool IsLoaded() const override { return true; }
            void SetFaceData(uint32_t, const void*, uint32_t, uint32_t, uint32_t, bool) override {}
            void GenerateMipmaps() override {}

        private:
            uint32_t Width;
            uint32_t Height;
            uint32_t Id;
        };

        class FNullFramebuffer final : public FFramebuffer {
        public:
            explicit FNullFramebuffer(const FFramebufferSpecification& InSpec) : Specification(InSpec), Id(NextNullId()) {}
            void Bind() override {}
            void Unbind() override {}
            void Resize(uint32_t InWidth, uint32_t InHeight) override {
                Specification.Width = InWidth;
                Specification.Height = InHeight;
            }
            int ReadPixel(uint32_t, int, int) override { return 0; }
            void ClearAttachment(uint32_t, int) override {}
            uint32_t GetRendererID() const override { return Id; }
            uint32_t GetColorAttachmentRendererID(uint32_t) const override { return Id; }
            uint32_t GetDepthAttachmentRendererID() const override { return Id; }
            void BindTexture(uint32_t, uint32_t) const override {}
            void BindDepthTexture(uint32_t) const override {}
            void AttachDepthTextureLayer(uint32_t) override {}
            void BlitToDefault(uint32_t, uint32_t) override {}
            void GenerateColorMipmaps() override {}
            const FFramebufferSpecification& GetSpecification() const override { return Specification; }

        private:
            FFramebufferSpecification Specification;
            uint32_t Id;
        };

        class FLegacyDriver final : public IRenderDriver {
        public:
            TScope<IGraphicsContext> CreateGraphicsContext(void*) override {
                return CreateScope<FNullGraphicsContext>();
            }
            TScope<IRenderAPI> CreateRenderAPI() override { return CreateScope<FNullRenderAPI>(); }
            TRef<FVertexBuffer> CreateVertexBuffer(unsigned int InSize) override {
                return CreateRef<FLegacyVertexBuffer>(InSize);
            }
            TRef<FVertexBuffer> CreateVertexBuffer(const float* InVertices, unsigned int InSize) override {
                if (!InVertices)
                    return CreateRef<FLegacyVertexBuffer>(InSize);
                return CreateRef<FLegacyVertexBuffer>(static_cast<const void*>(InVertices), InSize);
            }
            TRef<FIndexBuffer> CreateIndexBuffer(const uint32_t* InIndices, unsigned int InCount) override {
                return CreateRef<FLegacyIndexBuffer>(InIndices, InCount);
            }
            TRef<FVertexArray> CreateVertexArray() override { return CreateRef<FNullVertexArray>(); }
            TRef<FShader> CreateShader(const std::string& InFilePath) override {
                return CreateRef<FNullShader>(InFilePath);
            }
            TRef<FShader> CreateShader(const std::string& InName, const std::string&, const std::string&) override {
                return CreateRef<FNullShader>(InName);
            }
            TRef<FTexture2D> CreateTexture2D(uint32_t InWidth, uint32_t InHeight) override {
                return CreateRef<FNullTexture2D>(InWidth, InHeight);
            }
            TRef<FTexture2D> CreateTexture2D(const std::string& InPath) override {
                return CreateRef<FNullTexture2D>(1u, 1u, InPath);
            }
            TRef<FTexture2D> CreateTexture2DWithFormat(uint32_t InWidth, uint32_t InHeight, ETextureFormat) override {
                return CreateRef<FNullTexture2D>(InWidth, InHeight);
            }
            TRef<FTextureCube> CreateTextureCube(uint32_t InWidth, uint32_t InHeight, bool) override {
                return CreateRef<FNullTextureCube>(InWidth, InHeight);
            }
            TRef<FTextureCube> CreateTextureCube(const std::vector<std::string>&) override {
                return CreateRef<FNullTextureCube>(1u, 1u);
            }
            TRef<FFramebuffer> CreateFramebuffer(const FFramebufferSpecification& InSpec) override {
                return CreateRef<FNullFramebuffer>(InSpec);
            }
            TRef<FUniformBuffer> CreateUniformBuffer(unsigned int, unsigned int InBinding) override {
                return CreateRef<FNullUniformBuffer>(InBinding);
            }
        };

    } // namespace

    IRHIBuffer* GetRHIVertexBuffer(const FVertexArray& InVertexArray) {
        const auto& buffers = InVertexArray.GetVertexBuffers();
        if (buffers.empty())
            return nullptr;
        auto* buffer = dynamic_cast<FLegacyVertexBuffer*>(buffers.front().get());
        return buffer ? buffer->GetOrCreate() : nullptr;
    }

    IRHIBuffer* GetRHIIndexBuffer(const FVertexArray& InVertexArray) {
        auto* buffer = dynamic_cast<FLegacyIndexBuffer*>(InVertexArray.GetIndexBuffer().get());
        return buffer ? buffer->GetOrCreate() : nullptr;
    }

    uint32_t GetRHIIndexCount(const FVertexArray& InVertexArray) {
        const auto& indices = InVertexArray.GetIndexBuffer();
        return indices ? indices->GetCount() : 0;
    }

    FRHIVertexLayout MakeRHIVertexLayout(const FBufferLayout& InLayout) {
        FRHIVertexLayout layout;
        layout.Stride = InLayout.GetStride();
        for (const FBufferElement& element : InLayout.GetElements()) {
            FRHIVertexAttribute attribute;
            attribute.Offset = static_cast<uint32_t>(element.Offset);
            switch (element.Type) {
            case EShaderDataType::Float:
                attribute.Format = ERHIVertexFormat::Float;
                break;
            case EShaderDataType::Float2:
                attribute.Format = ERHIVertexFormat::Float2;
                break;
            case EShaderDataType::Float3:
                attribute.Format = ERHIVertexFormat::Float3;
                break;
            case EShaderDataType::Float4:
                attribute.Format = ERHIVertexFormat::Float4;
                break;
            // Bone indices: integers in the buffer, and integers in HLSL too (int4).
            case EShaderDataType::Int4:
            case EShaderDataType::Int4AsFloat:
                attribute.Format = ERHIVertexFormat::Int4;
                break;
            default:
                continue; // not used by any engine vertex format
            }
            layout.Attributes.push_back(attribute);
        }
        return layout;
    }

    FRHIVertexLayout GetRHIVertexLayout(const FVertexArray& InVertexArray) {
        const auto& buffers = InVertexArray.GetVertexBuffers();
        if (buffers.empty() || !buffers.front())
            return {};
        return MakeRHIVertexLayout(buffers.front()->GetLayout());
    }

    void FLegacyRenderDriver::Register() {
        if (!FRenderDriverRegistry::GetDriver(ERenderAPI::Null))
            FRenderDriverRegistry::RegisterDriver(ERenderAPI::Null, CreateScope<FLegacyDriver>());
        IRenderAPI::SetAPI(ERenderAPI::Null);
    }

} // namespace Leon
