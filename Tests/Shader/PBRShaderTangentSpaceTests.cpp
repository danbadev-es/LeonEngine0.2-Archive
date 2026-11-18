#include <doctest/doctest.h>
#include "GPU/HeadlessDevice.hpp"
#include <filesystem>

TEST_SUITE("Shader GPU - PBR_Lit.hlsl Tangent Space & TBN Orthonormality") {

    TEST_CASE("PBR_Lit.hlsl Hardware GPU Tangent and Bitangent Reconstruction") {
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
        shader->SetInt("u_UseAlbedoMap", 0);
        shader->SetInt("u_UseMetallicMap", 0);
        shader->SetInt("u_UseRoughnessMap", 0);
        shader->SetInt("u_UseAOMap", 0);
        shader->SetInt("u_UseEmissiveMap", 0);
        shader->SetInt("u_UsePlanarReflection", 0);
        shader->SetInt("u_AlphaMode", 0);
        shader->SetFloat("u_AlphaCutoff", 0.5f);
        Leon::TestGPU::SetFloat2(shader, "u_UVTiling", glm::vec2(1.0f));
        Leon::TestGPU::SetFloat2(shader, "u_UVOffset", glm::vec2(0.0f));

        // 1. Mode 20: Tangent Vector T (normalize(T) * 0.5 + 0.5)
        // Quad Tangent is (1, 0, 0) -> Mapped: (1.0, 0.5, 0.5)
        shader->SetInt("u_DebugMode", 20);
        gl.DrawQuad();
        glm::vec4 tangentColor = gl.ReadPixel();
        CHECK(tangentColor.r == doctest::Approx(1.0f).epsilon(0.02f));
        CHECK(tangentColor.g == doctest::Approx(0.5f).epsilon(0.02f));
        CHECK(tangentColor.b == doctest::Approx(0.5f).epsilon(0.02f));

        // 2. Mode 21: Bitangent Vector B (normalize(B) * 0.5 + 0.5)
        // Quad Bitangent is (0, 1, 0) -> Mapped: (0.5, 1.0, 0.5)
        shader->SetInt("u_DebugMode", 21);
        gl.DrawQuad();
        glm::vec4 bitangentColor = gl.ReadPixel();
        CHECK(bitangentColor.r == doctest::Approx(0.5f).epsilon(0.02f));
        CHECK(bitangentColor.g == doctest::Approx(1.0f).epsilon(0.02f));
        CHECK(bitangentColor.b == doctest::Approx(0.5f).epsilon(0.02f));

        // 3. Orthonormality check in reconstructed vectors
        glm::vec3 T = glm::vec3(tangentColor.r, tangentColor.g, tangentColor.b) * 2.0f - 1.0f;
        glm::vec3 B = glm::vec3(bitangentColor.r, bitangentColor.g, bitangentColor.b) * 2.0f - 1.0f;
        glm::vec3 N = glm::vec3(0.0f, 0.0f, 1.0f);

        CHECK(std::abs(glm::dot(T, N)) < 0.05f);
        CHECK(std::abs(glm::dot(B, N)) < 0.05f);
        CHECK(std::abs(glm::dot(T, B)) < 0.05f);
        CHECK(glm::length(T) == doctest::Approx(1.0f).epsilon(0.05f));
        CHECK(glm::length(B) == doctest::Approx(1.0f).epsilon(0.05f));
    }

    TEST_CASE("PBR_Lit.hlsl Hardware GPU Gram-Schmidt Orthogonalization on Skewed Inputs") {
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
        shader->SetInt("u_UseAlbedoMap", 0);
        shader->SetInt("u_UseMetallicMap", 0);
        shader->SetInt("u_UseRoughnessMap", 0);
        shader->SetInt("u_UseAOMap", 0);
        shader->SetInt("u_UseEmissiveMap", 0);
        shader->SetInt("u_UsePlanarReflection", 0);
        shader->SetInt("u_AlphaMode", 0);
        shader->SetFloat("u_AlphaCutoff", 0.5f);
        Leon::TestGPU::SetFloat2(shader, "u_UVTiling", glm::vec2(1.0f));
        Leon::TestGPU::SetFloat2(shader, "u_UVOffset", glm::vec2(0.0f));

        // Create custom mesh with SKEWED Tangent (1, 0, 1) and SKEWED Bitangent (1, 1, 0)
        // Normal is (0, 0, 1).
        // Gram-Schmidt MUST project T onto plane perp to N -> (1, 0, 0)
        // Gram-Schmidt MUST project B onto plane perp to N and T -> (0, 1, 0)
        std::vector<Leon::TestGPU::FTestVertex> skewedVertices = {
            {glm::vec3(-1.0f, -1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f), glm::vec2(0.0f, 0.0f),
             glm::vec4(1.0f, 0.0f, 1.0f, 1.0f)},
            {glm::vec3(1.0f, -1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f), glm::vec2(1.0f, 0.0f),
             glm::vec4(1.0f, 0.0f, 1.0f, 1.0f)},
            {glm::vec3(1.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f), glm::vec2(1.0f, 1.0f),
             glm::vec4(1.0f, 0.0f, 1.0f, 1.0f)},
            {glm::vec3(-1.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f), glm::vec2(0.0f, 1.0f),
             glm::vec4(1.0f, 0.0f, 1.0f, 1.0f)}};
        // The quad the device draws, with these vertices instead of its own.
        gl.SetQuadVertices(skewedVertices.data());

        // 1. Tangent test: Skewed (1, 0, 1) MUST be reconstructed to pure +X (1, 0, 0) -> (1.0, 0.5, 0.5)
        shader->SetInt("u_DebugMode", 20);
        gl.DrawQuad();
        glm::vec4 tangPix = gl.ReadPixel();
        CHECK(tangPix.r == doctest::Approx(1.0f).epsilon(0.02f));
        CHECK(tangPix.g == doctest::Approx(0.5f).epsilon(0.02f));
        CHECK(tangPix.b == doctest::Approx(0.5f).epsilon(0.02f));

        // 2. Bitangent test: Skewed (1, 1, 0) MUST be reconstructed to pure +Y (0, 1, 0) -> (0.5, 1.0, 0.5)
        shader->SetInt("u_DebugMode", 21);
        gl.DrawQuad();
        glm::vec4 bitangPix = gl.ReadPixel();
        CHECK(bitangPix.r == doctest::Approx(0.5f).epsilon(0.02f));
        CHECK(bitangPix.g == doctest::Approx(1.0f).epsilon(0.02f));
        CHECK(bitangPix.b == doctest::Approx(0.5f).epsilon(0.02f));

        // 3. Mirrored UVs: a tangent with w = -1 flips the bitangent to -Y -> (0.5, 0.0, 0.5)
        for (auto& vertex : skewedVertices)
            vertex.Tangent.w = -1.0f;
        gl.SetQuadVertices(skewedVertices.data());
        gl.DrawQuad();
        glm::vec4 mirroredPix = gl.ReadPixel();
        CHECK(mirroredPix.r == doctest::Approx(0.5f).epsilon(0.02f));
        CHECK(std::abs(mirroredPix.g) < 0.02f);
        CHECK(mirroredPix.b == doctest::Approx(0.5f).epsilon(0.02f));
    }
}
