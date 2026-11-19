#include <doctest/doctest.h>

#include "Engine/FGraphicsQuality.hpp"
#include "Engine/UWorld.hpp"
#include "GPU/HeadlessDevice.hpp"
#include "RenderCore/FReflectionProbeMath.hpp"

#include <array>
#include <cmath>
#include <vector>

// Reflection probes in the surface shader: which way a probe's cube is read (the box projection),
// how much a probe counts near the faces of its box, which level a rough surface reads and how two
// probes share a pixel. Each is checked on the GPU and held against the C++ twin of the same maths
// (FReflectionProbeMath.hpp), which is what the renderer uses to choose probes.

namespace {

    using namespace Leon;
    using namespace Leon::TestGPU;

    // The shader's debug views: what the probes give the pixel, and what the pixel ends up reflecting.
    constexpr int kProbeView = 41;
    constexpr int kReflectionView = 42;

    /** One pixel at the origin of the world, facing +Z, seen from InCamera. The sky is blue everywhere. */
    struct FProbePixel {
        FHeadlessDevice Device;
        TRef<FTestShader> Shader;

        explicit FProbePixel(const glm::vec3& InCamera = {0.0f, 0.0f, 1.0f}) {
            REQUIRE(Device.IsValid());
            Shader = FTestShader::Create("PBR_Lit");
            REQUIRE(Shader != nullptr);
            Device.ResetShaderUniforms(Shader);
            Device.BindDefaultTextures();
            Device.BindFramebuffer(1, 1);

            FCameraBufferData camera;
            camera.CameraPosition = glm::vec4(InCamera, 0.0f);
            Device.UpdateCameraUBO(camera);
            FLightingBufferData lighting;
            lighting.DirLight.Direction.w = 0.0f;
            lighting.EnvSkyColor = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
            lighting.EnvHorizonColor = glm::vec4(0.0f, 0.0f, 1.0f, 0.0f);
            lighting.EnvGroundColor = glm::vec4(0.0f, 0.0f, 1.0f, 0.0f);
            Device.UpdateLightingUBO(lighting);
            Shader->SetFloat("u_Roughness", 0.0f);
            Shader->SetInt("u_DebugMode", kProbeView);
        }

        /** An array of cubes of float pixels, InSize along the edge, with a full chain of levels. */
        FTestTexture MakeCubes(uint32_t InCubes, uint32_t InSize = 1) {
            uint32_t levels = 1;
            for (uint32_t size = InSize; size > 1; size /= 2)
                ++levels;
            FTestTexture cubes = Device.CreateCubeArray(InSize, levels, InCubes, ERHIFormat::RGBA32_Float);
            REQUIRE(cubes != nullptr);
            Device.BindTexture(kSlotReflectionProbes, cubes);
            return cubes;
        }

        /** Fills one face of one level with a colour. */
        void Fill(const FTestTexture& InCubes, uint32_t InCube, uint32_t InFace, const glm::vec3& InColor, uint32_t InMip = 0) {
            const uint32_t size = std::max(InCubes->GetDesc().Width >> InMip, 1u);
            std::vector<float> pixels;
            for (uint32_t i = 0; i < size * size; ++i)
                pixels.insert(pixels.end(), {InColor.r, InColor.g, InColor.b, 1.0f});
            Device.Upload(InCubes, pixels.data(), InMip, InCube * 6 + InFace);
        }

        /** The same colour on every level of a face. */
        void FillLevels(const FTestTexture& InCubes, uint32_t InCube, uint32_t InFace, const glm::vec3& InColor) {
            for (uint32_t mip = 0; mip < InCubes->GetDesc().MipLevels; ++mip)
                Fill(InCubes, InCube, InFace, InColor, mip);
        }

        void FillCube(const FTestTexture& InCubes, uint32_t InCube, const glm::vec3& InColor, uint32_t InMip = 0) {
            for (uint32_t face = 0; face < 6; ++face)
                Fill(InCubes, InCube, face, InColor, InMip);
        }

        void SetProbe(int InSlot, int InCube, const FReflectionProbeBox& InProbe) {
            const std::string slot = std::to_string(InSlot);
            SetFloat3(Shader, "u_ProbeBoxMin" + slot, InProbe.BoxMin);
            SetFloat3(Shader, "u_ProbeBoxMax" + slot, InProbe.BoxMax);
            SetFloat3(Shader, "u_ProbeCapture" + slot, InProbe.CapturePosition);
            Shader->SetInt("u_ProbeCube" + slot, InCube);
            Shader->SetFloat("u_ProbeIntensity" + slot, InProbe.Intensity);
            Shader->SetFloat("u_ProbeBlend" + slot, InProbe.BlendDistance);
        }

        glm::vec3 Draw() {
            Device.DrawQuad();
            return glm::vec3(Device.ReadPixel(0, 0));
        }
    };

    /** Index of the cube face a direction reads: +X, -X, +Y, -Y, +Z, -Z. */
    uint32_t FaceOf(const glm::vec3& InDirection) {
        const glm::vec3 magnitude = glm::abs(InDirection);
        if (magnitude.x >= magnitude.y && magnitude.x >= magnitude.z)
            return InDirection.x > 0.0f ? 0u : 1u;
        if (magnitude.y >= magnitude.z)
            return InDirection.y > 0.0f ? 2u : 3u;
        return InDirection.z > 0.0f ? 4u : 5u;
    }

    const glm::vec3 kRed(1.0f, 0.0f, 0.0f);
    const glm::vec3 kGreen(0.0f, 1.0f, 0.0f);
    const glm::vec3 kBlue(0.0f, 0.0f, 1.0f);

} // namespace

TEST_SUITE("Shader GPU - PBR_Lit.hlsl reflection probes") {

    TEST_CASE("a probe's cube is read towards the point of the box the reflection hits") {
        // The view comes in from +X, so the reflection leaves towards -X, slightly up along +Z.
        const glm::vec3 camera(2.0f, 0.0f, 1.0f);
        FProbePixel pixel(camera);
        // Faces of several pixels: a face of one pixel is blended with its neighbours all over.
        const FTestTexture cubes = pixel.MakeCubes(1, 8);
        for (uint32_t face = 0; face < 6; ++face)
            pixel.FillLevels(cubes, 0, face, kBlue);
        pixel.FillLevels(cubes, 0, 1, kRed);   // -X: where the reflection points
        pixel.FillLevels(cubes, 0, 5, kGreen); // -Z: where the wall it hits is, seen from the capture point

        // A tall narrow box: the reflection hits the wall at x = -1 half a metre up, and the cube was
        // captured from near the top of the box, nine metres above. From there that point of the wall
        // is almost straight down.
        FReflectionProbeBox probe;
        probe.BoxMin = {-1.0f, -1.0f, 0.0f};
        probe.BoxMax = {1.0f, 1.0f, 10.0f};
        probe.CapturePosition = {0.0f, 0.0f, 9.0f};
        pixel.SetProbe(0, 0, probe);

        const glm::vec3 reflected = glm::reflect(-glm::normalize(camera), glm::vec3(0.0f, 0.0f, 1.0f));
        REQUIRE(FaceOf(reflected) == 1);
        const glm::vec3 lookup = BoxProjectedDirection(glm::vec3(0.0f), reflected, probe);
        CHECK(lookup.x == doctest::Approx(-1.0f).epsilon(0.001));
        CHECK(lookup.z == doctest::Approx(-8.5f).epsilon(0.001));
        REQUIRE(FaceOf(lookup) == 5);
        const glm::vec3 projected = pixel.Draw();
        CHECK(projected.g == doctest::Approx(1.0f).epsilon(0.01));
        CHECK(projected.r == doctest::Approx(0.0f));

        // In a box so large that its walls are as good as infinitely far, and captured from where the
        // pixel is, the cube is read along the reflection itself, like the sky.
        probe.BoxMin = glm::vec3(-1000.0f);
        probe.BoxMax = glm::vec3(1000.0f);
        probe.CapturePosition = glm::vec3(0.0f);
        pixel.SetProbe(0, 0, probe);
        REQUIRE(FaceOf(BoxProjectedDirection(glm::vec3(0.0f), reflected, probe)) == 1);
        const glm::vec3 plain = pixel.Draw();
        CHECK(plain.r == doctest::Approx(1.0f).epsilon(0.01));
        CHECK(plain.g == doctest::Approx(0.0f));
        CHECK(pixel.Device.ValidationErrors() == 0);
    }

    TEST_CASE("a probe fades out over its blend distance outside the box, and the sky takes what it leaves") {
        FProbePixel pixel;
        const FTestTexture cubes = pixel.MakeCubes(1);
        pixel.FillCube(cubes, 0, kRed);
        pixel.Shader->SetInt("u_DebugMode", kReflectionView);

        FReflectionProbeBox probe;
        probe.BoxMin = {-1.0f, -1.0f, -1.0f};
        probe.BoxMax = {1.0f, 1.0f, 1.0f};
        probe.BlendDistance = 0.5f;

        // Inside: all probe.
        pixel.SetProbe(0, 0, probe);
        glm::vec3 color = pixel.Draw();
        CHECK(color.r == doctest::Approx(1.0f).epsilon(0.01));
        CHECK(color.b == doctest::Approx(0.0f));

        // A quarter of a metre outside a face: half and half.
        probe.BoxMin.x = 0.25f;
        probe.BoxMax.x = 2.0f;
        pixel.SetProbe(0, 0, probe);
        CHECK(ReflectionProbeWeight(glm::vec3(0.0f), probe) == doctest::Approx(0.5f));
        color = pixel.Draw();
        CHECK(color.r == doctest::Approx(0.5f).epsilon(0.01));
        CHECK(color.b == doctest::Approx(0.5f).epsilon(0.01));

        // Past the blend distance, and with no probe at all: the sky alone.
        probe.BoxMin.x = 0.75f;
        pixel.SetProbe(0, 0, probe);
        CHECK(ReflectionProbeWeight(glm::vec3(0.0f), probe) == 0.0f);
        color = pixel.Draw();
        CHECK(color.r == doctest::Approx(0.0f));
        CHECK(color.b == doctest::Approx(1.0f).epsilon(0.01));
        pixel.SetProbe(0, -1, probe);
        color = pixel.Draw();
        CHECK(color.r == doctest::Approx(0.0f));
        CHECK(color.b == doctest::Approx(1.0f).epsilon(0.01));
        CHECK(pixel.Device.ValidationErrors() == 0);
    }

    TEST_CASE("a rougher surface reads a blurrier level of the probe") {
        FProbePixel pixel;
        const FTestTexture cubes = pixel.MakeCubes(1, 4);
        // Level n is for roughness n / 4; a 4-pixel cube has three levels.
        pixel.FillCube(cubes, 0, kRed, 0);
        pixel.FillCube(cubes, 0, kGreen, 1);
        pixel.FillCube(cubes, 0, kBlue, 2);
        FReflectionProbeBox probe;
        probe.BoxMin = glm::vec3(-1.0f);
        probe.BoxMax = glm::vec3(1.0f);
        pixel.SetProbe(0, 0, probe);

        // The shader never goes below a roughness of 0.04: a sixth of the way to the next level.
        pixel.Shader->SetFloat("u_Roughness", 0.0f);
        glm::vec3 color = pixel.Draw();
        CHECK(color.r == doctest::Approx(0.84f).epsilon(0.02));
        CHECK(color.g == doctest::Approx(0.16f).epsilon(0.05));
        pixel.Shader->SetFloat("u_Roughness", 0.25f);
        color = pixel.Draw();
        CHECK(color.g == doctest::Approx(1.0f).epsilon(0.01));
        pixel.Shader->SetFloat("u_Roughness", 0.5f);
        color = pixel.Draw();
        CHECK(color.b == doctest::Approx(1.0f).epsilon(0.01));
        CHECK(color.r == doctest::Approx(0.0f));
        CHECK(pixel.Device.ValidationErrors() == 0);
    }

    TEST_CASE("two probes share a pixel by their weights, whichever is listed first") {
        FProbePixel pixel;
        const FTestTexture cubes = pixel.MakeCubes(2);
        pixel.FillCube(cubes, 0, kRed);
        pixel.FillCube(cubes, 1, kGreen);
        pixel.Shader->SetInt("u_DebugMode", kReflectionView);

        FReflectionProbeBox inside;
        inside.BoxMin = glm::vec3(-1.0f);
        inside.BoxMax = glm::vec3(1.0f);
        FReflectionProbeBox fringe = inside;
        fringe.BoxMin.x = 0.25f; // the pixel is half way through its fade
        fringe.BoxMax.x = 2.0f;

        // Both boxes hold the pixel: half each, nothing for the sky.
        pixel.SetProbe(0, 0, inside);
        pixel.SetProbe(1, 1, inside);
        glm::vec3 color = pixel.Draw();
        CHECK(color.r == doctest::Approx(0.5f).epsilon(0.01));
        CHECK(color.g == doctest::Approx(0.5f).epsilon(0.01));
        CHECK(color.b == doctest::Approx(0.0f));

        // One at full weight and one at half: two thirds and one third.
        float share0 = 0.0f;
        float share1 = 0.0f;
        float environment = 0.0f;
        BlendReflectionProbeWeights(ReflectionProbeWeight(glm::vec3(0.0f), inside),
                                    ReflectionProbeWeight(glm::vec3(0.0f), fringe), share0, share1, environment);
        CHECK(share0 == doctest::Approx(2.0f / 3.0f));
        CHECK(share1 == doctest::Approx(1.0f / 3.0f));
        CHECK(environment == 0.0f);
        pixel.SetProbe(1, 1, fringe);
        color = pixel.Draw();
        CHECK(color.r == doctest::Approx(share0).epsilon(0.01));
        CHECK(color.g == doctest::Approx(share1).epsilon(0.01));

        // The same two the other way round give the same picture.
        pixel.SetProbe(0, 1, fringe);
        pixel.SetProbe(1, 0, inside);
        const glm::vec3 swapped = pixel.Draw();
        CHECK(swapped.r == doctest::Approx(color.r).epsilon(0.001));
        CHECK(swapped.g == doctest::Approx(color.g).epsilon(0.001));

        // A probe's intensity scales what it reflects.
        fringe.Intensity = 3.0f;
        pixel.SetProbe(0, 1, fringe);
        CHECK(pixel.Draw().g == doctest::Approx(3.0f * share1).epsilon(0.01));
        CHECK(pixel.Device.ValidationErrors() == 0);
    }
}

TEST_SUITE("Reflection probe maths") {

    TEST_CASE("a ray leaves a box through the nearest face in its direction") {
        const glm::vec3 boxMin(-1.0f, -2.0f, -3.0f);
        const glm::vec3 boxMax(1.0f, 2.0f, 3.0f);
        CHECK(BoxExitDistance(glm::vec3(0.0f), {1.0f, 0.0f, 0.0f}, boxMin, boxMax) == doctest::Approx(1.0f));
        CHECK(BoxExitDistance(glm::vec3(0.0f), {0.0f, -1.0f, 0.0f}, boxMin, boxMax) == doctest::Approx(2.0f));
        CHECK(BoxExitDistance({0.5f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, boxMin, boxMax) == doctest::Approx(3.0f));
        // Along a diagonal the first face reached wins.
        const glm::vec3 diagonal = glm::normalize(glm::vec3(1.0f, 1.0f, 0.0f));
        CHECK(BoxExitDistance(glm::vec3(0.0f), diagonal, boxMin, boxMax) == doctest::Approx(std::sqrt(2.0f)));
        // Standing on a face and heading out: no distance at all, and never a negative one.
        CHECK(BoxExitDistance({1.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, boxMin, boxMax) == doctest::Approx(0.0f));
    }

    TEST_CASE("an object reads the probes of the rooms it is in, the one around its centre first") {
        std::vector<FReflectionProbeBox> probes(3);
        // Three rooms in a row along X, four metres wide each, sharing walls.
        for (int room = 0; room < 3; ++room) {
            probes[static_cast<size_t>(room)].BoxMin = {4.0f * static_cast<float>(room), 0.0f, 0.0f};
            probes[static_cast<size_t>(room)].BoxMax = {4.0f * static_cast<float>(room) + 4.0f, 3.0f, 4.0f};
        }
        int32_t chosen[kReflectionProbesPerDraw];

        // A chair in the middle of the second room touches no other room.
        SelectReflectionProbes(probes, {5.5f, 0.0f, 1.5f}, {6.5f, 1.0f, 2.5f}, chosen);
        CHECK(chosen[0] == 1);
        CHECK(chosen[1] == -1);

        // A door leaf in the wall between the first two rooms, a little more in the first.
        SelectReflectionProbes(probes, {3.6f, 0.0f, 1.0f}, {4.2f, 2.0f, 2.0f}, chosen);
        CHECK(chosen[0] == 0);
        CHECK(chosen[1] == 1);

        // A floor under all three: the room around its centre, then the lower-numbered neighbour.
        SelectReflectionProbes(probes, {0.0f, 0.0f, 0.0f}, {12.0f, 0.0f, 4.0f}, chosen);
        CHECK(chosen[0] == 1);
        CHECK(chosen[1] == 0);

        // Something far from every room reads none.
        SelectReflectionProbes(probes, {40.0f, 0.0f, 0.0f}, {41.0f, 1.0f, 1.0f}, chosen);
        CHECK(chosen[0] == -1);
        CHECK(chosen[1] == -1);

        // Just outside a room, within its fade, still reads it.
        SelectReflectionProbes(probes, {12.2f, 0.0f, 1.0f}, {12.4f, 1.0f, 2.0f}, chosen);
        CHECK(chosen[0] == 2);
        CHECK(chosen[1] == -1);
    }

    TEST_CASE("the quality presets set the size of a probe's cube") {
        CHECK(FGraphicsQuality::GetPreset(EGraphicsQuality::High).ReflectionProbeResolution == 128);
        CHECK(FGraphicsQuality::GetPreset(EGraphicsQuality::Medium).ReflectionProbeResolution == 64);
        CHECK(FGraphicsQuality::GetPreset(EGraphicsQuality::Low).ReflectionProbeResolution == 32);

        auto world = UWorld::Create("ProbeQuality");
        CHECK(world->GetPendingReflectionProbeResolution() == 128);
        FGraphicsQuality::ApplyToWorld(*world, EGraphicsQuality::Low);
        CHECK(world->GetPendingReflectionProbeResolution() == 32);
        // Choosing a quality still reads back as that quality.
        CHECK(FGraphicsQuality::InferFromWorld(*world) == EGraphicsQuality::Low);
        FGraphicsQuality::ApplyToWorld(*world, EGraphicsQuality::High);
        CHECK(world->GetPendingReflectionProbeResolution() == 128);
    }
}
