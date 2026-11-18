#pragma once

#include "RHI/RHITypes.hpp"

#include <functional>
#include <string>
#include <vector>

namespace Leon {

    class FVertexArray;
    class FBufferLayout;
    class FTexture2D;
    class FTextureCube;
    class IRHIBuffer;
    class IRHITexture;

    /**
     * The objects behind the engine's resource classes: FVertexBuffer, FIndexBuffer, FVertexArray,
     * FTexture2D and FTextureCube.
     *
     * Vertex buffers, index buffers and textures created through those interfaces are real RHI
     * resources, so meshes and materials loaded by the asset code can be drawn by the new renderer
     * without touching the asset code. Which shader draws them is the renderer's business.
     *
     * Without an RHI device (tests, tools) the resources are plain memory.
     */
    class FLegacyRenderDriver {
    public:
        /** Registers the driver. Safe to call more than once. */
        static void Register();
    };

    // --- Reaching the RHI objects behind the old interfaces ---------------------------------------

    /** The mesh's vertex buffer on the device; null without a device or for an empty array. */
    IRHIBuffer* GetRHIVertexBuffer(const FVertexArray& InVertexArray);
    IRHIBuffer* GetRHIIndexBuffer(const FVertexArray& InVertexArray);
    uint32_t GetRHIIndexCount(const FVertexArray& InVertexArray);
    /** The array's vertex format; empty when it has no vertex buffer. */
    FRHIVertexLayout GetRHIVertexLayout(const FVertexArray& InVertexArray);
    FRHIVertexLayout MakeRHIVertexLayout(const FBufferLayout& InLayout);

    /** The texture on the device, created and filled on first use; null without a device. */
    IRHITexture* GetRHITexture(const FTexture2D& InTexture);
    IRHITexture* GetRHITexture(const FTextureCube& InTexture);

    // --- Image files ------------------------------------------------------------------------------

    /** An image file decoded for upload. */
    struct FLegacyImage {
        uint32_t Width = 0;
        uint32_t Height = 0;
        /** RGBA8_UNorm, RGBA8_UNorm_sRGB or RGBA32_Float. */
        ERHIFormat Format = ERHIFormat::RGBA8_UNorm;
        /** Largest first, each half the size of the one before, tightly packed. */
        std::vector<std::vector<uint8_t>> Mips;
    };

    /**
     * How FTexture2D::Create(path) reads files. This module cannot decode images itself (the
     * decoders live with the assets), so the asset manager installs the function at start-up.
     * Without one, textures created from a path report IsLoaded() == false.
     */
    using FLegacyImageLoader = std::function<bool(const std::string& InPath, FLegacyImage& OutImage)>;
    void SetLegacyImageLoader(FLegacyImageLoader InLoader);

} // namespace Leon
