#include <doctest/doctest.h>
#include "GPU/HeadlessDevice.hpp"
#include "RenderCore/FColorSpace.hpp"
#include <vector>
#include <cmath>

TEST_SUITE("Shader GPU - Post-Processing Tone Mapping & ACES Pipeline") {

    TEST_CASE("ACES Hardware Curve Numerical Accuracy & Strict Monotonicity") {
        Leon::TestGPU::FHeadlessDevice gl;
        REQUIRE(gl.IsValid());

        auto shader = Leon::TestGPU::FTestShader::Create("Engine/Resources/Shaders/ToneMapping.glsl");
        REQUIRE(shader != nullptr);
        shader->Bind();

        gl.BindDefaultTextures();
        gl.BindFramebuffer(1, 1);

        auto hdrTex = gl.CreateEmpty2D(1, 1, Leon::ERHIFormat::RGBA32_Float);
        gl.BindTexture(0, hdrTex);
        gl.BindTexture(1, nullptr);
        shader->SetInt("u_HDRSceneTexture", 0);
        shader->SetInt("u_BloomTexture", 1);
        shader->SetInt("u_UseBloom", 0);
        shader->SetFloat("u_BloomIntensity", 0.0f);
        shader->SetInt("u_ToneMapper", 0); // 0 = ACES
        shader->SetFloat("u_Exposure", 1.0f);
        shader->SetFloat("u_Gamma", 2.2f);
        shader->SetInt("u_DebugMode", 0);

        // 1. Black FInput (0.0) -> Output strictly 0.0
        float val0[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        gl.Upload(hdrTex, val0);
        gl.DrawQuad();
        glm::vec4 pixel0 = gl.ReadPixel(0, 0);
        CHECK(pixel0.r == doctest::Approx(0.0f).epsilon(1e-5f));

        // 2. Middle Gray (0.18) -> Exact ACES value 0.266876 -> sRGB gamma = 0.54807
        float valMid[4] = {0.18f, 0.18f, 0.18f, 1.0f};
        gl.Upload(hdrTex, valMid);
        gl.DrawQuad();
        glm::vec4 pixelMid = gl.ReadPixel(0, 0);
        CHECK(pixelMid.r == doctest::Approx(Leon::LinearToSRGB(0.266876f)).epsilon(0.015f));
        // Luma in alpha must equal grayscale channel value
        CHECK(pixelMid.a == doctest::Approx(pixelMid.r).epsilon(0.01f));

        // 3. Unit White (1.0) -> Exact ACES value 0.803797 -> sRGB gamma = 0.90561
        float val1[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        gl.Upload(hdrTex, val1);
        gl.DrawQuad();
        glm::vec4 pixel1 = gl.ReadPixel(0, 0);
        CHECK(pixel1.r == doctest::Approx(Leon::LinearToSRGB(0.803797f)).epsilon(0.015f));

        // 4. Extreme HDR Brightness (1000.0) -> Bounded strictly <= 1.0
        float val1000[4] = {1000.0f, 1000.0f, 1000.0f, 1.0f};
        gl.Upload(hdrTex, val1000);
        gl.DrawQuad();
        glm::vec4 pixel1000 = gl.ReadPixel(0, 0);
        CHECK(!std::isnan(pixel1000.r));
        CHECK(!std::isinf(pixel1000.r));
        CHECK(pixel1000.r <= 1.0001f);
        CHECK(pixel1000.r >= 0.999f);

        // 5. Strict Monotonicity: 0.0 < 0.18 < 1.0 < 1000.0
        CHECK(pixel0.r < pixelMid.r);
        CHECK(pixelMid.r < pixel1.r);
        CHECK(pixel1.r < pixel1000.r);

        gl.DestroyTexture(hdrTex);
    }

    TEST_CASE("Tone Mapping Exposure Scaling & Multi-Operator Support") {
        Leon::TestGPU::FHeadlessDevice gl;
        REQUIRE(gl.IsValid());

        auto shader = Leon::TestGPU::FTestShader::Create("Engine/Resources/Shaders/ToneMapping.glsl");
        REQUIRE(shader != nullptr);
        shader->Bind();

        gl.BindDefaultTextures();
        gl.BindFramebuffer(1, 1);

        auto hdrTex = gl.CreateEmpty2D(1, 1, Leon::ERHIFormat::RGBA32_Float);
        gl.BindTexture(0, hdrTex);
        shader->SetInt("u_HDRSceneTexture", 0);
        shader->SetInt("u_UseBloom", 0);
        shader->SetFloat("u_Gamma", 2.2f);
        shader->SetInt("u_DebugMode", 0);

        float val[4] = {0.5f, 0.5f, 0.5f, 1.0f};
        gl.Upload(hdrTex, val);

        // 1. Exposure 1.0 vs Exposure 2.0 (Double exposure must increase output)
        shader->SetInt("u_ToneMapper", 0);
        shader->SetFloat("u_Exposure", 1.0f);
        gl.DrawQuad();
        glm::vec4 pixelExp1 = gl.ReadPixel(0, 0);

        shader->SetFloat("u_Exposure", 2.0f);
        gl.DrawQuad();
        glm::vec4 pixelExp2 = gl.ReadPixel(0, 0);

        CHECK(pixelExp1.r > 0.0f);
        CHECK(pixelExp2.r > pixelExp1.r);

        // 2. Reinhard Operator (ToneMapper = 1)
        shader->SetInt("u_ToneMapper", 1);
        shader->SetFloat("u_Exposure", 1.0f);
        gl.DrawQuad();
        glm::vec4 pixelReinhard = gl.ReadPixel(0, 0);

        CHECK(!std::isnan(pixelReinhard.r));
        CHECK(pixelReinhard.r > 0.0f);
        CHECK(pixelReinhard.r <= 1.0f);

        // 3. Neutral Clamped Operator (ToneMapper = 2) -> (0.5)^ (1/2.2) = 0.72974
        shader->SetInt("u_ToneMapper", 2);
        gl.DrawQuad();
        glm::vec4 pixelNeutral = gl.ReadPixel(0, 0);
        CHECK(pixelNeutral.r == doctest::Approx(0.72974f).epsilon(0.015f));

        gl.DestroyTexture(hdrTex);
    }

    TEST_CASE("Tone mapping applies a custom gamma and scales bloom by its intensity") {
        Leon::TestGPU::FHeadlessDevice gl;
        REQUIRE(gl.IsValid());

        auto shader = Leon::TestGPU::FTestShader::Create("Engine/Resources/Shaders/ToneMapping.glsl");
        REQUIRE(shader != nullptr);
        shader->Bind();
        gl.BindDefaultTextures();
        gl.BindFramebuffer(1, 1);

        auto scene = gl.Create1x1FloatTexture(0.5f, 0.5f, 0.5f, 1.0f);
        auto black = gl.Create1x1FloatTexture(0.0f, 0.0f, 0.0f, 1.0f);
        auto bloom = gl.Create1x1FloatTexture(1.0f, 1.0f, 1.0f, 1.0f);
        shader->SetInt("u_ToneMapper", 2); // clamp only: the output is the encoding of the input
        shader->SetFloat("u_Exposure", 1.0f);
        shader->SetInt("u_DebugMode", 0);

        // Gamma 1.8 is a plain power curve: 0.5 ^ (1 / 1.8).
        gl.BindTexture(0, scene);
        gl.BindTexture(1, black);
        shader->SetInt("u_UseBloom", 0);
        shader->SetFloat("u_Gamma", 1.8f);
        gl.DrawQuad();
        CHECK(gl.ReadPixel(0, 0).r == doctest::Approx(std::pow(0.5f, 1.0f / 1.8f)).epsilon(0.01f));

        // Bloom of 1 at intensity 0.25 over a black scene is 0.25 before encoding (sRGB: 0.5371).
        gl.BindTexture(0, black);
        gl.BindTexture(1, bloom);
        shader->SetInt("u_UseBloom", 1);
        shader->SetFloat("u_BloomIntensity", 0.25f);
        shader->SetFloat("u_Gamma", 2.2f);
        gl.DrawQuad();
        CHECK(gl.ReadPixel(0, 0).r == doctest::Approx(0.5371f).epsilon(0.01f));
        CHECK(gl.ValidationErrors() == 0);
    }
}
