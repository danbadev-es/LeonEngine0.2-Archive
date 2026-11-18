#include <doctest/doctest.h>

#include "RHIContractTests.hpp"

#include "RHID3D12/FD3D12RHI.hpp"
#include "ShaderCompiler/FShaderCompiler.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <array>
#include <cstdlib>
#include <string>

// These tests render for real, on WARP (the software Direct3D 12 adapter), with the debug layer on.
// They need no GPU, so they run anywhere, and every one of them ends by checking that the debug
// layer reported no error.

namespace Leon {

    namespace {

        const char* kTestShader = R"(
cbuffer Draw : register(b3) {
    float4 u_Color;
    float4 u_OffsetScale; // xy = offset, zw = scale, applied to the vertex position
};

Texture2D t_Texture : register(t0);
SamplerState s_PointClamp : register(s2);

struct FVertexInput {
    float3 Position : ATTRIBUTE0;
    float2 UV : ATTRIBUTE1;
};

struct FVertexOutput {
    float4 Position : SV_Position;
    float2 UV : TEXCOORD0;
};

FVertexOutput VSMain(FVertexInput InVertex) {
    FVertexOutput output;
    output.Position = float4(InVertex.Position.xy * u_OffsetScale.zw + u_OffsetScale.xy, InVertex.Position.z, 1.0);
    output.UV = InVertex.UV;
    return output;
}

float4 PSColor(FVertexOutput InPixel) : SV_Target {
    return u_Color;
}

float4 PSTexture(FVertexOutput InPixel) : SV_Target {
    return t_Texture.Sample(s_PointClamp, InPixel.UV);
}
)";

        struct FTestVertex {
            float X, Y, Z;
            float U, V;
        };

        struct FDrawConstants {
            float Color[4];
            float OffsetScale[4];
        };

        struct FPixel {
            uint8_t R, G, B, A;
            bool operator==(const FPixel& InOther) const {
                return R == InOther.R && G == InOther.G && B == InOther.B && A == InOther.A;
            }
        };

        doctest::String toString(const FPixel& InPixel) {
            return (std::to_string(InPixel.R) + "," + std::to_string(InPixel.G) + "," + std::to_string(InPixel.B) + "," +
                    std::to_string(InPixel.A))
                .c_str();
        }

        constexpr FPixel kRed{255, 0, 0, 255};
        constexpr FPixel kGreen{0, 255, 0, 255};
        constexpr FPixel kBlue{0, 0, 255, 255};
        constexpr FPixel kWhite{255, 255, 255, 255};

        /** A D3D12 device on WARP with validation, plus the handful of objects every test draws with. */
        struct FD3D12Fixture {
            IRHIDevice* Device = nullptr;
            TRef<IRHIShader> VertexShader;
            TRef<IRHIShader> ColorShader;
            TRef<IRHIShader> TextureShader;
            TRef<IRHIBuffer> Triangle;
            TRef<IRHIBuffer> Quad;

            FD3D12Fixture() {
                FD3D12RHI::Register();
                FRHI::Shutdown();
                FRHIInitDesc init;
                init.bEnableDebugLayer = true;
                // LEON_TEST_GPU=1 runs the same tests on the machine's real adapter.
                const char* gpu = std::getenv("LEON_TEST_GPU");
                init.bUseSoftwareAdapter = !(gpu && gpu[0] == '1');
                REQUIRE(FRHI::Init(ERHIBackend::D3D12, init));
                Device = FRHI::GetDevice();
                REQUIRE(Device);

                VertexShader = Compile("VSMain", ERHIShaderStage::Vertex);
                ColorShader = Compile("PSColor", ERHIShaderStage::Pixel);
                TextureShader = Compile("PSTexture", ERHIShaderStage::Pixel);

                // Counter-clockwise, apex at the top.
                const FTestVertex triangle[3] = {{-0.5f, -0.5f, 0.5f, 0, 1}, {0.5f, -0.5f, 0.5f, 1, 1}, {0.0f, 0.5f, 0.5f, 0.5f, 0}};
                Triangle = MakeVertices(triangle, 3, "TestTriangle");
                // Covers the whole target; UV (0,0) at the top left.
                const FTestVertex quad[6] = {{-1, -1, 0.5f, 0, 1}, {1, -1, 0.5f, 1, 1}, {1, 1, 0.5f, 1, 0},
                                             {-1, -1, 0.5f, 0, 1}, {1, 1, 0.5f, 1, 0},  {-1, 1, 0.5f, 0, 0}};
                Quad = MakeVertices(quad, 6, "TestQuad");
            }

            ~FD3D12Fixture() {
                // Nothing a test did may have upset the debug layer.
                Device->WaitIdle();
                CHECK(FD3D12RHI::GetValidationErrorCount() == 0);
                Triangle.reset();
                Quad.reset();
                VertexShader.reset();
                ColorShader.reset();
                TextureShader.reset();
                FRHI::Shutdown();
            }

            TRef<IRHIShader> Compile(const char* InEntry, ERHIShaderStage InStage) {
                FShaderCompileInput input;
                input.SourcePath = "RHID3D12Tests.hlsl";
                input.Source = kTestShader;
                input.EntryPoint = InEntry;
                input.Stage = InStage;
                FShaderCompileOutput output;
                const bool bCompiled = FShaderCompiler::Compile(input, output);
                REQUIRE_MESSAGE(bCompiled, output.Messages);
                FRHIShaderDesc desc;
                desc.Stage = InStage;
                desc.Bytecode = output.Bytecode.data();
                desc.BytecodeSize = output.Bytecode.size();
                desc.DebugName = InEntry;
                TRef<IRHIShader> shader = Device->CreateShader(desc);
                REQUIRE(shader);
                return shader;
            }

            TRef<IRHIBuffer> MakeVertices(const FTestVertex* InVertices, uint32_t InCount, const char* InName) {
                FRHIBufferDesc desc;
                desc.Size = sizeof(FTestVertex) * InCount;
                desc.Usage = ERHIBufferUsage::Vertex;
                desc.Stride = sizeof(FTestVertex);
                desc.DebugName = InName;
                TRef<IRHIBuffer> buffer = Device->CreateBuffer(desc, InVertices);
                REQUIRE(buffer);
                return buffer;
            }

            TRef<IRHITexture> MakeTarget(uint32_t InSize, ERHIFormat InFormat = ERHIFormat::RGBA8_UNorm) {
                FRHITextureDesc desc;
                desc.Width = InSize;
                desc.Height = InSize;
                desc.Format = InFormat;
                desc.Usage = RHIFormatIsDepth(InFormat)
                                 ? static_cast<uint8_t>(RHITextureUsage_DepthStencil)
                                 : static_cast<uint8_t>(RHITextureUsage_RenderTarget | RHITextureUsage_Sampled);
                desc.DebugName = RHIFormatIsDepth(InFormat) ? "TestDepth" : "TestTarget";
                TRef<IRHITexture> texture = Device->CreateTexture(desc);
                REQUIRE(texture);
                return texture;
            }

            TRef<IRHIPipeline> MakePipeline(IRHIShader* InPixelShader, bool bInDepth = false,
                                            ERHICull InCull = ERHICull::Back) {
                FRHIGraphicsPipelineDesc desc;
                desc.VertexShader = VertexShader.get();
                desc.PixelShader = InPixelShader;
                desc.VertexLayout.Stride = sizeof(FTestVertex);
                desc.VertexLayout.Attributes = {{ERHIVertexFormat::Float3, 0}, {ERHIVertexFormat::Float2, 12}};
                desc.Raster.Cull = InCull;
                desc.Depth.bTest = bInDepth;
                desc.Depth.bWrite = bInDepth;
                desc.DepthFormat = bInDepth ? ERHIFormat::D32_Float : ERHIFormat::Unknown;
                desc.DebugName = "TestPipeline";
                TRef<IRHIPipeline> pipeline = Device->CreateGraphicsPipeline(desc);
                REQUIRE(pipeline);
                return pipeline;
            }

            static void SetDraw(IRHICommandList& InCommands, FPixel InColor, float InOffsetX = 0.0f, float InOffsetY = 0.0f,
                                float InScaleX = 1.0f, float InScaleY = 1.0f) {
                const FDrawConstants constants = {{InColor.R / 255.0f, InColor.G / 255.0f, InColor.B / 255.0f, InColor.A / 255.0f},
                                                  {InOffsetX, InOffsetY, InScaleX, InScaleY}};
                InCommands.SetConstants(ERHIConstantSlot::Draw, &constants, sizeof(constants));
            }

            /** Reads a whole RGBA8 target; At(x, y) has y = 0 at the top row. */
            struct FImage {
                uint32_t Size = 0;
                std::vector<uint8_t> Pixels;
                FPixel At(uint32_t InX, uint32_t InY) const {
                    const uint8_t* p = Pixels.data() + (static_cast<size_t>(InY) * Size + InX) * 4;
                    return {p[0], p[1], p[2], p[3]};
                }
            };
            FImage Read(IRHITexture& InTexture) {
                FImage image;
                image.Size = InTexture.GetDesc().Width;
                REQUIRE(Device->ReadTexture(InTexture, 0, 0, image.Pixels));
                REQUIRE(image.Pixels.size() == static_cast<size_t>(image.Size) * image.Size * 4);
                return image;
            }

            static FRHIRenderPassDesc PassTo(IRHITexture& InColor, FPixel InClear, IRHITexture* InDepth = nullptr) {
                FRHIRenderPassDesc pass;
                pass.NumColorTargets = 1;
                pass.Color[0].Texture = &InColor;
                pass.Color[0].ClearColor = {InClear.R / 255.0f, InClear.G / 255.0f, InClear.B / 255.0f, InClear.A / 255.0f};
                pass.Depth.Texture = InDepth;
                pass.Name = "TestPass";
                return pass;
            }
        };

        LRESULT CALLBACK TestWindowProc(HWND InWindow, UINT InMessage, WPARAM InW, LPARAM InL) {
            return DefWindowProcW(InWindow, InMessage, InW, InL);
        }

        /** A window that is never shown: enough for a swapchain. */
        struct FHiddenWindow {
            HWND Handle = nullptr;
            FHiddenWindow() {
                WNDCLASSW windowClass{};
                windowClass.lpfnWndProc = TestWindowProc;
                windowClass.hInstance = GetModuleHandleW(nullptr);
                windowClass.lpszClassName = L"LeonRHITestWindow";
                RegisterClassW(&windowClass);
                Handle = CreateWindowExW(0, windowClass.lpszClassName, L"RHI test", WS_OVERLAPPEDWINDOW, 0, 0, 64, 32,
                                         nullptr, nullptr, windowClass.hInstance, nullptr);
            }
            ~FHiddenWindow() {
                if (Handle)
                    DestroyWindow(Handle);
            }
        };

    } // namespace

    TEST_SUITE("RHI Direct3D 12 backend") {

        TEST_CASE("the device starts on the software adapter") {
            FD3D12Fixture f;
            CHECK(f.Device->GetInfo().Backend == ERHIBackend::D3D12);
            const char* gpu = std::getenv("LEON_TEST_GPU");
            CHECK(f.Device->GetInfo().bSoftwareAdapter == !(gpu && gpu[0] == '1'));
            CHECK_FALSE(f.Device->GetInfo().AdapterName.empty());
        }

        TEST_CASE("buffers and textures meet the backend contract") {
            FD3D12Fixture f;
            Test::CheckBuffers(*f.Device);
            Test::CheckTextureRoundTrip(*f.Device);
        }

        TEST_CASE("a swapchain on a window meets the backend contract, renders and presents") {
            FD3D12Fixture f;
            FHiddenWindow window;
            REQUIRE(window.Handle != nullptr);
            Test::CheckSwapchain(*f.Device, window.Handle);

            TScope<IRHISwapchain> swapchain = f.Device->CreateSwapchain(window.Handle, 64, 32);
            REQUIRE(swapchain);
            TRef<IRHIPipeline> pipeline = f.MakePipeline(f.ColorShader.get());
            for (int frame = 0; frame < 3; ++frame) { // more frames than back buffers
                IRHICommandList& commands = f.Device->BeginFrame();
                commands.BeginRenderPass(FD3D12Fixture::PassTo(*swapchain->GetBackBuffer(), kBlue));
                commands.SetPipeline(pipeline.get());
                commands.SetVertexBuffer(f.Triangle.get());
                FD3D12Fixture::SetDraw(commands, kRed);
                commands.Draw(3);
                commands.EndRenderPass();
                f.Device->EndFrame();
                swapchain->Present(false);
            }
            swapchain->Resize(96, 40);
            CHECK(swapchain->GetBackBuffer()->GetDesc().Width == 96);
            swapchain.reset();
        }

        TEST_CASE("a triangle is rasterised where its vertices say, the right way up") {
            FD3D12Fixture f;
            TRef<IRHITexture> target = f.MakeTarget(64);
            TRef<IRHIPipeline> pipeline = f.MakePipeline(f.ColorShader.get());

            IRHICommandList& commands = f.Device->BeginFrame();
            commands.BeginRenderPass(FD3D12Fixture::PassTo(*target, kBlue));
            commands.SetPipeline(pipeline.get());
            commands.SetVertexBuffer(f.Triangle.get());
            FD3D12Fixture::SetDraw(commands, kRed);
            commands.Draw(3);
            commands.EndRenderPass();
            f.Device->EndFrame();

            const auto image = f.Read(*target);
            CHECK(image.At(32, 32) == kRed); // centre
            CHECK(image.At(1, 1) == kBlue);  // cleared, untouched
            CHECK(image.At(62, 62) == kBlue);
            // The apex is at the top: the triangle is narrow in the upper rows and wide in the lower ones.
            CHECK(image.At(20, 22) == kBlue);
            CHECK(image.At(32, 22) == kRed);
            CHECK(image.At(20, 44) == kRed);
            CHECK(image.At(44, 44) == kRed);
            CHECK(image.At(32, 52) == kBlue); // below the base (y = -0.5 is row 48)
        }

        TEST_CASE("counter-clockwise is the front face") {
            FD3D12Fixture f;
            TRef<IRHITexture> target = f.MakeTarget(32);

            SUBCASE("back-face culling keeps a counter-clockwise triangle") {
                TRef<IRHIPipeline> pipeline = f.MakePipeline(f.ColorShader.get(), false, ERHICull::Back);
                IRHICommandList& commands = f.Device->BeginFrame();
                commands.BeginRenderPass(FD3D12Fixture::PassTo(*target, kBlue));
                commands.SetPipeline(pipeline.get());
                commands.SetVertexBuffer(f.Triangle.get());
                FD3D12Fixture::SetDraw(commands, kRed);
                commands.Draw(3);
                commands.EndRenderPass();
                f.Device->EndFrame();
                CHECK(f.Read(*target).At(16, 16) == kRed);
            }
            SUBCASE("front-face culling removes it") {
                TRef<IRHIPipeline> pipeline = f.MakePipeline(f.ColorShader.get(), false, ERHICull::Front);
                IRHICommandList& commands = f.Device->BeginFrame();
                commands.BeginRenderPass(FD3D12Fixture::PassTo(*target, kBlue));
                commands.SetPipeline(pipeline.get());
                commands.SetVertexBuffer(f.Triangle.get());
                FD3D12Fixture::SetDraw(commands, kRed);
                commands.Draw(3);
                commands.EndRenderPass();
                f.Device->EndFrame();
                CHECK(f.Read(*target).At(16, 16) == kBlue);
            }
        }

        TEST_CASE("each draw sees the constants set before it, however often a slot is rewritten") {
            // The bug this backend must never have: in the OpenGL renderer a per-draw buffer rewritten
            // within a frame was read one update late, so every mesh was drawn with another one's data.
            FD3D12Fixture f;
            TRef<IRHITexture> target = f.MakeTarget(64);
            TRef<IRHIPipeline> pipeline = f.MakePipeline(f.ColorShader.get());

            const FPixel colors[4] = {kRed, kGreen, kWhite, {255, 255, 0, 255}};
            IRHICommandList& commands = f.Device->BeginFrame();
            commands.BeginRenderPass(FD3D12Fixture::PassTo(*target, kBlue));
            commands.SetPipeline(pipeline.get());
            commands.SetVertexBuffer(f.Quad.get());
            for (int i = 0; i < 4; ++i) {
                // Four quads, one per quadrant: scale to half size, move to the quadrant centre.
                const float offsetX = (i % 2 == 0) ? -0.5f : 0.5f;
                const float offsetY = (i < 2) ? 0.5f : -0.5f;
                FD3D12Fixture::SetDraw(commands, colors[i], offsetX, offsetY, 0.5f, 0.5f);
                commands.Draw(6);
            }
            commands.EndRenderPass();
            f.Device->EndFrame();

            const auto image = f.Read(*target);
            CHECK(image.At(16, 16) == colors[0]); // top left
            CHECK(image.At(48, 16) == colors[1]); // top right
            CHECK(image.At(16, 48) == colors[2]); // bottom left
            CHECK(image.At(48, 48) == colors[3]); // bottom right
        }

        TEST_CASE("a texture is sampled with its first row at the top, and rebinding takes effect per draw") {
            FD3D12Fixture f;
            TRef<IRHITexture> target = f.MakeTarget(64);
            TRef<IRHIPipeline> pipeline = f.MakePipeline(f.TextureShader.get());

            FRHITextureDesc textureDesc;
            textureDesc.Width = 2;
            textureDesc.Height = 2;
            textureDesc.DebugName = "TestChecker";
            TRef<IRHITexture> checker = f.Device->CreateTexture(textureDesc);
            REQUIRE(checker);
            // Row 0: red, green. Row 1: blue, white.
            const uint8_t texels[16] = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255};
            f.Device->UploadTexture(*checker, 0, 0, texels, 8);

            textureDesc.Width = 1;
            textureDesc.Height = 1;
            textureDesc.DebugName = "TestSolid";
            TRef<IRHITexture> solid = f.Device->CreateTexture(textureDesc);
            const uint8_t yellow[4] = {255, 255, 0, 255};
            f.Device->UploadTexture(*solid, 0, 0, yellow, 4);

            IRHICommandList& commands = f.Device->BeginFrame();
            commands.BeginRenderPass(FD3D12Fixture::PassTo(*target, kBlue));
            commands.SetPipeline(pipeline.get());
            commands.SetVertexBuffer(f.Quad.get());
            commands.SetTexture(0, checker.get());
            FD3D12Fixture::SetDraw(commands, kWhite, -0.5f, 0.0f, 0.5f, 1.0f); // left half
            commands.Draw(6);
            commands.SetTexture(0, solid.get());
            FD3D12Fixture::SetDraw(commands, kWhite, 0.5f, 0.0f, 0.5f, 1.0f); // right half
            commands.Draw(6);
            commands.EndRenderPass();
            f.Device->EndFrame();

            const auto image = f.Read(*target);
            CHECK(image.At(8, 16) == kRed);    // left half, top left texel
            CHECK(image.At(24, 16) == kGreen); // top right texel
            CHECK(image.At(8, 48) == kBlue);   // bottom left texel
            CHECK(image.At(24, 48) == kWhite); // bottom right texel
            CHECK(image.At(48, 32) == FPixel{255, 255, 0, 255});
        }

        TEST_CASE("an unbound texture slot reads black instead of failing") {
            FD3D12Fixture f;
            TRef<IRHITexture> target = f.MakeTarget(16);
            TRef<IRHIPipeline> pipeline = f.MakePipeline(f.TextureShader.get());
            IRHICommandList& commands = f.Device->BeginFrame();
            commands.BeginRenderPass(FD3D12Fixture::PassTo(*target, kBlue));
            commands.SetPipeline(pipeline.get());
            commands.SetVertexBuffer(f.Quad.get());
            commands.SetTexture(0, nullptr);
            FD3D12Fixture::SetDraw(commands, kWhite);
            commands.Draw(6);
            commands.EndRenderPass();
            f.Device->EndFrame();
            CHECK(f.Read(*target).At(8, 8) == FPixel{0, 0, 0, 0});
        }

        TEST_CASE("the depth test keeps the nearest surface whatever the draw order") {
            FD3D12Fixture f;
            TRef<IRHITexture> target = f.MakeTarget(32);
            TRef<IRHITexture> depth = f.MakeTarget(32, ERHIFormat::D32_Float);
            TRef<IRHIPipeline> pipeline = f.MakePipeline(f.ColorShader.get(), true);

            // The same quad at three depths: vertices carry z = 0.5; the buffers below override it.
            const auto quadAt = [&](float InZ, const char* InName) {
                const FTestVertex quad[6] = {{-1, -1, InZ, 0, 1}, {1, -1, InZ, 1, 1}, {1, 1, InZ, 1, 0},
                                             {-1, -1, InZ, 0, 1}, {1, 1, InZ, 1, 0},  {-1, 1, InZ, 0, 0}};
                return f.MakeVertices(quad, 6, InName);
            };
            TRef<IRHIBuffer> mid = quadAt(0.5f, "QuadMid");
            TRef<IRHIBuffer> near_ = quadAt(0.2f, "QuadNear");
            TRef<IRHIBuffer> far_ = quadAt(0.9f, "QuadFar");

            IRHICommandList& commands = f.Device->BeginFrame();
            commands.BeginRenderPass(FD3D12Fixture::PassTo(*target, kBlue, depth.get()));
            commands.SetPipeline(pipeline.get());
            commands.SetVertexBuffer(mid.get());
            FD3D12Fixture::SetDraw(commands, kGreen); // whole target, middle depth
            commands.Draw(6);
            commands.SetVertexBuffer(near_.get());
            FD3D12Fixture::SetDraw(commands, kRed, 0.0f, 0.0f, 0.5f, 0.5f); // centre, nearest
            commands.Draw(6);
            commands.SetVertexBuffer(far_.get());
            FD3D12Fixture::SetDraw(commands, kWhite); // whole target, farthest, drawn last
            commands.Draw(6);
            commands.EndRenderPass();
            f.Device->EndFrame();

            const auto image = f.Read(*target);
            CHECK(image.At(16, 16) == kRed);
            CHECK(image.At(2, 2) == kGreen);
            CHECK(image.At(29, 29) == kGreen);
        }

        TEST_CASE("a texture drawn in one pass can be sampled in the next") {
            FD3D12Fixture f;
            TRef<IRHITexture> first = f.MakeTarget(16);
            TRef<IRHITexture> second = f.MakeTarget(16);
            TRef<IRHIPipeline> colorPipeline = f.MakePipeline(f.ColorShader.get());
            TRef<IRHIPipeline> texturePipeline = f.MakePipeline(f.TextureShader.get());

            IRHICommandList& commands = f.Device->BeginFrame();
            commands.BeginRenderPass(FD3D12Fixture::PassTo(*first, kGreen));
            commands.SetPipeline(colorPipeline.get());
            commands.SetVertexBuffer(f.Triangle.get());
            FD3D12Fixture::SetDraw(commands, kRed);
            commands.Draw(3);
            commands.EndRenderPass();

            commands.BeginRenderPass(FD3D12Fixture::PassTo(*second, kBlue));
            commands.SetPipeline(texturePipeline.get());
            commands.SetVertexBuffer(f.Quad.get());
            commands.SetTexture(0, first.get());
            FD3D12Fixture::SetDraw(commands, kWhite);
            commands.Draw(6);
            commands.EndRenderPass();
            f.Device->EndFrame();

            const auto image = f.Read(*second);
            CHECK(image.At(8, 8) == kRed);     // the triangle, copied through the sampler
            CHECK(image.At(1, 1) == kGreen);   // the first pass's clear colour, not the second's
            CHECK(image.At(14, 14) == kGreen);
        }

        TEST_CASE("resources released mid-frame stay valid until the GPU is done with them") {
            FD3D12Fixture f;
            TRef<IRHITexture> target = f.MakeTarget(16);
            TRef<IRHIPipeline> pipeline = f.MakePipeline(f.ColorShader.get());
            for (int frame = 0; frame < 5; ++frame) {
                const FTestVertex quad[6] = {{-1, -1, 0.5f, 0, 1}, {1, -1, 0.5f, 1, 1}, {1, 1, 0.5f, 1, 0},
                                             {-1, -1, 0.5f, 0, 1}, {1, 1, 0.5f, 1, 0},  {-1, 1, 0.5f, 0, 0}};
                TRef<IRHIBuffer> temporary = f.MakeVertices(quad, 6, "Temporary");
                IRHICommandList& commands = f.Device->BeginFrame();
                commands.BeginRenderPass(FD3D12Fixture::PassTo(*target, kBlue));
                commands.SetPipeline(pipeline.get());
                commands.SetVertexBuffer(temporary.get());
                FD3D12Fixture::SetDraw(commands, kGreen);
                commands.Draw(6);
                temporary.reset(); // dropped while the frame that uses it is still being recorded
                commands.EndRenderPass();
                f.Device->EndFrame();
            }
            CHECK(f.Read(*target).At(8, 8) == kGreen);
        }

        TEST_CASE("a shader that does not compile reports where") {
            FShaderCompileInput input;
            input.SourcePath = "Broken.hlsl";
            input.Source = "float4 main() : SV_Target { return undefinedName; }";
            input.Stage = ERHIShaderStage::Pixel;
            FShaderCompileOutput output;
            CHECK_FALSE(FShaderCompiler::Compile(input, output));
            CHECK(output.Bytecode.empty());
            CHECK(output.Messages.find("undefinedName") != std::string::npos);
            CHECK(output.Messages.find("Broken.hlsl") != std::string::npos);
        }
    }

} // namespace Leon
