#include <doctest/doctest.h>

#include "GPU/HeadlessDevice.hpp"
#include "RenderCore/FColorSpace.hpp"
#include "RenderCore/FPostProcessConstants.hpp"

#include <cmath>
#include <cstring>
#include <vector>

// The look laid over the finished image: vignette, colour grading and chromatic aberration in the tone
// mapping pass, film grain in the pass after anti-aliasing. Each is defined by what it does to known
// pixels, and a neutral look must leave the image exactly as it is without one.

namespace {

    using Leon::TestGPU::FHeadlessDevice;
    using Leon::TestGPU::FTestShader;
    using Leon::TestGPU::FTestTexture;

    /** The tone mapping shader set up to do nothing but encode: clamp operator, exposure 1, no bloom. */
    Leon::TRef<FTestShader> MakeEncodingShader() {
        auto shader = FTestShader::Create("ToneMapping");
        REQUIRE(shader != nullptr);
        shader->Bind();
        shader->SetInt("u_ToneMapper", 2);
        shader->SetFloat("u_Exposure", 1.0f);
        shader->SetFloat("u_Gamma", 2.2f);
        shader->SetInt("u_UseBloom", 0);
        shader->SetInt("u_DebugMode", 0);
        return shader;
    }

    /** Draws a scene of one colour, given as the encoded colour the plain image would show, and reads it back. */
    glm::vec4 DrawEncoded(FHeadlessDevice& InDevice, const glm::vec3& InEncoded) {
        const glm::vec3 linear = Leon::SRGBToLinear(InEncoded);
        const FTestTexture scene = InDevice.Create1x1FloatTexture(linear.r, linear.g, linear.b, 1.0f);
        InDevice.BindTexture(0, scene);
        InDevice.DrawQuad();
        return InDevice.ReadPixel(0, 0);
    }

    void CheckColor(const glm::vec4& InPixel, const glm::vec3& InExpected) {
        CHECK(InPixel.r == doctest::Approx(InExpected.r).epsilon(0.01).scale(1.0));
        CHECK(InPixel.g == doctest::Approx(InExpected.g).epsilon(0.01).scale(1.0));
        CHECK(InPixel.b == doctest::Approx(InExpected.b).epsilon(0.01).scale(1.0));
    }

} // namespace

TEST_SUITE("Shader GPU - Post-Processing Look") {

    TEST_CASE("a neutral look is not in the constants at all") {
        Leon::FPostProcessSettings settings;
        const Leon::FToneMappingConstants plain = Leon::MakeToneMappingConstants(settings, 1.3f, true);
        CHECK(plain.ToneParams == glm::vec4(1.3f, settings.BloomIntensity, settings.Gamma, 0.0f));
        CHECK(plain.ToneFlags == glm::ivec4(0, 1, 0, 0));
        CHECK(plain.Vignette.x == 0.0f);
        const Leon::FFXAAConstants plainAA = Leon::MakeFXAAConstants(settings, {0.5f, 0.25f}, true, 77);
        CHECK(plainAA.FXAAParams == glm::vec4(0.5f, 0.25f, 1.0f, 0.0f));
        CHECK(plainAA.GrainParams == glm::vec4(0.0f));

        // A look is for the finished image: the debug views and "post-processing off" do not get it.
        settings.Look.VignetteIntensity = 0.5f;
        settings.Look.Saturation = 0.0f;
        settings.Look.ChromaticAberration = 0.01f;
        settings.Look.FilmGrainIntensity = 0.1f;
        CHECK(Leon::MakeToneMappingConstants(settings, 1.3f, true).ToneFlags.w == 1);
        CHECK(Leon::MakeFXAAConstants(settings, {0.5f, 0.25f}, true, 77).GrainParams.x == 0.1f);
        for (int off = 0; off < 2; ++off) {
            Leon::FPostProcessSettings without = settings;
            if (off == 0)
                without.bEnabled = false;
            else
                without.DebugMode = 1;
            const Leon::FToneMappingConstants tone = Leon::MakeToneMappingConstants(without, 1.3f, true);
            CHECK(tone.ToneParams.w == 0.0f);
            CHECK(tone.ToneFlags.w == 0);
            CHECK(tone.Vignette.x == 0.0f);
            CHECK(Leon::MakeFXAAConstants(without, {0.5f, 0.25f}, true, 77).GrainParams.x == 0.0f);
        }

        // Animated grain follows the frame; still grain does not.
        CHECK(Leon::MakeFXAAConstants(settings, {1.0f, 1.0f}, true, 5).GrainParams.y == 5.0f);
        settings.Look.FilmGrainSeed = 100;
        CHECK(Leon::MakeFXAAConstants(settings, {1.0f, 1.0f}, true, 5).GrainParams.y == 105.0f);
        settings.Look.bFilmGrainAnimated = false;
        CHECK(Leon::MakeFXAAConstants(settings, {1.0f, 1.0f}, true, 5).GrainParams.y == 100.0f);
    }

    TEST_CASE("a neutral look gives the pixels tone mapping alone gives") {
        FHeadlessDevice gl;
        REQUIRE(gl.IsValid());
        auto shader = FTestShader::Create("ToneMapping");
        REQUIRE(shader != nullptr);
        shader->Bind();
        gl.BindDefaultTextures();
        gl.BindFramebuffer(4, 4);
        shader->SetInt("u_ToneMapper", 0); // ACES
        shader->SetFloat("u_Exposure", 1.0f);
        shader->SetFloat("u_Gamma", 2.2f);
        shader->SetInt("u_UseBloom", 0);
        shader->SetInt("u_DebugMode", 0);

        // Sixteen different colours, from black to well above white.
        std::vector<float> texels;
        for (int i = 0; i < 16; ++i) {
            const float level = static_cast<float>(i * i) * 0.02f;
            texels.insert(texels.end(), {level, level * 0.5f, 0.18f, 1.0f});
        }
        const FTestTexture scene = gl.Create2D(4, 4, Leon::ERHIFormat::RGBA32_Float, texels.data());
        gl.BindTexture(0, scene);
        gl.DrawQuad();
        const std::vector<float> plain = gl.ReadPixels();
        REQUIRE(plain.size() == 4 * 4 * 4);

        // What the shader computed before it knew about looks: ACES, then sRGB, luma in alpha.
        const auto aces = [](float x) {
            return glm::clamp((x * (2.51f * x + 0.03f)) / (x * (2.43f * x + 0.59f) + 0.14f), 0.0f, 1.0f);
        };
        for (int i = 0; i < 16; ++i) {
            const glm::vec3 expected(Leon::LinearToSRGB(aces(texels[i * 4])), Leon::LinearToSRGB(aces(texels[i * 4 + 1])),
                                     Leon::LinearToSRGB(aces(texels[i * 4 + 2])));
            CAPTURE(i);
            CHECK(plain[i * 4] == doctest::Approx(expected.r).epsilon(0.002).scale(1.0));
            CHECK(plain[i * 4 + 1] == doctest::Approx(expected.g).epsilon(0.002).scale(1.0));
            CHECK(plain[i * 4 + 2] == doctest::Approx(expected.b).epsilon(0.002).scale(1.0));
            CHECK(plain[i * 4 + 3] ==
                  doctest::Approx(glm::dot(expected, glm::vec3(0.299f, 0.587f, 0.114f))).epsilon(0.002).scale(1.0));
        }

        // Every part of the look spelled out at its neutral value: the same bits.
        shader->SetFloat("u_VignetteIntensity", 0.0f);
        shader->SetFloat("u_VignetteRadius", 0.2f);
        shader->SetFloat("u_VignetteSoftness", 0.1f);
        shader->SetFloat3("u_Lift", 0.0f, 0.0f, 0.0f);
        shader->SetFloat3("u_GradeGamma", 1.0f, 1.0f, 1.0f);
        shader->SetFloat3("u_Gain", 1.0f, 1.0f, 1.0f);
        shader->SetFloat("u_Contrast", 1.0f);
        shader->SetFloat("u_Saturation", 1.0f);
        shader->SetFloat("u_ChromaticAberration", 0.0f);
        gl.DrawQuad();
        const std::vector<float> neutral = gl.ReadPixels();
        REQUIRE(neutral.size() == plain.size());
        CHECK(std::memcmp(neutral.data(), plain.data(), plain.size() * sizeof(float)) == 0);
        CHECK(gl.ValidationErrors() == 0);
    }

    TEST_CASE("the vignette darkens the corners and leaves the centre alone") {
        FHeadlessDevice gl;
        REQUIRE(gl.IsValid());
        auto shader = MakeEncodingShader();
        gl.BindDefaultTextures();
        gl.BindFramebuffer(9, 9);
        const FTestTexture scene = gl.Create1x1FloatTexture(0.5f, 0.5f, 0.5f, 1.0f);
        gl.BindTexture(0, scene);

        gl.DrawQuad();
        const glm::vec4 plain = gl.ReadPixel(4, 4);

        // Full strength from 0.7 of the way to the corners; nothing inside 0.3.
        shader->SetFloat("u_VignetteIntensity", 0.6f);
        shader->SetFloat("u_VignetteRadius", 0.3f);
        shader->SetFloat("u_VignetteSoftness", 0.4f);
        gl.DrawQuad();
        const glm::vec4 centre = gl.ReadPixel(4, 4);
        CHECK(centre.r == plain.r);
        CHECK(centre.a == plain.a);
        for (const glm::ivec2 corner : {glm::ivec2(0, 0), glm::ivec2(8, 0), glm::ivec2(0, 8), glm::ivec2(8, 8)}) {
            const glm::vec4 pixel = gl.ReadPixel(corner.x, corner.y);
            CHECK(pixel.r == doctest::Approx(plain.r * 0.4f).epsilon(0.01));
            // The luma anti-aliasing reads is the luma of what is shown.
            CHECK(pixel.a == doctest::Approx(plain.a * 0.4f).epsilon(0.01));
        }
        // Darker and darker along the diagonal, and the same on all four sides.
        float previous = centre.r;
        for (int step = 1; step <= 4; ++step) {
            const float value = gl.ReadPixel(4 + step, 4 + step).r;
            CHECK(value <= previous);
            previous = value;
        }
        CHECK(gl.ReadPixel(4, 0).r == doctest::Approx(gl.ReadPixel(4, 8).r).epsilon(0.001));
        CHECK(gl.ReadPixel(0, 4).r == doctest::Approx(gl.ReadPixel(8, 4).r).epsilon(0.001));
        CHECK(gl.ReadPixel(4, 0).r < centre.r);
        CHECK(gl.ReadPixel(4, 0).r > gl.ReadPixel(0, 0).r);
        CHECK(gl.ValidationErrors() == 0);
    }

    TEST_CASE("saturation 0 gives grey, and contrast moves colours away from mid grey") {
        FHeadlessDevice gl;
        REQUIRE(gl.IsValid());
        auto shader = MakeEncodingShader();
        gl.BindDefaultTextures();
        gl.BindFramebuffer(1, 1);

        const glm::vec3 color(0.8f, 0.3f, 0.1f);
        CheckColor(DrawEncoded(gl, color), color);

        shader->SetFloat("u_Saturation", 0.0f);
        const float grey = glm::dot(color, glm::vec3(0.2126f, 0.7152f, 0.0722f));
        const glm::vec4 desaturated = DrawEncoded(gl, color);
        CheckColor(desaturated, glm::vec3(grey));
        CHECK(desaturated.r == doctest::Approx(desaturated.g).epsilon(0.0001));
        CHECK(desaturated.g == doctest::Approx(desaturated.b).epsilon(0.0001));

        // Half way to grey, and beyond the colour.
        shader->SetFloat("u_Saturation", 0.5f);
        CheckColor(DrawEncoded(gl, color), glm::mix(glm::vec3(grey), color, 0.5f));
        shader->SetFloat("u_Saturation", 1.0f);

        shader->SetFloat("u_Contrast", 1.5f);
        CheckColor(DrawEncoded(gl, glm::vec3(0.5f)), glm::vec3(0.5f));
        CheckColor(DrawEncoded(gl, glm::vec3(0.7f, 0.3f, 0.5f)), glm::vec3(0.8f, 0.2f, 0.5f));
        // Never outside what a display can show.
        CheckColor(DrawEncoded(gl, glm::vec3(0.95f, 0.05f, 0.5f)), glm::vec3(1.0f, 0.0f, 0.5f));
        CHECK(gl.ValidationErrors() == 0);
    }

    TEST_CASE("lift moves black, gain moves white and gamma moves the middle") {
        FHeadlessDevice gl;
        REQUIRE(gl.IsValid());
        auto shader = MakeEncodingShader();
        gl.BindDefaultTextures();
        gl.BindFramebuffer(1, 1);
        const glm::vec3 black(0.0f);
        const glm::vec3 middle(0.5f);
        const glm::vec3 white(1.0f);

        // Lift: black becomes the lift colour, white stays, the middle goes half way.
        shader->SetFloat3("u_Lift", 0.1f, 0.2f, 0.0f);
        CheckColor(DrawEncoded(gl, black), {0.1f, 0.2f, 0.0f});
        CheckColor(DrawEncoded(gl, white), white);
        CheckColor(DrawEncoded(gl, middle), {0.55f, 0.6f, 0.5f});
        shader->SetFloat3("u_Lift", 0.0f, 0.0f, 0.0f);

        // Gain: a multiplication; black stays.
        shader->SetFloat3("u_Gain", 0.8f, 1.0f, 0.5f);
        CheckColor(DrawEncoded(gl, black), black);
        CheckColor(DrawEncoded(gl, white), {0.8f, 1.0f, 0.5f});
        CheckColor(DrawEncoded(gl, middle), {0.4f, 0.5f, 0.25f});
        shader->SetFloat3("u_Gain", 1.0f, 1.0f, 1.0f);

        // Gamma: black and white stay; above 1 the middle brightens, below 1 it darkens.
        shader->SetFloat3("u_GradeGamma", 2.0f, 1.0f, 0.5f);
        CheckColor(DrawEncoded(gl, black), black);
        CheckColor(DrawEncoded(gl, white), white);
        CheckColor(DrawEncoded(gl, middle), {std::sqrt(0.5f), 0.5f, 0.25f});

        // Together, in that order: gain * (c + lift * (1 - c)), then the power.
        shader->SetFloat3("u_Lift", 0.2f, 0.2f, 0.2f);
        shader->SetFloat3("u_Gain", 0.5f, 0.5f, 0.5f);
        shader->SetFloat3("u_GradeGamma", 2.0f, 2.0f, 2.0f);
        CheckColor(DrawEncoded(gl, middle), glm::vec3(std::sqrt(0.5f * (0.5f + 0.2f * 0.5f))));
        CHECK(gl.ValidationErrors() == 0);
    }

    TEST_CASE("chromatic aberration leaves the centre alone and pulls the channels apart at the edge") {
        FHeadlessDevice gl;
        REQUIRE(gl.IsValid());
        auto shader = MakeEncodingShader();
        gl.BindDefaultTextures();
        gl.BindFramebuffer(9, 9);

        // Black, with a white line down the middle and one near the left edge.
        std::vector<float> texels(9 * 9 * 4, 0.0f);
        for (int y = 0; y < 9; ++y) {
            for (int x : {1, 4}) {
                for (int channel = 0; channel < 4; ++channel)
                    texels[(y * 9 + x) * 4 + channel] = 1.0f;
            }
        }
        const FTestTexture scene = gl.Create2D(9, 9, Leon::ERHIFormat::RGBA32_Float, texels.data());
        gl.BindTexture(0, scene);
        gl.DrawQuad();
        const std::vector<float> plain = gl.ReadPixels();

        // A third: at the second column from the edge, exactly one pixel of shift.
        shader->SetFloat("u_ChromaticAberration", 1.0f / 3.0f);
        gl.DrawQuad();
        const std::vector<float> shifted = gl.ReadPixels();
        REQUIRE(shifted.size() == plain.size());
        const auto at = [](const std::vector<float>& InPixels, int InX, int InY) {
            return glm::vec4(InPixels[(InY * 9 + InX) * 4], InPixels[(InY * 9 + InX) * 4 + 1],
                             InPixels[(InY * 9 + InX) * 4 + 2], InPixels[(InY * 9 + InX) * 4 + 3]);
        };

        // The centre pixel reads all three channels from where it always did.
        CHECK(at(shifted, 4, 4) == at(plain, 4, 4));
        CHECK(at(shifted, 4, 4).r > 0.99f);

        // On the line near the edge green stays; red and blue come from the black on either side.
        const glm::vec4 line = at(shifted, 1, 4);
        CHECK(line.g > 0.99f);
        CHECK(line.r < 0.01f);
        CHECK(line.b < 0.01f);
        // Its red went inwards and its blue outwards: the pixels next to it are tinted.
        const glm::vec4 inner = at(shifted, 2, 4);
        CHECK(inner.g < 0.01f);
        CHECK(inner.r > 0.5f);
        CHECK(inner.b < 0.01f);
        const glm::vec4 outer = at(shifted, 0, 4);
        CHECK(outer.g < 0.01f);
        CHECK(outer.r < 0.01f);
        CHECK(outer.b > 0.5f);
        // Radial: along the column through the centre nothing moves sideways.
        CHECK(at(shifted, 4, 0).r == doctest::Approx(at(plain, 4, 0).r).epsilon(0.001));
        CHECK(at(shifted, 4, 0).b == doctest::Approx(at(plain, 4, 0).b).epsilon(0.001));
        CHECK(gl.ValidationErrors() == 0);
    }

    TEST_CASE("film grain is exact at zero, repeatable with a fixed seed and does not change the brightness") {
        FHeadlessDevice gl;
        REQUIRE(gl.IsValid());
        auto shader = FTestShader::Create("FXAA");
        REQUIRE(shader != nullptr);
        shader->Bind();
        gl.BindDefaultTextures();
        constexpr int kSize = 64;
        gl.BindFramebuffer(kSize, kSize);
        const FTestTexture image = gl.Create1x1FloatTexture(0.5f, 0.25f, 0.75f, 0.5f);
        gl.BindTexture(0, image);
        shader->SetFloat2("u_InverseScreenSize", 1.0f / kSize, 1.0f / kSize);

        const auto draw = [&] {
            gl.DrawQuad();
            return gl.ReadPixels();
        };
        for (int antiAlias = 0; antiAlias < 2; ++antiAlias) {
            shader->SetInt("u_FXAAEnabled", antiAlias);
            shader->SetFloat("u_FilmGrainIntensity", 0.0f);
            const std::vector<float> clean = draw();
            REQUIRE(clean.size() == kSize * kSize * 4);
            for (size_t i = 0; i < clean.size(); i += 4) {
                REQUIRE(clean[i] == 0.5f);
                REQUIRE(clean[i + 1] == 0.25f);
                REQUIRE(clean[i + 2] == 0.75f);
            }

            // A still pattern: the frame count does not move it, and drawing again gives the same bits.
            shader->SetFloat("u_FilmGrainIntensity", 0.1f);
            shader->SetInt("u_FilmGrainAnimated", 0);
            shader->SetInt("u_FilmGrainSeed", 7);
            shader->SetInt("u_FrameIndex", 0);
            const std::vector<float> grain = draw();
            CHECK(draw() == grain);
            shader->SetInt("u_FrameIndex", 31);
            CHECK(draw() == grain);

            double sum = 0.0;
            float lowest = 1.0f;
            float highest = 0.0f;
            size_t changed = 0;
            for (size_t i = 0; i < grain.size(); i += 4) {
                const float offset = grain[i] - 0.5f;
                sum += offset;
                lowest = std::min(lowest, offset);
                highest = std::max(highest, offset);
                changed += offset != 0.0f ? 1 : 0;
                // Brightness only: the three channels move together.
                REQUIRE(grain[i + 1] - 0.25f == doctest::Approx(offset).epsilon(0.001).scale(1.0));
                REQUIRE(grain[i + 2] - 0.75f == doctest::Approx(offset).epsilon(0.001).scale(1.0));
            }
            const double mean = sum / (kSize * kSize);
            CAPTURE(mean);
            // Amplitude 0.1 means -0.05 .. 0.05, and over 4096 pixels the mean is zero to a thousandth.
            CHECK(std::abs(mean) < 0.002);
            CHECK(lowest >= -0.0501f);
            CHECK(highest <= 0.0501f);
            CHECK(lowest < -0.04f);
            CHECK(highest > 0.04f);
            CHECK(changed > grain.size() / 4 * 99 / 100);

            // Another seed is another pattern; so is another frame when the grain is animated.
            shader->SetInt("u_FilmGrainSeed", 8);
            CHECK(draw() != grain);
            shader->SetInt("u_FilmGrainSeed", 7);
            shader->SetInt("u_FilmGrainAnimated", 1);
            shader->SetInt("u_FrameIndex", 0);
            const std::vector<float> first = draw();
            CHECK(first == grain);
            shader->SetInt("u_FrameIndex", 1);
            const std::vector<float> second = draw();
            CHECK(second != first);
            // Deterministic all the same: the same frame of the same seed is the same picture.
            CHECK(draw() == second);
        }
        CHECK(gl.ValidationErrors() == 0);
    }
}
