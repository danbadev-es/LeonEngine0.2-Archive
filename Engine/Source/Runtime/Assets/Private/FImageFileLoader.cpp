#include "Assets/FImageFileLoader.hpp"

#include "Assets/FHDRImporter.hpp"
#include "Assets/FTextureImporter.hpp"
#include "Core/FLog.hpp"
#include "RHI/FLegacyRenderDriver.hpp"
#include "RHI/FRenderer.hpp"
#include "Renderer/FColorSpace.hpp"

#include <stb_image.h>

#include <algorithm>
#include <cstring>

namespace Leon {

    namespace {

        bool EndsWith(const std::string& InText, const char* InSuffix) {
            const size_t length = std::strlen(InSuffix);
            return InText.size() >= length && InText.compare(InText.size() - length, length, InSuffix) == 0;
        }

        /** 8-bit RGBA with a full mip chain. */
        void FillFromRGBA8(std::vector<uint8_t> InPixels, uint32_t InWidth, uint32_t InHeight, bool bInSRGB,
                           FLegacyImage& OutImage) {
            std::vector<FTextureMipData> mips;
            FTextureImporter::GenerateMipmaps(InWidth, InHeight, InPixels, mips, bInSRGB);
            OutImage.Width = InWidth;
            OutImage.Height = InHeight;
            OutImage.Format = bInSRGB ? ERHIFormat::RGBA8_UNorm_sRGB : ERHIFormat::RGBA8_UNorm;
            for (FTextureMipData& mip : mips)
                OutImage.Mips.push_back(std::move(mip.Pixels));
        }

        /** Float RGBA with a full mip chain (2x2 box filter). */
        void FillFromRGBA32F(const float* InPixels, uint32_t InWidth, uint32_t InHeight, FLegacyImage& OutImage) {
            OutImage.Width = InWidth;
            OutImage.Height = InHeight;
            OutImage.Format = ERHIFormat::RGBA32_Float;
            const size_t bytes = static_cast<size_t>(InWidth) * InHeight * 4 * sizeof(float);
            OutImage.Mips.emplace_back(reinterpret_cast<const uint8_t*>(InPixels),
                                       reinterpret_cast<const uint8_t*>(InPixels) + bytes);
            uint32_t width = InWidth;
            uint32_t height = InHeight;
            while (width > 1 || height > 1) {
                const uint32_t nextWidth = std::max(1u, width / 2);
                const uint32_t nextHeight = std::max(1u, height / 2);
                std::vector<uint8_t> next(static_cast<size_t>(nextWidth) * nextHeight * 4 * sizeof(float));
                const float* source = reinterpret_cast<const float*>(OutImage.Mips.back().data());
                float* target = reinterpret_cast<float*>(next.data());
                for (uint32_t y = 0; y < nextHeight; ++y) {
                    const uint32_t y0 = std::min(y * 2, height - 1);
                    const uint32_t y1 = std::min(y * 2 + 1, height - 1);
                    for (uint32_t x = 0; x < nextWidth; ++x) {
                        const uint32_t x0 = std::min(x * 2, width - 1);
                        const uint32_t x1 = std::min(x * 2 + 1, width - 1);
                        for (uint32_t c = 0; c < 4; ++c) {
                            target[(static_cast<size_t>(y) * nextWidth + x) * 4 + c] =
                                0.25f * (source[(static_cast<size_t>(y0) * width + x0) * 4 + c] +
                                         source[(static_cast<size_t>(y0) * width + x1) * 4 + c] +
                                         source[(static_cast<size_t>(y1) * width + x0) * 4 + c] +
                                         source[(static_cast<size_t>(y1) * width + x1) * 4 + c]);
                        }
                    }
                }
                OutImage.Mips.push_back(std::move(next));
                width = nextWidth;
                height = nextHeight;
            }
        }

        /** Honours the texture resolution limit of the quality settings by dropping the largest mips. */
        void ApplyResolutionLimit(FLegacyImage& InOutImage) {
            const uint32_t limit = FRenderer::GetMaxTextureResolution();
            while (InOutImage.Mips.size() > 1 && std::max(InOutImage.Width, InOutImage.Height) > limit) {
                InOutImage.Mips.erase(InOutImage.Mips.begin());
                InOutImage.Width = std::max(1u, InOutImage.Width / 2);
                InOutImage.Height = std::max(1u, InOutImage.Height / 2);
            }
        }

        bool LoadNativeTexture(const std::string& InPath, FLegacyImage& OutImage) {
            FNativeTextureData native;
            if (!native.LoadFromFile(InPath)) {
                LE_CORE_ERROR("Failed to load native .ltex texture from: {0}", InPath);
                return false;
            }
            const bool bSRGB = native.Header.ColorSpace == 1;
            std::sort(native.Mips.begin(), native.Mips.end(),
                      [](const FTextureMipData& InA, const FTextureMipData& InB) { return InA.Level < InB.Level; });
            if (native.Mips.empty() || native.Mips.front().Level != 0 || native.Mips.front().Pixels.empty())
                return false;

            // Use the cooked chain when it is complete; otherwise rebuild it from the top level.
            bool bComplete = true;
            uint32_t width = native.Header.Width;
            uint32_t height = native.Header.Height;
            for (size_t i = 0; i < native.Mips.size(); ++i) {
                const FTextureMipData& mip = native.Mips[i];
                if (mip.Level != i || mip.Width != width || mip.Height != height ||
                    mip.Pixels.size() != static_cast<size_t>(width) * height * 4) {
                    bComplete = false;
                    break;
                }
                width = std::max(1u, width / 2);
                height = std::max(1u, height / 2);
            }
            if (!bComplete) {
                FillFromRGBA8(std::move(native.Mips.front().Pixels), native.Header.Width, native.Header.Height, bSRGB,
                              OutImage);
                return true;
            }
            OutImage.Width = native.Header.Width;
            OutImage.Height = native.Header.Height;
            OutImage.Format = bSRGB ? ERHIFormat::RGBA8_UNorm_sRGB : ERHIFormat::RGBA8_UNorm;
            for (FTextureMipData& mip : native.Mips)
                OutImage.Mips.push_back(std::move(mip.Pixels));
            return true;
        }

        bool LoadNativeHDR(const std::string& InPath, FLegacyImage& OutImage) {
            FNativeHDRData native;
            if (!native.LoadFromFile(InPath) ||
                native.Pixels.size() < static_cast<size_t>(native.Header.Width) * native.Header.Height * 4) {
                LE_CORE_ERROR("Failed to load native .lhdr texture from: {0}", InPath);
                return false;
            }
            FillFromRGBA32F(native.Pixels.data(), native.Header.Width, native.Header.Height, OutImage);
            return true;
        }

        bool LoadWithStb(const std::string& InPath, FLegacyImage& OutImage) {
            // Rows are stored bottom first, as the engine's texture coordinates expect (V = 0 at the
            // bottom of the picture).
            stbi_set_flip_vertically_on_load(1);
            int width = 0;
            int height = 0;
            int channels = 0;

            if (stbi_is_hdr(InPath.c_str())) {
                float* pixels = stbi_loadf(InPath.c_str(), &width, &height, &channels, 4);
                if (!pixels) {
                    LE_CORE_ERROR("Failed to load HDR texture from: {0}", InPath);
                    return false;
                }
                FillFromRGBA32F(pixels, static_cast<uint32_t>(width), static_cast<uint32_t>(height), OutImage);
                stbi_image_free(pixels);
                return true;
            }

            stbi_uc* pixels = stbi_load(InPath.c_str(), &width, &height, &channels, 0);
            if (!pixels) {
                LE_CORE_ERROR("Failed to load texture from: {0}", InPath);
                return false;
            }
            if (channels != 1 && channels != 3 && channels != 4) {
                LE_CORE_ERROR("Texture format not supported for: {0} ({1} channels)", InPath, channels);
                stbi_image_free(pixels);
                return false;
            }
            const size_t count = static_cast<size_t>(width) * height;
            std::vector<uint8_t> rgba(count * 4);
            for (size_t i = 0; i < count; ++i) {
                // A single channel is data (a mask, a height): red only, as shaders read it.
                rgba[i * 4 + 0] = pixels[i * channels];
                rgba[i * 4 + 1] = channels >= 3 ? pixels[i * channels + 1] : uint8_t{0};
                rgba[i * 4 + 2] = channels >= 3 ? pixels[i * channels + 2] : uint8_t{0};
                rgba[i * 4 + 3] = channels == 4 ? pixels[i * channels + 3] : uint8_t{255};
            }
            stbi_image_free(pixels);
            const bool bSRGB = channels >= 3 && !IsLinearDataTexturePath(InPath);
            FillFromRGBA8(std::move(rgba), static_cast<uint32_t>(width), static_cast<uint32_t>(height), bSRGB, OutImage);
            return true;
        }

    } // namespace

    bool LoadImageFile(const std::string& InPath, FLegacyImage& OutImage) {
        OutImage = {};
        bool bLoaded = false;
        if (EndsWith(InPath, ".ltex"))
            bLoaded = LoadNativeTexture(InPath, OutImage);
        else if (EndsWith(InPath, ".lhdr"))
            bLoaded = LoadNativeHDR(InPath, OutImage);
        else
            bLoaded = LoadWithStb(InPath, OutImage);
        if (bLoaded)
            ApplyResolutionLimit(OutImage);
        return bLoaded;
    }

    void InstallImageFileLoader() {
        SetLegacyImageLoader(&LoadImageFile);
    }

} // namespace Leon
