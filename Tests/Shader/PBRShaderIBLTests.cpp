#include <doctest/doctest.h>
#include "GPU/HeadlessDevice.hpp"
#include <filesystem>
#include <vector>

TEST_SUITE("Shader GPU - PBR_Lit.glsl IBL Integration") {

    TEST_CASE("PBR_Lit.glsl Real Hardware GPU Image-Based Lighting Pipeline") {
        Leon::TestGPU::FHeadlessDevice gl;
        REQUIRE(gl.IsValid());

        auto shader = Leon::TestGPU::FTestShader::Create("Engine/Resources/Shaders/PBR_Lit.glsl");
        REQUIRE(shader != nullptr);
        shader->Bind();

        gl.BindDefaultTextures();
        gl.BindFramebuffer(1, 1);

        Leon::TestGPU::FCameraBufferData camData;
        camData.ViewProjection = glm::mat4(1.0f);
        camData.CameraPosition = glm::vec4(0.0f, 0.0f, 1.0f, 0.0f);
        camData.CameraForward = glm::vec4(0.0f, 0.0f, -1.0f, 0.0f);
        gl.UpdateCameraUBO(camData);

        // Disable all direct lights so output is 100% pure indirect IBL ambient
        Leon::TestGPU::FLightingBufferData lightData;
        lightData.DirLight.Direction.w = 0.0f;
        lightData.LightCounts.x = 0;
        lightData.LightCounts.y = 0;
        lightData.EnvSkyColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f); // envIntensity = 1.0
        gl.UpdateLightingUBO(lightData);

        Leon::TestGPU::SetMat4(shader, "u_Model", glm::mat4(1.0f));
        Leon::TestGPU::SetMat3(shader, "u_NormalMatrix", glm::mat3(1.0f));
        shader->SetInt("u_UseIBL", 1); // Enable Real IBL
        shader->SetInt("u_UseShadows", 0);
        shader->SetInt("u_UseNormalMap", 0);
        shader->SetInt("u_UseAlbedoMap", 0);
        shader->SetInt("u_UseMetallicMap", 0);
        shader->SetInt("u_UseRoughnessMap", 0);
        shader->SetInt("u_UseAOMap", 0);
        shader->SetInt("u_UseEmissiveMap", 0);
        shader->SetInt("u_UsePlanarReflection", 0);
        shader->SetInt("u_DebugMode", 0);
        shader->SetFloat("u_AO", 1.0f);
        Leon::TestGPU::SetFloat3(shader, "u_EmissiveColor", glm::vec3(0.0f));
        Leon::TestGPU::SetFloat3(shader, "u_AlbedoColor", glm::vec3(1.0f));
        shader->SetFloat("u_Metallic", 0.0f);
        shader->SetFloat("u_Roughness", 0.5f);

        // 1. Create and Bind Constant White Irradiance Cubemap (L = PI = 3.14159)
        auto irradTex = gl.CreateFloatCube(3.14159265f, 3.14159265f, 3.14159265f, 3.14159265f);

        // 2. Constant prefilter cubemap (L = 1.0). One level is enough: every level held the same value.
        auto prefTex = gl.CreateFloatCube(1.0f, 1.0f, 1.0f, 1.0f);

        // 3. BRDF LUT (A = 0.8, B = 0.1)
        auto lutTex = gl.Create1x1FloatTexture(0.8f, 0.1f, 0.0f, 1.0f);

        // Bind to respective texture units
        gl.BindTexture(6, lutTex);

        gl.BindTexture(7, irradTex);

        gl.BindTexture(8, prefTex);

        shader->SetInt("u_DebugMode", 35);
        gl.DrawQuad();
        glm::vec4 pixelDiffuseIBL = gl.ReadPixel(0, 0);
        CHECK(pixelDiffuseIBL.r == doctest::Approx(1.0f).epsilon(0.02f));
        CHECK(pixelDiffuseIBL.g == doctest::Approx(1.0f).epsilon(0.02f));
        CHECK(pixelDiffuseIBL.b == doctest::Approx(1.0f).epsilon(0.02f));

        shader->SetInt("u_DebugMode", 0);
        gl.DrawQuad();
        glm::vec4 pixelIBL = gl.ReadPixel(0, 0);

        CHECK(!std::isnan(pixelIBL.r));
        CHECK(pixelIBL.r > 0.5f);

        // 4. Pure Metallic IBL Test (kD == 0, diffuseIBL == 0 -> validates BRDF LUT)
        // specularIBL = prefilteredColor * (F_IBL * lut.x + lut.y) = 1.0 * (1.0 * 0.8 + 0.1) = 0.90
        shader->SetFloat("u_Metallic", 1.0f);
        gl.DrawQuad();
        glm::vec4 pixelMetalIBL = gl.ReadPixel(0, 0);

        CHECK(pixelMetalIBL.r == doctest::Approx(0.90f).epsilon(0.02f));
        CHECK(pixelMetalIBL.g == doctest::Approx(0.90f).epsilon(0.02f));
        CHECK(pixelMetalIBL.b == doctest::Approx(0.90f).epsilon(0.02f));

        // 5. Black HDR Test (Zero Irradiance -> Zero IBL)
        irradTex = gl.CreateFloatCube(0.0f, 0.0f, 0.0f, 0.0f);
        prefTex = gl.CreateFloatCube(0.0f, 0.0f, 0.0f, 0.0f);
        gl.BindTexture(7, irradTex);
        gl.BindTexture(8, prefTex);

        gl.DrawQuad();
        glm::vec4 pixelBlackIBL = gl.ReadPixel(0, 0);

        CHECK(pixelBlackIBL.r == doctest::Approx(0.0f).epsilon(1e-4f));
        CHECK(pixelBlackIBL.g == doctest::Approx(0.0f).epsilon(1e-4f));
        CHECK(pixelBlackIBL.b == doctest::Approx(0.0f).epsilon(1e-4f));

        // Cleanup
        gl.DestroyTexture(irradTex);
        gl.DestroyTexture(prefTex);
        gl.DestroyTexture(lutTex);
    }
}
