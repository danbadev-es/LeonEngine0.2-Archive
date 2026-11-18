#include <doctest/doctest.h>
#include "GPU/HeadlessDevice.hpp"
#include "Renderer/FPostProcessRHI.hpp"

#include <vector>

// The whole post-process chain as the world renderer runs it: HDR scene in, finished 8-bit image out.

namespace {

    Leon::TRef<Leon::IRHITexture> MakeOutput(Leon::IRHIDevice& InDevice, uint32_t InWidth, uint32_t InHeight) {
        Leon::FRHITextureDesc desc;
        desc.Width = InWidth;
        desc.Height = InHeight;
        desc.Format = Leon::ERHIFormat::RGBA8_UNorm;
        desc.Usage = Leon::RHITextureUsage_Sampled | Leon::RHITextureUsage_RenderTarget;
        desc.DebugName = "TestOutput";
        return InDevice.CreateTexture(desc);
    }

    /** One frame of the chain. There is no depth buffer here, so ambient occlusion stays out. */
    void RunChain(Leon::IRHIDevice& InDevice, Leon::FPostProcessRHI& InChain, Leon::IRHITexture& InScene,
                  Leon::IRHITexture& InOutput, const Leon::FPostProcessSettings& InSettings) {
        Leon::IRHICommandList& commands = InDevice.BeginFrame();
        Leon::FPostProcessInputs inputs;
        inputs.SceneColor = &InScene;
        inputs.Output = &InOutput;
        InChain.Render(commands, inputs, InSettings);
        InDevice.EndFrame();
        InDevice.WaitIdle();
    }

} // namespace

TEST_SUITE("Shader GPU - End-to-End Post-Processing Pipeline") {

    TEST_CASE("End-to-End Pipeline Synthetic Scene (HDR -> Bloom -> Tone Mapping -> FXAA -> Output)") {
        Leon::TestGPU::FHeadlessDevice gl;
        REQUIRE(gl.IsValid());
        Leon::IRHIDevice& device = *gl.GetDevice();

        const uint32_t width = 32;
        const uint32_t height = 32;

        Leon::FPostProcessRHI chain;
        auto output = MakeOutput(device, width, height);
        REQUIRE(output != nullptr);

        Leon::FPostProcessSettings settings;
        settings.bEnabled = true;
        settings.bBloomEnabled = true;
        settings.BloomThreshold = 1.0f;
        settings.BloomSoftKnee = 0.5f;
        settings.BloomIntensity = 0.1f;
        settings.BloomRadius = 1.0f;
        settings.ToneMapper = 0; // ACES
        settings.Exposure = 1.0f;
        settings.Gamma = 2.2f;
        settings.bFXAAEnabled = true;
        settings.DebugMode = 0;

        // Case A: a black scene must come out strictly black.
        std::vector<float> blackPixels(width * height * 4, 0.0f);
        auto scene = gl.Create2D(width, height, Leon::ERHIFormat::RGBA32_Float, blackPixels.data());
        REQUIRE(scene != nullptr);

        RunChain(device, chain, *scene, *output, settings);
        std::vector<uint8_t> outBlack;
        REQUIRE(device.ReadTexture(*output, 0, 0, outBlack));
        REQUIRE(outBlack.size() == static_cast<size_t>(width) * height * 4);
        for (size_t i = 0; i < outBlack.size(); i += 4) {
            CHECK(outBlack[i + 0] == 0);
            CHECK(outBlack[i + 1] == 0);
            CHECK(outBlack[i + 2] == 0);
        }

        // Case B: a bright spot (the centre 4x4 pixels at 15.0, the rest black). The centre tone maps
        // to white and the pixels around it receive bloom.
        std::vector<float> hotspotPixels(width * height * 4, 0.0f);
        for (uint32_t y = 14; y <= 17; ++y) {
            for (uint32_t x = 14; x <= 17; ++x) {
                size_t idx = (y * width + x) * 4;
                hotspotPixels[idx + 0] = 15.0f;
                hotspotPixels[idx + 1] = 15.0f;
                hotspotPixels[idx + 2] = 15.0f;
                hotspotPixels[idx + 3] = 1.0f;
            }
        }
        gl.Upload(scene, hotspotPixels.data());

        RunChain(device, chain, *scene, *output, settings);
        std::vector<uint8_t> outHotspot;
        REQUIRE(device.ReadTexture(*output, 0, 0, outHotspot));
        REQUIRE(outHotspot.size() == static_cast<size_t>(width) * height * 4);

        size_t centerIdx = (15 * width + 15) * 4;
        CHECK(outHotspot[centerIdx + 0] > 200);
        CHECK(outHotspot[centerIdx + 1] > 200);
        CHECK(outHotspot[centerIdx + 2] > 200);

        // Outside the original 4x4 spot.
        size_t neighborIdx = (12 * width + 12) * 4;
        CHECK(outHotspot[neighborIdx + 0] > 0);
        CHECK(outHotspot[neighborIdx + 1] > 0);
        CHECK(outHotspot[neighborIdx + 2] > 0);

        // Without bloom the same pixel stays black: the glow above really is bloom.
        settings.bBloomEnabled = false;
        RunChain(device, chain, *scene, *output, settings);
        std::vector<uint8_t> outNoBloom;
        REQUIRE(device.ReadTexture(*output, 0, 0, outNoBloom));
        REQUIRE(outNoBloom.size() == outHotspot.size());
        CHECK(outNoBloom[neighborIdx + 0] == 0);
        CHECK(outNoBloom[centerIdx + 0] > 200);

        CHECK(gl.ValidationErrors() == 0);
    }

    TEST_CASE("Pipeline targets follow the output through resizes") {
        Leon::TestGPU::FHeadlessDevice gl;
        REQUIRE(gl.IsValid());
        Leon::IRHIDevice& device = *gl.GetDevice();

        Leon::FPostProcessRHI chain;
        Leon::FPostProcessSettings settings;
        auto scene = gl.Create1x1FloatTexture(0.5f, 0.5f, 0.5f, 1.0f);
        REQUIRE(scene != nullptr);

        const auto render = [&](uint32_t InWidth, uint32_t InHeight) {
            auto output = MakeOutput(device, InWidth, InHeight);
            REQUIRE(output != nullptr);
            RunChain(device, chain, *scene, *output, settings);
            // The chain really drew at this size: the far corner holds the tone-mapped grey.
            std::vector<uint8_t> pixels;
            REQUIRE(device.ReadTexture(*output, 0, 0, pixels));
            REQUIRE(pixels.size() == static_cast<size_t>(InWidth) * InHeight * 4);
            CHECK(pixels[pixels.size() - 4] > 100);
        };
        const auto checkSize = [](const Leon::IRHITexture* InTexture, uint32_t InWidth, uint32_t InHeight) {
            REQUIRE(InTexture != nullptr);
            CHECK(InTexture->GetDesc().Width == InWidth);
            CHECK(InTexture->GetDesc().Height == InHeight);
        };

        // 1. 1280x720
        render(1280, 720);
        checkSize(chain.GetToneMappedTarget(), 1280, 720);
        checkSize(chain.GetOcclusionTarget(), 640, 360);
        REQUIRE(chain.GetBloomTargets().size() == 5);
        checkSize(chain.GetBloomTargets()[0].get(), 640, 360);
        checkSize(chain.GetBloomTargets()[4].get(), 40, 22);

        // 2. Up to 1920x1080
        render(1920, 1080);
        checkSize(chain.GetToneMappedTarget(), 1920, 1080);
        REQUIRE(chain.GetBloomTargets().size() == 5);
        checkSize(chain.GetBloomTargets()[0].get(), 960, 540);

        // 3. And back
        render(1280, 720);
        checkSize(chain.GetToneMappedTarget(), 1280, 720);
        REQUIRE(chain.GetBloomTargets().size() == 5);
        checkSize(chain.GetBloomTargets()[0].get(), 640, 360);

        CHECK(gl.ValidationErrors() == 0);
    }
}
