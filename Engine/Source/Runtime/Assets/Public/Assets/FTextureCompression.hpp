#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Leon {

    /** How the pixels of a cooked texture are stored. The value is the `Format` field of an .ltex. */
    enum class ETextureCompression : uint32_t {
        None = 0, ///< RGBA8, four bytes a pixel.
        BC1 = 1,  ///< Colour, no alpha. Half a byte a pixel.
        BC3 = 2,  ///< Colour with alpha. One byte a pixel.
        BC4 = 3,  ///< One channel (read as red): roughness, metalness, occlusion. Half a byte a pixel.
        BC5 = 4,  ///< Two channels (red, green): a tangent-space normal, whose z is rebuilt. One byte a pixel.
    };

    /**
     * Block compression for cooked textures. The GPU samples these formats directly, so a texture
     * costs a quarter to an eighth of its RGBA8 size both on disk and in video memory.
     *
     * Blocks are 4 x 4 pixels. A level smaller than that is one block, padded by repeating its edge.
     */
    class FTextureCompression {
    public:
        static bool IsValid(uint32_t InFormat) { return InFormat <= static_cast<uint32_t>(ETextureCompression::BC5); }
        /** Bytes of one 4 x 4 block; 0 for None. */
        static uint32_t BytesPerBlock(ETextureCompression InFormat);
        /** Bytes of one mip level of the given size. */
        static size_t MipByteSize(ETextureCompression InFormat, uint32_t InWidth, uint32_t InHeight);

        /** InRGBA8 holds InWidth * InHeight * 4 bytes. Returns the level in InFormat (a copy for None). */
        static std::vector<uint8_t> Compress(ETextureCompression InFormat, const uint8_t* InRGBA8, uint32_t InWidth,
                                             uint32_t InHeight);
        /**
         * Back to RGBA8, for tools that read pixels (the baker, the exporter). BC4 fills red only;
         * BC5 rebuilds blue as the z of a unit normal.
         */
        static std::vector<uint8_t> Decompress(ETextureCompression InFormat, const uint8_t* InData, size_t InSize,
                                               uint32_t InWidth, uint32_t InHeight);
    };

} // namespace Leon
