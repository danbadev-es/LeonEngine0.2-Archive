#pragma once

// Checks every RHI backend must pass, written once against IRHIDevice. A backend's test file
// creates its device and calls these.

#include <doctest/doctest.h>

#include "RHI/IRHIDevice.hpp"

#include <cstring>
#include <vector>

namespace Leon::Test {

    /** Buffers accept initial data and partial rewrites; descriptions round-trip. */
    inline void CheckBuffers(IRHIDevice& InDevice) {
        const float vertices[6] = {0.0f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
        FRHIBufferDesc desc;
        desc.Size = sizeof(vertices);
        desc.Usage = ERHIBufferUsage::Vertex;
        desc.Stride = 12;
        desc.DebugName = "ContractVertices";
        TRef<IRHIBuffer> buffer = InDevice.CreateBuffer(desc, vertices);
        REQUIRE(buffer);
        CHECK(buffer->GetDesc().Size == sizeof(vertices));
        CHECK(buffer->GetDesc().Stride == 12);

        FRHIBufferDesc dynamic = desc;
        dynamic.bDynamic = true;
        dynamic.DebugName = "ContractDynamic";
        TRef<IRHIBuffer> rewritable = InDevice.CreateBuffer(dynamic);
        REQUIRE(rewritable);
        const float replacement[2] = {9.0f, 8.0f};
        InDevice.WriteBuffer(*rewritable, replacement, sizeof(replacement), 4);
    }

    /** A texture reads back exactly the pixels uploaded to it, per mip and per slice. */
    inline void CheckTextureRoundTrip(IRHIDevice& InDevice) {
        FRHITextureDesc desc;
        desc.Width = 4;
        desc.Height = 2;
        desc.MipLevels = 2;
        desc.ArraySize = 2;
        desc.Dimension = ERHITextureDimension::Texture2DArray;
        desc.Format = ERHIFormat::RGBA8_UNorm;
        desc.DebugName = "ContractTexture";
        TRef<IRHITexture> texture = InDevice.CreateTexture(desc);
        REQUIRE(texture);
        CHECK(texture->GetDesc().Width == 4);
        CHECK(texture->GetDesc().SliceCount() == 2);

        // Slice 1, mip 0: every pixel distinct. Uploaded with padded rows to prove the pitch is honoured.
        const uint32_t rowPitch = 4 * 4 + 12;
        std::vector<uint8_t> upload(rowPitch * 2, 0xEE);
        for (uint32_t y = 0; y < 2; ++y)
            for (uint32_t x = 0; x < 4; ++x) {
                uint8_t* pixel = upload.data() + y * rowPitch + x * 4;
                pixel[0] = static_cast<uint8_t>(10 * x);
                pixel[1] = static_cast<uint8_t>(100 + y);
                pixel[2] = 7;
                pixel[3] = 255;
            }
        InDevice.UploadTexture(*texture, 0, 1, upload.data(), rowPitch);

        // Slice 0, mip 1 (2x1).
        const uint8_t small[8] = {1, 2, 3, 4, 5, 6, 7, 8};
        InDevice.UploadTexture(*texture, 1, 0, small, 8);

        std::vector<uint8_t> pixels;
        REQUIRE(InDevice.ReadTexture(*texture, 0, 1, pixels));
        REQUIRE(pixels.size() == 4u * 2u * 4u); // tightly packed
        for (uint32_t y = 0; y < 2; ++y)
            for (uint32_t x = 0; x < 4; ++x) {
                const uint8_t* pixel = pixels.data() + (y * 4 + x) * 4;
                CHECK(pixel[0] == 10 * x);
                CHECK(pixel[1] == 100 + y);
                CHECK(pixel[2] == 7);
                CHECK(pixel[3] == 255);
            }

        REQUIRE(InDevice.ReadTexture(*texture, 1, 0, pixels));
        REQUIRE(pixels.size() == 8u);
        CHECK(std::memcmp(pixels.data(), small, 8) == 0);

        // Out of range subresources are refused rather than read.
        CHECK_FALSE(InDevice.ReadTexture(*texture, 5, 0, pixels));
        CHECK_FALSE(InDevice.ReadTexture(*texture, 0, 9, pixels));
    }

    /** A swapchain reports its size, hands out a back buffer and survives resizing and minimising. */
    inline void CheckSwapchain(IRHIDevice& InDevice, void* InWindowHandle) {
        TScope<IRHISwapchain> swapchain = InDevice.CreateSwapchain(InWindowHandle, 64, 32);
        REQUIRE(swapchain);
        CHECK(swapchain->GetWidth() == 64);
        CHECK(swapchain->GetHeight() == 32);
        REQUIRE(swapchain->GetBackBuffer() != nullptr);

        swapchain->Resize(128, 48);
        CHECK(swapchain->GetWidth() == 128);
        CHECK(swapchain->GetHeight() == 48);
        swapchain->Resize(0, 0); // minimised: ignored
        CHECK(swapchain->GetWidth() == 128);
    }

} // namespace Leon::Test
