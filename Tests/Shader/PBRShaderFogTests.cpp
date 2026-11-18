#include <doctest/doctest.h>
#include "GPU/HeadlessDevice.hpp"

#include <cmath>

// Distance/height fog in PBR_Lit.hlsl, checked on real pixels: an unlit emissive quad at the origin
// viewed from a known distance must blend toward the fog colour by 1 - exp(-distance * density).

namespace {

    const glm::vec3 kEmissive(1.0f, 0.5f, 0.2f);
    const glm::vec3 kFogColor(0.1f, 0.2f, 0.3f);

    struct FFogScene {
        Leon::TestGPU::FHeadlessDevice& GL;
        Leon::TRef<Leon::TestGPU::FTestShader> Shader;
        Leon::TestGPU::FLightingBufferData Light;

        explicit FFogScene(Leon::TestGPU::FHeadlessDevice& InGL, const char* InShaderPath) : GL(InGL) {
            Shader = Leon::TestGPU::FTestShader::Create(InShaderPath);
            Shader->Bind();
            GL.BindDefaultTextures();
            GL.BindFramebuffer(1, 1);

            Light.DirLight.Direction = glm::vec4(0.0f, 0.0f, -1.0f, 0.0f);
            Light.DirLight.Color = glm::vec4(0.0f);
            Light.LightCounts = glm::ivec4(0);
            Light.EnvSkyColor = glm::vec4(0.0f);
            Light.EnvHorizonColor = glm::vec4(0.0f);
            Light.EnvGroundColor = glm::vec4(0.0f);

            Leon::TestGPU::SetMat4(Shader, "u_Model", glm::mat4(1.0f));
            Leon::TestGPU::SetMat3(Shader, "u_NormalMatrix", glm::mat3(1.0f));
            Shader->SetInt("u_UseIBL", 0);
            Shader->SetInt("u_UseShadows", 0);
            Shader->SetInt("u_UseNormalMap", 0);
            Shader->SetInt("u_UseAlbedoMap", 0);
            Shader->SetInt("u_UseMetallicMap", 0);
            Shader->SetInt("u_UseRoughnessMap", 0);
            Shader->SetInt("u_UseAOMap", 0);
            Shader->SetInt("u_UseEmissiveMap", 0);
            Shader->SetInt("u_UsePlanarReflection", 0);
            Shader->SetInt("u_AlphaMode", 0);
            Shader->SetFloat("u_AlphaCutoff", 0.5f);
            Leon::TestGPU::SetFloat2(Shader, "u_UVTiling", glm::vec2(1.0f));
            Leon::TestGPU::SetFloat2(Shader, "u_UVOffset", glm::vec2(0.0f));
            Leon::TestGPU::SetFloat3(Shader, "u_AlbedoColor", glm::vec3(0.5f));
            Shader->SetFloat("u_Metallic", 0.0f);
            Shader->SetFloat("u_Roughness", 0.5f);
            Shader->SetFloat("u_AO", 1.0f);
            Leon::TestGPU::SetFloat3(Shader, "u_EmissiveColor", kEmissive);
            Shader->SetFloat("u_EmissiveIntensity", 1.0f);
            Shader->SetInt("u_DebugMode", 0);
        }

        /** Renders the quad seen from InCameraDistance metres along +Z and returns the HDR pixel. */
        glm::vec4 Render(float InCameraDistance) {
            Leon::TestGPU::FCameraBufferData cam;
            cam.ViewProjection = glm::mat4(1.0f);
            cam.CameraPosition = glm::vec4(0.0f, 0.0f, InCameraDistance, 0.0f);
            cam.CameraForward = glm::vec4(0.0f, 0.0f, -1.0f, 0.0f);
            GL.UpdateCameraUBO(cam);
            GL.UpdateLightingUBO(Light);
            GL.DrawQuad();
            return GL.ReadPixel();
        }
    };

    void CheckBlend(const glm::vec4& InPixel, float InFogAmount) {
        const glm::vec3 expected = glm::mix(kEmissive, kFogColor, InFogAmount);
        CHECK(InPixel.r == doctest::Approx(expected.r).epsilon(0.02));
        CHECK(InPixel.g == doctest::Approx(expected.g).epsilon(0.02));
        CHECK(InPixel.b == doctest::Approx(expected.b).epsilon(0.02));
    }

} // namespace

TEST_SUITE("Shader GPU - PBR_Lit.hlsl Distance Fog") {

    TEST_CASE("default lighting data has fog off and leaves the colour untouched") {
        Leon::TestGPU::FHeadlessDevice gl;
        REQUIRE(gl.IsValid());
        FFogScene scene(gl, "PBR_Lit");
        CHECK(scene.Light.FogColorDensity.a == 0.0f);
        CheckBlend(scene.Render(2.0f), 0.0f);
        CheckBlend(scene.Render(500.0f), 0.0f);
    }

    TEST_CASE("fog blends toward the fog colour exponentially with distance") {
        Leon::TestGPU::FHeadlessDevice gl;
        REQUIRE(gl.IsValid());
        FFogScene scene(gl, "PBR_Lit");
        scene.Light.FogColorDensity = glm::vec4(kFogColor, 0.5f);
        scene.Light.FogParams = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);

        CheckBlend(scene.Render(2.0f), 1.0f - std::exp(-1.0f));
        CheckBlend(scene.Render(6.0f), 1.0f - std::exp(-3.0f));
        // Far enough: only the fog colour is left.
        CheckBlend(scene.Render(60.0f), 1.0f);
    }

    TEST_CASE("start distance delays the fog and max opacity caps it") {
        Leon::TestGPU::FHeadlessDevice gl;
        REQUIRE(gl.IsValid());
        FFogScene scene(gl, "PBR_Lit");
        scene.Light.FogColorDensity = glm::vec4(kFogColor, 0.5f);

        scene.Light.FogParams = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f); // starts 1 m from the camera
        CheckBlend(scene.Render(0.5f), 0.0f);
        CheckBlend(scene.Render(2.0f), 1.0f - std::exp(-0.5f));

        scene.Light.FogParams = glm::vec4(0.0f, 0.0f, 0.0f, 0.25f); // never more than 25 %
        CheckBlend(scene.Render(60.0f), 0.25f);
    }

    TEST_CASE("height falloff thins the fog above the base height") {
        Leon::TestGPU::FHeadlessDevice gl;
        REQUIRE(gl.IsValid());
        FFogScene scene(gl, "PBR_Lit");
        scene.Light.FogColorDensity = glm::vec4(kFogColor, 0.5f);
        // The quad sits at y = 0, two metres above the fog base, with a falloff of 1 per metre.
        scene.Light.FogParams = glm::vec4(0.0f, 1.0f, -2.0f, 1.0f);
        const float heightFactor = std::exp(-2.0f);
        CheckBlend(scene.Render(2.0f), 1.0f - std::exp(-2.0f * 0.5f * heightFactor));

        // At or below the base height the fog is at full density.
        scene.Light.FogParams = glm::vec4(0.0f, 1.0f, 3.0f, 1.0f);
        CheckBlend(scene.Render(2.0f), 1.0f - std::exp(-1.0f));
    }
}
