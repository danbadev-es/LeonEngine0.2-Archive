#pragma once

#include <string>

namespace Leon {

    struct FLegacyImage;

    /**
     * Decodes an image file into pixels ready for the GPU, with its mip chain: cooked textures
     * (.ltex, .lhdr) and source images (PNG, JPG, TGA, BMP, HDR).
     *
     * Rows come out in the order the engine's texture coordinates expect, so the result is uploaded
     * as it is. Returns false, after logging why, when the file cannot be read.
     */
    bool LoadImageFile(const std::string& InPath, FLegacyImage& OutImage);

    /** Makes textures created from a path (FTexture2D::Create) use LoadImageFile. */
    void InstallImageFileLoader();

} // namespace Leon
