#include <doctest/doctest.h>
#include "GPU/HeadlessDevice.hpp"
#include <glm/gtc/matrix_transform.hpp>

using namespace Leon;
using namespace Leon::TestGPU;

TEST_SUITE("Shader GPU - Shadow Filtering Modes (Hard, PCF 3x3, PCF 5x5, Poisson Disk)") {

    TEST_CASE("PBR_Lit.hlsl Hardware GPU Multi-Filter Shadow Evaluation") {
        Leon::TestGPU::FHeadlessDevice gl;
        REQUIRE(gl.IsValid());

        auto shader = Leon::TestGPU::FTestShader::Create("PBR_Lit");
        REQUIRE(shader != nullptr);
        shader->Bind();

        gl.ResetShaderUniforms(shader);
        gl.BindDefaultTextures();
        gl.BindFramebuffer(1, 1);

        SetMat4(shader, "u_Model", glm::mat4(1.0f));
        SetMat3(shader, "u_NormalMatrix", glm::mat3(1.0f));

        Leon::TestGPU::FCameraBufferData camData;
        camData.ViewProjection = glm::mat4(1.0f);
        camData.CameraPosition = glm::vec4(0.0f, 0.0f, 2.0f, 1.0f); // Cascade 0
        camData.CameraForward = glm::vec4(0.0f, 0.0f, -1.0f, 0.0f);
        camData.CascadeSplits = glm::vec4(5.0f, 15.0f, 35.0f, 100.0f);
        camData.ShadowParams = glm::vec4(0.001f, 0.002f, 0.0f, 0.0f); // Zero normal bias

        for (int c = 0; c < 4; ++c) {
            camData.LightSpaceMatrices[c] = glm::mat4(1.0f);
        }

        Leon::TestGPU::FLightingBufferData lightData;
        lightData.DirLight.Direction = glm::vec4(0.0f, 0.0f, -1.0f, 1.0f);
        lightData.DirLight.Color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
        gl.UpdateLightingUBO(lightData);

        shader->SetInt("u_UseShadows", 1);
        shader->SetInt("u_DebugMode", 24); // Mode 24: Direct Shadow Factor (1.0 = Lit, 0.0 = Occluded)

        SUBCASE("Hard Shadow Mode (1 Tap - Lit Invariant)") {
            camData.ShadowSettings = glm::ivec4(0, 16, 0, 24); // filterMode = 0 (Hard)
            gl.UpdateCameraUBO(camData);

            gl.DrawQuad();
            glm::vec4 pix = gl.ReadPixel(0, 0);

            // Fully lit: 1.0 - shadow = 1.0
            CHECK(pix.r == doctest::Approx(1.0f).epsilon(0.02f));
        }

        SUBCASE("PCF 3x3 Mode (9 Taps - Filter Normalization)") {
            camData.ShadowSettings = glm::ivec4(1, 16, 0, 24); // filterMode = 1 (PCF3x3)
            gl.UpdateCameraUBO(camData);

            gl.DrawQuad();
            glm::vec4 pix = gl.ReadPixel(0, 0);

            // 9 taps of 1.0 / 9.0 = 1.0 -> Fully Lit (1.0)
            CHECK(pix.r == doctest::Approx(1.0f).epsilon(0.02f));
        }

        SUBCASE("PCF 5x5 Mode (25 Taps - Filter Normalization)") {
            camData.ShadowSettings = glm::ivec4(2, 16, 0, 24); // filterMode = 2 (PCF5x5)
            gl.UpdateCameraUBO(camData);

            gl.DrawQuad();
            glm::vec4 pix = gl.ReadPixel(0, 0);

            // 25 taps of 1.0 / 25.0 = 1.0 -> Fully Lit (1.0)
            CHECK(pix.r == doctest::Approx(1.0f).epsilon(0.02f));
        }

        SUBCASE("Poisson Disk Mode (16 Taps - Vogel Distribution Normalization)") {
            camData.ShadowSettings = glm::ivec4(3, 16, 0, 24); // filterMode = 3 (Poisson)
            gl.UpdateCameraUBO(camData);

            gl.DrawQuad();
            glm::vec4 pix = gl.ReadPixel(0, 0);

            // 16 taps of 1.0 / 16.0 = 1.0 -> Fully Lit (1.0)
            CHECK(pix.r == doctest::Approx(1.0f).epsilon(0.02f));
        }

        SUBCASE("PCF 5x5 near UV 0 skips OOB taps (occluded, not border-lit)") {
            float zeroDepth = 0.0f;
            gl.FillDepth(gl.GetDefaultShadowArrayTex(), zeroDepth, 0);

            glm::mat4 lightMat(1.0f);
            lightMat[3][0] = -0.90f;
            for (int c = 0; c < 4; ++c)
                camData.LightSpaceMatrices[c] = lightMat;
            camData.ShadowSettings = glm::ivec4(2, 16, 0, 24);
            camData.ShadowParams = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
            gl.UpdateCameraUBO(camData);

            gl.DrawQuad();
            glm::vec4 pix = gl.ReadPixel(0, 0);

            float oneDepth = 1.0f;
            gl.FillDepth(gl.GetDefaultShadowArrayTex(), oneDepth, 0);
            for (int c = 0; c < 4; ++c)
                camData.LightSpaceMatrices[c] = glm::mat4(1.0f);

            CHECK(pix.r == doctest::Approx(0.0f).epsilon(0.08f));
        }

        SUBCASE("Poisson Disk vs Hard Filter Penumbra Edge Response") {
            // Position quad right at shadow boundary in light space
            // deltaZ maps center to 1.000 (edge of shadow depth)
            glm::mat4 lightMat = glm::mat4(1.0f);
            lightMat[3][2] = 1.000f;
            for (int c = 0; c < 4; ++c)
                camData.LightSpaceMatrices[c] = lightMat;

            // In Poisson mode, sampling points with radius > 0 sample outside center
            camData.ShadowSettings = glm::ivec4(3, 16, 0, 24); // Poisson (mode 3)
            camData.ShadowParams = glm::vec4(0.0005f, 0.0f, 0.0f, 0.0f);
            gl.UpdateCameraUBO(camData);

            gl.DrawQuad();
            glm::vec4 pixPoisson = gl.ReadPixel(0, 0);
            CHECK(pixPoisson.r == doctest::Approx(1.0f).epsilon(0.02f));
        }
    }

    TEST_CASE("Each shadow filter has its own width at the edge of a shadow") {
        Leon::TestGPU::FHeadlessDevice gl;
        REQUIRE(gl.IsValid());

        auto shader = Leon::TestGPU::FTestShader::Create("PBR_Lit");
        REQUIRE(shader != nullptr);
        shader->Bind();
        gl.ResetShaderUniforms(shader);
        gl.BindDefaultTextures();
        gl.BindFramebuffer(1, 1);
        SetMat4(shader, "u_Model", glm::mat4(1.0f));
        SetMat3(shader, "u_NormalMatrix", glm::mat3(1.0f));

        // A 16x16 map: an occluder over the left half (columns 0..7), nothing over the right half.
        const uint32_t size = 16;
        auto map = gl.CreateDepth(Leon::ERHITextureDimension::Texture2DArray, size, size, 4);
        REQUIRE(map != nullptr);
        std::vector<float> texels(size * size, 1.0f);
        for (uint32_t y = 0; y < size; ++y) {
            for (uint32_t x = 0; x < size / 2; ++x)
                texels[y * size + x] = 0.0f;
        }
        gl.Upload(map, texels.data(), 0, 0);
        gl.BindTexture(Leon::kSlotCascadeShadow, map);

        Leon::TestGPU::FCameraBufferData camData;
        camData.ViewProjection = glm::mat4(1.0f);
        camData.CameraPosition = glm::vec4(0.0f, 0.0f, 2.0f, 1.0f);
        camData.CameraForward = glm::vec4(0.0f, 0.0f, -1.0f, 0.0f);
        camData.CascadeSplits = glm::vec4(5.0f, 15.0f, 35.0f, 100.0f);
        camData.ShadowParams = glm::vec4(0.0f);
        // The pixel falls on the centre of column 9: one and a half texels into the lit half.
        glm::mat4 lightMat(1.0f);
        lightMat[3][0] = 2.0f * (9.5f / static_cast<float>(size)) - 1.0f;
        for (int c = 0; c < 4; ++c)
            camData.LightSpaceMatrices[c] = lightMat;

        Leon::TestGPU::FLightingBufferData lightData;
        lightData.DirLight.Direction = glm::vec4(0.0f, 0.0f, -1.0f, 1.0f);
        lightData.DirLight.Color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
        gl.UpdateLightingUBO(lightData);
        shader->SetInt("u_UseShadows", 1);
        shader->SetInt("u_DebugMode", 24); // 1 = lit, 0 = in shadow

        const auto litWith = [&](int InFilter) {
            camData.ShadowSettings = glm::ivec4(InFilter, -1, 0, 0);
            gl.UpdateCameraUBO(camData);
            gl.DrawQuad();
            return gl.ReadPixel(0, 0).r;
        };

        // One tap, and a 3x3 kernel (columns 8..10), stay in the lit half.
        CHECK(litWith(0) == doctest::Approx(1.0f).epsilon(0.01f));
        CHECK(litWith(1) == doctest::Approx(1.0f).epsilon(0.01f));
        // The 5x5 kernel reaches column 7: one column of five is in shadow.
        CHECK(litWith(2) == doctest::Approx(0.8f).epsilon(0.02f));
        // The Poisson disk (1.75 texels) reaches it too, with fewer of its taps.
        const float poisson = litWith(3);
        CHECK(poisson < 0.99f);
        CHECK(poisson > 0.6f);
        CHECK(gl.ValidationErrors() == 0);
    }
}
