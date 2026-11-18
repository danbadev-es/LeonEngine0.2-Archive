#include "FImGuiRHI.hpp"

#include "Core/FLog.hpp"
#include "RenderCore/FShaderLibrary.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <map>
#include <vector>

namespace Leon::Editor {

    namespace {

        struct FImGuiConstants { // ImGui.hlsl
            glm::mat4 Projection{1.0f};
        };

        TRef<IRHITexture>& FontAtlas() {
            static TRef<IRHITexture> texture;
            return texture;
        }

        /** One pipeline per target format. Null entries are pipelines that failed to build. */
        std::map<ERHIFormat, TRef<IRHIPipeline>>& Pipelines() {
            static std::map<ERHIFormat, TRef<IRHIPipeline>> pipelines;
            return pipelines;
        }

        IRHIPipeline* PipelineFor(IRHIDevice& InDevice, ERHIFormat InFormat) {
            const auto found = Pipelines().find(InFormat);
            if (found != Pipelines().end())
                return found->second.get();

            TRef<IRHIPipeline> pipeline;
            const TRef<IRHIShader> vertexShader = FShaderLibrary::Get("ImGui.hlsl", "VSMain", ERHIShaderStage::Vertex);
            const TRef<IRHIShader> pixelShader = FShaderLibrary::Get("ImGui.hlsl", "PSMain", ERHIShaderStage::Pixel);
            if (vertexShader && pixelShader) {
                FRHIGraphicsPipelineDesc desc;
                desc.VertexShader = vertexShader.get();
                desc.PixelShader = pixelShader.get();
                desc.VertexLayout.Stride = sizeof(ImDrawVert);
                desc.VertexLayout.Attributes = {{ERHIVertexFormat::Float2, offsetof(ImDrawVert, pos)},
                                                {ERHIVertexFormat::Float2, offsetof(ImDrawVert, uv)},
                                                {ERHIVertexFormat::UByte4_UNorm, offsetof(ImDrawVert, col)}};
                desc.Raster.Cull = ERHICull::None;
                desc.Depth.bTest = false;
                desc.Depth.bWrite = false;
                desc.Blend.bEnabled = true;
                desc.ColorFormats[0] = InFormat;
                desc.DebugName = "ImGui";
                pipeline = InDevice.CreateGraphicsPipeline(desc);
            }
            Pipelines()[InFormat] = pipeline;
            return pipeline.get();
        }

    } // namespace

    bool FImGuiRHI::Init() {
        IRHIDevice* device = FRHI::GetDevice();
        if (!device)
            return false;

        ImGuiIO& io = ImGui::GetIO();
        io.BackendRendererName = "LeonEngine RHI";
        // Lists with more than 65535 vertices are split into commands with a vertex offset.
        io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
        unsigned char* pixels = nullptr;
        int width = 0;
        int height = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        if (!pixels || width <= 0 || height <= 0)
            return false;

        FRHITextureDesc desc;
        desc.Width = static_cast<uint32_t>(width);
        desc.Height = static_cast<uint32_t>(height);
        desc.Format = ERHIFormat::RGBA8_UNorm;
        desc.DebugName = "ImGuiFontAtlas";
        FontAtlas() = device->CreateTexture(desc);
        if (!FontAtlas())
            return false;
        device->UploadTexture(*FontAtlas(), 0, 0, pixels, static_cast<uint32_t>(width) * 4);
        io.Fonts->SetTexID(ToImGuiTexture(FontAtlas().get()));
        return true;
    }

    TRef<IRHITexture> FImGuiRHI::CreateTexture(const unsigned char* InPixels, int InWidth, int InHeight,
                                               const char* InDebugName) {
        IRHIDevice* device = FRHI::GetDevice();
        if (!device || !InPixels || InWidth <= 0 || InHeight <= 0)
            return nullptr;
        FRHITextureDesc desc;
        desc.Width = static_cast<uint32_t>(InWidth);
        desc.Height = static_cast<uint32_t>(InHeight);
        desc.Format = ERHIFormat::RGBA8_UNorm;
        desc.DebugName = InDebugName ? InDebugName : "EditorTexture";
        TRef<IRHITexture> texture = device->CreateTexture(desc);
        if (texture)
            device->UploadTexture(*texture, 0, 0, InPixels, static_cast<uint32_t>(InWidth) * 4);
        return texture;
    }

    void FImGuiRHI::Shutdown() {
        if (ImGui::GetCurrentContext())
            ImGui::GetIO().Fonts->SetTexID(0);
        FontAtlas().reset();
        Pipelines().clear();
    }

    void FImGuiRHI::Render(const ImDrawData* InDrawData, IRHICommandList& InCommands, IRHITexture& InTarget) {
        IRHIDevice* device = FRHI::GetDevice();
        if (!device || !InDrawData || InDrawData->CmdListsCount == 0)
            return;
        const float targetWidth = static_cast<float>(InTarget.GetDesc().Width);
        const float targetHeight = static_cast<float>(InTarget.GetDesc().Height);
        if (InDrawData->DisplaySize.x <= 0.0f || InDrawData->DisplaySize.y <= 0.0f)
            return;
        IRHIPipeline* pipeline = PipelineFor(*device, InTarget.GetDesc().Format);
        if (!pipeline)
            return;

        // ImGui positions are in its display space, which starts at DisplayPos and grows downwards.
        const ImVec2 origin = InDrawData->DisplayPos;
        FImGuiConstants constants;
        constants.Projection = glm::ortho(origin.x, origin.x + InDrawData->DisplaySize.x,
                                          origin.y + InDrawData->DisplaySize.y, origin.y, -1.0f, 1.0f);

        FRHIRenderPassDesc pass;
        pass.NumColorTargets = 1;
        pass.Color[0].Texture = &InTarget;
        pass.Color[0].LoadOp = ERHILoadOp::Load;
        pass.Name = "ImGui";
        InCommands.BeginRenderPass(pass);
        InCommands.SetPipeline(pipeline);
        InCommands.SetConstants(ERHIConstantSlot::Pass, &constants, sizeof(constants));

        for (int listIndex = 0; listIndex < InDrawData->CmdListsCount; ++listIndex) {
            const ImDrawList* list = InDrawData->CmdLists[listIndex];
            if (list->IdxBuffer.Size == 0 || list->VtxBuffer.Size == 0)
                continue;
            // ImGui's own buffers go to the GPU as they are; they only live for this frame.
            InCommands.SetVertexData(list->VtxBuffer.Data, static_cast<uint32_t>(list->VtxBuffer.Size * sizeof(ImDrawVert)),
                                     sizeof(ImDrawVert));
            InCommands.SetIndexData(list->IdxBuffer.Data, static_cast<uint32_t>(list->IdxBuffer.Size * sizeof(ImDrawIdx)),
                                    sizeof(ImDrawIdx));

            for (const ImDrawCmd& command : list->CmdBuffer) {
                if (command.UserCallback || command.ElemCount == 0)
                    continue; // callbacks are not used by the editor
                FRHIRect scissor;
                scissor.Left = static_cast<int32_t>(std::max(command.ClipRect.x - origin.x, 0.0f));
                scissor.Top = static_cast<int32_t>(std::max(command.ClipRect.y - origin.y, 0.0f));
                scissor.Right = static_cast<int32_t>(std::min(command.ClipRect.z - origin.x, targetWidth));
                scissor.Bottom = static_cast<int32_t>(std::min(command.ClipRect.w - origin.y, targetHeight));
                if (scissor.Right <= scissor.Left || scissor.Bottom <= scissor.Top)
                    continue;
                InCommands.SetScissor(scissor);
                InCommands.SetTexture(0, reinterpret_cast<IRHITexture*>(static_cast<intptr_t>(command.GetTexID())));
                InCommands.DrawIndexed(command.ElemCount, command.IdxOffset, static_cast<int32_t>(command.VtxOffset));
            }
        }
        InCommands.EndRenderPass();
    }

} // namespace Leon::Editor
