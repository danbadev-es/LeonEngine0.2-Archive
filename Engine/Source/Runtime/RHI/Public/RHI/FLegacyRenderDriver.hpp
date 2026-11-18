#pragma once

#include "RHI/RHITypes.hpp"

namespace Leon {

    class FVertexArray;
    class FBufferLayout;
    class IRHIBuffer;

    /**
     * Serves the engine's original resource interfaces (FVertexBuffer, FVertexArray, FTexture2D,
     * FShader, FRenderCommand...) while the renderer is moved to the RHI.
     *
     * Vertex and index buffers created through those interfaces are real RHI buffers, so meshes
     * loaded by the asset code can be drawn by the new renderer without touching the asset code.
     * Everything else (shaders, framebuffers, draw commands) does nothing: the passes still written
     * against the old interfaces run but produce no image.
     *
     * Without an RHI device (tests, tools) the buffers are plain memory.
     */
    class FLegacyRenderDriver {
    public:
        /** Registers the driver and makes it the active API. Safe to call more than once. */
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

} // namespace Leon
