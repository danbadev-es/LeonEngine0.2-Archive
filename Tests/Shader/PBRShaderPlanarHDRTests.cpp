#include <doctest/doctest.h>
#include "GPU/HeadlessDevice.hpp"
#include "Renderer/FWorldRenderer.hpp"
#include "Engine/UWorld.hpp"

#include <cmath>
#include <vector>

TEST_SUITE("Shader GPU - Planar Reflection HDR Composition") {

    TEST_CASE("WorldRenderer planar capture is a float target (not LDR RGBA8) sized by quality") {
        auto world = Leon::UWorld::Create("PlanarFormatWorld");
        REQUIRE(world != nullptr);
        auto* renderer = world->GetWorldRenderer();
        REQUIRE(renderer != nullptr);

        const auto epic = renderer->GetPlanarCaptureDesc();
        CHECK(epic.Format == Leon::ERHIFormat::RGBA16_Float);
        CHECK(epic.MipLevels == 5); // roughness * 4 LOD blur chain
        CHECK(renderer->GetPlanarReflectionQuality() == Leon::EPlanarReflectionQuality::Epic);
        CHECK(renderer->GetPlanarReflectionResolutionScale() == doctest::Approx(1.0f));

        renderer->SetPlanarReflectionQuality(Leon::EPlanarReflectionQuality::Low);
        renderer->OnViewportResize(1280, 720);
        const auto low = renderer->GetPlanarCaptureDesc();
        CHECK(low.Width == 320);
        CHECK(low.Height == 180);
        CHECK(low.MipLevels == 3);
        CHECK(low.Format == Leon::ERHIFormat::RGBA16_Float);
        renderer->SetPlanarReflectionQuality(Leon::EPlanarReflectionQuality::Epic);
    }

    TEST_CASE("Planar Karis HDR: metallic mirror keeps HDR Li after BRDF scale") {
        Leon::TestGPU::FHeadlessDevice gl;
        REQUIRE(gl.IsValid());

        auto shader = Leon::TestGPU::FTestShader::Create("Engine/Resources/Shaders/PBR_Lit.glsl");
        REQUIRE(shader != nullptr);
        shader->Bind();

        gl.BindDefaultTextures();
        // Float HDR readback target
        gl.BindFramebuffer(1, 1);

        Leon::TestGPU::FCameraBufferData camData;
        camData.ViewProjection = glm::mat4(1.0f);
        Leon::TestGPU::ApplyFloorView(camData);
        gl.UpdateCameraUBO(camData);

        Leon::TestGPU::FLightingBufferData lightData;
        lightData.DirLight.Direction.w = 0.0f;
        lightData.LightCounts.x = 0;
        lightData.LightCounts.y = 0;
        lightData.EnvSkyColor = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
        gl.UpdateLightingUBO(lightData);

        Leon::TestGPU::SetMat4(shader, "u_Model", glm::mat4(1.0f));
        Leon::TestGPU::SetMat3(shader, "u_NormalMatrix", Leon::TestGPU::FloorFacingNormalMatrix());
        shader->SetInt("u_UseIBL", 0);
        shader->SetInt("u_UseShadows", 0);
        shader->SetInt("u_UseSpotShadows", 0);
        shader->SetInt("u_UseNormalMap", 0);
        shader->SetInt("u_UseAlbedoMap", 0);
        shader->SetInt("u_UseMetallicMap", 0);
        shader->SetInt("u_UseRoughnessMap", 0);
        shader->SetInt("u_UseAOMap", 0);
        shader->SetInt("u_UseEmissiveMap", 0);
        shader->SetInt("u_DebugMode", 0);
        Leon::TestGPU::SetMat4(shader, "u_PlanarViewProjection", glm::mat4(1.0f));
        shader->SetFloat("u_AO", 1.0f);
        Leon::TestGPU::SetFloat3(shader, "u_EmissiveColor", glm::vec3(0.0f));
        Leon::TestGPU::SetFloat3(shader, "u_AlbedoColor", glm::vec3(1.0f));
        shader->SetFloat("u_Metallic", 1.0f);
        shader->SetFloat("u_Roughness", 0.04f);

        // HDR planar sample: radiance 8 (would clamp to 1.0 on RGBA8)
        float hdrPixel[4] = {8.0f, 8.0f, 8.0f, 1.0f};
        auto planarTex = gl.Create2D(1, 1, Leon::ERHIFormat::RGBA32_Float, hdrPixel);
        gl.BindTexture(5, planarTex);

        shader->SetInt("u_UsePlanarReflection", 1);
        gl.DrawQuad();
        glm::vec4 pixel = gl.ReadPixel(0, 0);

        CHECK(!std::isnan(pixel.r));
        CHECK(!std::isinf(pixel.r));
        // Metallic mirror + Karis: Li * (F*A+B) keeps HDR (>> 1), not LDR-clipped to ~1
        CHECK(pixel.r > 2.0f);
        CHECK(pixel.r < 30.0f);

        gl.DestroyTexture(planarTex);
    }

    TEST_CASE("Dielectric wet floor: HDR planar emissive is BRDF-scaled (not raw Li white stamp)") {
        Leon::TestGPU::FHeadlessDevice gl;
        REQUIRE(gl.IsValid());

        auto shader = Leon::TestGPU::FTestShader::Create("Engine/Resources/Shaders/PBR_Lit.glsl");
        REQUIRE(shader != nullptr);
        shader->Bind();
        gl.BindDefaultTextures();
        gl.BindFramebuffer(1, 1);

        // Floor-facing N (+Y) with camera above → near-normal incidence (low dielectric Fresnel)
        Leon::TestGPU::FCameraBufferData camData;
        camData.ViewProjection = glm::mat4(1.0f);
        Leon::TestGPU::ApplyFloorView(camData);
        gl.UpdateCameraUBO(camData);

        Leon::TestGPU::FLightingBufferData lightData{};
        lightData.EnvSkyColor = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
        gl.UpdateLightingUBO(lightData);

        Leon::TestGPU::SetMat4(shader, "u_Model", glm::mat4(1.0f));
        Leon::TestGPU::SetMat3(shader, "u_NormalMatrix", Leon::TestGPU::FloorFacingNormalMatrix());
        shader->SetInt("u_UseIBL", 0);
        shader->SetInt("u_UseShadows", 0);
        shader->SetInt("u_UseSpotShadows", 0);
        shader->SetInt("u_UseNormalMap", 0);
        shader->SetInt("u_UseAlbedoMap", 0);
        shader->SetInt("u_DebugMode", 0);
        Leon::TestGPU::SetMat4(shader, "u_PlanarViewProjection", glm::mat4(1.0f));
        shader->SetFloat("u_AO", 1.0f);
        Leon::TestGPU::SetFloat3(shader, "u_EmissiveColor", glm::vec3(0.0f));
        // Match M_FloorTiles-ish dielectric
        Leon::TestGPU::SetFloat3(shader, "u_AlbedoColor", glm::vec3(0.75f));
        shader->SetFloat("u_Metallic", 0.08f);
        shader->SetFloat("u_Roughness", 0.18f);

        // FWindow-like HDR emissive stamp (intensity 6)
        float windowHdr[4] = {6.0f, 5.1f, 2.7f, 1.0f};
        auto planarTex = gl.Create2D(1, 1, Leon::ERHIFormat::RGBA32_Float, windowHdr);
        gl.BindTexture(5, planarTex);

        shader->SetInt("u_UsePlanarReflection", 1);
        gl.DrawQuad();
        glm::vec4 pixel = gl.ReadPixel(0, 0);

        CHECK(!std::isnan(pixel.r));
        CHECK(!std::isinf(pixel.r));
        // Karis: reflectionLi * (F*A+B). At near-normal incidence dielectric F0≈0.04,
        // so specular must be far below raw planar Li (6). Previous planar*F_IBL at grazing
        // and mip0-only sampling produced near-white stamps after tonemap.
        float peak = std::max(pixel.r, std::max(pixel.g, pixel.b));
        CHECK(peak < 2.5f);
        CHECK(peak > 0.01f);

        gl.DestroyTexture(planarTex);
    }

    TEST_CASE("IBL ON + Planar OFF vs Planar ON matrix — planar adds specular without NaN") {
        Leon::TestGPU::FHeadlessDevice gl;
        REQUIRE(gl.IsValid());

        auto shader = Leon::TestGPU::FTestShader::Create("Engine/Resources/Shaders/PBR_Lit.glsl");
        REQUIRE(shader != nullptr);
        shader->Bind();
        gl.BindDefaultTextures();
        gl.BindFramebuffer(1, 1);

        Leon::TestGPU::FCameraBufferData camData;
        camData.ViewProjection = glm::mat4(1.0f);
        Leon::TestGPU::ApplyFloorView(camData);
        gl.UpdateCameraUBO(camData);

        Leon::TestGPU::FLightingBufferData lightData{};
        lightData.EnvSkyColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
        gl.UpdateLightingUBO(lightData);

        Leon::TestGPU::SetMat4(shader, "u_Model", glm::mat4(1.0f));
        Leon::TestGPU::SetMat3(shader, "u_NormalMatrix", Leon::TestGPU::FloorFacingNormalMatrix());
        shader->SetInt("u_UseShadows", 0);
        shader->SetInt("u_UseSpotShadows", 0);
        shader->SetInt("u_UseNormalMap", 0);
        shader->SetInt("u_UseAlbedoMap", 0);
        shader->SetInt("u_DebugMode", 0);
        Leon::TestGPU::SetMat4(shader, "u_PlanarViewProjection", glm::mat4(1.0f));
        shader->SetFloat("u_AO", 1.0f);
        Leon::TestGPU::SetFloat3(shader, "u_EmissiveColor", glm::vec3(0.0f));
        Leon::TestGPU::SetFloat3(shader, "u_AlbedoColor", glm::vec3(0.75f));
        shader->SetFloat("u_Metallic", 0.0f);
        shader->SetFloat("u_Roughness", 0.18f);

        float bright[4] = {4.0f, 3.5f, 2.5f, 1.0f};
        auto planarTex = gl.Create2D(1, 1, Leon::ERHIFormat::RGBA32_Float, bright);
        gl.BindTexture(5, planarTex);

        auto drawCombo = [&](int useIbl, int usePlanar) {
            shader->SetInt("u_UseIBL", useIbl);
            shader->SetInt("u_UsePlanarReflection", usePlanar);
            gl.DrawQuad();
            return gl.ReadPixel(0, 0);
        };

        glm::vec4 A = drawCombo(0, 0); // IBL OFF, Planar OFF
        glm::vec4 B = drawCombo(1, 0); // IBL ON, Planar OFF
        glm::vec4 C = drawCombo(0, 1); // IBL OFF, Planar ON
        glm::vec4 D = drawCombo(1, 1); // IBL ON, Planar ON

        for (const auto& p : {A, B, C, D}) {
            CHECK(!std::isnan(p.r));
            CHECK(!std::isinf(p.r));
            CHECK(p.r >= 0.0f);
            CHECK(p.r < 1000.0f); // no firefly amplification
        }

        // Planar ON with bright HDR sample must increase luminance vs planar OFF (same IBL state)
        CHECK(C.r + C.g + C.b > A.r + A.g + A.b);
        CHECK(D.r + D.g + D.b >= B.r + B.g + B.b - 1e-3f);

        gl.DestroyTexture(planarTex);
    }
}
