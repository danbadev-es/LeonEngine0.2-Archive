#include <doctest/doctest.h>
#include "GPU/HeadlessDevice.hpp"

#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <string>
#include <vector>

namespace {

    void UploadHemisphereKernel(const Leon::TRef<Leon::TestGPU::FTestShader>& InShader) {
        const glm::vec3 samples[16] = {
            {0.12f, 0.04f, 0.22f},   {-0.18f, 0.09f, 0.31f}, {0.05f, -0.21f, 0.28f}, {-0.08f, -0.11f, 0.42f},
            {0.27f, 0.15f, 0.36f},   {-0.31f, 0.02f, 0.48f}, {0.14f, 0.33f, 0.41f},  {-0.22f, -0.29f, 0.55f},
            {0.41f, -0.07f, 0.52f},  {-0.37f, 0.24f, 0.61f}, {0.09f, -0.38f, 0.58f}, {0.33f, 0.31f, 0.66f},
            {-0.44f, -0.18f, 0.71f}, {0.19f, 0.46f, 0.74f},  {-0.28f, 0.39f, 0.80f}, {0.07f, -0.47f, 0.88f},
        };
        for (int i = 0; i < 16; ++i) {
            glm::vec3 s = glm::normalize(samples[i]);
            InShader->SetFloat3("u_Samples[" + std::to_string(i) + "]", s.x, s.y, s.z);
        }
    }

    /**
     * The depth buffer (0 near, 1 far, first row at the top) of a camera looking at a square floor
     * on the plane y = 0, worked out per pixel instead of rasterised.
     */
    std::vector<float> FloorDepth(const glm::mat4& InView, const glm::mat4& InProjection, float InHalfSize,
                                  uint32_t InWidth, uint32_t InHeight) {
        const glm::mat4 viewProjection = InProjection * InView;
        const glm::mat4 toWorld = glm::inverse(viewProjection);
        const glm::mat4 toDepth = Leon::ClipDepthZeroToOne() * viewProjection;
        std::vector<float> depth(static_cast<size_t>(InWidth) * InHeight, 1.0f);
        for (uint32_t y = 0; y < InHeight; ++y) {
            for (uint32_t x = 0; x < InWidth; ++x) {
                const float ndcX = 2.0f * (static_cast<float>(x) + 0.5f) / static_cast<float>(InWidth) - 1.0f;
                const float ndcY = 1.0f - 2.0f * (static_cast<float>(y) + 0.5f) / static_cast<float>(InHeight);
                glm::vec4 nearPoint = toWorld * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
                glm::vec4 farPoint = toWorld * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
                nearPoint /= nearPoint.w;
                farPoint /= farPoint.w;
                const glm::vec3 direction = glm::vec3(farPoint) - glm::vec3(nearPoint);
                if (direction.y >= 0.0f)
                    continue; // looking at the sky
                const float t = -nearPoint.y / direction.y;
                if (t < 0.0f || t > 1.0f)
                    continue;
                const glm::vec3 hit = glm::vec3(nearPoint) + direction * t;
                if (std::abs(hit.x) > InHalfSize || std::abs(hit.z) > InHalfSize)
                    continue;
                const glm::vec4 clip = toDepth * glm::vec4(hit, 1.0f);
                depth[static_cast<size_t>(y) * InWidth + x] = clip.z / clip.w;
            }
        }
        return depth;
    }

} // namespace

TEST_SUITE("Shader GPU - SSAO") {

    TEST_CASE("SSAO leaves a perspective floor unoccluded") {
        Leon::TestGPU::FHeadlessDevice gl;
        REQUIRE(gl.IsValid());

        const uint32_t width = 64;
        const uint32_t height = 64;

        glm::mat4 view =
            glm::lookAt(glm::vec3(0.0f, 4.0f, 8.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 proj = glm::perspective(glm::radians(70.0f), 1.0f, 0.1f, 100.0f);

        // An 80 m floor seen at a grazing angle: the case where comparing depths alone self-occludes.
        const std::vector<float> depth = FloorDepth(view, proj, 40.0f, width, height);
        auto depthTex = gl.Create2D(width, height, Leon::ERHIFormat::R32_Float, depth.data());
        REQUIRE(depthTex != nullptr);

        auto ssao = Leon::TestGPU::FTestShader::Create("SSAO");
        REQUIRE(ssao != nullptr);

        gl.BindFramebuffer(static_cast<int>(width), static_cast<int>(height));
        ssao->Bind();
        gl.BindTexture(0, depthTex);
        ssao->SetInt("u_DepthTexture", 0);
        ssao->SetMat4("u_Projection", glm::value_ptr(proj));
        ssao->SetFloat("u_Radius", 0.45f);
        ssao->SetFloat("u_Bias", 0.025f);
        ssao->SetInt("u_KernelSize", 16);
        UploadHemisphereKernel(ssao);
        gl.DrawQuad();

        const std::vector<float> pixels = gl.ReadPixels();
        REQUIRE(pixels.size() == static_cast<size_t>(width) * height * 4);
        CHECK(gl.ValidationErrors() == 0);

        float minAO = 1.0f;
        float sumAO = 0.0f;
        int count = 0;
        for (uint32_t y = height / 4; y < (3 * height) / 4; ++y) {
            for (uint32_t x = width / 4; x < (3 * width) / 4; ++x) {
                float ao = pixels[(y * width + x) * 4];
                minAO = std::min(minAO, ao);
                sumAO += ao;
                ++count;
            }
        }
        REQUIRE(count > 0);
        CHECK(sumAO / static_cast<float>(count) > 0.92f);
        CHECK(minAO > 0.80f);
    }

    TEST_CASE("SSAO composite keeps bright specular when AO is dark") {
        Leon::TestGPU::FHeadlessDevice gl;
        REQUIRE(gl.IsValid());

        auto shader = Leon::TestGPU::FTestShader::Create("SSAOComposite");
        REQUIRE(shader != nullptr);
        shader->Bind();

        gl.BindFramebuffer(1, 1);

        auto hdrTex = gl.Create1x1FloatTexture(3.0f, 3.0f, 3.0f, 1.0f);
        auto aoTex = gl.Create1x1Texture(25, 25, 25, 255);
        gl.BindTexture(0, hdrTex);
        gl.BindTexture(1, aoTex);
        shader->SetInt("u_HDRSceneTexture", 0);
        shader->SetInt("u_SSAOTexture", 1);
        shader->SetFloat("u_Intensity", 1.0f);
        shader->SetInt("u_DebugAO", 0);
        gl.DrawQuad();
        glm::vec4 pixel = gl.ReadPixel(0, 0);
        CHECK(pixel.r > 2.4f);
        CHECK(pixel.g > 2.4f);
        CHECK(pixel.b > 2.4f);

        gl.DestroyTexture(hdrTex);
        gl.DestroyTexture(aoTex);
    }
}
