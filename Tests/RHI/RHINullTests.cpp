#include <doctest/doctest.h>

#include "RHIContractTests.hpp"

#include "RHINull/FNullRHI.hpp"

namespace Leon {

    namespace {

        /** One null device for the duration of a test. */
        struct FNullDeviceScope {
            FNullDeviceScope() {
                FNullRHI::Register();
                FRHI::Shutdown();
                REQUIRE(FRHI::Init(ERHIBackend::Null));
                Device = FRHI::GetDevice();
                REQUIRE(Device);
            }
            ~FNullDeviceScope() { FRHI::Shutdown(); }
            IRHIDevice* Device = nullptr;
        };

    } // namespace

    TEST_SUITE("RHI null backend") {

        TEST_CASE("the device is created through the registry and removed on shutdown") {
            FNullRHI::Register();
            FRHI::Shutdown();
            CHECK_FALSE(FRHI::IsInitialized());
            CHECK(FRHI::GetDevice() == nullptr);
            CHECK(FRHI::IsBackendRegistered(ERHIBackend::Null));

            REQUIRE(FRHI::Init(ERHIBackend::Null));
            CHECK(FRHI::IsInitialized());
            CHECK(FRHI::GetDevice()->GetInfo().Backend == ERHIBackend::Null);
            CHECK_FALSE(FRHI::Init(ERHIBackend::Null)); // a second device is refused

            FRHI::Shutdown();
            CHECK(FRHI::GetDevice() == nullptr);
        }

        TEST_CASE("format helpers") {
            CHECK(RHIFormatBytesPerPixel(ERHIFormat::RGBA8_UNorm) == 4);
            CHECK(RHIFormatBytesPerPixel(ERHIFormat::RGBA16_Float) == 8);
            CHECK(RHIFormatBytesPerPixel(ERHIFormat::RGBA32_Float) == 16);
            CHECK(RHIFormatBytesPerPixel(ERHIFormat::R8_UNorm) == 1);
            CHECK(RHIFormatBytesPerPixel(ERHIFormat::Unknown) == 0);
            CHECK(RHIFormatIsDepth(ERHIFormat::D32_Float));
            CHECK(RHIFormatIsDepth(ERHIFormat::D24_UNorm_S8_UInt));
            CHECK_FALSE(RHIFormatIsDepth(ERHIFormat::RGBA8_UNorm));

            FRHITextureDesc cubes;
            cubes.Dimension = ERHITextureDimension::TextureCubeArray;
            cubes.ArraySize = 4;
            CHECK(cubes.SliceCount() == 24);
        }

        TEST_CASE("buffers, textures and the swapchain meet the backend contract") {
            FNullDeviceScope scope;
            Test::CheckBuffers(*scope.Device);
            Test::CheckTextureRoundTrip(*scope.Device);
            Test::CheckSwapchain(*scope.Device, nullptr);
        }

        TEST_CASE("a frame counts its passes and only the draws made inside a pass") {
            FNullDeviceScope scope;
            IRHIDevice& device = *scope.Device;

            FRHIShaderDesc shaderDesc;
            shaderDesc.Stage = ERHIShaderStage::Vertex;
            TRef<IRHIShader> vertexShader = device.CreateShader(shaderDesc);
            REQUIRE(vertexShader);
            CHECK(vertexShader->GetStage() == ERHIShaderStage::Vertex);

            FRHIGraphicsPipelineDesc pipelineDesc;
            CHECK(device.CreateGraphicsPipeline(pipelineDesc) == nullptr); // no vertex shader
            pipelineDesc.VertexShader = vertexShader.get();
            TRef<IRHIPipeline> pipeline = device.CreateGraphicsPipeline(pipelineDesc);
            REQUIRE(pipeline);

            FRHITextureDesc targetDesc;
            targetDesc.Width = 8;
            targetDesc.Height = 8;
            targetDesc.Usage = RHITextureUsage_RenderTarget | RHITextureUsage_Sampled;
            TRef<IRHITexture> target = device.CreateTexture(targetDesc);

            const FNullRHI::FStats before = FNullRHI::GetStats();
            IRHICommandList& commands = device.BeginFrame();
            commands.Draw(3); // outside a pass: not a draw
            FRHIRenderPassDesc pass;
            pass.NumColorTargets = 1;
            pass.Color[0].Texture = target.get();
            commands.BeginRenderPass(pass);
            commands.SetPipeline(pipeline.get());
            const float constants[4] = {1.0f, 2.0f, 3.0f, 4.0f};
            commands.SetConstants(ERHIConstantSlot::Draw, constants, sizeof(constants));
            commands.SetTexture(0, nullptr);
            commands.Draw(3);
            commands.DrawIndexed(6);
            commands.EndRenderPass();
            device.EndFrame();

            const FNullRHI::FStats after = FNullRHI::GetStats();
            CHECK(after.Frames == before.Frames + 1);
            CHECK(after.RenderPasses == before.RenderPasses + 1);
            CHECK(after.DrawCalls == before.DrawCalls + 2);
        }
    }

} // namespace Leon
