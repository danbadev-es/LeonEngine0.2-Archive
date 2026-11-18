// How a world is drawn.
//
// Drawn today: static and skinned geometry (opaque, masked and blended) with materials, direct lights,
// lightmaps, image-based lighting, fog and shadows (sun cascades, one spot light, point lights), plus
// skinned outlines and the sky, into an HDR target that FPostProcessRHI turns into the final image
// (ambient occlusion, bloom, tone mapping, anti-aliasing). World-space text, debug lines and particles
// submitted through FSceneBatchesRHI are drawn at the end of the scene pass. Surfaces that ask for it
// reflect a capture of the scene mirrored in the floor or in a wall mirror.
//
// Order of a frame: view state (FWorldRenderer::UpdateViewState) -> collect what is visible -> shadow
// maps -> reflection captures -> scene pass -> post-processing.

#include "Renderer/FWorldRenderer.hpp"

#include "FPostProcessRHI.hpp"
#include "FWorldRendererInternals.hpp"

#include "Assets/FAnimTypes.hpp"
#include "Assets/FLightmapAsset.hpp"
#include "Assets/UAssetManager.hpp"
#include "Assets/USkeletalMesh.hpp"
#include "Assets/UStaticMesh.hpp"
#include "Core/FLog.hpp"
#include "Engine/Components.hpp"
#include "Engine/UWorld.hpp"
#include "Gameplay/FGameplayDebugger.hpp"
#include "Physics/FCollisionQuery.hpp"
#include "RHI/FFrameProfiler.hpp"
#include "RHI/FLegacyRenderDriver.hpp"
#include "RHI/FRenderer.hpp"
#include "RHI/FTexture.hpp"
#include "RHI/FTextureCube.hpp"
#include "RHI/FVertexArray.hpp"
#include "RHI/IRHIDevice.hpp"
#include "Renderer/FImmediateDrawRHI.hpp"
#include "Renderer/FDebugRenderer.hpp"
#include "Renderer/FLight.hpp"
#include "Renderer/FMaterialInstance.hpp"
#include "Renderer/FParticleRenderer.hpp"
#include "Renderer/FShaderConstants.hpp"
#include "Renderer/FShaderLibrary.hpp"
#include "Renderer/FShadowMath.hpp"
#include "Renderer/FTextRenderer.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace Leon {

    namespace {

        ERHIBlend ToRHIBlend(EBlendFactor InFactor) {
            switch (InFactor) {
            case EBlendFactor::Zero:
                return ERHIBlend::Zero;
            case EBlendFactor::One:
                return ERHIBlend::One;
            case EBlendFactor::SrcColor:
                return ERHIBlend::SrcColor;
            case EBlendFactor::OneMinusSrcColor:
                return ERHIBlend::InvSrcColor;
            case EBlendFactor::SrcAlpha:
                return ERHIBlend::SrcAlpha;
            case EBlendFactor::OneMinusSrcAlpha:
                return ERHIBlend::InvSrcAlpha;
            case EBlendFactor::DstAlpha:
                return ERHIBlend::DstAlpha;
            case EBlendFactor::OneMinusDstAlpha:
                return ERHIBlend::InvDstAlpha;
            case EBlendFactor::DstColor:
                return ERHIBlend::DstColor;
            case EBlendFactor::OneMinusDstColor:
                return ERHIBlend::InvDstColor;
            }
            return ERHIBlend::One;
        }

        ERHICompare ToRHICompare(EDepthFunc InFunc) {
            switch (InFunc) {
            case EDepthFunc::Less:
                return ERHICompare::Less;
            case EDepthFunc::LessEqual:
                return ERHICompare::LessEqual;
            case EDepthFunc::Equal:
                return ERHICompare::Equal;
            case EDepthFunc::Always:
                return ERHICompare::Always;
            }
            return ERHICompare::Less;
        }

        /** One mesh section to draw, with everything resolved to RHI objects. */
        struct FSceneDraw {
            IRHIPipeline* Pipeline = nullptr;
            IRHIBuffer* Vertices = nullptr;
            IRHIBuffer* Indices = nullptr;
            const FMaterialInstance* Material = nullptr;
            IRHITexture* Lightmap = nullptr;
            /** Bone matrices of a skinned mesh; null for static geometry. */
            const std::vector<glm::mat4>* Bones = nullptr;
            bool bReceiveShadows = true;
            /** The material asks for planar reflections (wet floors, mirrors). */
            bool bPlanar = false;
            glm::mat4 Model{1.0f};
            glm::mat4 NormalMatrix{1.0f};
            glm::vec2 LightmapScale{1.0f};
            glm::vec2 LightmapBias{0.0f};
            uint32_t IndexCount = 0;
            uint32_t FirstIndex = 0;
            float DistanceSq = 0.0f;
        };

        /** One section of a skinned mesh to outline. */
        struct FOutlineDraw {
            IRHIBuffer* Vertices = nullptr;
            IRHIBuffer* Indices = nullptr;
            const std::vector<glm::mat4>* Bones = nullptr;
            glm::mat4 Model{1.0f};
            glm::vec4 ColorWidth{0.0f};
            uint32_t IndexCount = 0;
            uint32_t FirstIndex = 0;
        };

        /** One mesh section that can block light. */
        struct FShadowCaster {
            const FVertexArray* VertexArray = nullptr;
            IRHIBuffer* Vertices = nullptr;
            IRHIBuffer* Indices = nullptr;
            const FMaterialInstance* Material = nullptr;
            const std::vector<glm::mat4>* Bones = nullptr;
            glm::mat4 Model{1.0f};
            glm::vec3 WorldMin{0.0f};
            glm::vec3 WorldMax{0.0f};
            uint32_t IndexCount = 0;
            uint32_t FirstIndex = 0;
        };

        void SetBonePalette(IRHICommandList& InCommands, const std::vector<glm::mat4>& InPalette) {
            static std::vector<glm::mat4> padded(kMaxBones);
            for (uint32_t i = 0; i < kMaxBones; ++i)
                padded[i] = i < InPalette.size() ? InPalette[i] : glm::mat4(1.0f);
            InCommands.SetConstants(ERHIConstantSlot::Extra, padded.data(),
                                    static_cast<uint32_t>(padded.size() * sizeof(glm::mat4)));
        }

        /** What the lights and the environment of the world add up to this frame. */
        struct FSceneLighting {
            FLitPassConstants Constants;
            float Exposure = 1.0f;
            /** Sky settings of the world; defaults when it has none (bHasSkybox false). */
            FSkyboxComponent Skybox;
            bool bHasSkybox = false;
            /** The sun as its component gives it (not normalised); valid when bHasSun. */
            glm::vec3 SunDirection{0.0f, -1.0f, 0.0f};
            bool bHasSun = false;
            /** The spot lights in the order of Constants.SpotLights, and whether each may cast shadows. */
            std::vector<FSpotLight> Spots;
            std::vector<bool> SpotCastsShadows;
        };

        FSceneLighting GatherLighting(UWorld& InWorld) {
            FSceneLighting lighting;
            FLitPassConstants& constants = lighting.Constants;
            auto& registry = InWorld.GetRegistry();
            // Static lights are in the lightmaps; adding them again would light the scene twice.
            const bool bSkipBakedLights = InWorld.AreLightmapsTrusted();
            const auto contributes = [bSkipBakedLights](bool bInEnabled, ELightMobility InMobility) {
                return bInEnabled && !(InMobility == ELightMobility::Static && bSkipBakedLights);
            };

            for (auto entity : registry.view<FDirectionalLightComponent>()) {
                const auto& component = registry.get<FDirectionalLightComponent>(entity);
                if (!contributes(component.bEnabled, component.Mobility))
                    continue;
                constants.DirLight.Direction = glm::vec4(glm::normalize(component.Light.Direction), 1.0f);
                constants.DirLight.Color = glm::vec4(component.Light.Color, component.Light.Intensity);
                lighting.SunDirection = component.Light.Direction;
                lighting.bHasSun = true;
                break;
            }

            uint32_t pointCount = 0;
            for (auto entity : registry.view<FPointLightComponent>()) {
                const auto& component = registry.get<FPointLightComponent>(entity);
                if (!contributes(component.bEnabled, component.Mobility) || pointCount >= kMaxPointLights)
                    continue;
                glm::vec3 position = component.Light.Position;
                if (const auto* transform = registry.try_get<FTransformComponent>(entity))
                    position = transform->Translation;
                FPointLightConstants& light = constants.PointLights[pointCount++];
                light.Position = glm::vec4(position, 1.0f);
                light.Color = glm::vec4(component.Light.Color, component.Light.Intensity);
                light.Params = glm::vec4(component.Light.Radius, -1.0f, 0.0f, 0.0f);
            }

            uint32_t spotCount = 0;
            for (auto entity : registry.view<FSpotLightComponent>()) {
                const auto& component = registry.get<FSpotLightComponent>(entity);
                if (!contributes(component.bEnabled, component.Mobility) || spotCount >= kMaxSpotLights)
                    continue;
                FSpotLight spot = component.Light;
                if (const auto* transform = registry.try_get<FTransformComponent>(entity))
                    SyncSpotLightFromTransform(spot, transform->Translation, transform->Rotation);
                float innerDegrees = spot.CutOff;
                float outerDegrees = spot.OuterCutOff;
                if (innerDegrees > outerDegrees)
                    std::swap(innerDegrees, outerDegrees);
                outerDegrees = std::max(outerDegrees, innerDegrees + 0.25f);
                FSpotLightConstants& light = constants.SpotLights[spotCount++];
                light.Position = glm::vec4(spot.Position, 1.0f);
                light.Direction = glm::vec4(glm::normalize(spot.Direction), std::cos(glm::radians(innerDegrees)));
                light.Color = glm::vec4(spot.Color, std::cos(glm::radians(outerDegrees)));
                light.Params = glm::vec4(spot.Radius, spot.Intensity, 0.0f, 0.0f);
                lighting.Spots.push_back(spot);
                lighting.SpotCastsShadows.push_back(component.bCastShadows);
            }
            constants.LightCounts = glm::ivec4(static_cast<int>(pointCount), static_cast<int>(spotCount), 0, 0);

            FSkyboxComponent skybox;
            const auto skyboxView = registry.view<FSkyboxComponent>();
            const bool bHasSkybox = skyboxView.begin() != skyboxView.end();
            if (bHasSkybox)
                skybox = registry.get<FSkyboxComponent>(*skyboxView.begin());
            constants.EnvSkyColor = glm::vec4(skybox.SkyZenithColor, skybox.EnvironmentIntensity);
            constants.EnvHorizonColor = glm::vec4(skybox.HorizonColor, 0.0f);
            constants.EnvGroundColor = glm::vec4(skybox.GroundColor, 0.0f);
            const bool bFog = bHasSkybox && skybox.bFogEnabled && skybox.FogDensity > 0.0f;
            constants.FogColorDensity = glm::vec4(skybox.FogColor, bFog ? skybox.FogDensity : 0.0f);
            constants.FogParams =
                glm::vec4(std::max(skybox.FogStartDistance, 0.0f), std::max(skybox.FogHeightFalloff, 0.0f),
                          skybox.FogBaseHeight, glm::clamp(skybox.FogMaxOpacity, 0.0f, 1.0f));
            lighting.Exposure = skybox.Exposure;
            lighting.Skybox = skybox;
            lighting.bHasSkybox = bHasSkybox;
            return lighting;
        }

    } // namespace

    struct FWorldRendererRHIState {
        TRef<IRHITexture> SceneColor;
        TRef<IRHITexture> SceneDepth;
        /** Bound where a cube map is expected and none is available. */
        TRef<IRHITexture> BlackCube;
        /** Surface pipelines by vertex format and render state. Null: the pipeline failed to build. */
        std::map<std::string, TRef<IRHIPipeline>> SurfacePipelines;
        TRef<IRHIPipeline> OutlinePipeline;
        TRef<IRHIPipeline> SkyPipeline;
        /** The scene mirrored in a plane: 0 = floor, 1 = wall mirror. Mip levels hold blurrier copies. */
        TRef<IRHITexture> PlanarColor[2];
        TRef<IRHITexture> PlanarDepth;
        TRef<IRHIPipeline> DownsamplePipeline;
        /** Depth maps seen from the lights: four sun cascades, one spot light, a cube per point light. */
        TRef<IRHITexture> CascadeShadowMap;
        TRef<IRHITexture> SpotShadowMap;
        TRef<IRHITexture> PointShadowMap;
        std::map<std::string, TRef<IRHIPipeline>> ShadowPipelines;
        FPostProcessRHI PostProcess;
    };

    void FWorldRenderer::RenderRHI(const FPerspectiveCamera& InCamera, IRHICommandList& InCommands,
                                   IRHITexture& InTarget) {
        UpdateViewState(InCamera);
        IRHIDevice* device = FRHI::GetDevice();
        if (!device || !World)
            return;
        if (!RHIState)
            RHIState = std::make_shared<FWorldRendererRHIState>();
        FWorldRendererRHIState& state = *RHIState;
        // CPU time of each stage, for the on-screen profiler.
        std::optional<FFrameProfiler::FScope> stageTimer;

        // World-space text, particles and gameplay debug shapes are queued here and drawn at the end
        // of the scene pass.
        {
            auto textView = World->GetRegistry().view<FTransformComponent, FTextComponent>();
            FTextRenderer::BeginScene(InCamera);
            for (auto entity : textView) {
                auto [transform, text] = textView.get<FTransformComponent, FTextComponent>(entity);
                if (!text.Text.empty())
                    FTextRenderer::DrawString(text.Text, transform.GetTransform(), text.Color, text.Size,
                                              text.Alignment, text.bDoubleSided);
            }
            FTextRenderer::EndScene();

            FFrameProfiler::FScope particleTimer(&FFrameProfiler::Working().ParticlesMs);
            FParticleRenderer::Render(World, InCamera);
        }
        if (FGameplayDebugger::IsEnabled()) {
            FDebugRenderer::BeginScene(InCamera);
            if (FGameplayDebugger::ShowPhysics())
                DrawDebugWorldColliders(*World);
            FDebugRenderer::DrawQueuedTraces();
            FDebugRenderer::EndScene(true);
            FDebugRenderer::ClearQueuedTraces();
        }

        // --- Targets ---
        const uint32_t width = InTarget.GetDesc().Width;
        const uint32_t height = InTarget.GetDesc().Height;
        if (!state.SceneColor || state.SceneColor->GetDesc().Width != width ||
            state.SceneColor->GetDesc().Height != height) {
            FRHITextureDesc colorDesc;
            colorDesc.Width = width;
            colorDesc.Height = height;
            colorDesc.Format = ERHIFormat::RGBA16_Float;
            colorDesc.Usage = RHITextureUsage_Sampled | RHITextureUsage_RenderTarget;
            colorDesc.ClearColor = {0.04f, 0.05f, 0.07f, 1.0f};
            colorDesc.DebugName = "SceneColor";
            state.SceneColor = device->CreateTexture(colorDesc);

            FRHITextureDesc depthDesc;
            depthDesc.Width = width;
            depthDesc.Height = height;
            depthDesc.Format = ERHIFormat::D32_Float;
            depthDesc.Usage = RHITextureUsage_DepthStencil | RHITextureUsage_Sampled; // ambient occlusion reads it
            depthDesc.DebugName = "SceneDepth";
            state.SceneDepth = device->CreateTexture(depthDesc);
        }
        if (!state.BlackCube) {
            FRHITextureDesc cubeDesc;
            cubeDesc.Dimension = ERHITextureDimension::TextureCube;
            cubeDesc.DebugName = "BlackCube";
            state.BlackCube = device->CreateTexture(cubeDesc);
            const uint8_t black[4] = {0, 0, 0, 255};
            for (uint32_t face = 0; state.BlackCube && face < 6; ++face)
                device->UploadTexture(*state.BlackCube, 0, face, black, 4);
        }
        // Shadow maps exist even with shadows off (at 1x1), so the shaders always have something bound.
        const bool bShadowsOn = ShadowSettings.bEnableShadows;
        const auto ensureShadowMap = [&](TRef<IRHITexture>& InOutMap, ERHITextureDimension InDimension,
                                         uint32_t InResolution, uint32_t InArraySize, const char* InName) {
            const uint32_t resolution = bShadowsOn ? std::max(InResolution, 1u) : 1u;
            if (InOutMap && InOutMap->GetDesc().Width == resolution)
                return;
            FRHITextureDesc desc;
            desc.Width = resolution;
            desc.Height = resolution;
            desc.ArraySize = InArraySize;
            desc.Format = ERHIFormat::D32_Float;
            desc.Dimension = InDimension;
            desc.Usage = RHITextureUsage_DepthStencil | RHITextureUsage_Sampled;
            desc.DebugName = InName;
            InOutMap = device->CreateTexture(desc);
        };
        ensureShadowMap(state.CascadeShadowMap, ERHITextureDimension::Texture2DArray, ShadowSettings.CascadeResolution, 4,
                        "CascadeShadowMap");
        ensureShadowMap(state.SpotShadowMap, ERHITextureDimension::Texture2D, ShadowSettings.SpotResolution, 1,
                        "SpotShadowMap");
        ensureShadowMap(state.PointShadowMap, ERHITextureDimension::TextureCubeArray,
                        ShadowSettings.PointShadowResolution, FShadowSettings::kMaxShadowedPointLights,
                        "PointShadowMap");
        if (!state.SceneColor || !state.SceneDepth || !state.BlackCube || !state.CascadeShadowMap ||
            !state.SpotShadowMap || !state.PointShadowMap)
            return;

        // --- Shaders and pipelines ---
        TRef<IRHIShader> surfaceVS = FShaderLibrary::Get("PBR_Lit.hlsl", "VSMain", ERHIShaderStage::Vertex);
        TRef<IRHIShader> surfacePS = FShaderLibrary::Get("PBR_Lit.hlsl", "PSMain", ERHIShaderStage::Pixel);
        TRef<IRHIShader> skinnedVS =
            FShaderLibrary::Get("PBR_Lit.hlsl", "VSMain", ERHIShaderStage::Vertex, {{"SKINNED", "1"}});
        TRef<IRHIShader> outlineVS = FShaderLibrary::Get("Outline_Skinned.hlsl", "VSMain", ERHIShaderStage::Vertex);
        TRef<IRHIShader> outlinePS = FShaderLibrary::Get("Outline_Skinned.hlsl", "PSMain", ERHIShaderStage::Pixel);
        TRef<IRHIShader> shadowVS = FShaderLibrary::Get("ShadowDepth.hlsl", "VSMain", ERHIShaderStage::Vertex);
        TRef<IRHIShader> shadowSkinnedVS =
            FShaderLibrary::Get("ShadowDepth.hlsl", "VSMain", ERHIShaderStage::Vertex, {{"SKINNED", "1"}});
        TRef<IRHIShader> shadowPS = FShaderLibrary::Get("ShadowDepth.hlsl", "PSMain", ERHIShaderStage::Pixel);
        TRef<IRHIShader> shadowLinearPS = FShaderLibrary::Get("ShadowDepth.hlsl", "PSMainLinear", ERHIShaderStage::Pixel);
        TRef<IRHIShader> skyVS = FShaderLibrary::Get("Skybox.hlsl", "VSMain", ERHIShaderStage::Vertex);
        TRef<IRHIShader> skyPS = FShaderLibrary::Get("Skybox.hlsl", "PSMain", ERHIShaderStage::Pixel);

        // Where submitted draws go and how they are prepared. The main view uses the defaults; each
        // reflection capture points these at its own lists while it collects.
        std::vector<FSceneDraw>* collectOpaque = nullptr;
        std::vector<FSceneDraw>* collectTransparent = nullptr;
        bool bCollectMirrored = false;
        glm::mat4 collectNormalTransform(1.0f);

        const auto surfacePipeline = [&](const FVertexArray& InVertexArray, const FMaterialInstance& InMaterial,
                                         const glm::mat4& InModel, bool bInTransparent,
                                         bool bInSkinned) -> IRHIPipeline* {
            IRHIShader* vertexShader = bInSkinned ? skinnedVS.get() : surfaceVS.get();
            if (!vertexShader || !surfacePS)
                return nullptr;
            const FMaterialPipelineState& materialState = InMaterial.GetPipelineState();
            ECullMode cull = InMaterial.GetDoubleSided() ? ECullMode::None : materialState.CullMode;
            cull = FlipCullForNegativeScale(cull, InModel);
            // A mirrored view reverses the winding of everything it shows.
            if (bCollectMirrored && cull != ECullMode::None)
                cull = cull == ECullMode::Back ? ECullMode::Front : ECullMode::Back;

            FRHIGraphicsPipelineDesc desc;
            desc.VertexLayout = GetRHIVertexLayout(InVertexArray);
            desc.Raster.Cull =
                cull == ECullMode::Back ? ERHICull::Back : (cull == ECullMode::Front ? ERHICull::Front : ERHICull::None);
            desc.Raster.bWireframe = bWireframeEnabled;
            desc.Depth.bWrite = !bInTransparent;
            desc.Depth.Compare = ToRHICompare(materialState.DepthFunc);
            desc.Blend.bEnabled = bInTransparent;
            desc.Blend.SrcColor = ToRHIBlend(materialState.SrcBlend);
            desc.Blend.DstColor = ToRHIBlend(materialState.DstBlend);

            std::string key = std::to_string(desc.VertexLayout.Stride);
            for (const FRHIVertexAttribute& attribute : desc.VertexLayout.Attributes)
                key += ":" + std::to_string(static_cast<int>(attribute.Format)) + "@" + std::to_string(attribute.Offset);
            key += std::string(bInSkinned ? "|skinned" : "") + (bWireframeEnabled ? "|wire" : "") + "|c" +
                   std::to_string(static_cast<int>(desc.Raster.Cull)) + "d" +
                   std::to_string(static_cast<int>(desc.Depth.Compare)) + (bInTransparent ? "t" : "o") +
                   std::to_string(static_cast<int>(desc.Blend.SrcColor)) + "," +
                   std::to_string(static_cast<int>(desc.Blend.DstColor));

            const auto found = state.SurfacePipelines.find(key);
            if (found != state.SurfacePipelines.end())
                return found->second.get();
            desc.VertexShader = vertexShader;
            desc.PixelShader = surfacePS.get();
            desc.ColorFormats[0] = ERHIFormat::RGBA16_Float;
            desc.DepthFormat = ERHIFormat::D32_Float;
            desc.DebugName = bInSkinned ? "SkinnedSurface" : "Surface";
            TRef<IRHIPipeline> pipeline = device->CreateGraphicsPipeline(desc);
            state.SurfacePipelines[key] = pipeline;
            return pipeline.get();
        };

        // bInBias: push depth away from the light to hide self-shadowing. bInLinearDepth: point-light cubes.
        const auto shadowPipeline = [&](const FVertexArray& InVertexArray, bool bInSkinned, ERHICull InCull, bool bInBias,
                                        bool bInLinearDepth) -> IRHIPipeline* {
            IRHIShader* vertexShader = bInSkinned ? shadowSkinnedVS.get() : shadowVS.get();
            IRHIShader* pixelShader = bInLinearDepth ? shadowLinearPS.get() : shadowPS.get();
            if (!vertexShader || !pixelShader)
                return nullptr;
            FRHIGraphicsPipelineDesc desc;
            desc.VertexLayout = GetRHIVertexLayout(InVertexArray);
            std::string key = std::to_string(desc.VertexLayout.Stride);
            for (const FRHIVertexAttribute& attribute : desc.VertexLayout.Attributes)
                key += ":" + std::to_string(static_cast<int>(attribute.Format)) + "@" + std::to_string(attribute.Offset);
            key += std::string(bInSkinned ? "|skinned" : "") + "|c" + std::to_string(static_cast<int>(InCull)) +
                   (bInBias ? "b" : "") + (bInLinearDepth ? "l" : "");
            const auto found = state.ShadowPipelines.find(key);
            if (found != state.ShadowPipelines.end())
                return found->second.get();
            desc.VertexShader = vertexShader;
            desc.PixelShader = pixelShader;
            desc.Raster.Cull = InCull;
            if (bInBias) {
                desc.Raster.DepthBias = 4;
                desc.Raster.SlopeScaledDepthBias = 2.0f;
            }
            desc.NumColorTargets = 0;
            desc.ColorFormats[0] = ERHIFormat::Unknown;
            desc.DepthFormat = ERHIFormat::D32_Float;
            desc.DebugName = "ShadowDepth";
            TRef<IRHIPipeline> pipeline = device->CreateGraphicsPipeline(desc);
            state.ShadowPipelines[key] = pipeline;
            return pipeline.get();
        };

        // --- What to draw ---
        // Everything is resolved to device objects here, before the pass starts: creating a buffer or a
        // texture uploads data, which must not happen in the middle of a render pass.
        std::vector<FSceneDraw> opaque;
        std::vector<FSceneDraw> transparent;
        std::vector<FOutlineDraw> outlines;
        std::vector<FShadowCaster> staticCasters;
        std::vector<FShadowCaster> skinnedCasters;
        auto& registry = World->GetRegistry();
        const FFrustumPlanes frustum = ExtractFrustumPlanes(InCamera.GetViewProjectionMatrix());
        const glm::vec3 cameraPosition = InCamera.GetPosition();
        const bool bLightmapsTrusted = World->AreLightmapsTrusted();
        const TRef<FMaterialInstance> defaultMaterial = UAssetManager::GetDefaultMaterialInstance();

        const auto lightmapFor = [&](EComponentMobility InMobility, int32_t InIndex,
                                     const std::string& InAssetPath) -> IRHITexture* {
            if (!bLightmapsTrusted || InMobility != EComponentMobility::Static || InIndex < 0 || InAssetPath.empty())
                return nullptr;
            const TRef<FLightmapAsset> lightmap = UAssetManager::GetLightmap(InAssetPath);
            const TRef<FTexture2D> texture = lightmap ? lightmap->GetOrCreateGPUTexture() : nullptr;
            return texture ? GetRHITexture(*texture) : nullptr;
        };

        const auto submit = [&](const FVertexArray& InVertexArray, const FMaterialInstance* InMaterial,
                                const glm::mat4& InModel, uint32_t InIndexCount, uint32_t InFirstIndex,
                                IRHITexture* InLightmap, const glm::vec2& InLightmapScale,
                                const glm::vec2& InLightmapBias, const glm::vec3& InLocalMin,
                                const glm::vec3& InLocalMax, const std::vector<glm::mat4>* InBones,
                                bool bInReceiveShadows) {
            if (!InMaterial || InIndexCount == 0)
                return;
            const bool bTransparent = InMaterial->GetAlphaMode() == EAlphaMode::Blend;
            FSceneDraw draw;
            draw.Pipeline = surfacePipeline(InVertexArray, *InMaterial, InModel, bTransparent, InBones != nullptr);
            draw.Bones = InBones;
            draw.bReceiveShadows = bInReceiveShadows;
            draw.Vertices = GetRHIVertexBuffer(InVertexArray);
            draw.Indices = GetRHIIndexBuffer(InVertexArray);
            if (!draw.Pipeline || !draw.Vertices || !draw.Indices)
                return;
            draw.Material = InMaterial;
            draw.Lightmap = InLightmap;
            draw.bPlanar = InMaterial->GetUsePlanarReflection();
            draw.Model = InModel;
            draw.NormalMatrix = glm::mat4(SafeNormalMatrix(collectNormalTransform * InModel));
            draw.LightmapScale = InLightmapScale;
            draw.LightmapBias = InLightmapBias;
            draw.IndexCount = InIndexCount;
            draw.FirstIndex = InFirstIndex;
            if (bTransparent) {
                draw.DistanceSq = TransparentSortDistanceSq(InModel, InLocalMin, InLocalMax, cameraPosition);
                collectTransparent->push_back(draw);
            } else {
                collectOpaque->push_back(draw);
            }
        };
        collectOpaque = &opaque;
        collectTransparent = &transparent;

        // A mesh section that blocks light. Lights cull the list themselves: a caster outside the
        // camera's view can still throw a shadow into it.
        const auto submitCaster = [&](std::vector<FShadowCaster>& OutCasters, const FVertexArray& InVertexArray,
                                      const FMaterialInstance* InMaterial, const glm::mat4& InModel,
                                      uint32_t InIndexCount, uint32_t InFirstIndex, const glm::vec3& InLocalMin,
                                      const glm::vec3& InLocalMax, const std::vector<glm::mat4>* InBones) {
            if (!bShadowsOn || !InMaterial || InIndexCount == 0)
                return;
            FShadowCaster caster;
            caster.VertexArray = &InVertexArray;
            caster.Vertices = GetRHIVertexBuffer(InVertexArray);
            caster.Indices = GetRHIIndexBuffer(InVertexArray);
            if (!caster.Vertices || !caster.Indices)
                return;
            caster.Material = InMaterial;
            caster.Bones = InBones;
            caster.Model = InModel;
            caster.IndexCount = InIndexCount;
            caster.FirstIndex = InFirstIndex;
            TransformAABB(InLocalMin, InLocalMax, InModel, caster.WorldMin, caster.WorldMax);
            if (InBones) {
                // An animated pose can reach outside the bounds of the rest pose.
                const glm::vec3 center = 0.5f * (caster.WorldMin + caster.WorldMax);
                const glm::vec3 extent =
                    0.5f * (caster.WorldMax - caster.WorldMin) * FShadowSettings::kSkinnedShadowBoundsPadding;
                caster.WorldMin = center - extent;
                caster.WorldMax = center + extent;
            }
            OutCasters.push_back(caster);
        };

        // Procedural primitives (boxes, planes...).
        auto meshView = registry.view<FTransformComponent, FMeshComponent>();
        for (auto entity : meshView) {
            auto [transform, mesh] = meshView.get<FTransformComponent, FMeshComponent>(entity);
            if (!mesh.VertexArray || !mesh.bVisible)
                continue;
            const glm::mat4 model = ResolveActorWorldMatrix(World, entity, transform);
            TRef<FMaterialInstance> material;
            if (const auto* component = registry.try_get<FMaterialComponent>(entity))
                material = component->MaterialInstance;
            if (!material)
                material = defaultMaterial;
            const glm::vec3 extent = ProceduralMeshLocalExtent(mesh);
            const uint32_t indexCount = GetRHIIndexCount(*mesh.VertexArray);
            if (mesh.bCastShadows)
                submitCaster(staticCasters, *mesh.VertexArray, material.get(), model, indexCount, 0, -extent, extent,
                             nullptr);
            if (IsProceduralMeshCulled(model, mesh, frustum))
                continue;
            submit(*mesh.VertexArray, material.get(), model, indexCount, 0,
                   lightmapFor(mesh.Mobility, mesh.LightmapIndex, mesh.LightmapAssetPath), mesh.LightmapScale,
                   mesh.LightmapBias, -extent, extent, nullptr, mesh.bReceiveShadows);
        }

        // Imported static meshes.
        auto staticView = registry.view<FTransformComponent, FStaticMeshComponent>();
        for (auto entity : staticView) {
            auto [transform, component] = staticView.get<FTransformComponent, FStaticMeshComponent>(entity);
            if (!component.bVisible || !component.StaticMesh)
                continue;
            const glm::mat4 world = ResolveActorWorldMatrix(World, entity, transform);
            const uint32_t lodCount = std::max(1u, component.StaticMesh->GetLODCount());
            if (component.bCastShadows) {
                const uint32_t shadowLod = ShadowLODIndex(component, std::min(component.CurrentLOD, lodCount - 1));
                if (const TRef<FVertexArray> shadowVertexArray = component.StaticMesh->GetLODVertexArray(shadowLod)) {
                    for (const auto& submesh : component.StaticMesh->GetLODSubmeshes(shadowLod)) {
                        const TRef<FMaterialInstance> material =
                            ResolveStaticSubmeshMaterial(*component.StaticMesh, submesh, component.MaterialOverrides);
                        submitCaster(staticCasters, *shadowVertexArray, material ? material.get() : defaultMaterial.get(),
                                     world * submesh.LocalTransform, submesh.IndexCount, submesh.IndexOffset,
                                     component.StaticMesh->GetBoundsMin(), component.StaticMesh->GetBoundsMax(), nullptr);
                    }
                }
            }
            if (IsStaticMeshCulled(world, component, frustum))
                continue;
            const uint32_t lod = std::min(component.CurrentLOD, lodCount - 1);
            RecordStaticMeshLODStats(lod, component.StaticMesh->GetSourceTriangleCount(),
                                     component.StaticMesh->GetLODTriangleCount(lod));
            const TRef<FVertexArray> vertexArray = component.StaticMesh->GetLODVertexArray(lod);
            if (!vertexArray)
                continue;
            IRHITexture* lightmap = lightmapFor(component.Mobility, component.LightmapIndex, component.LightmapAssetPath);
            for (const auto& submesh : component.StaticMesh->GetLODSubmeshes(lod)) {
                const TRef<FMaterialInstance> material =
                    ResolveStaticSubmeshMaterial(*component.StaticMesh, submesh, component.MaterialOverrides);
                submit(*vertexArray, material ? material.get() : defaultMaterial.get(), world * submesh.LocalTransform,
                       submesh.IndexCount, submesh.IndexOffset, lightmap, component.LightmapScale,
                       component.LightmapBias, component.StaticMesh->GetBoundsMin(),
                       component.StaticMesh->GetBoundsMax(), nullptr, component.bReceiveShadows);
            }
        }

        // Skinned meshes (characters).
        auto skinnedView = registry.view<FTransformComponent, FSkinnedMeshRenderState>();
        for (auto entity : skinnedView) {
            auto [transform, skinned] = skinnedView.get<FTransformComponent, FSkinnedMeshRenderState>(entity);
            if (!skinned.bVisible || !skinned.SkeletalMesh)
                continue;
            const TRef<FVertexArray> vertexArray = skinned.SkeletalMesh->GetVertexArray();
            if (!vertexArray)
                continue;
            const glm::mat4 world = ResolveActorWorldMatrix(World, entity, transform);
            if (skinned.bCastShadows && IsSkinnedShadowCasterSelected(static_cast<uint32_t>(entity))) {
                for (const auto& submesh : skinned.SkeletalMesh->GetSubmeshes()) {
                    const TRef<FMaterialInstance> material =
                        ResolveSkeletalSubmeshMaterial(*skinned.SkeletalMesh, submesh, skinned.MaterialOverrides);
                    submitCaster(skinnedCasters, *vertexArray, material ? material.get() : defaultMaterial.get(),
                                 SkeletalModelMatrix(world, skinned, submesh.LocalTransform), submesh.IndexCount,
                                 submesh.IndexOffset, skinned.SkeletalMesh->GetBoundsMin(),
                                 skinned.SkeletalMesh->GetBoundsMax(), &skinned.BonePalette);
                }
            }
            if (IsSkeletalMeshCulled(world, skinned, frustum))
                continue;
            for (const auto& submesh : skinned.SkeletalMesh->GetSubmeshes()) {
                if (submesh.IndexCount == 0)
                    continue;
                const TRef<FMaterialInstance> material =
                    ResolveSkeletalSubmeshMaterial(*skinned.SkeletalMesh, submesh, skinned.MaterialOverrides);
                const glm::mat4 model = SkeletalModelMatrix(world, skinned, submesh.LocalTransform);
                submit(*vertexArray, material ? material.get() : defaultMaterial.get(), model, submesh.IndexCount,
                       submesh.IndexOffset, nullptr, glm::vec2(1.0f), glm::vec2(0.0f),
                       skinned.SkeletalMesh->GetBoundsMin(), skinned.SkeletalMesh->GetBoundsMax(),
                       &skinned.BonePalette, skinned.bReceiveShadows);
                if (skinned.bDrawOutline) {
                    FOutlineDraw outline;
                    outline.Vertices = GetRHIVertexBuffer(*vertexArray);
                    outline.Indices = GetRHIIndexBuffer(*vertexArray);
                    outline.Bones = &skinned.BonePalette;
                    outline.Model = model;
                    outline.ColorWidth = glm::vec4(skinned.OutlineColor, skinned.OutlineWidth);
                    outline.IndexCount = submesh.IndexCount;
                    outline.FirstIndex = submesh.IndexOffset;
                    if (outline.Vertices && outline.Indices)
                        outlines.push_back(outline);
                }
            }
            if (!outlines.empty() && !state.OutlinePipeline && outlineVS && outlinePS) {
                // The mesh pushed out along its normals, back faces only, behind what is already drawn.
                FRHIGraphicsPipelineDesc desc;
                desc.VertexShader = outlineVS.get();
                desc.PixelShader = outlinePS.get();
                desc.VertexLayout = GetRHIVertexLayout(*vertexArray);
                desc.Raster.Cull = ERHICull::Front;
                desc.Depth.bWrite = false;
                desc.Depth.Compare = ERHICompare::LessEqual;
                desc.ColorFormats[0] = ERHIFormat::RGBA16_Float;
                desc.DepthFormat = ERHIFormat::D32_Float;
                desc.DebugName = "SkinnedOutline";
                state.OutlinePipeline = device->CreateGraphicsPipeline(desc);
            }
        }

        // Fewest state changes for opaque surfaces; back to front for blended ones.
        std::sort(opaque.begin(), opaque.end(), [](const FSceneDraw& InA, const FSceneDraw& InB) {
            if (InA.Pipeline != InB.Pipeline)
                return InA.Pipeline < InB.Pipeline;
            if (InA.Material != InB.Material)
                return InA.Material < InB.Material;
            return InA.Vertices < InB.Vertices;
        });
        std::sort(transparent.begin(), transparent.end(),
                  [](const FSceneDraw& InA, const FSceneDraw& InB) { return InA.DistanceSq > InB.DistanceSq; });

        // Material textures, resolved now for the same reason as the buffers.
        struct FMaterialBinding {
            FMaterialConstants Constants;
            IRHITexture* Textures[6] = {};
        };
        std::map<const FMaterialInstance*, FMaterialBinding> materialBindings;
        const auto prepareMaterial = [&](const FMaterialInstance& InMaterial) {
            if (materialBindings.count(&InMaterial))
                return;
            FMaterialBinding binding;
            int textureBits = 0;
            // Order: albedo, normal, metallic, ambient occlusion, roughness, emissive.
            for (uint32_t slot = 0; slot < 6; ++slot) {
                TRef<FTexture2D> texture = InMaterial.GetTexture(slot);
                if (!texture && slot == 5)
                    texture = InMaterial.GetTexture(9); // emissive used to live in slot 9
                if (!texture || !texture->IsLoaded())
                    continue;
                binding.Textures[slot] = GetRHITexture(*texture);
                if (binding.Textures[slot])
                    textureBits |= 1 << slot;
            }
            binding.Constants.AlbedoColor = glm::vec4(InMaterial.GetAlbedoColor(), 1.0f);
            binding.Constants.EmissiveColor = glm::vec4(InMaterial.GetEmissiveColor(), InMaterial.GetEmissiveIntensity());
            binding.Constants.SurfaceParams = glm::vec4(InMaterial.GetMetallic(), InMaterial.GetRoughness(),
                                                        InMaterial.GetAO(), InMaterial.GetNormalScale());
            binding.Constants.SurfaceParams2 =
                glm::vec4(InMaterial.GetOcclusionStrength(), InMaterial.GetAlphaCutoff(), 0.0f, 0.0f);
            binding.Constants.UVTransform = glm::vec4(InMaterial.GetUVTiling(), InMaterial.GetUVOffset());
            binding.Constants.MaterialFlags = glm::ivec4(textureBits, static_cast<int>(InMaterial.GetAlphaMode()),
                                                         InMaterial.GetDoubleSided() ? 1 : 0, 0);
            materialBindings.emplace(&InMaterial, binding);
        };
        for (const FSceneDraw& draw : opaque)
            prepareMaterial(*draw.Material);
        for (const FSceneDraw& draw : transparent)
            prepareMaterial(*draw.Material);
        for (const FShadowCaster& caster : staticCasters)
            prepareMaterial(*caster.Material);
        for (const FShadowCaster& caster : skinnedCasters)
            prepareMaterial(*caster.Material);

        // --- Lighting ---
        FSceneLighting lighting = GatherLighting(*World);
        IRHITexture* brdfLut = IBLEnvironment.BRDFLUT ? GetRHITexture(*IBLEnvironment.BRDFLUT) : nullptr;
        IRHITexture* irradiance = IBLEnvironment.IrradianceMap ? GetRHITexture(*IBLEnvironment.IrradianceMap) : nullptr;
        IRHITexture* prefilter = IBLEnvironment.PrefilterMap ? GetRHITexture(*IBLEnvironment.PrefilterMap) : nullptr;
        const bool bIBL = bUseIBL && brdfLut && irradiance && prefilter;
        lighting.Constants.PassFlags = glm::ivec4(bIBL ? 1 : 0, DebugMode, 0, 0);

        // --- Shadow maps ---
        stageTimer.emplace(&FFrameProfiler::Working().ShadowMs);
        const uint32_t drawsBeforeShadows = FRenderer::GetStats().DrawCalls;
        FLitPassConstants& lit = lighting.Constants;
        lit.ShadowParams = glm::vec4(ShadowSettings.ConstantBias, ShadowSettings.SlopeBias, ShadowSettings.NormalBias,
                                     ShadowSettings.CascadeBlendWidth);

        // Draws the casters a light can see into one slice of a shadow map.
        // InLightViewProjection is in the engine's clip convention (depth -1..1).
        const auto drawShadowMap = [&](IRHITexture& InMap, uint32_t InSlice, const glm::mat4& InLightViewProjection,
                                       bool bInCullFrontFaces, bool bInBias, bool bInSkinnedCasters,
                                       const glm::vec4& InPointLight, const char* InName) {
            const bool bCubeFace = InPointLight.w > 0.0f;
            const FFrustumPlanes lightFrustum = ExtractFrustumPlanes(InLightViewProjection);
            FShadowPassConstants passConstants;
            passConstants.LightViewProjection =
                (bCubeFace ? ClipFlipY() : glm::mat4(1.0f)) * ClipDepthZeroToOne() * InLightViewProjection;
            passConstants.PointLight = InPointLight;
            // Mirroring the image reverses the winding, so the face to cull swaps with it.
            const ERHICull cull = (bInCullFrontFaces != bCubeFace) ? ERHICull::Front : ERHICull::Back;

            FRHIRenderPassDesc pass;
            pass.Depth.Texture = &InMap;
            pass.Depth.ArraySlice = InSlice;
            pass.Name = InName;
            InCommands.BeginRenderPass(pass);
            InCommands.SetConstants(ERHIConstantSlot::Pass, &passConstants, sizeof(passConstants));

            IRHIPipeline* boundPipeline = nullptr;
            const FMaterialInstance* boundMaterial = nullptr;
            const auto drawCasters = [&](const std::vector<FShadowCaster>& InCasters) {
                for (const FShadowCaster& caster : InCasters) {
                    if (!AABBIntersectsFrustum(caster.WorldMin, caster.WorldMax, lightFrustum))
                        continue;
                    IRHIPipeline* pipeline =
                        shadowPipeline(*caster.VertexArray, caster.Bones != nullptr,
                                       caster.Material->GetDoubleSided() ? ERHICull::None : cull, bInBias, bCubeFace);
                    if (!pipeline)
                        continue;
                    if (pipeline != boundPipeline) {
                        InCommands.SetPipeline(pipeline);
                        boundPipeline = pipeline;
                    }
                    if (caster.Material != boundMaterial) {
                        // Only masked materials need their texture: the alpha cuts holes in the shadow.
                        const FMaterialBinding& binding = materialBindings.at(caster.Material);
                        const bool bAlphaTest =
                            caster.Material->GetAlphaMode() == EAlphaMode::Mask && binding.Textures[0] != nullptr;
                        FShadowMaterialConstants material;
                        material.UVTransform = binding.Constants.UVTransform;
                        material.AlphaTest = glm::vec4(bAlphaTest ? 1.0f : 0.0f, caster.Material->GetAlphaCutoff(), 0.0f, 0.0f);
                        InCommands.SetConstants(ERHIConstantSlot::Material, &material, sizeof(material));
                        InCommands.SetTexture(kSlotAlbedo, bAlphaTest ? binding.Textures[0] : nullptr);
                        boundMaterial = caster.Material;
                    }
                    if (caster.Bones)
                        SetBonePalette(InCommands, *caster.Bones);
                    InCommands.SetConstants(ERHIConstantSlot::Draw, &caster.Model, sizeof(caster.Model));
                    InCommands.SetVertexBuffer(caster.Vertices);
                    InCommands.SetIndexBuffer(caster.Indices);
                    InCommands.DrawIndexed(caster.IndexCount, caster.FirstIndex);
                    ++FRenderer::GetStatsMutable().DrawCalls;
                }
            };
            drawCasters(staticCasters);
            if (bInSkinnedCasters)
                drawCasters(skinnedCasters);
            InCommands.EndRenderPass();
        };

        // Sun: the view frustum is cut into slices by distance, each with its own map.
        const uint32_t cascadeCount = std::min(ShadowSettings.CascadeCount, 4u);
        if (bShadowsOn && lighting.bHasSun && cascadeCount > 0) {
            const float nearClip = InCamera.GetNearClip();
            const float farClip = ShadowSettings.ShadowDistance;
            const std::vector<float> splits = ShadowMath::CalculateCascadeSplits(
                cascadeCount, nearClip, farClip, ShadowSettings.SplitLambda, ShadowSettings.SplitScheme);
            // Four far distances are always sent; unused cascades repeat the last one.
            float splitPlane[5] = {nearClip, farClip, farClip, farClip, farClip};
            for (size_t i = 0; i < splits.size() && i < 5; ++i)
                splitPlane[i] = splits[i];
            lit.CascadeSplits = glm::vec4(splitPlane[1], splitPlane[2], splitPlane[3], splitPlane[4]);

            glm::mat4 lastMatrix(1.0f);
            for (uint32_t cascade = 0; cascade < 4; ++cascade) {
                if (cascade < cascadeCount) {
                    float sliceNear = splitPlane[cascade];
                    float sliceFar = splitPlane[cascade + 1];
                    ShadowMath::CascadeSliceDepthRange(cascade, splits, ShadowSettings.CascadeBlendWidth,
                                                       FShadowSettings::kCascadeBlendMinMeters, sliceNear, sliceFar);
                    const glm::mat4 sliceProjection = glm::perspective(
                        glm::radians(InCamera.GetFOV()), InCamera.GetAspectRatio(), sliceNear, sliceFar);
                    const auto corners = ShadowMath::GetFrustumCornersWorldSpace(sliceProjection, InCamera.GetViewMatrix());
                    float worldUnitsPerTexel = 0.01f;
                    lastMatrix = ShadowMath::CalculateCascadeMatrix(corners, lighting.SunDirection,
                                                                    ShadowSettings.CascadeResolution,
                                                                    ShadowSettings.bStabilizeCascades, worldUnitsPerTexel);
                    drawShadowMap(*state.CascadeShadowMap, cascade, lastMatrix, true, true,
                                  cascade < ShadowSettings.MaxSkinnedShadowCascades, glm::vec4(0.0f), "SunShadow");
                }
                lit.LightSpaceMatrices[cascade] = ClipToShadowMap() * lastMatrix;
            }
            lit.ShadowFlags.x = 1;
        }

        // Spot: one light gets a map, the one the settings point at.
        int shadowedSpot = -1;
        if (bShadowsOn && !lighting.Spots.empty()) {
            const int candidate = std::clamp(ShadowSettings.ShadowedSpotIndex, 0, static_cast<int>(lighting.Spots.size()) - 1);
            if (lighting.SpotCastsShadows[static_cast<size_t>(candidate)])
                shadowedSpot = candidate;
        }
        if (shadowedSpot >= 0) {
            const FSpotLight& spot = lighting.Spots[static_cast<size_t>(shadowedSpot)];
            const glm::vec3 direction = glm::normalize(spot.Direction);
            const glm::vec3 up = std::abs(direction.y) < 0.99f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(0.0f, 0.0f, 1.0f);
            const float fieldOfView = glm::clamp(spot.OuterCutOff * 2.0f + 2.0f, 10.0f, 160.0f);
            const float farPlane = std::max(spot.Radius * 1.05f, 1.0f);
            const glm::mat4 spotMatrix = glm::perspective(glm::radians(fieldOfView), 1.0f, 0.1f, farPlane) *
                                         glm::lookAt(spot.Position, spot.Position + direction, up);
            drawShadowMap(*state.SpotShadowMap, 0, spotMatrix, false, true, true, glm::vec4(0.0f), "SpotShadow");
            lit.SpotLightSpaceMatrix = ClipToShadowMap() * spotMatrix;
            lit.ShadowFlags.y = 1;
        }

        // Point: the first lights each get a cube, six views from the light's position.
        uint32_t shadowedPointCount = 0;
        if (bShadowsOn) {
            const uint32_t maxShadowed =
                std::min(ShadowSettings.MaxShadowedPointLights, FShadowSettings::kMaxShadowedPointLights);
            const uint32_t pointCount = static_cast<uint32_t>(lit.LightCounts.x);
            for (uint32_t i = 0; i < pointCount && shadowedPointCount < maxShadowed; ++i) {
                FPointLightConstants& light = lit.PointLights[i];
                const glm::vec3 position(light.Position);
                const float farPlane = std::max(light.Params.x, 1.0f);
                const glm::mat4 faceProjection = ShadowMath::PointCubeFaceProjection(0.05f, farPlane);
                for (uint32_t face = 0; face < 6; ++face)
                    drawShadowMap(*state.PointShadowMap, shadowedPointCount * 6 + face,
                                  faceProjection * ShadowMath::PointCubeFaceView(position, face), false, false, true,
                                  glm::vec4(position, farPlane), "PointShadow");
                light.Params.y = static_cast<float>(shadowedPointCount);
                ++shadowedPointCount;
            }
        }
        lit.ShadowFlags.z = shadowedPointCount > 0 ? 1 : 0;
        lit.ShadowSettings = glm::ivec4(static_cast<int>(ShadowSettings.FilterMode), shadowedSpot,
                                        static_cast<int>(ShadowSettings.CascadeCount), 0);

        FFrameProfiler::Working().ShadowDrawCalls = FRenderer::GetStats().DrawCalls - drawsBeforeShadows;
        stageTimer.reset();

        // --- Sky ---
        const bool bDrawSky = lighting.bHasSkybox && lighting.Skybox.bEnabled && skyVS && skyPS;
        if (bDrawSky && !state.SkyPipeline) {
            FRHIGraphicsPipelineDesc desc;
            desc.VertexShader = skyVS.get();
            desc.PixelShader = skyPS.get();
            desc.Raster.Cull = ERHICull::None;
            desc.Depth.bWrite = false;
            desc.Depth.Compare = ERHICompare::LessEqual; // it sits exactly on the far plane
            desc.ColorFormats[0] = ERHIFormat::RGBA16_Float;
            desc.DepthFormat = ERHIFormat::D32_Float;
            desc.DebugName = "Sky";
            state.SkyPipeline = device->CreateGraphicsPipeline(desc);
        }
        IRHITexture* panorama = nullptr;
        if (bDrawSky && lighting.Skybox.bUseHDREnvironmentMap && lighting.Skybox.HDREnvironmentMap &&
            lighting.Skybox.HDREnvironmentMap->IsLoaded())
            panorama = GetRHITexture(*lighting.Skybox.HDREnvironmentMap);

        FFrameConstants frame;
        frame.ViewProjection = ClipDepthZeroToOne() * InCamera.GetViewProjectionMatrix();
        frame.View = InCamera.GetViewMatrix();
        frame.ViewPosition = glm::vec4(cameraPosition, 1.0f);
        frame.CameraForward = glm::vec4(InCamera.GetForwardDirection(), 0.0f);
        frame.ViewportSize = {static_cast<float>(width), static_cast<float>(height), 1.0f / static_cast<float>(width),
                              1.0f / static_cast<float>(height)};

        // --- Helpers shared by the scene pass and the reflection captures ---
        // Textures every lit draw can read. The planar captures are passed in: while one is being drawn
        // it cannot also be read.
        const auto bindSceneTextures = [&](IRHITexture* InPlanarFloor, IRHITexture* InPlanarWall) {
            InCommands.SetTexture(kSlotBRDFLUT, brdfLut);
            InCommands.SetTexture(kSlotIrradiance, bIBL ? irradiance : state.BlackCube.get());
            InCommands.SetTexture(kSlotPrefilter, bIBL ? prefilter : state.BlackCube.get());
            InCommands.SetTexture(kSlotCascadeShadow, state.CascadeShadowMap.get());
            InCommands.SetTexture(kSlotSpotShadow, state.SpotShadowMap.get());
            InCommands.SetTexture(kSlotPointShadow, state.PointShadowMap.get());
            InCommands.SetTexture(kSlotPlanarFloor, InPlanarFloor);
            InCommands.SetTexture(kSlotPlanarWall, InPlanarWall);
        };

        // bInCapture: the draws belong to a reflection capture, which shows no shadows or reflections.
        const auto drawAll = [&](const std::vector<FSceneDraw>& InDraws, const char* InName, bool bInCapture) {
            if (InDraws.empty())
                return;
            InCommands.PushMarker(InName);
            IRHIPipeline* boundPipeline = nullptr;
            const FMaterialInstance* boundMaterial = nullptr;
            IRHIBuffer* boundVertices = nullptr;
            IRHIBuffer* boundIndices = nullptr;
            IRHITexture* boundLightmap = nullptr;
            const std::vector<glm::mat4>* boundBones = nullptr;
            bool bLightmapBound = false;
            for (const FSceneDraw& draw : InDraws) {
                if (draw.Pipeline != boundPipeline) {
                    InCommands.SetPipeline(draw.Pipeline);
                    boundPipeline = draw.Pipeline;
                }
                if (draw.Material != boundMaterial) {
                    const FMaterialBinding& binding = materialBindings.at(draw.Material);
                    InCommands.SetConstants(ERHIConstantSlot::Material, &binding.Constants, sizeof(binding.Constants));
                    InCommands.SetTexture(kSlotAlbedo, binding.Textures[0]);
                    InCommands.SetTexture(kSlotNormal, binding.Textures[1]);
                    InCommands.SetTexture(kSlotMetallic, binding.Textures[2]);
                    InCommands.SetTexture(kSlotAO, binding.Textures[3]);
                    InCommands.SetTexture(kSlotRoughness, binding.Textures[4]);
                    InCommands.SetTexture(kSlotEmissive, binding.Textures[5]);
                    boundMaterial = draw.Material;
                }
                if (draw.Vertices != boundVertices) {
                    InCommands.SetVertexBuffer(draw.Vertices);
                    boundVertices = draw.Vertices;
                }
                if (draw.Indices != boundIndices) {
                    InCommands.SetIndexBuffer(draw.Indices);
                    boundIndices = draw.Indices;
                }
                if (!bLightmapBound || draw.Lightmap != boundLightmap) {
                    InCommands.SetTexture(kSlotLightmap, draw.Lightmap);
                    boundLightmap = draw.Lightmap;
                    bLightmapBound = true;
                }
                if (draw.Bones && draw.Bones != boundBones) {
                    SetBonePalette(InCommands, *draw.Bones);
                    boundBones = draw.Bones;
                }
                FDrawConstants constants;
                constants.Model = draw.Model;
                constants.NormalMatrix = draw.NormalMatrix;
                constants.LightmapScaleBias = glm::vec4(draw.LightmapScale, draw.LightmapBias);
                constants.DrawFlags = glm::ivec4(draw.Lightmap ? 1 : 0, 0, (draw.bReceiveShadows && !bInCapture) ? 1 : 0,
                                                 (draw.bPlanar && !bInCapture) ? 1 : 0);
                InCommands.SetConstants(ERHIConstantSlot::Draw, &constants, sizeof(constants));
                InCommands.DrawIndexed(draw.IndexCount, draw.FirstIndex);
                FRenderStats& stats = FRenderer::GetStatsMutable();
                ++stats.DrawCalls;
                stats.TriangleCount += draw.IndexCount / 3;
            }
            InCommands.PopMarker();
        };

        // The sky as seen with the given view and projection (engine clip convention).
        const auto drawSky = [&](const glm::mat4& InView, const glm::mat4& InProjection) {
            if (!bDrawSky || !state.SkyPipeline)
                return;
            const FSkyboxComponent& skybox = lighting.Skybox;
            const FDirectionalLightConstants& sun = lighting.Constants.DirLight;
            FSkyConstants sky;
            // The view without its translation: the sky is infinitely far, so only rotation matters.
            sky.ClipToDirection = glm::inverse(ClipDepthZeroToOne() * InProjection * glm::mat4(glm::mat3(InView)));
            sky.SunDirection = sun.Direction.w > 0.5f ? glm::vec4(-glm::vec3(sun.Direction), 0.0f)
                                                      : glm::vec4(0.0f, 1.0f, 0.0f, 0.0f);
            sky.SkyColor = glm::vec4(skybox.SkyZenithColor, skybox.EnvironmentIntensity);
            sky.HorizonColor = glm::vec4(skybox.HorizonColor, 0.0f);
            sky.GroundColor = glm::vec4(skybox.GroundColor, 0.0f);
            sky.SunColor = glm::vec4(skybox.SunColor, skybox.SunIntensity);
            sky.SkyFlags = glm::ivec4(panorama ? 1 : 0, 0, 0, 0);
            InCommands.PushMarker("Sky");
            InCommands.SetPipeline(state.SkyPipeline.get());
            InCommands.SetConstants(ERHIConstantSlot::Material, &sky, sizeof(sky));
            InCommands.SetTexture(0, panorama);
            InCommands.Draw(3);
            InCommands.PopMarker();
        };

        // --- Planar reflections: the scene again, mirrored in a plane ---
        // Only when something on screen shows them. The planes and the mirrored cameras were chosen in
        // UpdateViewState (floor always; a wall mirror when the camera faces one).
        stageTimer.emplace(&FFrameProfiler::Working().PlanarMs);
        const auto wantsPlanar = [](const FSceneDraw& InDraw) { return InDraw.bPlanar; };
        const bool bPlanar = bEnablePlanarReflection && (std::any_of(opaque.begin(), opaque.end(), wantsPlanar) ||
                                                         std::any_of(transparent.begin(), transparent.end(), wantsPlanar));
        bool bPlanarCaptured[2] = {false, false};
        if (bPlanar) {
            // Sized as a fraction of the view; the quality setting decides how many blur levels it has.
            const FPlanarCaptureDesc captureDesc = GetPlanarCaptureDesc();
            const uint32_t captureWidth = captureDesc.Width;
            const uint32_t captureHeight = captureDesc.Height;
            const uint32_t levels = captureDesc.MipLevels;
            if (!state.PlanarColor[0] || state.PlanarColor[0]->GetDesc().Width != captureWidth ||
                state.PlanarColor[0]->GetDesc().Height != captureHeight ||
                state.PlanarColor[0]->GetDesc().MipLevels != levels) {
                FRHITextureDesc colorDesc;
                colorDesc.Width = captureWidth;
                colorDesc.Height = captureHeight;
                colorDesc.MipLevels = levels;
                colorDesc.Format = captureDesc.Format;
                colorDesc.Usage = RHITextureUsage_Sampled | RHITextureUsage_RenderTarget;
                colorDesc.ClearColor = {0.04f, 0.05f, 0.07f, 1.0f};
                colorDesc.DebugName = "PlanarFloor";
                state.PlanarColor[0] = device->CreateTexture(colorDesc);
                colorDesc.DebugName = "PlanarWall";
                state.PlanarColor[1] = device->CreateTexture(colorDesc);
                FRHITextureDesc depthDesc;
                depthDesc.Width = captureWidth;
                depthDesc.Height = captureHeight;
                depthDesc.Format = ERHIFormat::D32_Float;
                depthDesc.Usage = RHITextureUsage_DepthStencil;
                depthDesc.DebugName = "PlanarDepth";
                state.PlanarDepth = device->CreateTexture(depthDesc);
            }
            if (!state.DownsamplePipeline) {
                const TRef<IRHIShader> downVS = FShaderLibrary::Get("Downsample.hlsl", "VSMain", ERHIShaderStage::Vertex);
                const TRef<IRHIShader> downPS = FShaderLibrary::Get("Downsample.hlsl", "PSMain", ERHIShaderStage::Pixel);
                if (downVS && downPS) {
                    FRHIGraphicsPipelineDesc desc;
                    desc.VertexShader = downVS.get();
                    desc.PixelShader = downPS.get();
                    desc.Raster.Cull = ERHICull::None;
                    desc.Depth.bTest = false;
                    desc.Depth.bWrite = false;
                    desc.ColorFormats[0] = ERHIFormat::RGBA16_Float;
                    desc.DebugName = "Downsample";
                    state.DownsamplePipeline = device->CreateGraphicsPipeline(desc);
                }
            }

            const auto capture = [&](uint32_t InIndex, const glm::vec3& InPlaneNormal, float InPlaneDistance,
                                     const glm::mat4& InMirroredViewProjection, const char* InName) {
                IRHITexture* target = state.PlanarColor[InIndex].get();
                if (!target || !state.PlanarDepth)
                    return;
                const glm::mat4 reflection = PlanarReflectionMatrix(InPlaneNormal, InPlaneDistance);
                const glm::mat4 mirrorView = InCamera.GetViewMatrix() * reflection;
                const glm::vec3 mirrorPosition = ReflectPointThroughPlane(cameraPosition, InPlaneNormal, InPlaneDistance);
                const glm::vec3 mirrorForward =
                    SafeNormalize(glm::mat3(reflection) * InCamera.GetForwardDirection(), glm::vec3(0.0f, 0.0f, -1.0f));

                // What the mirrored camera sees.
                std::vector<FSceneDraw> captureOpaque;
                std::vector<FSceneDraw> captureTransparent;
                collectOpaque = &captureOpaque;
                collectTransparent = &captureTransparent;
                bCollectMirrored = true;
                collectNormalTransform = reflection;
                const FFrustumPlanes captureFrustum = ExtractFrustumPlanes(InMirroredViewProjection);
                for (auto entity : meshView) {
                    auto [transform, mesh] = meshView.get<FTransformComponent, FMeshComponent>(entity);
                    if (!mesh.VertexArray ||
                        !CanContributeToPlanarReflection(mesh.bVisible, mesh.bVisibleInReflection, mesh.Mobility))
                        continue;
                    const glm::mat4 model = ResolveActorWorldMatrix(World, entity, transform);
                    if (IsProceduralMeshOutsideLightFrustum(model, mesh, captureFrustum))
                        continue;
                    TRef<FMaterialInstance> material;
                    if (const auto* component = registry.try_get<FMaterialComponent>(entity))
                        material = component->MaterialInstance;
                    if (!material)
                        material = defaultMaterial;
                    const glm::vec3 extent = ProceduralMeshLocalExtent(mesh);
                    submit(*mesh.VertexArray, material.get(), model, GetRHIIndexCount(*mesh.VertexArray), 0,
                           lightmapFor(mesh.Mobility, mesh.LightmapIndex, mesh.LightmapAssetPath), mesh.LightmapScale,
                           mesh.LightmapBias, -extent, extent, nullptr, false);
                }
                for (auto entity : staticView) {
                    auto [transform, component] = staticView.get<FTransformComponent, FStaticMeshComponent>(entity);
                    if (!component.StaticMesh || !CanContributeToPlanarReflection(component.bVisible,
                                                                                  component.bVisibleInReflection,
                                                                                  component.Mobility))
                        continue;
                    const glm::mat4 world = ResolveActorWorldMatrix(World, entity, transform);
                    if (IsStaticMeshOutsideLightFrustum(world, component, captureFrustum))
                        continue;
                    const uint32_t lod =
                        std::min(component.CurrentLOD, std::max(1u, component.StaticMesh->GetLODCount()) - 1);
                    const TRef<FVertexArray> vertexArray = component.StaticMesh->GetLODVertexArray(lod);
                    if (!vertexArray)
                        continue;
                    IRHITexture* lightmap =
                        lightmapFor(component.Mobility, component.LightmapIndex, component.LightmapAssetPath);
                    for (const auto& submesh : component.StaticMesh->GetLODSubmeshes(lod)) {
                        const TRef<FMaterialInstance> material =
                            ResolveStaticSubmeshMaterial(*component.StaticMesh, submesh, component.MaterialOverrides);
                        submit(*vertexArray, material ? material.get() : defaultMaterial.get(),
                               world * submesh.LocalTransform, submesh.IndexCount, submesh.IndexOffset, lightmap,
                               component.LightmapScale, component.LightmapBias, component.StaticMesh->GetBoundsMin(),
                               component.StaticMesh->GetBoundsMax(), nullptr, false);
                    }
                }
                for (auto entity : skinnedView) {
                    auto [transform, skinned] = skinnedView.get<FTransformComponent, FSkinnedMeshRenderState>(entity);
                    if (!skinned.bVisible || !skinned.bVisibleInReflection || !skinned.SkeletalMesh)
                        continue;
                    const TRef<FVertexArray> vertexArray = skinned.SkeletalMesh->GetVertexArray();
                    if (!vertexArray)
                        continue;
                    const glm::mat4 world = ResolveActorWorldMatrix(World, entity, transform);
                    if (IsSkeletalMeshOutsideLightFrustum(world, skinned, captureFrustum))
                        continue;
                    for (const auto& submesh : skinned.SkeletalMesh->GetSubmeshes()) {
                        const TRef<FMaterialInstance> material =
                            ResolveSkeletalSubmeshMaterial(*skinned.SkeletalMesh, submesh, skinned.MaterialOverrides);
                        submit(*vertexArray, material ? material.get() : defaultMaterial.get(),
                               SkeletalModelMatrix(world, skinned, submesh.LocalTransform), submesh.IndexCount,
                               submesh.IndexOffset, nullptr, glm::vec2(1.0f), glm::vec2(0.0f),
                               skinned.SkeletalMesh->GetBoundsMin(), skinned.SkeletalMesh->GetBoundsMax(),
                               &skinned.BonePalette, false);
                    }
                }
                collectOpaque = &opaque;
                collectTransparent = &transparent;
                bCollectMirrored = false;
                collectNormalTransform = glm::mat4(1.0f);
                for (const FSceneDraw& draw : captureOpaque)
                    prepareMaterial(*draw.Material);
                for (const FSceneDraw& draw : captureTransparent)
                    prepareMaterial(*draw.Material);

                FFrameConstants captureFrame = frame;
                captureFrame.ViewProjection = ClipDepthZeroToOne() * InMirroredViewProjection;
                captureFrame.View = mirrorView;
                captureFrame.ViewPosition = glm::vec4(mirrorPosition, 1.0f);
                captureFrame.CameraForward = glm::vec4(mirrorForward, 0.0f);
                captureFrame.ViewportSize = {static_cast<float>(captureWidth), static_cast<float>(captureHeight),
                                             1.0f / static_cast<float>(captureWidth),
                                             1.0f / static_cast<float>(captureHeight)};
                // The same lights, without shadows, reflections or debug views, cut at the mirror plane
                // so that nothing behind the mirror shows up in it.
                FLitPassConstants capturePass = lighting.Constants;
                capturePass.PassFlags.y = 0;
                capturePass.ShadowFlags = glm::ivec4(0);
                capturePass.ShadowSettings.y = -1;
                capturePass.ClipPlane = glm::vec4(InPlaneNormal, InPlaneDistance);
                capturePass.PlanarFlags = glm::ivec4(0, 0, 1, 0);

                FRHIRenderPassDesc pass;
                pass.NumColorTargets = 1;
                pass.Color[0].Texture = target;
                pass.Color[0].ClearColor = {0.04f, 0.05f, 0.07f, 1.0f};
                pass.Depth.Texture = state.PlanarDepth.get();
                pass.Name = InName;
                InCommands.BeginRenderPass(pass);
                InCommands.SetConstants(ERHIConstantSlot::Frame, &captureFrame, sizeof(captureFrame));
                InCommands.SetConstants(ERHIConstantSlot::Pass, &capturePass, sizeof(capturePass));
                bindSceneTextures(nullptr, nullptr);
                drawAll(captureOpaque, "Opaque", true);
                drawSky(mirrorView, InCamera.GetProjectionMatrix());
                drawAll(captureTransparent, "Transparent", true);
                InCommands.EndRenderPass();
                InCommands.GenerateMips(*target, state.DownsamplePipeline.get());

                lit.PlanarMatrix[InIndex] = ClipToShadowMap() * InMirroredViewProjection;
                lit.PlanarPlane[InIndex] = glm::vec4(InPlaneNormal, InPlaneDistance);
                bPlanarCaptured[InIndex] = true;
            };
            capture(0, PlanarPlaneNormal, PlanarPlaneDistance, PlanarViewProjection, "PlanarFloor");
            if (bWallPlanarActive)
                capture(1, WallPlanarPlaneNormal, WallPlanarPlaneDistance, WallPlanarViewProjection, "PlanarWall");
        }
        lit.PlanarFlags = glm::ivec4(bPlanarCaptured[0] ? 1 : 0, bPlanarCaptured[1] ? 1 : 0, 0, 0);
        stageTimer.reset();

        // --- Scene pass: linear HDR colour ---
        stageTimer.emplace(&FFrameProfiler::Working().OpaqueMs);
        FRHIRenderPassDesc scenePass;
        scenePass.NumColorTargets = 1;
        scenePass.Color[0].Texture = state.SceneColor.get();
        scenePass.Color[0].ClearColor = {0.04f, 0.05f, 0.07f, 1.0f};
        scenePass.Depth.Texture = state.SceneDepth.get();
        scenePass.Name = "Scene";
        InCommands.BeginRenderPass(scenePass);
        InCommands.SetConstants(ERHIConstantSlot::Frame, &frame, sizeof(frame));
        InCommands.SetConstants(ERHIConstantSlot::Pass, &lighting.Constants, sizeof(lighting.Constants));
        bindSceneTextures(bPlanarCaptured[0] ? state.PlanarColor[0].get() : nullptr,
                          bPlanarCaptured[1] ? state.PlanarColor[1].get() : nullptr);
        drawAll(opaque, "Opaque", false);
        if (!outlines.empty() && state.OutlinePipeline) {
            InCommands.PushMarker("Outlines");
            InCommands.SetPipeline(state.OutlinePipeline.get());
            for (const FOutlineDraw& outline : outlines) {
                FOutlineConstants outlineConstants;
                outlineConstants.ColorWidth = outline.ColorWidth;
                FDrawConstants constants;
                constants.Model = outline.Model;
                constants.NormalMatrix = glm::mat4(SafeNormalMatrix(outline.Model));
                constants.LightmapScaleBias = glm::vec4(0.0f);
                constants.DrawFlags = glm::ivec4(0);
                SetBonePalette(InCommands, *outline.Bones);
                InCommands.SetConstants(ERHIConstantSlot::Material, &outlineConstants, sizeof(outlineConstants));
                InCommands.SetConstants(ERHIConstantSlot::Draw, &constants, sizeof(constants));
                InCommands.SetVertexBuffer(outline.Vertices);
                InCommands.SetIndexBuffer(outline.Indices);
                InCommands.DrawIndexed(outline.IndexCount, outline.FirstIndex);
            }
            InCommands.PopMarker();
        }
        drawSky(InCamera.GetViewMatrix(), InCamera.GetProjectionMatrix());
        drawAll(transparent, "Transparent", false);
        FSceneBatchesRHI::Flush(InCommands, ERHIFormat::RGBA16_Float, ERHIFormat::D32_Float);
        InCommands.EndRenderPass();
        stageTimer.reset();

        // --- HDR scene to the output ---
        FPostProcessInputs post;
        post.SceneColor = state.SceneColor.get();
        post.SceneDepth = state.SceneDepth.get();
        post.Output = &InTarget;
        post.Projection = ClipDepthZeroToOne() * InCamera.GetProjectionMatrix();
        post.Exposure = lighting.Exposure;
        stageTimer.emplace(&FFrameProfiler::Working().PostProcessMs);
        // Exposure is a property of the sky; the stored settings follow it so tools show the value in use.
        PostProcessSettings.Exposure = lighting.Exposure;
        state.PostProcess.Render(InCommands, post, PostProcessSettings);
        stageTimer.reset();

        const FRenderStats& stats = FRenderer::GetStats();
        FFrameTiming& profile = FFrameProfiler::Working();
        profile.VisibleActors = static_cast<int32_t>(stats.MeshesDrawn);
        profile.CulledActors = static_cast<int32_t>(stats.MeshesCulled);
        profile.DrawCalls = stats.DrawCalls;
        profile.TriangleCount = stats.TriangleCount;
    }

} // namespace Leon
