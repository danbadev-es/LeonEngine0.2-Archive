#include <doctest/doctest.h>
#include "GPU/HeadlessDevice.hpp"
#include <vector>
#include <cmath>

TEST_SUITE("Shader GPU - Post-Processing FXAA Pipeline") {

    TEST_CASE("FXAA Uniform Color Field Zero Distortion Invariant") {
        Leon::TestGPU::FHeadlessDevice gl;
        REQUIRE(gl.IsValid());

        auto shader = Leon::TestGPU::FTestShader::Create("Engine/Resources/Shaders/FXAA.glsl");
        REQUIRE(shader != nullptr);
        shader->Bind();

        gl.BindDefaultTextures();
        gl.BindFramebuffer(16, 16);

        // 16x16 Uniform Color Texture (R=0.6, G=0.6, B=0.6, A=0.6)
        auto ldrTex = gl.CreateEmpty2D(16, 16, Leon::ERHIFormat::RGBA8_UNorm);
        std::vector<uint8_t> uniformData(16 * 16 * 4, static_cast<uint8_t>(0.6f * 255.0f));
        gl.Upload(ldrTex, uniformData.data());

        gl.BindTexture(0, ldrTex);
        shader->SetInt("u_LDRTexture", 0);
        shader->SetFloat2("u_InverseScreenSize", 1.0f / 16.0f, 1.0f / 16.0f);
        shader->SetInt("u_FXAAEnabled", 1);

        gl.DrawQuad();

        const std::vector<uint8_t> readback = gl.ReadPixelsRGBA8();
        REQUIRE(readback.size() == 16 * 16 * 4);

        // Check across multiple sample coordinates (center, corners)
        for (int y = 0; y < 16; y += 4) {
            for (int x = 0; x < 16; x += 4) {
                size_t idx = (y * 16 + x) * 4;
                float r = static_cast<float>(readback[idx + 0]) / 255.0f;
                float g = static_cast<float>(readback[idx + 1]) / 255.0f;
                float b = static_cast<float>(readback[idx + 2]) / 255.0f;

                CHECK(!std::isnan(r));
                CHECK(!std::isinf(r));
                CHECK(r == doctest::Approx(0.6f).epsilon(0.02f));
                CHECK(g == doctest::Approx(0.6f).epsilon(0.02f));
                CHECK(b == doctest::Approx(0.6f).epsilon(0.02f));
            }
        }

        // Sub-threshold contrast test (100 on left, 108 on right -> below lumaMax * 0.125 = 0.053)
        std::vector<uint8_t> subThresholdData(16 * 16 * 4, 0);
        for (int y = 0; y < 16; ++y) {
            for (int x = 0; x < 16; ++x) {
                uint8_t val = (x >= 8) ? 108 : 100;
                size_t idx = (y * 16 + x) * 4;
                subThresholdData[idx + 0] = val;
                subThresholdData[idx + 1] = val;
                subThresholdData[idx + 2] = val;
                subThresholdData[idx + 3] = val;
            }
        }
        gl.Upload(ldrTex, subThresholdData.data());
        gl.DrawQuad();

        const std::vector<uint8_t> readSub = gl.ReadPixelsRGBA8();
        REQUIRE(readSub.size() == 16 * 16 * 4);
        CHECK(readSub[(8 * 16 + 7) * 4] == 100); // Must not blur sub-threshold differences
        CHECK(readSub[(8 * 16 + 8) * 4] == 108);
        // Exactly, not to the nearest 8-bit value: on a step this small the filter would move the
        // pixel by less than half a level.
        const std::vector<float> exactSub = gl.ReadPixels();
        REQUIRE(exactSub.size() == 16 * 16 * 4);
        CHECK(exactSub[(8 * 16 + 7) * 4] == doctest::Approx(100.0f / 255.0f).epsilon(0.0005f));
        CHECK(exactSub[(8 * 16 + 8) * 4] == doctest::Approx(108.0f / 255.0f).epsilon(0.0005f));

        gl.DestroyTexture(ldrTex);
    }

    TEST_CASE("FXAA High-Contrast Edge Anti-Aliasing Response") {
        Leon::TestGPU::FHeadlessDevice gl;
        REQUIRE(gl.IsValid());

        auto shader = Leon::TestGPU::FTestShader::Create("Engine/Resources/Shaders/FXAA.glsl");
        REQUIRE(shader != nullptr);
        shader->Bind();

        gl.BindDefaultTextures();

        gl.BindFramebuffer(16, 16);

        // 16x16 Texture with horizontal bar (row 8 = 255, other rows = 0)
        auto edgeTex = gl.CreateEmpty2D(16, 16, Leon::ERHIFormat::RGBA8_UNorm);

        std::vector<uint8_t> edgeData(16 * 16 * 4, 0);
        for (int y = 0; y < 16; ++y) {
            for (int x = 0; x < 16; ++x) {
                uint8_t val = (y == 8) ? 255 : 0;
                size_t idx = (y * 16 + x) * 4;
                edgeData[idx + 0] = val;
                edgeData[idx + 1] = val;
                edgeData[idx + 2] = val;
                edgeData[idx + 3] = val; // Luma in alpha
            }
        }
        gl.Upload(edgeTex, edgeData.data());

        gl.BindTexture(0, edgeTex);
        shader->SetInt("u_LDRTexture", 0);
        shader->SetFloat2("u_InverseScreenSize", 1.0f / 16.0f, 1.0f / 16.0f);

        // 1. FXAA Disabled -> Hard edge preserved exactly
        shader->SetInt("u_FXAAEnabled", 0);
        gl.DrawQuad();

        const std::vector<uint8_t> readbackOff = gl.ReadPixelsRGBA8();
        REQUIRE(readbackOff.size() == 16 * 16 * 4);

        CHECK(readbackOff[(8 * 16 + 8) * 4] == 255);
        CHECK(readbackOff[(7 * 16 + 8) * 4] == 0);

        // 2. FXAA Enabled -> Directional horizontal edge is detected and anti-aliased vertically
        shader->SetInt("u_FXAAEnabled", 1);
        gl.DrawQuad();

        const std::vector<uint8_t> readbackOn = gl.ReadPixelsRGBA8();
        REQUIRE(readbackOn.size() == 16 * 16 * 4);

        float centerVal = static_cast<float>(readbackOn[(8 * 16 + 8) * 4]) / 255.0f;
        float neighborVal = static_cast<float>(readbackOn[(7 * 16 + 8) * 4]) / 255.0f;

        CHECK(!std::isnan(centerVal));
        CHECK(!std::isnan(neighborVal));
        CHECK(centerVal < 0.95f);  // Center peak intensity softened
        CHECK(neighborVal > 0.0f); // Radiance distributed to orthogonal neighbor across the edge

        gl.DestroyTexture(edgeTex);
    }

    TEST_CASE("FXAA Subpixel Anti-Aliasing on Isolated High-Frequency Feature") {
        Leon::TestGPU::FHeadlessDevice gl;
        REQUIRE(gl.IsValid());

        auto shader = Leon::TestGPU::FTestShader::Create("Engine/Resources/Shaders/FXAA.glsl");
        REQUIRE(shader != nullptr);
        shader->Bind();

        gl.BindDefaultTextures();

        gl.BindFramebuffer(16, 16);

        // 16x16 Texture: Black field with an isolated single white pixel at (8, 8)
        auto pointTex = gl.CreateEmpty2D(16, 16, Leon::ERHIFormat::RGBA8_UNorm);

        std::vector<uint8_t> pointData(16 * 16 * 4, 0);
        size_t centerIdx = (8 * 16 + 8) * 4;
        pointData[centerIdx + 0] = 255;
        pointData[centerIdx + 1] = 255;
        pointData[centerIdx + 2] = 255;
        pointData[centerIdx + 3] = 255; // Luma in alpha

        gl.Upload(pointTex, pointData.data());

        gl.BindTexture(0, pointTex);
        shader->SetInt("u_LDRTexture", 0);
        shader->SetFloat2("u_InverseScreenSize", 1.0f / 16.0f, 1.0f / 16.0f);

        // When FXAA is ON: Subpixel blend must blend the isolated single-pixel hotspot
        shader->SetInt("u_FXAAEnabled", 1);
        gl.DrawQuad();

        const std::vector<uint8_t> readback = gl.ReadPixelsRGBA8();
        REQUIRE(readback.size() == 16 * 16 * 4);

        float centerVal = static_cast<float>(readback[centerIdx + 0]) / 255.0f;
        CHECK(!std::isnan(centerVal));
        CHECK(centerVal < 0.95f); // Peak intensity attenuated by subpixel filter
        CHECK(centerVal > 0.10f);

        gl.DestroyTexture(pointTex);
    }
}
