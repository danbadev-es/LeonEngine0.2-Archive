#include "RHI/FNullRenderDriver.hpp"

#include "RHI/FTextureCube.hpp"
#include "RHI/IRenderDriver.hpp"

#include <algorithm>
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

        class FNullVertexBuffer final : public FVertexBuffer {
        public:
            void Bind() const override {}
            void Unbind() const override {}
            void SetData(const void*, unsigned int) override {}
            const FBufferLayout& GetLayout() const override { return Layout; }
            void SetLayout(const FBufferLayout& InLayout) override { Layout = InLayout; }

        private:
            FBufferLayout Layout;
        };

        class FNullIndexBuffer final : public FIndexBuffer {
        public:
            explicit FNullIndexBuffer(unsigned int InCount) : Count(InCount) {}
            void Bind() const override {}
            void Unbind() const override {}
            unsigned int GetCount() const override { return Count; }

        private:
            unsigned int Count;
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

        class FNullDriver final : public IRenderDriver {
        public:
            TScope<IGraphicsContext> CreateGraphicsContext(void*) override {
                return CreateScope<FNullGraphicsContext>();
            }
            TScope<IRenderAPI> CreateRenderAPI() override { return CreateScope<FNullRenderAPI>(); }
            TRef<FVertexBuffer> CreateVertexBuffer(unsigned int) override { return CreateRef<FNullVertexBuffer>(); }
            TRef<FVertexBuffer> CreateVertexBuffer(const float*, unsigned int) override {
                return CreateRef<FNullVertexBuffer>();
            }
            TRef<FIndexBuffer> CreateIndexBuffer(const uint32_t*, unsigned int InCount) override {
                return CreateRef<FNullIndexBuffer>(InCount);
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

    void FNullRenderDriver::Register() {
        if (!FRenderDriverRegistry::GetDriver(ERenderAPI::Null))
            FRenderDriverRegistry::RegisterDriver(ERenderAPI::Null, CreateScope<FNullDriver>());
        IRenderAPI::SetAPI(ERenderAPI::Null);
    }

} // namespace Leon
