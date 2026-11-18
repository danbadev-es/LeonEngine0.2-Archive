#include <doctest/doctest.h>

#include "Assets/FTextureCompression.hpp"
#include "Assets/FTextureImporter.hpp"
#include "GPU/HeadlessDevice.hpp"

#include <stb_image_write.h>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <vector>

// Cooked textures are block compressed. The encoder is the engine's own, so these tests hold it to
// what a texture needs: flat areas exact, gradients close, each channel kept apart, and the bytes
// in the layout the GPU decodes.

namespace Leon {

    namespace {

        namespace fs = std::filesystem;

        struct FImage {
            uint32_t Width = 0;
            uint32_t Height = 0;
            std::vector<uint8_t> Pixels;
        };

        template <typename TFunction> FImage MakeImage(uint32_t InWidth, uint32_t InHeight, TFunction&& InPixel) {
            FImage image{InWidth, InHeight, std::vector<uint8_t>(static_cast<size_t>(InWidth) * InHeight * 4)};
            for (uint32_t y = 0; y < InHeight; ++y) {
                for (uint32_t x = 0; x < InWidth; ++x)
                    InPixel(x, y, &image.Pixels[(static_cast<size_t>(y) * InWidth + x) * 4]);
            }
            return image;
        }

        FImage RoundTrip(ETextureCompression InFormat, const FImage& InImage) {
            const std::vector<uint8_t> packed =
                FTextureCompression::Compress(InFormat, InImage.Pixels.data(), InImage.Width, InImage.Height);
            CHECK(packed.size() == FTextureCompression::MipByteSize(InFormat, InImage.Width, InImage.Height));
            return {InImage.Width, InImage.Height,
                    FTextureCompression::Decompress(InFormat, packed.data(), packed.size(), InImage.Width,
                                                    InImage.Height)};
        }

        /** Mean and largest absolute difference of one channel. */
        struct FError {
            double Mean = 0.0;
            int Max = 0;
        };
        FError ChannelError(const FImage& InA, const FImage& InB, int InChannel) {
            FError error;
            REQUIRE(InA.Pixels.size() == InB.Pixels.size());
            const size_t count = InA.Pixels.size() / 4;
            for (size_t i = 0; i < count; ++i) {
                const int d = std::abs(static_cast<int>(InA.Pixels[i * 4 + InChannel]) -
                                       static_cast<int>(InB.Pixels[i * 4 + InChannel]));
                error.Mean += d;
                error.Max = std::max(error.Max, d);
            }
            error.Mean /= static_cast<double>(count);
            return error;
        }

    } // namespace

    TEST_SUITE("Texture block compression") {

        TEST_CASE("sizes: half a byte or one byte a pixel, and one block for the smallest levels") {
            CHECK(FTextureCompression::MipByteSize(ETextureCompression::None, 64, 32) == 64u * 32u * 4u);
            CHECK(FTextureCompression::MipByteSize(ETextureCompression::BC1, 64, 32) == 64u * 32u / 2u);
            CHECK(FTextureCompression::MipByteSize(ETextureCompression::BC4, 64, 32) == 64u * 32u / 2u);
            CHECK(FTextureCompression::MipByteSize(ETextureCompression::BC3, 64, 32) == 64u * 32u);
            CHECK(FTextureCompression::MipByteSize(ETextureCompression::BC5, 64, 32) == 64u * 32u);
            CHECK(FTextureCompression::MipByteSize(ETextureCompression::BC1, 2, 2) == 8u);
            CHECK(FTextureCompression::MipByteSize(ETextureCompression::BC5, 1, 1) == 16u);
        }

        TEST_CASE("a flat colour survives to within what 5:6:5 can hold") {
            const FImage flat = MakeImage(8, 8, [](uint32_t, uint32_t, uint8_t* OutPixel) {
                OutPixel[0] = 200;
                OutPixel[1] = 97;
                OutPixel[2] = 31;
                OutPixel[3] = 255;
            });
            const FImage back = RoundTrip(ETextureCompression::BC1, flat);
            CHECK(ChannelError(flat, back, 0).Max <= 4);
            CHECK(ChannelError(flat, back, 1).Max <= 2);
            CHECK(ChannelError(flat, back, 2).Max <= 4);
            CHECK(back.Pixels[3] == 255);
        }

        TEST_CASE("a colour gradient stays close, and a two-colour block keeps both colours") {
            // The colours of a block lie on one line, which is what the format can hold: a fade
            // from one colour to another, as most of a real texture is.
            const FImage gradient = MakeImage(64, 64, [](uint32_t InX, uint32_t InY, uint8_t* OutPixel) {
                const uint32_t t = InX + InY;
                OutPixel[0] = static_cast<uint8_t>(20 + t);
                OutPixel[1] = static_cast<uint8_t>(200 - t);
                OutPixel[2] = static_cast<uint8_t>(60 + t / 2);
                OutPixel[3] = 255;
            });
            const FImage back = RoundTrip(ETextureCompression::BC1, gradient);
            for (int channel = 0; channel < 3; ++channel) {
                const FError error = ChannelError(gradient, back, channel);
                CHECK(error.Mean < 2.0);
                CHECK(error.Max <= 6);
            }

            // Red changing across and green changing down is a plane of colours inside every block,
            // more than two endpoints can describe. The error grows, but stays bounded.
            const FImage plane = MakeImage(64, 64, [](uint32_t InX, uint32_t InY, uint8_t* OutPixel) {
                OutPixel[0] = static_cast<uint8_t>(InX * 4);
                OutPixel[1] = static_cast<uint8_t>(InY * 4);
                OutPixel[2] = 128;
                OutPixel[3] = 255;
            });
            const FImage planeBack = RoundTrip(ETextureCompression::BC1, plane);
            CHECK(ChannelError(plane, planeBack, 0).Mean < 4.0);
            CHECK(ChannelError(plane, planeBack, 1).Mean < 4.0);

            // Black and white tiles inside each block: the two extremes, nothing in between.
            const FImage checker = MakeImage(8, 8, [](uint32_t InX, uint32_t InY, uint8_t* OutPixel) {
                const uint8_t value = ((InX / 2 + InY / 2) % 2) ? 255 : 0;
                OutPixel[0] = OutPixel[1] = OutPixel[2] = value;
                OutPixel[3] = 255;
            });
            const FImage checkerBack = RoundTrip(ETextureCompression::BC1, checker);
            CHECK(ChannelError(checker, checkerBack, 0).Max == 0);
            CHECK(ChannelError(checker, checkerBack, 1).Max == 0);
        }

        TEST_CASE("a single channel: constants exact, ramps within a step") {
            const FImage constant = MakeImage(4, 4, [](uint32_t, uint32_t, uint8_t* OutPixel) {
                OutPixel[0] = 173;
                OutPixel[1] = 9; // ignored: BC4 keeps red only
                OutPixel[2] = 9;
                OutPixel[3] = 255;
            });
            const FImage constantBack = RoundTrip(ETextureCompression::BC4, constant);
            CHECK(ChannelError(constant, constantBack, 0).Max == 0);
            CHECK(constantBack.Pixels[1] == 0);

            const FImage ramp = MakeImage(64, 64, [](uint32_t InX, uint32_t InY, uint8_t* OutPixel) {
                OutPixel[0] = static_cast<uint8_t>((InX * 3 + InY) & 0xFF);
                OutPixel[1] = OutPixel[2] = 0;
                OutPixel[3] = 255;
            });
            const FError error = ChannelError(ramp, RoundTrip(ETextureCompression::BC4, ramp), 0);
            CHECK(error.Mean < 1.5);

            // The full range in one block: both ends are reachable.
            const FImage extremes = MakeImage(4, 4, [](uint32_t InX, uint32_t, uint8_t* OutPixel) {
                OutPixel[0] = InX < 2 ? 0 : 255;
                OutPixel[1] = OutPixel[2] = 0;
                OutPixel[3] = 255;
            });
            CHECK(ChannelError(extremes, RoundTrip(ETextureCompression::BC4, extremes), 0).Max == 0);
        }

        TEST_CASE("a normal map keeps x and y apart and gets its z back") {
            const FImage normals = MakeImage(32, 32, [](uint32_t InX, uint32_t InY, uint8_t* OutPixel) {
                const float x = (static_cast<float>(InX) / 31.0f - 0.5f) * 1.2f;
                const float y = (static_cast<float>(InY) / 31.0f - 0.5f) * 0.6f;
                const float z = std::sqrt(std::max(0.0f, 1.0f - x * x - y * y));
                OutPixel[0] = static_cast<uint8_t>((x * 0.5f + 0.5f) * 255.0f + 0.5f);
                OutPixel[1] = static_cast<uint8_t>((y * 0.5f + 0.5f) * 255.0f + 0.5f);
                OutPixel[2] = static_cast<uint8_t>((z * 0.5f + 0.5f) * 255.0f + 0.5f);
                OutPixel[3] = 255;
            });
            const FImage back = RoundTrip(ETextureCompression::BC5, normals);
            CHECK(ChannelError(normals, back, 0).Mean < 1.0);
            CHECK(ChannelError(normals, back, 1).Mean < 1.0);
            // Blue was never stored: it is rebuilt from the other two.
            CHECK(ChannelError(normals, back, 2).Max <= 3);
        }

        TEST_CASE("alpha travels beside the colour without disturbing it") {
            const FImage image = MakeImage(16, 16, [](uint32_t InX, uint32_t InY, uint8_t* OutPixel) {
                OutPixel[0] = 220;
                OutPixel[1] = 40;
                OutPixel[2] = 40;
                OutPixel[3] = static_cast<uint8_t>(InX * 16 + InY);
            });
            const FImage back = RoundTrip(ETextureCompression::BC3, image);
            CHECK(ChannelError(image, back, 3).Mean < 2.0);
            CHECK(ChannelError(image, back, 0).Max <= 4);
        }

        TEST_CASE("levels smaller than a block decode to their own size") {
            const FImage tiny = MakeImage(2, 1, [](uint32_t InX, uint32_t, uint8_t* OutPixel) {
                OutPixel[0] = InX ? 255 : 0;
                OutPixel[1] = OutPixel[2] = 0;
                OutPixel[3] = 255;
            });
            const FImage back = RoundTrip(ETextureCompression::BC4, tiny);
            REQUIRE(back.Pixels.size() == 2u * 1u * 4u);
            CHECK(back.Pixels[0] == 0);
            CHECK(back.Pixels[4] == 255);
        }

        TEST_CASE("the importer picks the format from what the texture is for") {
            const fs::path folder = fs::path("build") / "TextureCompressionTest";
            std::error_code ec;
            fs::create_directories(folder, ec);
            const FImage picture = MakeImage(16, 16, [](uint32_t InX, uint32_t InY, uint8_t* OutPixel) {
                OutPixel[0] = static_cast<uint8_t>(40 + (InX + InY) * 6);
                OutPixel[1] = static_cast<uint8_t>(220 - (InX + InY) * 5);
                OutPixel[2] = 128;
                OutPixel[3] = 255;
            });
            auto importAs = [&](const char* InName, const FImage& InImage, FNativeTextureData& OutTexture) {
                const std::string path = (folder / InName).string();
                REQUIRE(stbi_write_png(path.c_str(), static_cast<int>(InImage.Width), static_cast<int>(InImage.Height),
                                       4, InImage.Pixels.data(), static_cast<int>(InImage.Width) * 4) != 0);
                REQUIRE(FTextureImporter::Import(path, FTextureImportSettings::DetectFromFileName(InName), OutTexture));
            };

            FNativeTextureData color, normal, rough, orm, generic;
            importAs("T_Test_Color.png", picture, color);
            importAs("T_Test_Normal.png", picture, normal);
            importAs("T_Test_Roughness.png", picture, rough);
            importAs("T_Test_ORM.png", picture, orm);
            importAs("T_Test.png", picture, generic);
            CHECK(color.GetCompression() == ETextureCompression::BC1);
            CHECK(normal.GetCompression() == ETextureCompression::BC5);
            CHECK(rough.GetCompression() == ETextureCompression::BC4);
            CHECK(orm.GetCompression() == ETextureCompression::BC1);
            CHECK(orm.Header.ColorSpace == static_cast<uint32_t>(ETextureColorSpace::Linear));
            CHECK(generic.GetCompression() == ETextureCompression::None);

            // Colour with real transparency needs the format that has alpha.
            FImage cutout = picture;
            cutout.Pixels[3] = 0;
            FNativeTextureData withAlpha;
            importAs("T_Cutout_Color.png", cutout, withAlpha);
            CHECK(withAlpha.GetCompression() == ETextureCompression::BC3);

            // A size that is not a multiple of four cannot be cut into blocks.
            const FImage odd = MakeImage(10, 6, [](uint32_t, uint32_t, uint8_t* OutPixel) {
                OutPixel[0] = OutPixel[1] = OutPixel[2] = 90;
                OutPixel[3] = 255;
            });
            FNativeTextureData oddTexture;
            importAs("T_Odd_Color.png", odd, oddTexture);
            CHECK(oddTexture.GetCompression() == ETextureCompression::None);

            // The whole mip chain is stored, written and read back, and decodes to the picture.
            CHECK(color.Mips.size() == 5);
            const std::string cooked = (folder / "T_Test_Color.ltex").string();
            REQUIRE(color.SaveToFile(cooked));
            FNativeTextureData loaded;
            REQUIRE(loaded.LoadFromFile(cooked));
            CHECK(loaded.GetCompression() == ETextureCompression::BC1);
            CHECK(loaded.Mips[0].Pixels.size() == 16u * 16u / 2u);
            CHECK(loaded.Mips[4].Pixels.size() == 8u);
            REQUIRE(loaded.DecodeToRGBA8());
            CHECK(loaded.GetCompression() == ETextureCompression::None);
            const FImage decoded{16, 16, loaded.Mips[0].Pixels};
            CHECK(ChannelError(picture, decoded, 0).Mean < 3.0);
            CHECK(ChannelError(picture, decoded, 1).Mean < 3.0);

            fs::remove_all(folder, ec);
        }

        TEST_CASE("the GPU reads the blocks as the same picture") {
            TestGPU::FHeadlessDevice gl;
            REQUIRE(gl.IsValid());
            auto shader = TestGPU::FTestShader::Create("PBR_Lit");
            shader->Bind();
            gl.BindDefaultTextures();
            gl.BindFramebuffer(1, 1);

            TestGPU::FCameraBufferData camera;
            camera.ViewProjection = glm::mat4(1.0f);
            camera.CameraPosition = glm::vec4(0.0f, 0.0f, 1.0f, 0.0f);
            camera.CameraForward = glm::vec4(0.0f, 0.0f, -1.0f, 0.0f);
            gl.UpdateCameraUBO(camera);
            TestGPU::FLightingBufferData lights;
            lights.DirLight.Direction = glm::vec4(0.0f, 0.0f, -1.0f, 0.0f);
            lights.DirLight.Color = glm::vec4(0.0f);
            lights.LightCounts = glm::ivec4(0);
            lights.EnvSkyColor = lights.EnvHorizonColor = lights.EnvGroundColor = glm::vec4(0.0f);
            gl.UpdateLightingUBO(lights);

            TestGPU::SetMat4(shader, "u_Model", glm::mat4(1.0f));
            TestGPU::SetMat3(shader, "u_NormalMatrix", glm::mat3(1.0f));
            for (const char* off : {"u_UseIBL", "u_UseShadows", "u_UseNormalMap", "u_UseAlbedoMap", "u_UseMetallicMap",
                                    "u_UseRoughnessMap", "u_UseAOMap", "u_UsePlanarReflection", "u_AlphaMode",
                                    "u_DebugMode"})
                shader->SetInt(off, 0);
            shader->SetFloat("u_AlphaCutoff", 0.5f);
            TestGPU::SetFloat2(shader, "u_UVTiling", glm::vec2(1.0f));
            TestGPU::SetFloat2(shader, "u_UVOffset", glm::vec2(0.0f));
            TestGPU::SetFloat3(shader, "u_AlbedoColor", glm::vec3(0.0f));
            shader->SetFloat("u_Metallic", 0.0f);
            shader->SetFloat("u_Roughness", 0.5f);
            shader->SetFloat("u_AO", 1.0f);

            // In the dark the pixel is the emissive map alone: what the sampler returned.
            shader->SetInt("u_UseEmissiveMap", 1);
            TestGPU::SetFloat3(shader, "u_EmissiveColor", glm::vec3(1.0f));
            shader->SetFloat("u_EmissiveIntensity", 1.0f);

            const FImage flat = MakeImage(4, 4, [](uint32_t, uint32_t, uint8_t* OutPixel) {
                OutPixel[0] = 255;
                OutPixel[1] = 128;
                OutPixel[2] = 0;
                OutPixel[3] = 255;
            });

            SUBCASE("BC1, linear") {
                const std::vector<uint8_t> blocks =
                    FTextureCompression::Compress(ETextureCompression::BC1, flat.Pixels.data(), 4, 4);
                gl.BindTexture(9, gl.Create2D(4, 4, ERHIFormat::BC1_UNorm, blocks.data()));
                gl.DrawQuad();
                const glm::vec4 pixel = gl.ReadPixel();
                CHECK(pixel.r == doctest::Approx(1.0f).epsilon(0.02));
                CHECK(pixel.g == doctest::Approx(128.0f / 255.0f).epsilon(0.03));
                CHECK(pixel.b == doctest::Approx(0.0f).epsilon(0.02));
            }
            SUBCASE("BC1, sRGB: the GPU converts to linear") {
                const std::vector<uint8_t> blocks =
                    FTextureCompression::Compress(ETextureCompression::BC1, flat.Pixels.data(), 4, 4);
                gl.BindTexture(9, gl.Create2D(4, 4, ERHIFormat::BC1_UNorm_sRGB, blocks.data()));
                gl.DrawQuad();
                const glm::vec4 pixel = gl.ReadPixel();
                CHECK(pixel.r == doctest::Approx(1.0f).epsilon(0.02));
                CHECK(pixel.g == doctest::Approx(0.2158f).epsilon(0.06)); // 128 / 255 as sRGB
            }
            SUBCASE("BC4: one channel, in red") {
                const std::vector<uint8_t> blocks =
                    FTextureCompression::Compress(ETextureCompression::BC4, flat.Pixels.data(), 4, 4);
                gl.BindTexture(9, gl.Create2D(4, 4, ERHIFormat::BC4_UNorm, blocks.data()));
                gl.DrawQuad();
                const glm::vec4 pixel = gl.ReadPixel();
                CHECK(pixel.r == doctest::Approx(1.0f).epsilon(0.01));
                CHECK(pixel.g == doctest::Approx(0.0f).epsilon(0.01));
            }
            SUBCASE("BC5: red and green") {
                const std::vector<uint8_t> blocks =
                    FTextureCompression::Compress(ETextureCompression::BC5, flat.Pixels.data(), 4, 4);
                gl.BindTexture(9, gl.Create2D(4, 4, ERHIFormat::BC5_UNorm, blocks.data()));
                gl.DrawQuad();
                const glm::vec4 pixel = gl.ReadPixel();
                CHECK(pixel.r == doctest::Approx(1.0f).epsilon(0.01));
                CHECK(pixel.g == doctest::Approx(128.0f / 255.0f).epsilon(0.01));
                CHECK(pixel.b == doctest::Approx(0.0f).epsilon(0.01));
            }
            SUBCASE("BC3: a picture with two halves keeps its left and its right") {
                // 8 x 4: a red block beside a blue one. Reading the wrong block order would swap them.
                const FImage halves = MakeImage(8, 4, [](uint32_t InX, uint32_t, uint8_t* OutPixel) {
                    OutPixel[0] = InX < 4 ? 255 : 0;
                    OutPixel[1] = 0;
                    OutPixel[2] = InX < 4 ? 0 : 255;
                    OutPixel[3] = 255;
                });
                const std::vector<uint8_t> blocks =
                    FTextureCompression::Compress(ETextureCompression::BC3, halves.Pixels.data(), 8, 4);
                gl.BindTexture(9, gl.Create2D(8, 4, ERHIFormat::BC3_UNorm, blocks.data()));
                gl.BindFramebuffer(2, 1);
                gl.DrawQuad();
                const std::vector<float> pixels = gl.ReadPixels();
                REQUIRE(pixels.size() >= 8);
                CHECK(pixels[0] == doctest::Approx(1.0f).epsilon(0.02)); // left: red
                CHECK(pixels[2] == doctest::Approx(0.0f).epsilon(0.02));
                CHECK(pixels[4] == doctest::Approx(0.0f).epsilon(0.02)); // right: blue
                CHECK(pixels[6] == doctest::Approx(1.0f).epsilon(0.02));
            }
        }
    }

} // namespace Leon
