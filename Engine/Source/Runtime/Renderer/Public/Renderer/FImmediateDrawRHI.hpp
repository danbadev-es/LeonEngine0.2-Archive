#pragma once

#include "RHI/IRHIDevice.hpp"

#include <glm/glm.hpp>

namespace Leon {

    // Drawing for geometry that is rebuilt every frame: the HUD, debug overlays, world-space text,
    // debug lines and particles. Vertices are copied when submitted, so callers can reuse their arrays.

    /** A corner of a 2D shape. */
    struct FCanvasVertex {
        glm::vec2 Position; ///< In the units of the projection passed with the draw (pixels for the HUD).
        glm::vec2 TexCoord;
        glm::vec4 Color;
    };

    /** 2D shapes on top of the finished frame (see FRHIFrame). Does nothing outside a frame. */
    class FCanvasRHI {
    public:
        /**
         * InCount vertices forming triangles, alpha blended over the frame's output.
         * With InAlphaTexture the alpha of each pixel is multiplied by the texture's alpha (glyphs from
         * a font atlas); the colour always comes from the vertices.
         */
        static void DrawTriangles(const FCanvasVertex* InVertices, uint32_t InCount, const glm::mat4& InProjection,
                                  IRHITexture* InAlphaTexture = nullptr);
    };

    /** How a batch submitted to FSceneBatchesRHI is drawn. */
    enum class ESceneBatch : uint8_t {
        Lines,             ///< Line list of {position, colour}; hidden by nearer geometry.
        LinesOnTop,        ///< Same, drawn over everything.
        AdditiveTriangles, ///< Triangles of {position, colour} added to the image (particles).
        Text,              ///< Triangles of {position, texture coordinate, colour} with a font atlas.
    };

    /**
     * World-space shapes drawn with the scene. They are collected while the frame is built and drawn
     * by the world renderer inside its scene pass, so they are lit by nothing but share the scene's
     * depth buffer and go through the same post-processing.
     *
     * Vertex formats: Lines and AdditiveTriangles take {vec3 position, vec4 colour} (28 bytes); Text
     * takes {vec3 position, vec2 texture coordinate, vec4 colour} (36 bytes).
     */
    class FSceneBatchesRHI {
    public:
        static void Submit(ESceneBatch InKind, const void* InVertices, uint32_t InCount,
                           IRHITexture* InTexture = nullptr);

        /**
         * Draws what was submitted and forgets it. Call inside the scene's render pass, with the frame
         * constants (camera) already set; the formats are those of the pass's targets.
         */
        static void Flush(IRHICommandList& InCommands, ERHIFormat InColorFormat, ERHIFormat InDepthFormat);
    };

    /** Releases the pipelines both classes above keep. Call before the device goes away. */
    void ShutdownImmediateDrawRHI();

} // namespace Leon
