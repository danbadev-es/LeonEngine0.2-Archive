#include "Assets/FLightmapAsset.hpp"
#include "Core/FLog.hpp"
#include "RHI/FTexture.hpp"
#include "RenderCore/FIBLMath.hpp"

#include <glm/gtc/packing.hpp>
#include <stb_image.h>
#include <stb_image_write.h>

#include <cstdlib>
#include <cstring>
#include <fstream>

// Defined by stb_image_write (ThirdParty/stb/stb_image.cpp); its header does not declare it.
extern "C" unsigned char* stbi_zlib_compress(unsigned char* data, int data_len, int* out_len, int quality);

namespace Leon {

    void FLightmapAsset::Allocate(uint32_t InWidth, uint32_t InHeight) {
        Header.Width = InWidth;
        Header.Height = InHeight;
        Header.PixelFormat = static_cast<uint32_t>(ELightmapPixelFormat::RGBA32F);
        Header.ChannelCount = 4;
        Header.bIsHDR = 1;
        Header.PayloadSize = InWidth * InHeight * 4 * sizeof(float);
        Pixels.assign(static_cast<size_t>(InWidth) * InHeight * 4, 0.0f);
        GPUTexture.reset();
    }

    void FLightmapAsset::ComputeContentHash() {
        Header.ContentHash = ComputePixelHash();
    }

    uint64_t FLightmapAsset::ComputePixelHash() const {
        uint64_t hash = 14695981039346656037ull;
        auto feed = [&](const void* data, size_t size) {
            const auto* bytes = static_cast<const uint8_t*>(data);
            for (size_t i = 0; i < size; ++i) {
                hash ^= bytes[i];
                hash *= 1099511628211ull;
            }
        };
        feed(&Header.Width, sizeof(Header.Width));
        feed(&Header.Height, sizeof(Header.Height));
        feed(&Header.PixelFormat, sizeof(Header.PixelFormat));
        if (!Pixels.empty())
            feed(Pixels.data(), Pixels.size() * sizeof(float));
        return hash;
    }

    namespace {

        /**
         * What a version 3 file holds: the atlas as half floats, each texel stored as its difference
         * from the one on its left, split into a plane of low bytes and a plane of high bytes per
         * channel, and deflated. Lightmaps are smooth and largely empty, so this is a small fraction
         * of the floats (which is what version 2 stored, at 16 bytes a texel).
         */
        std::vector<uint8_t> PackPlanes(const std::vector<float>& InPixels, uint32_t InWidth, uint32_t InHeight) {
            const size_t texels = static_cast<size_t>(InWidth) * InHeight;
            std::vector<uint8_t> planes(texels * 8);
            for (uint32_t channel = 0; channel < 4; ++channel) {
                uint8_t* low = planes.data() + texels * (channel * 2);
                uint8_t* high = low + texels;
                for (uint32_t y = 0; y < InHeight; ++y) {
                    uint16_t previous = 0;
                    for (uint32_t x = 0; x < InWidth; ++x) {
                        const size_t texel = static_cast<size_t>(y) * InWidth + x;
                        const uint16_t value = glm::packHalf1x16(InPixels[texel * 4 + channel]);
                        const uint16_t delta = static_cast<uint16_t>(value - previous);
                        previous = value;
                        low[texel] = static_cast<uint8_t>(delta & 0xFF);
                        high[texel] = static_cast<uint8_t>(delta >> 8);
                    }
                }
            }
            return planes;
        }

        void UnpackPlanes(const uint8_t* InPlanes, uint32_t InWidth, uint32_t InHeight, std::vector<float>& OutPixels) {
            const size_t texels = static_cast<size_t>(InWidth) * InHeight;
            OutPixels.resize(texels * 4);
            for (uint32_t channel = 0; channel < 4; ++channel) {
                const uint8_t* low = InPlanes + texels * (channel * 2);
                const uint8_t* high = low + texels;
                for (uint32_t y = 0; y < InHeight; ++y) {
                    uint16_t previous = 0;
                    for (uint32_t x = 0; x < InWidth; ++x) {
                        const size_t texel = static_cast<size_t>(y) * InWidth + x;
                        const uint16_t delta = static_cast<uint16_t>(low[texel] | (high[texel] << 8));
                        previous = static_cast<uint16_t>(previous + delta);
                        OutPixels[texel * 4 + channel] = glm::unpackHalf1x16(previous);
                    }
                }
            }
        }

    } // namespace

    bool FLightmapAsset::SaveToFile(const std::string& InFilePath) const {
        std::ofstream file(InFilePath, std::ios::binary);
        if (!file.is_open()) {
            LE_CORE_ERROR("FLightmapAsset: Failed to write \"{0}\"", InFilePath);
            return false;
        }

        FLightmapHeader header = Header;
        header.Magic = LLIGHTMAP_MAGIC;
        header.Version = LLIGHTMAP_VERSION;
        header.PixelFormat = static_cast<uint32_t>(ELightmapPixelFormat::RGBA16F);
        header.PayloadSize = 0;

        unsigned char* packed = nullptr;
        if (!Pixels.empty()) {
            std::vector<uint8_t> planes = PackPlanes(Pixels, Header.Width, Header.Height);
            int packedSize = 0;
            packed = stbi_zlib_compress(planes.data(), static_cast<int>(planes.size()), &packedSize, 8);
            if (!packed || packedSize <= 0) {
                LE_CORE_ERROR("FLightmapAsset: Failed to compress \"{0}\"", InFilePath);
                return false;
            }
            header.PayloadSize = static_cast<uint32_t>(packedSize);
        }

        file.write(reinterpret_cast<const char*>(&header), sizeof(header));
        if (packed) {
            file.write(reinterpret_cast<const char*>(packed), static_cast<std::streamsize>(header.PayloadSize));
            std::free(packed);
        }
        return file.good();
    }

    bool FLightmapAsset::LoadFromFile(const std::string& InFilePath) {
        std::ifstream file(InFilePath, std::ios::binary);
        if (!file.is_open()) {
            LE_CORE_ERROR("FLightmapAsset: Failed to open \"{0}\"", InFilePath);
            return false;
        }

        FLightmapHeader header{};
        file.read(reinterpret_cast<char*>(&header), sizeof(header));
        if (!file.good() || header.Magic != LLIGHTMAP_MAGIC) {
            LE_CORE_ERROR("FLightmapAsset: Invalid magic in \"{0}\"", InFilePath);
            return false;
        }
        if (header.Version != LLIGHTMAP_VERSION && header.Version != LLIGHTMAP_VERSION_FLOATS) {
            LE_CORE_ERROR("FLightmapAsset: Unsupported version {0} in \"{1}\"", header.Version, InFilePath);
            return false;
        }
        if (header.Width == 0 || header.Height == 0 || header.ChannelCount != 4 || header.Width > 16384 || header.Height > 16384) {
            LE_CORE_ERROR("FLightmapAsset: Invalid dimensions in \"{0}\"", InFilePath);
            return false;
        }

        const size_t texels = static_cast<size_t>(header.Width) * header.Height;
        std::vector<float> pixels;
        if (header.Version == LLIGHTMAP_VERSION_FLOATS) {
            // Version 2: the floats as they are.
            const size_t expectedBytes = texels * 4 * sizeof(float);
            if (header.PayloadSize != expectedBytes) {
                LE_CORE_ERROR("FLightmapAsset: Payload size mismatch in \"{0}\"", InFilePath);
                return false;
            }
            pixels.resize(texels * 4);
            file.read(reinterpret_cast<char*>(pixels.data()), static_cast<std::streamsize>(expectedBytes));
            if (!file.good()) {
                LE_CORE_ERROR("FLightmapAsset: Truncated payload in \"{0}\"", InFilePath);
                return false;
            }
        } else {
            std::vector<char> packed(header.PayloadSize);
            file.read(packed.data(), static_cast<std::streamsize>(packed.size()));
            if (!file.good() || packed.empty()) {
                LE_CORE_ERROR("FLightmapAsset: Truncated payload in \"{0}\"", InFilePath);
                return false;
            }
            const size_t expectedBytes = texels * 8;
            int planeBytes = 0;
            char* planes = stbi_zlib_decode_malloc_guesssize(packed.data(), static_cast<int>(packed.size()),
                                                             static_cast<int>(expectedBytes), &planeBytes);
            if (!planes || static_cast<size_t>(planeBytes) != expectedBytes) {
                std::free(planes);
                LE_CORE_ERROR("FLightmapAsset: Corrupt payload in \"{0}\"", InFilePath);
                return false;
            }
            UnpackPlanes(reinterpret_cast<const uint8_t*>(planes), header.Width, header.Height, pixels);
            std::free(planes);
        }

        Pixels = std::move(pixels);
        Header = header;
        Header.Version = LLIGHTMAP_VERSION;
        AssetPath = InFilePath;
        GPUTexture.reset();
        return true;
    }

    TRef<FTexture2D> FLightmapAsset::GetOrCreateGPUTexture() {
        if (GPUTexture)
            return GPUTexture;
        if (Pixels.empty() || Header.Width == 0 || Header.Height == 0)
            return nullptr;

        GPUTexture = FTexture2D::CreateWithFormat(Header.Width, Header.Height, ETextureFormat::RGBA16F);
        if (!GPUTexture)
            return nullptr;
        GPUTexture->SetDataFloat(Pixels.data(), static_cast<uint32_t>(Pixels.size() * sizeof(float)));
        return GPUTexture;
    }

} // namespace Leon
