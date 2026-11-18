#include "RHINull/FNullRHI.hpp"

#include "RHI/IRHIDevice.hpp"

#include <algorithm>
#include <cstring>
#include <map>

namespace Leon {

    namespace {

        FNullRHI::FStats GStats;

        class FNullBuffer final : public IRHIBuffer {
        public:
            FNullBuffer(const FRHIBufferDesc& InDesc, const void* InInitialData) : Desc(InDesc) {
                Data.resize(static_cast<size_t>(InDesc.Size));
                if (InInitialData && !Data.empty())
                    std::memcpy(Data.data(), InInitialData, Data.size());
            }
            const FRHIBufferDesc& GetDesc() const override { return Desc; }

            FRHIBufferDesc Desc;
            std::vector<uint8_t> Data;
        };

        class FNullTexture final : public IRHITexture {
        public:
            explicit FNullTexture(const FRHITextureDesc& InDesc) : Desc(InDesc) {}
            const FRHITextureDesc& GetDesc() const override { return Desc; }

            static uint64_t Key(uint32_t InMip, uint32_t InSlice) { return (static_cast<uint64_t>(InSlice) << 8) | InMip; }

            FRHITextureDesc Desc;
            /** Pixels per (mip, slice), tightly packed. Absent until uploaded. */
            std::map<uint64_t, std::vector<uint8_t>> Subresources;
        };

        class FNullShader final : public IRHIShader {
        public:
            explicit FNullShader(ERHIShaderStage InStage) : Stage(InStage) {}
            ERHIShaderStage GetStage() const override { return Stage; }

        private:
            ERHIShaderStage Stage;
        };

        class FNullPipeline final : public IRHIPipeline {};

        class FNullCommandList final : public IRHICommandList {
        public:
            void BeginRenderPass(const FRHIRenderPassDesc&) override {
                bInPass = true;
                ++GStats.RenderPasses;
            }
            void EndRenderPass() override { bInPass = false; }
            void SetViewport(const FRHIViewport&) override {}
            void SetScissor(const FRHIRect&) override {}
            void SetPipeline(IRHIPipeline*) override {}
            void SetVertexBuffer(IRHIBuffer*) override {}
            void SetIndexBuffer(IRHIBuffer*) override {}
            void SetVertexData(const void*, uint32_t, uint32_t) override {}
            void SetIndexData(const void*, uint32_t, uint32_t) override {}
            void SetConstants(ERHIConstantSlot, const void*, uint32_t) override {}
            void SetTexture(uint32_t, IRHITexture*) override {}
            void Draw(uint32_t, uint32_t) override {
                if (bInPass)
                    ++GStats.DrawCalls;
            }
            void DrawIndexed(uint32_t, uint32_t, int32_t, uint32_t) override {
                if (bInPass)
                    ++GStats.DrawCalls;
            }
            void GenerateMips(IRHITexture&, IRHIPipeline*) override {}
            void PushMarker(const char*) override {}
            void PopMarker() override {}

            bool bInPass = false;
        };

        class FNullSwapchain final : public IRHISwapchain {
        public:
            FNullSwapchain(uint32_t InWidth, uint32_t InHeight) { Recreate(InWidth, InHeight); }
            uint32_t GetWidth() const override { return BackBuffer->Desc.Width; }
            uint32_t GetHeight() const override { return BackBuffer->Desc.Height; }
            IRHITexture* GetBackBuffer() override { return BackBuffer.get(); }
            void Resize(uint32_t InWidth, uint32_t InHeight) override {
                if (InWidth > 0 && InHeight > 0)
                    Recreate(InWidth, InHeight);
            }
            void Present(bool) override {}

        private:
            void Recreate(uint32_t InWidth, uint32_t InHeight) {
                FRHITextureDesc desc;
                desc.Width = std::max(InWidth, 1u);
                desc.Height = std::max(InHeight, 1u);
                desc.Usage = RHITextureUsage_RenderTarget;
                desc.DebugName = "NullBackBuffer";
                BackBuffer = CreateRef<FNullTexture>(desc);
            }
            TRef<FNullTexture> BackBuffer;
        };

        class FNullDevice final : public IRHIDevice {
        public:
            FNullDevice() {
                Info.Backend = ERHIBackend::Null;
                Info.AdapterName = "Null device";
                Info.bSoftwareAdapter = true;
                GStats = {};
            }

            const FRHIDeviceInfo& GetInfo() const override { return Info; }

            TRef<IRHIBuffer> CreateBuffer(const FRHIBufferDesc& InDesc, const void* InInitialData) override {
                return CreateRef<FNullBuffer>(InDesc, InInitialData);
            }

            void WriteBuffer(IRHIBuffer& InBuffer, const void* InData, uint64_t InSize, uint64_t InOffset) override {
                auto& buffer = static_cast<FNullBuffer&>(InBuffer);
                if (!InData || InOffset + InSize > buffer.Data.size())
                    return;
                std::memcpy(buffer.Data.data() + InOffset, InData, static_cast<size_t>(InSize));
            }

            TRef<IRHITexture> CreateTexture(const FRHITextureDesc& InDesc) override {
                return CreateRef<FNullTexture>(InDesc);
            }

            void UploadTexture(IRHITexture& InTexture, uint32_t InMipLevel, uint32_t InArraySlice, const void* InData,
                               uint32_t InRowPitch) override {
                auto& texture = static_cast<FNullTexture&>(InTexture);
                if (!InData || InMipLevel >= texture.Desc.MipLevels || InArraySlice >= texture.Desc.SliceCount())
                    return;
                const uint32_t width = std::max(texture.Desc.Width >> InMipLevel, 1u);
                const uint32_t height = std::max(texture.Desc.Height >> InMipLevel, 1u);
                const uint32_t rowBytes = RHIFormatRowBytes(texture.Desc.Format, width);
                const uint32_t rows = RHIFormatRowCount(texture.Desc.Format, height);
                std::vector<uint8_t>& pixels = texture.Subresources[FNullTexture::Key(InMipLevel, InArraySlice)];
                pixels.resize(static_cast<size_t>(rowBytes) * rows);
                for (uint32_t y = 0; y < rows; ++y)
                    std::memcpy(pixels.data() + static_cast<size_t>(y) * rowBytes,
                                static_cast<const uint8_t*>(InData) + static_cast<size_t>(y) * InRowPitch, rowBytes);
            }

            bool ReadTexture(IRHITexture& InTexture, uint32_t InMipLevel, uint32_t InArraySlice,
                             std::vector<uint8_t>& OutPixels) override {
                auto& texture = static_cast<FNullTexture&>(InTexture);
                if (InMipLevel >= texture.Desc.MipLevels || InArraySlice >= texture.Desc.SliceCount())
                    return false;
                const auto found = texture.Subresources.find(FNullTexture::Key(InMipLevel, InArraySlice));
                if (found != texture.Subresources.end()) {
                    OutPixels = found->second;
                    return true;
                }
                // Never written: reads as zero, like a freshly cleared target.
                const uint32_t width = std::max(texture.Desc.Width >> InMipLevel, 1u);
                const uint32_t height = std::max(texture.Desc.Height >> InMipLevel, 1u);
                OutPixels.assign(static_cast<size_t>(RHIFormatRowBytes(texture.Desc.Format, width)) *
                                     RHIFormatRowCount(texture.Desc.Format, height),
                                 0);
                return true;
            }

            TRef<IRHIShader> CreateShader(const FRHIShaderDesc& InDesc) override {
                return CreateRef<FNullShader>(InDesc.Stage);
            }

            TRef<IRHIPipeline> CreateGraphicsPipeline(const FRHIGraphicsPipelineDesc& InDesc) override {
                // A pipeline without a vertex shader is a caller bug on every backend.
                if (!InDesc.VertexShader)
                    return nullptr;
                return CreateRef<FNullPipeline>();
            }

            TScope<IRHISwapchain> CreateSwapchain(void*, uint32_t InWidth, uint32_t InHeight) override {
                return CreateScope<FNullSwapchain>(InWidth, InHeight);
            }

            IRHICommandList& BeginFrame() override {
                CommandList.bInPass = false;
                return CommandList;
            }

            void EndFrame() override { ++GStats.Frames; }
            void WaitIdle() override {}

        private:
            FRHIDeviceInfo Info;
            FNullCommandList CommandList;
        };

    } // namespace

    void FNullRHI::Register() {
        if (FRHI::IsBackendRegistered(ERHIBackend::Null))
            return;
        FRHI::RegisterBackend(ERHIBackend::Null,
                              [](const FRHIInitDesc&) -> TScope<IRHIDevice> { return CreateScope<FNullDevice>(); });
    }

    FNullRHI::FStats FNullRHI::GetStats() {
        return GStats;
    }

} // namespace Leon
