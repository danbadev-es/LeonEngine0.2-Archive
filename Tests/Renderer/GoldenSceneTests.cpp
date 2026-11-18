#include <doctest/doctest.h>
#include "GPU/HeadlessDevice.hpp"
#include <cmath>

TEST_SUITE("Renderer golden constraints (no pixel goldens from a broken pipeline)") {

    TEST_CASE("Diagnostic PBR frame is finite and IBL diffuse energy holds") {
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
        gl.UpdateCameraUBO(camData);

        Leon::TestGPU::FLightingBufferData lightData;
        lightData.DirLight.Direction = glm::vec4(0.0f, 0.0f, -1.0f, 1.0f);
        lightData.DirLight.Color = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
        lightData.EnvSkyColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
        gl.UpdateLightingUBO(lightData);

        Leon::TestGPU::SetMat4(shader, "u_Model", glm::mat4(1.0f));
        Leon::TestGPU::SetMat3(shader, "u_NormalMatrix", glm::mat3(1.0f));
        gl.ResetShaderUniforms(shader);
        shader->SetInt("u_UseIBL", 1);
        Leon::TestGPU::SetFloat3(shader, "u_AlbedoColor", glm::vec3(1.0f));

        // Irradiance of pi from every direction: a white diffuse surface must reflect exactly 1.
        auto irrad = gl.CreateFloatCube(3.14159265f, 3.14159265f, 3.14159265f, 1.0f);
        REQUIRE(irrad != nullptr);
        gl.BindTexture(Leon::kSlotIrradiance, irrad);

        shader->SetInt("u_DebugMode", 35);
        gl.DrawQuad();
        glm::vec4 energy = gl.ReadPixel();
        CHECK(!std::isnan(energy.r));
        CHECK(energy.r == doctest::Approx(1.0f).epsilon(0.03f));

        shader->SetInt("u_DebugMode", 0);
        gl.DrawQuad();
        glm::vec4 lit = gl.ReadPixel();
        CHECK(!std::isnan(lit.r));
        CHECK(lit.r >= 0.0f);

        shader->SetInt("u_DebugMode", 38);
        gl.DrawQuad();
        glm::vec4 diff = gl.ReadPixel();
        CHECK(!std::isnan(diff.r));

        shader->SetInt("u_DebugMode", 39);
        gl.DrawQuad();
        glm::vec4 spec = gl.ReadPixel();
        CHECK(!std::isnan(spec.r));

        CHECK(gl.ValidationErrors() == 0);
        gl.DestroyTexture(irrad);
    }
}
