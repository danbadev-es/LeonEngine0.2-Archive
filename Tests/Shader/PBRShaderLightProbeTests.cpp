#include <doctest/doctest.h>

#include "GPU/HeadlessDevice.hpp"
#include "RenderCore/FLightProbeGrid.hpp"

// The surface shader's use of baked ambient light: what a light probe says arrives on a surface,
// evaluated with the surface's normal, in place of the sky's irradiance. Held against the C++ twin
// (FLightProbeSample::Irradiance), which is what the baker's tests are written against.

namespace {

    using namespace Leon;
    using namespace Leon::TestGPU;

    constexpr float kPi = 3.14159265f;
    // The debug view that shows the diffuse ambient term: irradiance * albedo / pi.
    constexpr int kDiffuseAmbientView = 35;

    void SetProbe(const TRef<FTestShader>& InShader, const FLightProbeSample& InSample) {
        InShader->SetFloat4("u_AmbientSH0", InSample.Red.x, InSample.Red.y, InSample.Red.z, InSample.Red.w);
        InShader->SetFloat4("u_AmbientSH1", InSample.Green.x, InSample.Green.y, InSample.Green.z, InSample.Green.w);
        InShader->SetFloat4("u_AmbientSH2", InSample.Blue.x, InSample.Blue.y, InSample.Blue.z, InSample.Blue.w);
    }

} // namespace

TEST_SUITE("Shader GPU - PBR_Lit.hlsl light probes") {

    TEST_CASE("the diffuse ambient of a surface is its light probe's irradiance along the normal") {
        FHeadlessDevice gl;
        REQUIRE(gl.IsValid());
        auto shader = FTestShader::Create("PBR_Lit");
        REQUIRE(shader != nullptr);
        gl.ResetShaderUniforms(shader);
        gl.BindDefaultTextures();
        gl.BindFramebuffer(1, 1);
        FCameraBufferData camera;
        camera.CameraPosition = glm::vec4(0.0f, 0.0f, 1.0f, 0.0f);
        gl.UpdateCameraUBO(camera);
        FLightingBufferData lighting;
        lighting.DirLight.Direction.w = 0.0f;
        // A sky of one grey: without a probe every surface receives pi * 0.5 from it.
        lighting.EnvSkyColor = glm::vec4(0.5f, 0.5f, 0.5f, 1.0f);
        lighting.EnvHorizonColor = lighting.EnvGroundColor = glm::vec4(0.5f, 0.5f, 0.5f, 0.0f);
        gl.UpdateLightingUBO(lighting);
        SetFloat3(shader, "u_AlbedoColor", glm::vec3(1.0f, 0.5f, 0.25f));
        shader->SetInt("u_DebugMode", kDiffuseAmbientView);

        // Strong red light from +Z (where the quad faces), a little green from above, blue from below.
        FLightProbeSample probe;
        probe.AddRadiance({0.0f, 0.0f, 1.0f}, {4.0f, 0.0f, 0.0f});
        probe.AddRadiance({0.0f, 1.0f, 0.0f}, {0.0f, 2.0f, 0.0f});
        probe.AddRadiance({0.0f, -1.0f, 0.0f}, {0.0f, 0.0f, 8.0f});
        SetProbe(shader, probe);

        const glm::vec3 albedo(1.0f, 0.5f, 0.25f);
        const auto expect = [&](const glm::vec3& InNormal) {
            const glm::vec3 expected = probe.Irradiance(InNormal) * albedo / kPi;
            gl.DrawQuad();
            const glm::vec4 pixel = gl.ReadPixel(0, 0);
            CAPTURE(InNormal.y);
            CAPTURE(InNormal.z);
            CHECK(pixel.r == doctest::Approx(expected.r).epsilon(0.01));
            CHECK(pixel.g == doctest::Approx(expected.g).epsilon(0.01));
            CHECK(pixel.b == doctest::Approx(expected.b).epsilon(0.01));
        };

        // Not used until the draw says so: the sky's irradiance, as always.
        gl.DrawQuad();
        const glm::vec4 fromSky = gl.ReadPixel(0, 0);
        CHECK(fromSky.r == doctest::Approx(0.5f * albedo.r).epsilon(0.01));
        CHECK(fromSky.b == doctest::Approx(0.5f * albedo.b).epsilon(0.01));

        shader->SetInt("u_UseLightProbe", 1);
        // Facing +Z: all of the red (1/4 + 1/2), a quarter of the lights at right angles to it.
        REQUIRE(probe.Irradiance({0.0f, 0.0f, 1.0f}).r == doctest::Approx(3.0f));
        REQUIRE(probe.Irradiance({0.0f, 0.0f, 1.0f}).g == doctest::Approx(0.5f));
        expect({0.0f, 0.0f, 1.0f});
        // Turned to face up: the green light in full, and none of the blue one below (never negative).
        SetMat3(shader, "u_NormalMatrix", FloorFacingNormalMatrix());
        REQUIRE(probe.Irradiance({0.0f, 1.0f, 0.0f}).b == 0.0f);
        expect({0.0f, 1.0f, 0.0f});

        // The lightmap, where there is one, still decides: a probe is for what has none.
        shader->SetInt("u_DebugMode", 0);
        shader->SetInt("u_UseLightmap", 1);
        gl.BindTexture(kSlotLightmap, gl.Create1x1FloatTexture(0.0f, 0.0f, 0.0f));
        gl.DrawQuad();
        const glm::vec4 lightmappedWithProbe = gl.ReadPixel(0, 0);
        shader->SetInt("u_UseLightProbe", 0);
        gl.DrawQuad();
        const glm::vec4 lightmapped = gl.ReadPixel(0, 0);
        CHECK(lightmappedWithProbe.r == lightmapped.r);
        CHECK(lightmappedWithProbe.g == lightmapped.g);
        CHECK(lightmappedWithProbe.b == lightmapped.b);
        CHECK(gl.ValidationErrors() == 0);
    }
}

TEST_SUITE("Light probe sample") {

    TEST_CASE("a light adds its irradiance through two harmonics: a quarter everywhere, a half along its direction") {
        FLightProbeSample sample;
        const glm::vec3 direction = glm::normalize(glm::vec3(1.0f, 2.0f, -2.0f));
        sample.AddRadiance(direction, {2.0f, 4.0f, 8.0f});
        CHECK(sample.Irradiance(direction).r == doctest::Approx(1.5f));
        CHECK(sample.Irradiance(direction).g == doctest::Approx(3.0f));
        CHECK(sample.Irradiance(direction).b == doctest::Approx(6.0f));
        // At right angles: the quarter alone. Facing away: a quarter less a half, which is no light.
        const glm::vec3 across = glm::normalize(glm::cross(direction, glm::vec3(0.0f, 1.0f, 0.0f)));
        CHECK(sample.Irradiance(across).r == doctest::Approx(0.5f));
        CHECK(sample.Irradiance(-direction) == glm::vec3(0.0f));
    }
}
