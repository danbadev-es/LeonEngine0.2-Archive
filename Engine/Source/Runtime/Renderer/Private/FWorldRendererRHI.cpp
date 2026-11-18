// The world renderer on the new RHI. Passes are moved here one at a time from the files written
// against the old interfaces (FWorldRendererGeometry.cpp, FWorldRendererLighting.cpp, ...).

#include "Renderer/FWorldRenderer.hpp"

#include "FWorldRendererInternals.hpp"

#include "Assets/UAssetManager.hpp"
#include "Assets/UStaticMesh.hpp"
#include "Core/FLog.hpp"
#include "Engine/Components.hpp"
#include "Engine/UWorld.hpp"
#include "RHI/FLegacyRenderDriver.hpp"
#include "RHI/IRHIDevice.hpp"
#include "Renderer/FMaterialInstance.hpp"
#include "Renderer/FShaderLibrary.hpp"

#include <glm/gtc/matrix_inverse.hpp>

#include <map>
#include <string>

namespace Leon {

    namespace {

        // Mirrors of the constant buffers in Engine/Shaders. Keep field order and types identical.
        struct FFrameConstants {
            glm::mat4 ViewProjection;
            glm::mat4 View;
            glm::vec4 ViewPosition;
            glm::vec4 ViewportSize;
        };

        struct FMaterialConstants {
            glm::vec4 BaseColor;
        };

        struct FDrawConstants {
            glm::mat4 Model;
            glm::mat4 NormalMatrix;
        };

        /**
         * The engine's projection matrices produce clip depth in -1..1; Direct3D expects 0..1.
         * Applied here until the cameras build 0..1 projections themselves.
         */
        glm::mat4 ClipDepthZeroToOne() {
            glm::mat4 fix(1.0f);
            fix[2][2] = 0.5f;
            fix[3][2] = 0.5f;
            return fix;
        }

        std::string LayoutKey(const FRHIVertexLayout& InLayout) {
            std::string key = std::to_string(InLayout.Stride);
            for (const FRHIVertexAttribute& attribute : InLayout.Attributes)
                key += ":" + std::to_string(static_cast<int>(attribute.Format)) + "@" + std::to_string(attribute.Offset);
            return key;
        }

    } // namespace

    struct FWorldRendererRHIState {
        TRef<IRHITexture> Depth;
        /** One pipeline per vertex format drawn so far. A null entry is a pipeline that failed to build. */
        std::map<std::string, TRef<IRHIPipeline>> OpaquePipelines;
    };

    void FWorldRenderer::RenderRHI(const FPerspectiveCamera& InCamera, IRHICommandList& InCommands,
                                   IRHITexture& InTarget) {
        IRHIDevice* device = FRHI::GetDevice();
        if (!device || !World)
            return;
        if (!RHIState)
            RHIState = std::make_shared<FWorldRendererRHIState>();
        FWorldRendererRHIState& state = *RHIState;

        const uint32_t width = InTarget.GetDesc().Width;
        const uint32_t height = InTarget.GetDesc().Height;
        if (!state.Depth || state.Depth->GetDesc().Width != width || state.Depth->GetDesc().Height != height) {
            FRHITextureDesc depthDesc;
            depthDesc.Width = width;
            depthDesc.Height = height;
            depthDesc.Format = ERHIFormat::D32_Float;
            depthDesc.Usage = RHITextureUsage_DepthStencil;
            depthDesc.DebugName = "SceneDepth";
            state.Depth = device->CreateTexture(depthDesc);
        }

        TRef<IRHIShader> vertexShader = FShaderLibrary::Get("PBR_Lit.hlsl", "VSMain", ERHIShaderStage::Vertex);
        TRef<IRHIShader> pixelShader = FShaderLibrary::Get("PBR_Lit.hlsl", "PSMain", ERHIShaderStage::Pixel);

        FRHIRenderPassDesc pass;
        pass.NumColorTargets = 1;
        pass.Color[0].Texture = &InTarget;
        pass.Color[0].ClearColor = {0.01f, 0.012f, 0.016f, 1.0f};
        pass.Depth.Texture = state.Depth.get();
        pass.Name = "Opaque";
        InCommands.BeginRenderPass(pass);

        if (vertexShader && pixelShader && state.Depth) {
            FFrameConstants frame;
            frame.ViewProjection = ClipDepthZeroToOne() * InCamera.GetViewProjectionMatrix();
            frame.View = InCamera.GetViewMatrix();
            frame.ViewPosition = glm::vec4(InCamera.GetPosition(), 1.0f);
            frame.ViewportSize = {static_cast<float>(width), static_cast<float>(height), 1.0f / static_cast<float>(width),
                                  1.0f / static_cast<float>(height)};
            InCommands.SetConstants(ERHIConstantSlot::Frame, &frame, sizeof(frame));

            const auto pipelineFor = [&](const FVertexArray& InVertexArray) -> IRHIPipeline* {
                const FRHIVertexLayout layout = GetRHIVertexLayout(InVertexArray);
                const std::string key = LayoutKey(layout);
                const auto found = state.OpaquePipelines.find(key);
                if (found != state.OpaquePipelines.end())
                    return found->second.get();
                FRHIGraphicsPipelineDesc desc;
                desc.VertexShader = vertexShader.get();
                desc.PixelShader = pixelShader.get();
                desc.VertexLayout = layout;
                desc.ColorFormats[0] = InTarget.GetDesc().Format;
                desc.DepthFormat = ERHIFormat::D32_Float;
                desc.DebugName = "Opaque";
                TRef<IRHIPipeline> pipeline = device->CreateGraphicsPipeline(desc);
                state.OpaquePipelines[key] = pipeline;
                return pipeline.get();
            };

            const auto draw = [&](const FVertexArray& InVertexArray, const glm::mat4& InModel,
                                  const FMaterialInstance* InMaterial, uint32_t InIndexCount, uint32_t InFirstIndex) {
                IRHIBuffer* vertices = GetRHIVertexBuffer(InVertexArray);
                IRHIBuffer* indices = GetRHIIndexBuffer(InVertexArray);
                IRHIPipeline* pipeline = pipelineFor(InVertexArray);
                if (!vertices || !indices || !pipeline || InIndexCount == 0)
                    return;
                FMaterialConstants material;
                material.BaseColor = glm::vec4(InMaterial ? InMaterial->GetAlbedoColor() : glm::vec3(0.5f), 1.0f);
                FDrawConstants constants;
                constants.Model = InModel;
                constants.NormalMatrix = glm::mat4(glm::inverseTranspose(glm::mat3(InModel)));
                InCommands.SetPipeline(pipeline);
                InCommands.SetVertexBuffer(vertices);
                InCommands.SetIndexBuffer(indices);
                InCommands.SetConstants(ERHIConstantSlot::Material, &material, sizeof(material));
                InCommands.SetConstants(ERHIConstantSlot::Draw, &constants, sizeof(constants));
                InCommands.DrawIndexed(InIndexCount, InFirstIndex);
            };

            auto& registry = World->GetRegistry();

            // Procedural primitives (boxes, planes...).
            auto meshView = registry.view<FTransformComponent, FMeshComponent>();
            for (auto entity : meshView) {
                auto [transform, mesh] = meshView.get<FTransformComponent, FMeshComponent>(entity);
                if (!mesh.VertexArray || !mesh.bVisible)
                    continue;
                const FMaterialInstance* material = nullptr;
                if (registry.all_of<FMaterialComponent>(entity))
                    material = registry.get<FMaterialComponent>(entity).MaterialInstance.get();
                draw(*mesh.VertexArray, ResolveActorWorldMatrix(World, entity, transform), material,
                     GetRHIIndexCount(*mesh.VertexArray), 0);
            }

            // Imported static meshes.
            auto staticView = registry.view<FTransformComponent, FStaticMeshComponent>();
            for (auto entity : staticView) {
                auto [transform, component] = staticView.get<FTransformComponent, FStaticMeshComponent>(entity);
                if (!component.bVisible || !component.StaticMesh)
                    continue;
                const TRef<FVertexArray> vertexArray = component.StaticMesh->GetLODVertexArray(0);
                if (!vertexArray)
                    continue;
                const glm::mat4 world = ResolveActorWorldMatrix(World, entity, transform);
                for (const FStaticSubmesh& submesh : component.StaticMesh->GetLODSubmeshes(0)) {
                    const TRef<FMaterialInstance> material =
                        ResolveStaticSubmeshMaterial(*component.StaticMesh, submesh, component.MaterialOverrides);
                    draw(*vertexArray, world * submesh.LocalTransform, material.get(), submesh.IndexCount,
                         submesh.IndexOffset);
                }
            }
        }

        InCommands.EndRenderPass();
    }

} // namespace Leon
