#include <doctest/doctest.h>
#include "GPU/HeadlessDevice.hpp"
#include <filesystem>

TEST_SUITE("Shader GPU - PBR_Lit.hlsl Alpha Modes & Cutoff") {

    TEST_CASE("PBR_Lit.hlsl Hardware GPU Alpha Masking and Discard") {
        Leon::TestGPU::FHeadlessDevice gl;
        REQUIRE(gl.IsValid());

        const std::string shaderPath = "PBR_Lit";
        auto shader = Leon::TestGPU::FTestShader::Create(shaderPath);
        shader->Bind();

        gl.BindDefaultTextures();
        gl.BindFramebuffer(1, 1);

        Leon::TestGPU::FCameraBufferData camData;
        camData.ViewProjection = glm::mat4(1.0f);
        camData.CameraPosition = glm::vec4(0.0f, 0.0f, 1.0f, 0.0f);
        camData.CameraForward = glm::vec4(0.0f, 0.0f, -1.0f, 0.0f);
        gl.UpdateCameraUBO(camData);

        Leon::TestGPU::FLightingBufferData lightData;
        lightData.DirLight.Direction = glm::vec4(0.0f, 0.0f, -1.0f, 1.0f);
        lightData.DirLight.Color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
        lightData.LightCounts = glm::ivec4(0);
        gl.UpdateLightingUBO(lightData);

        Leon::TestGPU::SetMat4(shader, "u_Model", glm::mat4(1.0f));
        Leon::TestGPU::SetMat3(shader, "u_NormalMatrix", glm::mat3(1.0f));
        shader->SetInt("u_UseIBL", 0);
        shader->SetInt("u_UseShadows", 0);
        shader->SetInt("u_UseNormalMap", 0);
        shader->SetInt("u_UseMetallicMap", 0);
        shader->SetInt("u_UseRoughnessMap", 0);
        shader->SetInt("u_UseAOMap", 0);
        shader->SetInt("u_UseEmissiveMap", 0);
        shader->SetInt("u_UsePlanarReflection", 0);
        shader->SetFloat("u_NormalScale", 1.0f);
        shader->SetFloat("u_OcclusionStrength", 1.0f);
        Leon::TestGPU::SetFloat2(shader, "u_UVTiling", glm::vec2(1.0f));
        Leon::TestGPU::SetFloat2(shader, "u_UVOffset", glm::vec2(0.0f));

        Leon::TestGPU::SetFloat3(shader, "u_AlbedoColor", glm::vec3(1.0f));
        shader->SetFloat("u_Metallic", 0.0f);
        shader->SetFloat("u_Roughness", 0.5f);
        shader->SetFloat("u_AO", 1.0f);
        Leon::TestGPU::SetFloat3(shader, "u_EmissiveColor", glm::vec3(0.0f));
        shader->SetFloat("u_EmissiveIntensity", 0.0f);
        shader->SetInt("u_DebugMode", 14); // Mode 14: BaseColor

        // Create texture with alpha = 0.3 (76/255)
        auto lowAlphaTex = gl.Create1x1Texture(255, 255, 255, 76);
        gl.BindTexture(0, lowAlphaTex);
        shader->SetInt("u_UseAlbedoMap", 1);

        // 1. Opaque Mode (AlphaMode = 0): low alpha must NOT discard fragment
        shader->SetInt("u_AlphaMode", 0);
        shader->SetFloat("u_AlphaCutoff", 0.5f);
        gl.BindFramebuffer(1, 1); // Clear to (0,0,0,0)
        gl.DrawQuad();
        glm::vec4 opaquePixel = gl.ReadPixel();
        CHECK(opaquePixel.r == doctest::Approx(1.0f).epsilon(0.01f));

        // 2. Mask Mode (AlphaMode = 1) with Cutoff = 0.5: alpha 0.3 < 0.5 -> MUST discard
        shader->SetInt("u_AlphaMode", 1);
        shader->SetFloat("u_AlphaCutoff", 0.5f);
        gl.BindFramebuffer(1, 1); // Clear to (0,0,0,0)
        gl.DrawQuad();
        glm::vec4 discardedPixel = gl.ReadPixel();
        CHECK(std::abs(discardedPixel.r) < 0.001f);
        CHECK(std::abs(discardedPixel.a) < 0.001f);

        // 3. Mask Mode with Cutoff = 0.2: alpha 0.3 >= 0.2 -> MUST pass
        shader->SetFloat("u_AlphaCutoff", 0.2f);
        gl.BindFramebuffer(1, 1);
        gl.DrawQuad();
        glm::vec4 passedPixel = gl.ReadPixel();
        CHECK(passedPixel.r == doctest::Approx(1.0f).epsilon(0.01f));

        gl.DestroyTexture(lowAlphaTex);
    }

    TEST_CASE("PBR_Lit.hlsl: a material's opacity is alpha without a texture, and multiplies one") {
        Leon::TestGPU::FHeadlessDevice gl;
        REQUIRE(gl.IsValid());
        auto shader = Leon::TestGPU::FTestShader::Create("PBR_Lit");
        shader->Bind();
        gl.BindDefaultTextures();
        gl.BindFramebuffer(1, 1);

        Leon::TestGPU::FCameraBufferData camData;
        camData.ViewProjection = glm::mat4(1.0f);
        camData.CameraPosition = glm::vec4(0.0f, 0.0f, 1.0f, 0.0f);
        camData.CameraForward = glm::vec4(0.0f, 0.0f, -1.0f, 0.0f);
        gl.UpdateCameraUBO(camData);
        Leon::TestGPU::FLightingBufferData lightData;
        lightData.DirLight.Direction = glm::vec4(0.0f, 0.0f, -1.0f, 1.0f);
        lightData.DirLight.Color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
        lightData.LightCounts = glm::ivec4(0);
        gl.UpdateLightingUBO(lightData);

        Leon::TestGPU::SetMat4(shader, "u_Model", glm::mat4(1.0f));
        Leon::TestGPU::SetMat3(shader, "u_NormalMatrix", glm::mat3(1.0f));
        for (const char* off : {"u_UseIBL", "u_UseShadows", "u_UseNormalMap", "u_UseAlbedoMap", "u_UseMetallicMap",
                                "u_UseRoughnessMap", "u_UseAOMap", "u_UseEmissiveMap", "u_UsePlanarReflection"})
            shader->SetInt(off, 0);
        Leon::TestGPU::SetFloat2(shader, "u_UVTiling", glm::vec2(1.0f));
        Leon::TestGPU::SetFloat2(shader, "u_UVOffset", glm::vec2(0.0f));
        Leon::TestGPU::SetFloat3(shader, "u_AlbedoColor", glm::vec3(1.0f));
        shader->SetFloat("u_Roughness", 0.5f);
        shader->SetFloat("u_AO", 1.0f);
        shader->SetInt("u_DebugMode", 14); // base colour

        // The mask cutoff shows what alpha the shader computed: below it the pixel is not drawn.
        shader->SetInt("u_AlphaMode", 1);
        shader->SetFloat("u_AlphaCutoff", 0.5f);
        auto drawn = [&]() {
            gl.BindFramebuffer(1, 1);
            gl.DrawQuad();
            return gl.ReadPixel().r > 0.5f;
        };

        // No texture: the opacity alone is the alpha.
        shader->SetFloat("u_Opacity", 0.3f);
        CHECK_FALSE(drawn());
        shader->SetFloat("u_Opacity", 0.8f);
        CHECK(drawn());

        // With a texture the two multiply: 0.8 of the texture, then 0.8 x 0.5 = 0.4.
        auto texture = gl.Create1x1Texture(255, 255, 255, 204);
        gl.BindTexture(0, texture);
        shader->SetInt("u_UseAlbedoMap", 1);
        shader->SetFloat("u_Opacity", 1.0f);
        CHECK(drawn());
        shader->SetFloat("u_Opacity", 0.5f);
        CHECK_FALSE(drawn());

        gl.DestroyTexture(texture);
    }
}
