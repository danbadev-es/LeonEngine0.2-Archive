#pragma once

#include "RHI/IRHIDevice.hpp"
#include "RenderCore/FPostProcessSettings.hpp"

#include <glm/glm.hpp>

#include <map>
#include <string>
#include <vector>

namespace Leon {

    /** What a frame gives the post-process chain. */
    struct FPostProcessInputs {
        /** Linear HDR colour of the scene. */
        IRHITexture* SceneColor = nullptr;
        /** Depth of the scene; must be creatable as a sampled texture. */
        IRHITexture* SceneDepth = nullptr;
        /** Where the finished image goes (the window's back buffer, or the editor's viewport). */
        IRHITexture* Output = nullptr;
        /** The camera's projection as the scene was drawn with it: clip depth 0..1. */
        glm::mat4 Projection{1.0f};
        float Exposure = 1.0f;
    };

    /**
     * Turns the HDR scene into the final image on the RHI: ambient occlusion, bloom, tone mapping (with
     * the look of the map: chromatic aberration, colour grading, vignette), anti-aliasing and film
     * grain, in that order. Owns the intermediate targets and recreates them when the output
     * changes size.
     */
    class FPostProcessRHI {
    public:
        /** Call between render passes: it begins and ends its own. */
        void Render(IRHICommandList& InCommands, const FPostProcessInputs& InInputs,
                    const FPostProcessSettings& InSettings);

        // The intermediate targets, as the last Render left them (empty or null before the first).
        /** Bloom levels, largest first: half the output, then half of that, and so on. */
        const std::vector<TRef<IRHITexture>>& GetBloomTargets() const { return BloomDown; }
        /** Ambient occlusion, at half the output's size. */
        IRHITexture* GetOcclusionTarget() const { return Occlusion.get(); }
        /** The tone-mapped image anti-aliasing reads, at the output's size. */
        IRHITexture* GetToneMappedTarget() const { return ToneMapped.get(); }

    private:
        void EnsureTargets(IRHIDevice& InDevice, uint32_t InWidth, uint32_t InHeight);
        IRHIPipeline* GetPipeline(IRHIDevice& InDevice, const char* InFile, const char* InPixelEntry,
                                  ERHIFormat InTargetFormat, bool bInAdditive = false);

        uint32_t Width = 0;
        uint32_t Height = 0;
        /** Half resolution and smaller, largest first. */
        std::vector<TRef<IRHITexture>> BloomDown;
        std::vector<TRef<IRHITexture>> BloomUp;
        TRef<IRHITexture> Occlusion;
        TRef<IRHITexture> OcclusionBlur;
        TRef<IRHITexture> OccludedScene;
        TRef<IRHITexture> ToneMapped;
        /** By shader, entry point, target format and blending. Null: the pipeline failed to build. */
        std::map<std::string, TRef<IRHIPipeline>> Pipelines;
        /** FShaderLibrary::GetGeneration when Pipelines was filled. */
        uint64_t ShaderGeneration = 0;
        glm::vec4 OcclusionKernel[16]{};
        bool bKernelBuilt = false;
        /** Frames drawn so far: what animated film grain is seeded with. */
        uint32_t FrameIndex = 0;
    };

} // namespace Leon
