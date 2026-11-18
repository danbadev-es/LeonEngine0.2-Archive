#include <doctest/doctest.h>
#include "GPU/HeadlessDevice.hpp"
#include "RHI/FRenderer.hpp"

TEST_SUITE("Renderer pipeline - viewport / render target resize") {

    TEST_CASE("Render targets recreated at each size have the requested extents and can be drawn to") {
        Leon::TestGPU::FHeadlessDevice gl;
        REQUIRE(gl.IsValid());

        auto shader = Leon::TestGPU::FTestShader::Create("Engine/Resources/Shaders/ToneMapping.glsl");
        REQUIRE(shader != nullptr);
        shader->Bind();
        shader->SetInt("u_UseBloom", 0);
        auto scene = gl.Create1x1FloatTexture(0.5f, 0.5f, 0.5f, 1.0f);
        gl.BindTexture(0, scene);

        const int sizes[][2] = {{1280, 720}, {1920, 1080}, {2560, 1440}, {800, 600}, {1280, 720}, {1920, 1080}, {800, 600}};
        for (const auto& size : sizes) {
            gl.BindFramebuffer(size[0], size[1]);
            CHECK(gl.GetWidth() == static_cast<uint32_t>(size[0]));
            CHECK(gl.GetHeight() == static_cast<uint32_t>(size[1]));
            gl.DrawQuad();
            // The far corner was drawn: the target is as large as it says.
            CHECK(gl.ReadPixel(size[0] - 1, size[1] - 1).r > 0.1f);
        }
        CHECK(gl.ValidationErrors() == 0);
    }

    TEST_CASE("Named render target VRAM is tracked and released") {
        Leon::TestGPU::FHeadlessDevice gl;
        REQUIRE(gl.IsValid());

        const auto& stats = Leon::FRenderer::GetStats();
        const size_t targetsBefore = stats.GPUMemory[static_cast<size_t>(Leon::EGPUMemoryCategory::Framebuffer)].Bytes;
        const size_t totalBefore = stats.AllocatedGPUMemoryBytes;

        {
            Leon::FRHITextureDesc desc;
            desc.Width = 64;
            desc.Height = 64;
            desc.Format = Leon::ERHIFormat::RGBA8_UNorm;
            desc.Usage = Leon::RHITextureUsage_Sampled | Leon::RHITextureUsage_RenderTarget;
            desc.DebugName = "VRAMTest";
            auto target = gl.GetDevice()->CreateTexture(desc);
            REQUIRE(target != nullptr);
            REQUIRE(stats.NamedGPUMemory.contains("VRAMTest"));
            CHECK(stats.NamedGPUMemory.at("VRAMTest").Bytes == 64ull * 64ull * 4ull);
            CHECK(stats.GPUMemory[static_cast<size_t>(Leon::EGPUMemoryCategory::Framebuffer)].Bytes ==
                  targetsBefore + 64ull * 64ull * 4ull);
            CHECK(stats.AllocatedGPUMemoryBytes == totalBefore + 64ull * 64ull * 4ull);
        }

        CHECK_FALSE(stats.NamedGPUMemory.contains("VRAMTest"));
        CHECK(stats.GPUMemory[static_cast<size_t>(Leon::EGPUMemoryCategory::Framebuffer)].Bytes == targetsBefore);
        CHECK(stats.AllocatedGPUMemoryBytes == totalBefore);
    }

    TEST_CASE("Mesh and texture VRAM is tracked by kind") {
        Leon::TestGPU::FHeadlessDevice gl;
        REQUIRE(gl.IsValid());

        const auto& stats = Leon::FRenderer::GetStats();
        const auto bytes = [&stats](Leon::EGPUMemoryCategory InCategory) {
            return stats.GPUMemory[static_cast<size_t>(InCategory)].Bytes;
        };
        const size_t texturesBefore = bytes(Leon::EGPUMemoryCategory::Texture2D);
        const size_t cubesBefore = bytes(Leon::EGPUMemoryCategory::TextureCube);
        const size_t verticesBefore = bytes(Leon::EGPUMemoryCategory::VertexBuffer);

        {
            // 8x8 with its mip chain: 64 + 16 + 4 + 1 texels of four bytes.
            auto texture = gl.CreateEmpty2D(8, 8, Leon::ERHIFormat::RGBA8_UNorm, 4);
            auto cube = gl.CreateFloatCube(1.0f, 1.0f, 1.0f);
            Leon::FRHIBufferDesc bufferDesc;
            bufferDesc.Size = 1024;
            bufferDesc.Usage = Leon::ERHIBufferUsage::Vertex;
            bufferDesc.Stride = 16;
            auto buffer = gl.GetDevice()->CreateBuffer(bufferDesc, nullptr);
            REQUIRE(texture != nullptr);
            REQUIRE(cube != nullptr);
            REQUIRE(buffer != nullptr);
            CHECK(bytes(Leon::EGPUMemoryCategory::Texture2D) == texturesBefore + 85ull * 4ull);
            CHECK(bytes(Leon::EGPUMemoryCategory::TextureCube) == cubesBefore + 6ull * 16ull);
            CHECK(bytes(Leon::EGPUMemoryCategory::VertexBuffer) == verticesBefore + 1024ull);
        }

        CHECK(bytes(Leon::EGPUMemoryCategory::Texture2D) == texturesBefore);
        CHECK(bytes(Leon::EGPUMemoryCategory::TextureCube) == cubesBefore);
        CHECK(bytes(Leon::EGPUMemoryCategory::VertexBuffer) == verticesBefore);
    }
}
