#include <doctest/doctest.h>
#include "GPU/HeadlessDevice.hpp"

#include <algorithm>
#include <utility>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <vector>

TEST_SUITE("Shader GPU - Skybox.glsl equirect wrap") {

    TEST_CASE("HDR skybox looking toward -X has no 1x1-mip vertical seam") {
        Leon::TestGPU::FHeadlessDevice gl;
        REQUIRE(gl.IsValid());

        const uint32_t width = 64;
        const uint32_t height = 64;
        auto shader = Leon::TestGPU::FTestShader::Create("Engine/Resources/Shaders/Skybox.glsl");
        REQUIRE(shader != nullptr);

        const int hdrW = 256;
        const int hdrH = 128;
        std::vector<float> hdr(static_cast<size_t>(hdrW) * hdrH * 4, 0.0f);
        for (int y = 0; y < hdrH; ++y) {
            for (int x = 0; x < hdrW; ++x) {
                const size_t i = (static_cast<size_t>(y) * hdrW + x) * 4;
                const bool bSun = x >= 112 && x < 144;
                hdr[i + 0] = bSun ? 48.0f : 0.05f;
                hdr[i + 1] = bSun ? 40.0f : 0.15f;
                hdr[i + 2] = bSun ? 24.0f : 0.80f;
                hdr[i + 3] = 1.0f;
            }
        }

        // The whole mip chain, box filtered: the last level is the average of the panorama, sun
        // included, which is what a seam would show.
        const uint32_t levels = 9;
        auto hdrTex = gl.CreateEmpty2D(hdrW, hdrH, Leon::ERHIFormat::RGBA32_Float, levels);
        REQUIRE(hdrTex != nullptr);
        gl.Upload(hdrTex, hdr.data());
        std::vector<float> previous = hdr;
        int previousW = hdrW;
        int previousH = hdrH;
        for (uint32_t level = 1; level < levels; ++level) {
            const int levelW = std::max(previousW / 2, 1);
            const int levelH = std::max(previousH / 2, 1);
            std::vector<float> mip(static_cast<size_t>(levelW) * levelH * 4, 0.0f);
            for (int y = 0; y < levelH; ++y) {
                for (int x = 0; x < levelW; ++x) {
                    for (int tap = 0; tap < 4; ++tap) {
                        const int sourceX = std::min(x * 2 + (tap & 1), previousW - 1);
                        const int sourceY = std::min(y * 2 + (tap >> 1), previousH - 1);
                        for (int channel = 0; channel < 4; ++channel)
                            mip[(static_cast<size_t>(y) * levelW + x) * 4 + channel] +=
                                0.25f * previous[(static_cast<size_t>(sourceY) * previousW + sourceX) * 4 + channel];
                    }
                }
            }
            gl.Upload(hdrTex, mip.data(), level);
            previous = std::move(mip);
            previousW = levelW;
            previousH = levelH;
        }

        // Toward -X, where the panorama wraps, turned by about one pixel: looking straight at it the
        // wrap falls between two 2x2 pixel blocks, where screen derivatives never see it.
        glm::mat4 view = glm::lookAt(glm::vec3(0.0f), glm::vec3(-1.0f, 0.0f, 0.019f), glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 proj = glm::perspective(glm::radians(70.0f), 1.0f, 0.1f, 1000.0f);

        gl.BindFramebuffer(static_cast<int>(width), static_cast<int>(height));

        shader->Bind();
        shader->SetMat4("u_View", glm::value_ptr(view));
        shader->SetMat4("u_Projection", glm::value_ptr(proj));
        shader->SetFloat("u_EnvironmentIntensity", 1.0f);
        shader->SetInt("u_UseHDREnvironmentMap", 1);
        shader->SetInt("u_HDREnvironmentMap", 0);
        gl.BindTexture(0, hdrTex);

        gl.DrawQuad();

        const std::vector<float> pixels = gl.ReadPixels();
        REQUIRE(pixels.size() == static_cast<size_t>(width) * height * 4);
        CHECK(gl.ValidationErrors() == 0);

        std::vector<float> colLum(width, 0.0f);
        for (uint32_t x = 0; x < width; ++x) {
            float sum = 0.0f;
            for (uint32_t y = 0; y < height; ++y) {
                const size_t i = (static_cast<size_t>(y) * width + x) * 4;
                sum += 0.2126f * pixels[i] + 0.7152f * pixels[i + 1] + 0.0722f * pixels[i + 2];
            }
            colLum[x] = sum / static_cast<float>(height);
        }

        std::vector<float> sorted = colLum;
        std::sort(sorted.begin(), sorted.end());
        const float median = sorted[sorted.size() / 2];
        const float maxLum = *std::max_element(colLum.begin(), colLum.end());
        REQUIRE(median > 0.02f);
        CHECK(maxLum < median * 2.5f);

        gl.DestroyTexture(hdrTex);
    }
}
