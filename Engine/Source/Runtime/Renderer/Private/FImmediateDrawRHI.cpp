#include "Renderer/FImmediateDrawRHI.hpp"

#include "RHI/FRHIFrame.hpp"
#include "Renderer/FShaderConstants.hpp"
#include "Renderer/FShaderLibrary.hpp"

#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace Leon {

    namespace {

        constexpr uint32_t kLineVertexSize = 28; // vec3 + vec4
        constexpr uint32_t kTextVertexSize = 36; // vec3 + vec2 + vec4
        /** Batches kept when nothing is drawing them (no world on screen): beyond this they are dropped. */
        constexpr size_t kMaxPendingBatches = 256;

        struct FSceneBatch {
            ESceneBatch Kind = ESceneBatch::Lines;
            std::vector<uint8_t> Vertices;
            uint32_t Count = 0;
            IRHITexture* Texture = nullptr;
        };

        std::vector<FSceneBatch>& PendingBatches() {
            static std::vector<FSceneBatch> batches;
            return batches;
        }

        /** Null entries are pipelines that failed to build. */
        std::map<std::string, TRef<IRHIPipeline>>& Pipelines() {
            static std::map<std::string, TRef<IRHIPipeline>> pipelines;
            return pipelines;
        }

        uint32_t VertexSize(ESceneBatch InKind) {
            return InKind == ESceneBatch::Text ? kTextVertexSize : kLineVertexSize;
        }

        IRHIPipeline* CanvasPipeline(IRHIDevice& InDevice, ERHIFormat InTargetFormat) {
            const std::string key = "canvas|" + std::to_string(static_cast<int>(InTargetFormat));
            const auto found = Pipelines().find(key);
            if (found != Pipelines().end())
                return found->second.get();

            TRef<IRHIPipeline> pipeline;
            const TRef<IRHIShader> vertexShader = FShaderLibrary::Get("Canvas.hlsl", "VSMain", ERHIShaderStage::Vertex);
            const TRef<IRHIShader> pixelShader = FShaderLibrary::Get("Canvas.hlsl", "PSMain", ERHIShaderStage::Pixel);
            if (vertexShader && pixelShader) {
                FRHIGraphicsPipelineDesc desc;
                desc.VertexShader = vertexShader.get();
                desc.PixelShader = pixelShader.get();
                desc.VertexLayout.Stride = sizeof(FCanvasVertex);
                desc.VertexLayout.Attributes = {{ERHIVertexFormat::Float2, 0},
                                                {ERHIVertexFormat::Float2, 8},
                                                {ERHIVertexFormat::Float4, 16}};
                // The projection flips Y (pixel rows grow downwards), which reverses the winding.
                desc.Raster.Cull = ERHICull::None;
                desc.Depth.bTest = false;
                desc.Depth.bWrite = false;
                desc.Blend.bEnabled = true;
                desc.ColorFormats[0] = InTargetFormat;
                desc.DebugName = "Canvas";
                pipeline = InDevice.CreateGraphicsPipeline(desc);
            }
            Pipelines()[key] = pipeline;
            return pipeline.get();
        }

        IRHIPipeline* ScenePipeline(IRHIDevice& InDevice, ESceneBatch InKind, ERHIFormat InColorFormat,
                                    ERHIFormat InDepthFormat) {
            const std::string key = "scene|" + std::to_string(static_cast<int>(InKind)) + "|" +
                                    std::to_string(static_cast<int>(InColorFormat)) + "|" +
                                    std::to_string(static_cast<int>(InDepthFormat));
            const auto found = Pipelines().find(key);
            if (found != Pipelines().end())
                return found->second.get();

            const bool bText = InKind == ESceneBatch::Text;
            const char* file = bText ? "WorldText.hlsl" : "DebugLine.hlsl";
            TRef<IRHIPipeline> pipeline;
            const TRef<IRHIShader> vertexShader = FShaderLibrary::Get(file, "VSMain", ERHIShaderStage::Vertex);
            const TRef<IRHIShader> pixelShader = FShaderLibrary::Get(file, "PSMain", ERHIShaderStage::Pixel);
            if (vertexShader && pixelShader) {
                FRHIGraphicsPipelineDesc desc;
                desc.VertexShader = vertexShader.get();
                desc.PixelShader = pixelShader.get();
                desc.VertexLayout.Stride = VertexSize(InKind);
                if (bText)
                    desc.VertexLayout.Attributes = {{ERHIVertexFormat::Float3, 0},
                                                    {ERHIVertexFormat::Float2, 12},
                                                    {ERHIVertexFormat::Float4, 20}};
                else
                    desc.VertexLayout.Attributes = {{ERHIVertexFormat::Float3, 0}, {ERHIVertexFormat::Float4, 12}};
                desc.Raster.Cull = ERHICull::None;
                desc.Blend.bEnabled = true;
                switch (InKind) {
                case ESceneBatch::Lines:
                    desc.Topology = ERHITopology::LineList;
                    desc.Depth.bWrite = false;
                    desc.Depth.Compare = ERHICompare::LessEqual;
                    break;
                case ESceneBatch::LinesOnTop:
                    desc.Topology = ERHITopology::LineList;
                    desc.Depth.bTest = false;
                    desc.Depth.bWrite = false;
                    break;
                case ESceneBatch::AdditiveTriangles:
                    desc.Depth.bWrite = false;
                    desc.Blend.DstColor = ERHIBlend::One;
                    desc.Blend.DstAlpha = ERHIBlend::One;
                    break;
                case ESceneBatch::Text:
                    break; // depth tested and written, like the surface it sits on
                }
                desc.ColorFormats[0] = InColorFormat;
                desc.DepthFormat = InDepthFormat;
                desc.DebugName = file;
                pipeline = InDevice.CreateGraphicsPipeline(desc);
            }
            Pipelines()[key] = pipeline;
            return pipeline.get();
        }

    } // namespace

    void FCanvasRHI::DrawTriangles(const FCanvasVertex* InVertices, uint32_t InCount, const glm::mat4& InProjection,
                                   IRHITexture* InAlphaTexture) {
        IRHIDevice* device = FRHI::GetDevice();
        IRHICommandList* commands = FRHIFrame::GetCommands();
        IRHITexture* output = FRHIFrame::GetOutput();
        if (!device || !commands || !output || !InVertices || InCount == 0)
            return;
        IRHIPipeline* pipeline = CanvasPipeline(*device, output->GetDesc().Format);
        if (!pipeline)
            return;

        FCanvasConstants constants;
        constants.Projection = InProjection;
        constants.CanvasFlags.x = InAlphaTexture ? 1 : 0;

        FRHIRenderPassDesc pass;
        pass.NumColorTargets = 1;
        pass.Color[0].Texture = output;
        pass.Color[0].LoadOp = ERHILoadOp::Load;
        pass.Name = "Canvas";
        commands->BeginRenderPass(pass);
        commands->SetPipeline(pipeline);
        commands->SetConstants(ERHIConstantSlot::Pass, &constants, sizeof(constants));
        commands->SetTexture(0, InAlphaTexture);
        commands->SetVertexData(InVertices, InCount * static_cast<uint32_t>(sizeof(FCanvasVertex)), sizeof(FCanvasVertex));
        commands->Draw(InCount);
        commands->EndRenderPass();
    }

    void FSceneBatchesRHI::Submit(ESceneBatch InKind, const void* InVertices, uint32_t InCount, IRHITexture* InTexture) {
        if (!FRHI::GetDevice() || !InVertices || InCount == 0)
            return;
        std::vector<FSceneBatch>& batches = PendingBatches();
        if (batches.size() >= kMaxPendingBatches)
            batches.clear();
        FSceneBatch& batch = batches.emplace_back();
        batch.Kind = InKind;
        batch.Count = InCount;
        batch.Texture = InTexture;
        const auto* bytes = static_cast<const uint8_t*>(InVertices);
        batch.Vertices.assign(bytes, bytes + static_cast<size_t>(InCount) * VertexSize(InKind));
    }

    void FSceneBatchesRHI::Flush(IRHICommandList& InCommands, ERHIFormat InColorFormat, ERHIFormat InDepthFormat) {
        std::vector<FSceneBatch>& batches = PendingBatches();
        IRHIDevice* device = FRHI::GetDevice();
        if (device && !batches.empty()) {
            InCommands.PushMarker("SceneBatches");
            for (const FSceneBatch& batch : batches) {
                IRHIPipeline* pipeline = ScenePipeline(*device, batch.Kind, InColorFormat, InDepthFormat);
                if (!pipeline)
                    continue;
                InCommands.SetPipeline(pipeline);
                if (batch.Kind == ESceneBatch::Text)
                    InCommands.SetTexture(0, batch.Texture);
                InCommands.SetVertexData(batch.Vertices.data(), static_cast<uint32_t>(batch.Vertices.size()),
                                         VertexSize(batch.Kind));
                InCommands.Draw(batch.Count);
            }
            InCommands.PopMarker();
        }
        batches.clear();
    }

    void ShutdownImmediateDrawRHI() {
        Pipelines().clear();
        PendingBatches().clear();
    }

} // namespace Leon
