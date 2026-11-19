#include <doctest/doctest.h>
#include "GPU/HeadlessDevice.hpp"
#include <filesystem>

TEST_SUITE("Shader GPU - PBR_Lit.hlsl Shadow Map Hardware PCF") {

    TEST_CASE("PBR_Lit.hlsl Directional Shadow Occlusion (Unshadowed vs Fully Shadowed)") {
        Leon::TestGPU::FHeadlessDevice gl;
        REQUIRE(gl.IsValid());

        auto shader = Leon::TestGPU::FTestShader::Create("PBR_Lit");
        REQUIRE(shader != nullptr);
        shader->Bind();

        gl.BindDefaultTextures();
        gl.BindFramebuffer(1, 1);

        Leon::TestGPU::FCameraBufferData camData;
        camData.ViewProjection = glm::mat4(1.0f);
        camData.CameraPosition = glm::vec4(0.0f, 0.0f, 1.0f, 0.0f);
        camData.CameraForward = glm::vec4(0.0f, 0.0f, -1.0f, 0.0f);
        gl.UpdateCameraUBO(camData);

        Leon::TestGPU::FLightingBufferData lightData;
        lightData.DirLight.Direction = glm::vec4(0.0f, 0.0f, -1.0f, 1.0f); // L = +Z
        lightData.DirLight.Color = glm::vec4(1.0f, 1.0f, 1.0f, 2.0f);      // Intensity = 2.0
        lightData.LightCounts.x = 0;
        lightData.LightCounts.y = 0;
        gl.UpdateLightingUBO(lightData);

        Leon::TestGPU::SetMat4(shader, "u_Model", glm::mat4(1.0f));
        Leon::TestGPU::SetMat3(shader, "u_NormalMatrix", glm::mat3(1.0f));
        shader->SetInt("u_UseIBL", 0);
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

        // 1. Shadows Disabled (u_UseShadows = 0)
        shader->SetInt("u_UseShadows", 0);
        gl.DrawQuad();
        glm::vec4 pixelUnshadowed = gl.ReadPixel(0, 0);

        CHECK(!std::isnan(pixelUnshadowed.r));
        CHECK(pixelUnshadowed.r > 0.1f);
    }

    TEST_CASE("PBR_Lit.hlsl Spot Light Shadow Evaluation") {
        Leon::TestGPU::FHeadlessDevice gl;
        REQUIRE(gl.IsValid());

        auto shader = Leon::TestGPU::FTestShader::Create("PBR_Lit");
        REQUIRE(shader != nullptr);
        shader->Bind();

        gl.BindDefaultTextures();
        gl.BindFramebuffer(1, 1);

        Leon::TestGPU::FCameraBufferData camData;
        camData.ViewProjection = glm::mat4(1.0f);
        camData.SpotLightSpaceMatrix = glm::mat4(1.0f);
        camData.CameraPosition = glm::vec4(0.0f, 0.0f, 1.0f, 0.0f);
        camData.CameraForward = glm::vec4(0.0f, 0.0f, -1.0f, 0.0f);
        camData.ShadowParams = glm::vec4(0.001f, 0.002f, 0.02f, 0.0f);
        camData.ShadowSettings = glm::ivec4(1, 16, 0, 0);
        gl.UpdateCameraUBO(camData);

        Leon::TestGPU::FLightingBufferData lightData;
        lightData.DirLight.Direction = glm::vec4(0.0f, 0.0f, -1.0f, 0.0f); // Dir disabled
        lightData.LightCounts.x = 0;
        lightData.LightCounts.y = 1;

        // Spot light 0 active facing -Z
        lightData.SpotLights[0].Position = glm::vec4(0.0f, 0.0f, 2.0f, 1.0f);
        lightData.SpotLights[0].Direction = glm::vec4(0.0f, 0.0f, -1.0f, glm::cos(glm::radians(20.0f)));
        lightData.SpotLights[0].Color = glm::vec4(1.0f, 1.0f, 1.0f, glm::cos(glm::radians(45.0f)));
        lightData.SpotLights[0].Params = glm::vec4(10.0f, 5.0f, 0.0f, 0.0f);
        gl.UpdateLightingUBO(lightData);

        Leon::TestGPU::SetMat4(shader, "u_Model", glm::mat4(1.0f));
        Leon::TestGPU::SetMat3(shader, "u_NormalMatrix", glm::mat3(1.0f));
        shader->SetInt("u_UseIBL", 0);
        shader->SetInt("u_UseNormalMap", 0);
        shader->SetInt("u_UseAlbedoMap", 0);
        shader->SetInt("u_UseMetallicMap", 0);
        shader->SetInt("u_UseRoughnessMap", 0);
        shader->SetInt("u_UseAOMap", 0);
        shader->SetInt("u_UseEmissiveMap", 0);
        shader->SetInt("u_UsePlanarReflection", 0);
        shader->SetInt("u_DebugMode", 10); // Direct Radiance Lo only
        shader->SetFloat("u_AO", 1.0f);
        Leon::TestGPU::SetFloat3(shader, "u_EmissiveColor", glm::vec3(0.0f));
        Leon::TestGPU::SetFloat3(shader, "u_AlbedoColor", glm::vec3(1.0f));
        shader->SetFloat("u_Metallic", 0.0f);
        shader->SetFloat("u_Roughness", 0.5f);

        shader->SetInt("u_UseSpotShadows", 1);
        gl.DrawQuad();
        glm::vec4 spotLit = gl.ReadPixel(0, 0);

        CHECK(!std::isnan(spotLit.r));
        CHECK(spotLit.r > 0.05f);
    }

    TEST_CASE("The spot light's shadow map decides whether its light arrives") {
        Leon::TestGPU::FHeadlessDevice gl;
        REQUIRE(gl.IsValid());

        auto shader = Leon::TestGPU::FTestShader::Create("PBR_Lit");
        REQUIRE(shader != nullptr);
        shader->Bind();
        gl.ResetShaderUniforms(shader);
        gl.BindDefaultTextures();
        gl.BindFramebuffer(1, 1);
        Leon::TestGPU::SetMat4(shader, "u_Model", glm::mat4(1.0f));
        Leon::TestGPU::SetMat3(shader, "u_NormalMatrix", glm::mat3(1.0f));

        Leon::TestGPU::FCameraBufferData camData;
        camData.ViewProjection = glm::mat4(1.0f);
        camData.SpotLightSpaceMatrix = glm::mat4(1.0f);
        camData.CameraPosition = glm::vec4(0.0f, 0.0f, 1.0f, 0.0f);
        camData.CameraForward = glm::vec4(0.0f, 0.0f, -1.0f, 0.0f);
        camData.ShadowParams = glm::vec4(0.001f, 0.002f, 0.0f, 0.0f);
        camData.ShadowSettings = glm::ivec4(0, 0, 0, 0); // hard filter; spot light 0 owns the shadow map
        gl.UpdateCameraUBO(camData);

        Leon::TestGPU::FLightingBufferData lightData;
        lightData.DirLight.Direction = glm::vec4(0.0f, 0.0f, -1.0f, 0.0f);
        lightData.LightCounts.x = 0;
        lightData.LightCounts.y = 1;
        lightData.SpotLights[0].Position = glm::vec4(0.0f, 0.0f, 2.0f, 1.0f);
        lightData.SpotLights[0].Direction = glm::vec4(0.0f, 0.0f, -1.0f, glm::cos(glm::radians(20.0f)));
        lightData.SpotLights[0].Color = glm::vec4(1.0f, 1.0f, 1.0f, glm::cos(glm::radians(45.0f)));
        lightData.SpotLights[0].Params = glm::vec4(10.0f, 5.0f, 0.0f, 0.0f);
        gl.UpdateLightingUBO(lightData);
        shader->SetInt("u_UseSpotShadows", 1);

        // Nothing between the light and the surface.
        shader->SetInt("u_DebugMode", 26); // spot shadow factor: 1 = lit
        gl.DrawQuad();
        CHECK(gl.ReadPixel(0, 0).r == doctest::Approx(1.0f).epsilon(0.01f));
        shader->SetInt("u_DebugMode", 10); // direct light
        gl.DrawQuad();
        const float lit = gl.ReadPixel(0, 0).r;
        CHECK(lit > 0.05f);

        // An occluder right at the light.
        gl.FillDepth(gl.GetDefaultShadowTex(), 0.0f);
        shader->SetInt("u_DebugMode", 26);
        gl.DrawQuad();
        CHECK(gl.ReadPixel(0, 0).r == doctest::Approx(0.0f).epsilon(0.01f));
        shader->SetInt("u_DebugMode", 10);
        gl.DrawQuad();
        CHECK(std::abs(gl.ReadPixel(0, 0).r) < 0.001f);

        // A light that does not own the map is not shadowed by it.
        camData.ShadowSettings = glm::ivec4(0, -1, 0, 0);
        gl.UpdateCameraUBO(camData);
        gl.DrawQuad();
        CHECK(gl.ReadPixel(0, 0).r == doctest::Approx(lit).epsilon(0.01f));
        CHECK(gl.ValidationErrors() == 0);
    }

    TEST_CASE("Each shadowed spot light reads its own layer of the shadow map") {
        Leon::TestGPU::FHeadlessDevice gl;
        REQUIRE(gl.IsValid());

        auto shader = Leon::TestGPU::FTestShader::Create("PBR_Lit");
        REQUIRE(shader != nullptr);
        shader->Bind();
        gl.ResetShaderUniforms(shader);
        gl.BindDefaultTextures();
        gl.BindFramebuffer(1, 1);
        Leon::TestGPU::SetMat4(shader, "u_Model", glm::mat4(1.0f));
        Leon::TestGPU::SetMat3(shader, "u_NormalMatrix", glm::mat3(1.0f));

        // A red and a green light in the same place, a blue one without shadows, and one more red
        // light on the last layer of the map.
        Leon::TestGPU::FCameraBufferData camData;
        camData.ViewProjection = glm::mat4(1.0f);
        camData.CameraPosition = glm::vec4(0.0f, 0.0f, 1.0f, 0.0f);
        camData.CameraForward = glm::vec4(0.0f, 0.0f, -1.0f, 0.0f);
        camData.ShadowParams = glm::vec4(0.001f, 0.002f, 0.0f, 0.0f);
        camData.ShadowSettings = glm::ivec4(0, 0, 0, 0);       // hard filter; light 0 owns layer 0
        camData.ExtraShadowedSpots = glm::ivec3(1, -1, 3);     // light 1 owns layer 1, light 3 owns layer 3
        gl.UpdateCameraUBO(camData);

        Leon::TestGPU::FLightingBufferData lightData;
        lightData.DirLight.Direction = glm::vec4(0.0f, 0.0f, -1.0f, 0.0f);
        lightData.LightCounts.x = 0;
        lightData.LightCounts.y = 3;
        const glm::vec3 colors[4] = {{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f}};
        for (int i = 0; i < 4; ++i) {
            lightData.SpotLights[i].Position = glm::vec4(0.0f, 0.0f, 2.0f, 1.0f);
            lightData.SpotLights[i].Direction = glm::vec4(0.0f, 0.0f, -1.0f, glm::cos(glm::radians(20.0f)));
            lightData.SpotLights[i].Color = glm::vec4(colors[i], glm::cos(glm::radians(45.0f)));
            lightData.SpotLights[i].Params = glm::vec4(10.0f, 5.0f, 0.0f, 0.0f);
        }
        gl.UpdateLightingUBO(lightData);
        shader->SetInt("u_UseSpotShadows", 1);
        shader->SetInt("u_DebugMode", 10); // direct light

        const Leon::TestGPU::FTestTexture& map = gl.GetDefaultShadowTex();
        gl.DrawQuad();
        const glm::vec4 open = gl.ReadPixel(0, 0);
        CHECK(open.r > 0.05f);
        CHECK(open.g > 0.05f);
        CHECK(open.b > 0.05f);

        // Something in front of the red light only.
        gl.FillDepth(map, 0.0f, 0);
        gl.DrawQuad();
        glm::vec4 pixel = gl.ReadPixel(0, 0);
        CHECK(std::abs(pixel.r) < 0.001f);
        CHECK(pixel.g == doctest::Approx(open.g).epsilon(0.01f));
        CHECK(pixel.b == doctest::Approx(open.b).epsilon(0.01f));

        // In front of the green light only.
        gl.FillDepth(map, 1.0f, 0);
        gl.FillDepth(map, 0.0f, 1);
        gl.DrawQuad();
        pixel = gl.ReadPixel(0, 0);
        CHECK(pixel.r == doctest::Approx(open.r).epsilon(0.01f));
        CHECK(std::abs(pixel.g) < 0.001f);
        CHECK(pixel.b == doctest::Approx(open.b).epsilon(0.01f));

        // In front of both; the blue light has no layer and is never shadowed, nor does a layer
        // nobody owns (2) shadow anything.
        gl.FillDepth(map, 0.0f, 0);
        gl.FillDepth(map, 0.0f, 2);
        gl.DrawQuad();
        pixel = gl.ReadPixel(0, 0);
        CHECK(std::abs(pixel.r) < 0.001f);
        CHECK(std::abs(pixel.g) < 0.001f);
        CHECK(pixel.b == doctest::Approx(open.b).epsilon(0.01f));

        // The fourth light, on the last layer.
        lightData.LightCounts.y = 4;
        gl.UpdateLightingUBO(lightData);
        gl.DrawQuad();
        CHECK(gl.ReadPixel(0, 0).r == doctest::Approx(open.r).epsilon(0.01f));
        gl.FillDepth(map, 0.0f, 3);
        gl.DrawQuad();
        CHECK(std::abs(gl.ReadPixel(0, 0).r) < 0.001f);
        CHECK(gl.ValidationErrors() == 0);
    }
}
