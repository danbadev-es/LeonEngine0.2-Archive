#include "Renderer/FPostProcessRHI.hpp"

#include "RenderCore/FShaderConstants.hpp"
#include "RenderCore/FShaderLibrary.hpp"

#include <algorithm>
#include <random>

namespace Leon {

    namespace {

        constexpr uint32_t kBloomLevels = 5;

        TRef<IRHITexture> MakeTarget(IRHIDevice& InDevice, uint32_t InWidth, uint32_t InHeight, ERHIFormat InFormat,
                                     const char* InName) {
            FRHITextureDesc desc;
            desc.Width = std::max(InWidth, 1u);
            desc.Height = std::max(InHeight, 1u);
            desc.Format = InFormat;
            desc.Usage = RHITextureUsage_Sampled | RHITextureUsage_RenderTarget;
            desc.ClearColor = {0.0f, 0.0f, 0.0f, 0.0f};
            desc.DebugName = InName;
            return InDevice.CreateTexture(desc);
        }

        glm::vec2 TexelSizeOf(const IRHITexture& InTexture) {
            return {1.0f / static_cast<float>(InTexture.GetDesc().Width),
                    1.0f / static_cast<float>(InTexture.GetDesc().Height)};
        }

    } // namespace

    void FPostProcessRHI::EnsureTargets(IRHIDevice& InDevice, uint32_t InWidth, uint32_t InHeight) {
        if (Width == InWidth && Height == InHeight && ToneMapped)
            return;
        Width = InWidth;
        Height = InHeight;

        BloomDown.clear();
        BloomUp.clear();
        uint32_t levelWidth = std::max(InWidth / 2, 1u);
        uint32_t levelHeight = std::max(InHeight / 2, 1u);
        for (uint32_t level = 0; level < kBloomLevels; ++level) {
            BloomDown.push_back(MakeTarget(InDevice, levelWidth, levelHeight, ERHIFormat::RGBA16_Float, "BloomDown"));
            BloomUp.push_back(MakeTarget(InDevice, levelWidth, levelHeight, ERHIFormat::RGBA16_Float, "BloomUp"));
            levelWidth = std::max(levelWidth / 2, 1u);
            levelHeight = std::max(levelHeight / 2, 1u);
        }
        Occlusion = MakeTarget(InDevice, InWidth / 2, InHeight / 2, ERHIFormat::RGBA8_UNorm, "Occlusion");
        OcclusionBlur = MakeTarget(InDevice, InWidth / 2, InHeight / 2, ERHIFormat::RGBA8_UNorm, "OcclusionBlur");
        OccludedScene = MakeTarget(InDevice, InWidth, InHeight, ERHIFormat::RGBA16_Float, "OccludedScene");
        ToneMapped = MakeTarget(InDevice, InWidth, InHeight, ERHIFormat::RGBA8_UNorm, "ToneMapped");
    }

    IRHIPipeline* FPostProcessRHI::GetPipeline(IRHIDevice& InDevice, const char* InFile, const char* InPixelEntry,
                                               ERHIFormat InTargetFormat, bool bInAdditive) {
        const std::string key = std::string(InFile) + "|" + InPixelEntry + "|" +
                                std::to_string(static_cast<int>(InTargetFormat)) + (bInAdditive ? "|add" : "");
        const auto found = Pipelines.find(key);
        if (found != Pipelines.end())
            return found->second.get();

        TRef<IRHIPipeline> pipeline;
        const TRef<IRHIShader> vertexShader = FShaderLibrary::Get(InFile, "VSMain", ERHIShaderStage::Vertex);
        const TRef<IRHIShader> pixelShader = FShaderLibrary::Get(InFile, InPixelEntry, ERHIShaderStage::Pixel);
        if (vertexShader && pixelShader) {
            FRHIGraphicsPipelineDesc desc;
            desc.VertexShader = vertexShader.get();
            desc.PixelShader = pixelShader.get();
            desc.Raster.Cull = ERHICull::None;
            desc.Depth.bTest = false;
            desc.Depth.bWrite = false;
            if (bInAdditive) {
                desc.Blend.bEnabled = true;
                desc.Blend.SrcColor = ERHIBlend::One;
                desc.Blend.DstColor = ERHIBlend::One;
                desc.Blend.SrcAlpha = ERHIBlend::One;
                desc.Blend.DstAlpha = ERHIBlend::One;
            }
            desc.ColorFormats[0] = InTargetFormat;
            desc.DebugName = std::string(InFile) + ":" + InPixelEntry;
            pipeline = InDevice.CreateGraphicsPipeline(desc);
        }
        Pipelines[key] = pipeline;
        return pipeline.get();
    }

    void FPostProcessRHI::Render(IRHICommandList& InCommands, const FPostProcessInputs& InInputs,
                                 const FPostProcessSettings& InSettings) {
        IRHIDevice* device = FRHI::GetDevice();
        if (!device || !InInputs.SceneColor || !InInputs.Output)
            return;
        IRHITexture& output = *InInputs.Output;
        if (ShaderGeneration != FShaderLibrary::GetGeneration()) {
            Pipelines.clear();
            ShaderGeneration = FShaderLibrary::GetGeneration();
        }
        EnsureTargets(*device, output.GetDesc().Width, output.GetDesc().Height);
        if (!ToneMapped || !OccludedScene || !Occlusion || !OcclusionBlur || BloomDown.size() != kBloomLevels ||
            BloomUp.size() != kBloomLevels)
            return;

        if (!bKernelBuilt) {
            // Points in a hemisphere, denser near the centre. A fixed seed keeps the image stable.
            std::mt19937 random(0x5353414F);
            std::uniform_real_distribution<float> signedUnit(-1.0f, 1.0f);
            std::uniform_real_distribution<float> unit(0.0f, 1.0f);
            for (int i = 0; i < 16; ++i) {
                glm::vec3 sample(signedUnit(random), signedUnit(random), unit(random));
                sample = glm::normalize(sample) * unit(random);
                const float t = static_cast<float>(i) / 16.0f;
                OcclusionKernel[i] = glm::vec4(sample * (0.1f + t * t * 0.9f), 0.0f);
            }
            bKernelBuilt = true;
        }

        // The textures the next draw reads, by register. Applied inside its pass.
        IRHITexture* inputs[3] = {nullptr, nullptr, nullptr};
        const auto setInput = [&inputs](uint32_t InSlot, IRHITexture* InTexture) { inputs[InSlot] = InTexture; };

        // One full-screen triangle into InTarget. bInKeep draws over what the target holds.
        const auto draw = [&](IRHITexture& InTarget, IRHIPipeline* InPipeline, const void* InConstants,
                              uint32_t InConstantsSize, const char* InName, bool bInKeep = false) {
            FRHIRenderPassDesc pass;
            pass.NumColorTargets = 1;
            pass.Color[0].Texture = &InTarget;
            pass.Color[0].LoadOp = bInKeep ? ERHILoadOp::Load : ERHILoadOp::Clear;
            pass.Color[0].ClearColor = InTarget.GetDesc().ClearColor;
            pass.Name = InName;
            InCommands.BeginRenderPass(pass);
            if (InPipeline) {
                InCommands.SetPipeline(InPipeline);
                InCommands.SetConstants(ERHIConstantSlot::Pass, InConstants, InConstantsSize);
                for (uint32_t slot = 0; slot < 3; ++slot)
                    InCommands.SetTexture(slot, inputs[slot]);
                InCommands.Draw(3);
            }
            InCommands.EndRenderPass();
        };

        IRHITexture* scene = InInputs.SceneColor;

        // --- Ambient occlusion ---
        const bool bOcclusion = InSettings.bEnabled && InSettings.bSSAOEnabled && InInputs.SceneDepth &&
                                InSettings.DebugMode != 1;
        if (bOcclusion) {
            FSSAOConstants constants;
            constants.Projection = InInputs.Projection;
            constants.InverseProjection = glm::inverse(InInputs.Projection);
            for (int i = 0; i < 16; ++i)
                constants.Samples[i] = OcclusionKernel[i];
            constants.SSAOParams = glm::vec4(InSettings.SSAORadius, InSettings.SSAOBias,
                                             static_cast<float>(std::clamp(InSettings.SSAOKernelSize, 1, 16)),
                                             InSettings.SSAOIntensity);
            const glm::vec2 texel = TexelSizeOf(*Occlusion);

            setInput(0, InInputs.SceneDepth);
            draw(*Occlusion, GetPipeline(*device, "SSAO.hlsl", "PSOcclusion", ERHIFormat::RGBA8_UNorm), &constants,
                 sizeof(constants), "SSAO");

            IRHIPipeline* blur = GetPipeline(*device, "SSAO.hlsl", "PSBlur", ERHIFormat::RGBA8_UNorm);
            constants.BlurParams = glm::vec4(texel, 1.0f, 0.0f);
            setInput(1, Occlusion.get());
            draw(*OcclusionBlur, blur, &constants, sizeof(constants), "SSAOBlurH");
            constants.BlurParams = glm::vec4(texel, 0.0f, 0.0f);
            setInput(1, OcclusionBlur.get());
            draw(*Occlusion, blur, &constants, sizeof(constants), "SSAOBlurV");

            constants.BlurParams = glm::vec4(texel, 0.0f, InSettings.DebugMode == 5 ? 1.0f : 0.0f);
            setInput(1, Occlusion.get());
            setInput(2, scene);
            draw(*OccludedScene, GetPipeline(*device, "SSAO.hlsl", "PSComposite", ERHIFormat::RGBA16_Float), &constants,
                 sizeof(constants), "SSAOComposite");
            setInput(1, nullptr);
            setInput(2, nullptr);
            scene = OccludedScene.get();
        }

        // --- Bloom ---
        const uint32_t bloomLevels = std::min(kBloomLevels, InSettings.BloomMipCount);
        const bool bBloom = InSettings.bEnabled && InSettings.bBloomEnabled && InSettings.DebugMode != 1 &&
                            InSettings.DebugMode != 5 && bloomLevels >= 2;
        if (bBloom) {
            FBloomConstants constants;
            constants.BloomParams = glm::vec4(InSettings.BloomThreshold, InSettings.BloomSoftKnee, 0.0f, 0.0f);
            setInput(0, scene);
            draw(*BloomDown[0], GetPipeline(*device, "Bloom.hlsl", "PSBrightPass", ERHIFormat::RGBA16_Float), &constants,
                 sizeof(constants), "BloomBright");

            IRHIPipeline* downsample = GetPipeline(*device, "Bloom.hlsl", "PSDownsample", ERHIFormat::RGBA16_Float);
            for (uint32_t level = 1; level < bloomLevels; ++level) {
                constants.BloomParams.w = level == 1 ? 1.0f : 0.0f;
                constants.TexelSize = glm::vec4(TexelSizeOf(*BloomDown[level - 1]), 0.0f, 0.0f);
                setInput(0, BloomDown[level - 1].get());
                draw(*BloomDown[level], downsample, &constants, sizeof(constants), "BloomDown");
            }

            // Back up the chain: each level is its own downsample plus the blurred level below it.
            IRHIPipeline* copy = GetPipeline(*device, "Bloom.hlsl", "PSUpsample", ERHIFormat::RGBA16_Float);
            IRHIPipeline* add = GetPipeline(*device, "Bloom.hlsl", "PSUpsample", ERHIFormat::RGBA16_Float, true);
            for (uint32_t level = bloomLevels - 1; level > 0; --level) {
                IRHITexture& target = *BloomUp[level - 1];
                IRHITexture& smaller = level == bloomLevels - 1 ? *BloomDown[level] : *BloomUp[level];
                constants.BloomParams.z = 0.0f; // radius 0: a plain copy
                constants.TexelSize = glm::vec4(TexelSizeOf(*BloomDown[level - 1]), 0.0f, 0.0f);
                setInput(0, BloomDown[level - 1].get());
                draw(target, copy, &constants, sizeof(constants), "BloomUpBase");
                constants.BloomParams.z = InSettings.BloomRadius;
                constants.TexelSize = glm::vec4(TexelSizeOf(smaller), 0.0f, 0.0f);
                setInput(0, &smaller);
                draw(target, add, &constants, sizeof(constants), "BloomUpAdd", true);
            }
        }

        // --- Tone mapping: HDR to display colours, luma in alpha for the next pass ---
        {
            FToneMappingConstants constants;
            constants.ToneParams = glm::vec4(InInputs.Exposure, InSettings.BloomIntensity, InSettings.Gamma, 0.0f);
            constants.ToneFlags = glm::ivec4(InSettings.ToneMapper, bBloom ? 1 : 0, InSettings.DebugMode, 0);
            setInput(0, scene);
            setInput(1, bBloom ? BloomUp[0].get() : nullptr);
            draw(*ToneMapped, GetPipeline(*device, "ToneMapping.hlsl", "PSMain", ERHIFormat::RGBA8_UNorm), &constants,
                 sizeof(constants), "ToneMapping");
            setInput(1, nullptr);
        }

        // --- Anti-aliasing, straight into the output ---
        {
            const bool bFXAA = InSettings.bEnabled && InSettings.bFXAAEnabled && InSettings.DebugMode != 4;
            FFXAAConstants constants;
            constants.FXAAParams = glm::vec4(TexelSizeOf(*ToneMapped), bFXAA ? 1.0f : 0.0f, 0.0f);
            setInput(0, ToneMapped.get());
            draw(output, GetPipeline(*device, "FXAA.hlsl", "PSMain", output.GetDesc().Format), &constants,
                 sizeof(constants), "FXAA");
        }
    }

} // namespace Leon
