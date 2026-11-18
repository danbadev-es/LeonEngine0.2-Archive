#include <doctest/doctest.h>

#include "Assets/FAnimTypes.hpp"
#include "RenderCore/FShaderConstants.hpp"
#include "RenderCore/FShaderLibrary.hpp"
#include "ShaderCompiler/FShaderCompiler.hpp"

#include <glm/glm.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

// Every constant buffer an engine shader declares is filled from a C++ structure. Nothing ties the
// two together but care: add a field on one side only and the shader reads garbage, with no error.
// These tests compile each shader (no GPU needed) and hold the size the compiler reports for each
// buffer against the size of the structure that fills it.

namespace Leon {

    namespace {

        struct FExpectedBuffer {
            const char* Name;
            size_t Size;
            /**
             * False where the shader declares only the first fields of a larger structure (it then
             * needs no more bytes than the structure has, but may need fewer).
             */
            bool bWhole = true;
        };

        struct FShaderCase {
            const char* File;
            const char* EntryPoint;
            ERHIShaderStage Stage;
            FShaderLibrary::FDefines Defines;
            std::vector<FExpectedBuffer> Buffers;
        };

        size_t Padded(size_t InSize) {
            return (InSize + 15) / 16 * 16;
        }

        void CheckShader(const FShaderCase& InCase) {
            FShaderCompileInput input;
            input.SourcePath = std::string("Engine/Shaders/") + InCase.File;
            input.EntryPoint = InCase.EntryPoint;
            input.Stage = InCase.Stage;
            input.Defines = InCase.Defines;
            FShaderCompileOutput output;
            const std::string label = std::string(InCase.File) + ":" + InCase.EntryPoint;
            REQUIRE_MESSAGE(FShaderCompiler::Compile(input, output), label << "\n" << output.Messages);

            size_t matched = 0;
            for (const FShaderConstantBuffer& buffer : output.ConstantBuffers) {
                const FExpectedBuffer* expected = nullptr;
                for (const FExpectedBuffer& candidate : InCase.Buffers) {
                    if (buffer.Name == candidate.Name)
                        expected = &candidate;
                }
                // A buffer the shader uses and the table does not know is a structure nobody checks.
                REQUIRE_MESSAGE(expected != nullptr, label << " uses constant buffer '" << buffer.Name << "', which this test does not cover");
                ++matched;
                INFO(label << " / " << buffer.Name << ": shader " << buffer.Size << " bytes, C++ " << expected->Size);
                if (expected->bWhole)
                    CHECK(buffer.Size == Padded(expected->Size));
                else
                    CHECK(buffer.Size <= Padded(expected->Size));
            }
            // An entry point that uses none of its buffers would make this test vacuous.
            CHECK_MESSAGE(matched > 0, label << " uses no constant buffer");
        }

        const FExpectedBuffer kFrame{"FrameConstants", sizeof(FFrameConstants)};
        const FExpectedBuffer kLitPass{"PassConstants", sizeof(FLitPassConstants)};
        const FExpectedBuffer kMaterial{"MaterialConstants", sizeof(FMaterialConstants)};
        const FExpectedBuffer kDraw{"DrawConstants", sizeof(FDrawConstants)};
        const FExpectedBuffer kDrawStart{"DrawConstants", sizeof(FDrawConstants), false};
        const FExpectedBuffer kBones{"BonePalette", sizeof(glm::mat4) * kMaxBones};

    } // namespace

    TEST_SUITE("Shader constant buffers match their C++ structures") {

        TEST_CASE("the surface shader, in its three variants") {
            CheckShader({"PBR_Lit.hlsl", "VSMain", ERHIShaderStage::Vertex, {}, {kFrame, kLitPass, kMaterial, kDraw}});
            CheckShader({"PBR_Lit.hlsl", "PSMain", ERHIShaderStage::Pixel, {}, {kFrame, kLitPass, kMaterial, kDraw}});
            CheckShader({"PBR_Lit.hlsl", "VSMain", ERHIShaderStage::Vertex, {{"SKINNED", "1"}},
                         {kFrame, kLitPass, kMaterial, kDraw, kBones}});
            CheckShader({"PBR_Lit.hlsl", "VSMain", ERHIShaderStage::Vertex, {{"INSTANCED", "1"}},
                         {kFrame, kLitPass, kMaterial, kDraw, {"InstanceConstants", sizeof(FInstanceConstants)}}});
        }

        TEST_CASE("shadow depth and outlines") {
            const std::vector<FExpectedBuffer> shadow = {{"PassConstants", sizeof(FShadowPassConstants)},
                                                         {"MaterialConstants", sizeof(FShadowMaterialConstants)},
                                                         kDrawStart, kBones};
            CheckShader({"ShadowDepth.hlsl", "VSMain", ERHIShaderStage::Vertex, {}, shadow});
            CheckShader({"ShadowDepth.hlsl", "VSMain", ERHIShaderStage::Vertex, {{"SKINNED", "1"}}, shadow});
            CheckShader({"ShadowDepth.hlsl", "PSMainLinear", ERHIShaderStage::Pixel, {}, shadow});

            const std::vector<FExpectedBuffer> outline = {kFrame, {"MaterialConstants", sizeof(FOutlineConstants)},
                                                          kDrawStart, kBones};
            CheckShader({"Outline_Skinned.hlsl", "VSMain", ERHIShaderStage::Vertex, {}, outline});
            CheckShader({"Outline_Skinned.hlsl", "PSMain", ERHIShaderStage::Pixel, {}, outline});
        }

        TEST_CASE("sky and post-processing") {
            CheckShader({"Skybox.hlsl", "PSMain", ERHIShaderStage::Pixel, {},
                         {kFrame, {"MaterialConstants", sizeof(FSkyConstants)}}});
            const std::vector<FExpectedBuffer> bloom = {kFrame, {"PassConstants", sizeof(FBloomConstants)}};
            CheckShader({"Bloom.hlsl", "PSBrightPass", ERHIShaderStage::Pixel, {}, bloom});
            CheckShader({"Bloom.hlsl", "PSDownsample", ERHIShaderStage::Pixel, {}, bloom});
            CheckShader({"Bloom.hlsl", "PSUpsample", ERHIShaderStage::Pixel, {}, bloom});
            const std::vector<FExpectedBuffer> ssao = {kFrame, {"PassConstants", sizeof(FSSAOConstants)}};
            CheckShader({"SSAO.hlsl", "PSOcclusion", ERHIShaderStage::Pixel, {}, ssao});
            CheckShader({"SSAO.hlsl", "PSBlur", ERHIShaderStage::Pixel, {}, ssao});
            CheckShader({"SSAO.hlsl", "PSComposite", ERHIShaderStage::Pixel, {}, ssao});
            CheckShader({"ToneMapping.hlsl", "PSMain", ERHIShaderStage::Pixel, {},
                         {kFrame, {"PassConstants", sizeof(FToneMappingConstants)}}});
            CheckShader({"FXAA.hlsl", "PSMain", ERHIShaderStage::Pixel, {},
                         {kFrame, {"PassConstants", sizeof(FFXAAConstants)}}});
        }

        TEST_CASE("2D drawing") {
            const std::vector<FExpectedBuffer> canvas = {kFrame, {"PassConstants", sizeof(FCanvasConstants)}};
            CheckShader({"Canvas.hlsl", "VSMain", ERHIShaderStage::Vertex, {}, canvas});
            CheckShader({"Canvas.hlsl", "PSMain", ERHIShaderStage::Pixel, {}, canvas});
        }

        TEST_CASE("a buffer that does not match is noticed") {
            // The check itself: the same shader held against a structure 16 bytes too large.
            FShaderCompileInput input;
            input.SourcePath = "Engine/Shaders/ToneMapping.hlsl";
            input.EntryPoint = "PSMain";
            input.Stage = ERHIShaderStage::Pixel;
            FShaderCompileOutput output;
            REQUIRE(FShaderCompiler::Compile(input, output));
            bool bFound = false;
            for (const FShaderConstantBuffer& buffer : output.ConstantBuffers) {
                if (buffer.Name != "PassConstants")
                    continue;
                bFound = true;
                CHECK(buffer.Register == 1);
                CHECK(buffer.Size == Padded(sizeof(FToneMappingConstants)));
                CHECK(buffer.Size != Padded(sizeof(FToneMappingConstants) + 16));
            }
            CHECK(bFound);
        }
    }

    TEST_SUITE("Shader library") {

        TEST_CASE("an edit to the shader folder is noticed once and changes the generation") {
            namespace fs = std::filesystem;
            const std::string original = FShaderLibrary::GetShaderDirectory();
            const fs::path folder = fs::path("build") / "ShaderReloadTest";
            std::error_code ec;
            fs::remove_all(folder, ec);
            fs::create_directories(folder, ec);
            const auto save = [&folder](const char* InName, const char* InText) {
                std::ofstream(folder / InName, std::ios::binary | std::ios::trunc) << InText;
            };
            save("A.hlsl", "float4 PSMain() : SV_Target { return 0; }");

            FShaderLibrary::SetShaderDirectory(folder.string());
            // The first look only takes note of what is there.
            CHECK_FALSE(FShaderLibrary::ReloadIfSourcesChanged());
            CHECK_FALSE(FShaderLibrary::ReloadIfSourcesChanged());
            const uint64_t before = FShaderLibrary::GetGeneration();

            save("A.hlsl", "float4 PSMain() : SV_Target { return float4(1.0, 0.0, 0.0, 1.0); }");
            CHECK(FShaderLibrary::ReloadIfSourcesChanged());
            CHECK(FShaderLibrary::GetGeneration() != before);
            CHECK_FALSE(FShaderLibrary::ReloadIfSourcesChanged());

            // A new file counts; a file that is not a shader does not.
            save("B.hlsli", "// shared");
            CHECK(FShaderLibrary::ReloadIfSourcesChanged());
            save("Notes.txt", "not a shader");
            CHECK_FALSE(FShaderLibrary::ReloadIfSourcesChanged());

            FShaderLibrary::SetShaderDirectory(original);
            fs::remove_all(folder, ec);
        }
    }

} // namespace Leon
