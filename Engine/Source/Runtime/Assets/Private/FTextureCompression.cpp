#include "Assets/FTextureCompression.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace Leon {

    namespace {

        struct FColor3 {
            float R = 0.0f;
            float G = 0.0f;
            float B = 0.0f;
        };

        FColor3 operator+(const FColor3& InA, const FColor3& InB) {
            return {InA.R + InB.R, InA.G + InB.G, InA.B + InB.B};
        }
        FColor3 operator-(const FColor3& InA, const FColor3& InB) {
            return {InA.R - InB.R, InA.G - InB.G, InA.B - InB.B};
        }
        FColor3 operator*(const FColor3& InA, float InScale) {
            return {InA.R * InScale, InA.G * InScale, InA.B * InScale};
        }
        float Dot(const FColor3& InA, const FColor3& InB) {
            return InA.R * InB.R + InA.G * InB.G + InA.B * InB.B;
        }

        uint32_t BlocksAcross(uint32_t InPixels) {
            return std::max(1u, (InPixels + 3) / 4);
        }

        /** The 16 pixels of a block; coordinates past the edge repeat the edge. */
        void ReadBlock(const uint8_t* InRGBA8, uint32_t InWidth, uint32_t InHeight, uint32_t InBlockX,
                       uint32_t InBlockY, uint8_t OutPixels[16][4]) {
            for (uint32_t y = 0; y < 4; ++y) {
                const uint32_t sy = std::min(InBlockY * 4 + y, InHeight - 1);
                for (uint32_t x = 0; x < 4; ++x) {
                    const uint32_t sx = std::min(InBlockX * 4 + x, InWidth - 1);
                    std::memcpy(OutPixels[y * 4 + x], InRGBA8 + (static_cast<size_t>(sy) * InWidth + sx) * 4, 4);
                }
            }
        }

        // --- BC1 colour -------------------------------------------------------------------------

        uint16_t To565(const FColor3& InColor) {
            const auto channel = [](float InValue, float InMax) {
                return static_cast<uint32_t>(std::clamp(InValue / 255.0f * InMax + 0.5f, 0.0f, InMax));
            };
            return static_cast<uint16_t>((channel(InColor.R, 31.0f) << 11) | (channel(InColor.G, 63.0f) << 5) |
                                         channel(InColor.B, 31.0f));
        }

        FColor3 From565(uint16_t InPacked) {
            const uint32_t r = (InPacked >> 11) & 31;
            const uint32_t g = (InPacked >> 5) & 63;
            const uint32_t b = InPacked & 31;
            return {static_cast<float>((r << 3) | (r >> 2)), static_cast<float>((g << 2) | (g >> 4)),
                    static_cast<float>((b << 3) | (b >> 2))};
        }

        /** The four colours of a block in four-colour mode. */
        void Palette(uint16_t InColor0, uint16_t InColor1, FColor3 OutPalette[4]) {
            OutPalette[0] = From565(InColor0);
            OutPalette[1] = From565(InColor1);
            OutPalette[2] = OutPalette[0] * (2.0f / 3.0f) + OutPalette[1] * (1.0f / 3.0f);
            OutPalette[3] = OutPalette[0] * (1.0f / 3.0f) + OutPalette[1] * (2.0f / 3.0f);
        }

        float AssignIndices(const FColor3 InPixels[16], const FColor3 InPalette[4], uint8_t OutIndices[16]) {
            float error = 0.0f;
            for (int i = 0; i < 16; ++i) {
                float best = 1.0e30f;
                for (uint8_t p = 0; p < 4; ++p) {
                    const FColor3 d = InPixels[i] - InPalette[p];
                    const float distance = Dot(d, d);
                    if (distance < best) {
                        best = distance;
                        OutIndices[i] = p;
                    }
                }
                error += best;
            }
            return error;
        }

        /**
         * Eight bytes: two 5:6:5 colours and sixteen 2-bit indices. The endpoints start at the ends
         * of the block's main colour axis and are then fitted to the pixels each index chose.
         */
        void EncodeColorBlock(const uint8_t InPixels[16][4], uint8_t* OutBlock) {
            FColor3 pixels[16];
            FColor3 mean;
            for (int i = 0; i < 16; ++i) {
                pixels[i] = {static_cast<float>(InPixels[i][0]), static_cast<float>(InPixels[i][1]),
                             static_cast<float>(InPixels[i][2])};
                mean = mean + pixels[i];
            }
            mean = mean * (1.0f / 16.0f);

            // Main axis of the colours: power iteration on the covariance.
            float cov[6] = {0, 0, 0, 0, 0, 0}; // rr rg rb gg gb bb
            FColor3 low = pixels[0];
            FColor3 high = pixels[0];
            for (int i = 0; i < 16; ++i) {
                const FColor3 d = pixels[i] - mean;
                cov[0] += d.R * d.R;
                cov[1] += d.R * d.G;
                cov[2] += d.R * d.B;
                cov[3] += d.G * d.G;
                cov[4] += d.G * d.B;
                cov[5] += d.B * d.B;
                low = {std::min(low.R, pixels[i].R), std::min(low.G, pixels[i].G), std::min(low.B, pixels[i].B)};
                high = {std::max(high.R, pixels[i].R), std::max(high.G, pixels[i].G), std::max(high.B, pixels[i].B)};
            }
            FColor3 axis = high - low;
            if (Dot(axis, axis) < 1.0e-6f) {
                // One colour: both endpoints the same, every index 0.
                const uint16_t color = To565(mean);
                OutBlock[0] = static_cast<uint8_t>(color & 0xFF);
                OutBlock[1] = static_cast<uint8_t>(color >> 8);
                OutBlock[2] = OutBlock[0];
                OutBlock[3] = OutBlock[1];
                std::memset(OutBlock + 4, 0, 4);
                return;
            }
            for (int iteration = 0; iteration < 4; ++iteration) {
                const FColor3 next{cov[0] * axis.R + cov[1] * axis.G + cov[2] * axis.B,
                                   cov[1] * axis.R + cov[3] * axis.G + cov[4] * axis.B,
                                   cov[2] * axis.R + cov[4] * axis.G + cov[5] * axis.B};
                const float length = std::sqrt(Dot(next, next));
                if (length < 1.0e-6f)
                    break;
                axis = next * (1.0f / length);
            }
            const float axisLength = std::sqrt(Dot(axis, axis));
            axis = axis * (1.0f / std::max(axisLength, 1.0e-6f));

            float minT = 1.0e30f;
            float maxT = -1.0e30f;
            for (int i = 0; i < 16; ++i) {
                const float t = Dot(pixels[i] - mean, axis);
                minT = std::min(minT, t);
                maxT = std::max(maxT, t);
            }
            FColor3 end0 = mean + axis * maxT;
            FColor3 end1 = mean + axis * minT;

            uint16_t best0 = To565(end0);
            uint16_t best1 = To565(end1);
            uint8_t bestIndices[16] = {};
            float bestError = 1.0e30f;
            for (int pass = 0; pass < 3; ++pass) {
                const uint16_t c0 = To565(end0);
                const uint16_t c1 = To565(end1);
                FColor3 palette[4];
                Palette(c0, c1, palette);
                uint8_t indices[16];
                const float error = AssignIndices(pixels, palette, indices);
                if (error < bestError) {
                    bestError = error;
                    best0 = c0;
                    best1 = c1;
                    std::memcpy(bestIndices, indices, sizeof(indices));
                }

                // Least squares: the endpoints that best explain the pixels given their indices.
                static const float kWeight0[4] = {1.0f, 0.0f, 2.0f / 3.0f, 1.0f / 3.0f};
                float aa = 0.0f, ab = 0.0f, bb = 0.0f;
                FColor3 sumA;
                FColor3 sumB;
                for (int i = 0; i < 16; ++i) {
                    const float a = kWeight0[indices[i]];
                    const float b = 1.0f - a;
                    aa += a * a;
                    ab += a * b;
                    bb += b * b;
                    sumA = sumA + pixels[i] * a;
                    sumB = sumB + pixels[i] * b;
                }
                const float determinant = aa * bb - ab * ab;
                if (std::abs(determinant) < 1.0e-4f)
                    break;
                end0 = (sumA * bb - sumB * ab) * (1.0f / determinant);
                end1 = (sumB * aa - sumA * ab) * (1.0f / determinant);
            }

            // Four-colour mode needs colour 0 above colour 1 as numbers.
            if (best0 < best1) {
                std::swap(best0, best1);
                static const uint8_t kSwapped[4] = {1, 0, 3, 2};
                for (uint8_t& index : bestIndices)
                    index = kSwapped[index];
            } else if (best0 == best1) {
                std::memset(bestIndices, 0, sizeof(bestIndices));
            }

            OutBlock[0] = static_cast<uint8_t>(best0 & 0xFF);
            OutBlock[1] = static_cast<uint8_t>(best0 >> 8);
            OutBlock[2] = static_cast<uint8_t>(best1 & 0xFF);
            OutBlock[3] = static_cast<uint8_t>(best1 >> 8);
            for (int row = 0; row < 4; ++row) {
                OutBlock[4 + row] = static_cast<uint8_t>(bestIndices[row * 4] | (bestIndices[row * 4 + 1] << 2) |
                                                         (bestIndices[row * 4 + 2] << 4) |
                                                         (bestIndices[row * 4 + 3] << 6));
            }
        }

        void DecodeColorBlock(const uint8_t* InBlock, bool bAlwaysFourColors, uint8_t OutPixels[16][4]) {
            const uint16_t c0 = static_cast<uint16_t>(InBlock[0] | (InBlock[1] << 8));
            const uint16_t c1 = static_cast<uint16_t>(InBlock[2] | (InBlock[3] << 8));
            FColor3 palette[4];
            Palette(c0, c1, palette);
            bool bTransparent = false;
            if (!bAlwaysFourColors && c0 <= c1) {
                palette[2] = (palette[0] + palette[1]) * 0.5f;
                palette[3] = {0.0f, 0.0f, 0.0f};
                bTransparent = true;
            }
            for (int i = 0; i < 16; ++i) {
                const uint8_t index = (InBlock[4 + i / 4] >> ((i % 4) * 2)) & 3;
                OutPixels[i][0] = static_cast<uint8_t>(palette[index].R + 0.5f);
                OutPixels[i][1] = static_cast<uint8_t>(palette[index].G + 0.5f);
                OutPixels[i][2] = static_cast<uint8_t>(palette[index].B + 0.5f);
                OutPixels[i][3] = (bTransparent && index == 3) ? uint8_t{0} : uint8_t{255};
            }
        }

        // --- BC4 single channel -----------------------------------------------------------------

        void ValuePalette(uint8_t InValue0, uint8_t InValue1, float OutPalette[8]) {
            OutPalette[0] = InValue0;
            OutPalette[1] = InValue1;
            if (InValue0 > InValue1) {
                for (int i = 1; i <= 6; ++i)
                    OutPalette[1 + i] = (static_cast<float>(7 - i) * InValue0 + static_cast<float>(i) * InValue1) / 7.0f;
            } else {
                for (int i = 1; i <= 4; ++i)
                    OutPalette[1 + i] = (static_cast<float>(5 - i) * InValue0 + static_cast<float>(i) * InValue1) / 5.0f;
                OutPalette[6] = 0.0f;
                OutPalette[7] = 255.0f;
            }
        }

        /** Eight bytes: two 8-bit values and sixteen 3-bit indices into the eight steps between them. */
        void EncodeValueBlock(const uint8_t InValues[16], uint8_t* OutBlock) {
            uint8_t low = 255;
            uint8_t high = 0;
            for (int i = 0; i < 16; ++i) {
                low = std::min(low, InValues[i]);
                high = std::max(high, InValues[i]);
            }
            if (low == high) {
                OutBlock[0] = high;
                OutBlock[1] = low;
                std::memset(OutBlock + 2, 0, 6);
                return;
            }

            // The extremes are not always the best endpoints: try pulling each end in a little.
            const int range = high - low;
            const int step = std::max(1, range / 16);
            uint8_t bestHigh = high;
            uint8_t bestLow = low;
            float bestError = 1.0e30f;
            for (int insetHigh = 0; insetHigh < 3; ++insetHigh) {
                for (int insetLow = 0; insetLow < 3; ++insetLow) {
                    const int candidateHigh = high - insetHigh * step;
                    const int candidateLow = low + insetLow * step;
                    if (candidateHigh <= candidateLow)
                        continue;
                    float palette[8];
                    ValuePalette(static_cast<uint8_t>(candidateHigh), static_cast<uint8_t>(candidateLow), palette);
                    float error = 0.0f;
                    for (int i = 0; i < 16; ++i) {
                        float best = 1.0e30f;
                        for (int p = 0; p < 8; ++p) {
                            const float d = static_cast<float>(InValues[i]) - palette[p];
                            best = std::min(best, d * d);
                        }
                        error += best;
                    }
                    if (error < bestError) {
                        bestError = error;
                        bestHigh = static_cast<uint8_t>(candidateHigh);
                        bestLow = static_cast<uint8_t>(candidateLow);
                    }
                }
            }

            float palette[8];
            ValuePalette(bestHigh, bestLow, palette);
            uint64_t bits = 0;
            for (int i = 0; i < 16; ++i) {
                float best = 1.0e30f;
                uint64_t index = 0;
                for (int p = 0; p < 8; ++p) {
                    const float d = static_cast<float>(InValues[i]) - palette[p];
                    if (d * d < best) {
                        best = d * d;
                        index = static_cast<uint64_t>(p);
                    }
                }
                bits |= index << (3 * i);
            }
            OutBlock[0] = bestHigh;
            OutBlock[1] = bestLow;
            for (int i = 0; i < 6; ++i)
                OutBlock[2 + i] = static_cast<uint8_t>((bits >> (8 * i)) & 0xFF);
        }

        void DecodeValueBlock(const uint8_t* InBlock, uint8_t OutValues[16]) {
            float palette[8];
            ValuePalette(InBlock[0], InBlock[1], palette);
            uint64_t bits = 0;
            for (int i = 0; i < 6; ++i)
                bits |= static_cast<uint64_t>(InBlock[2 + i]) << (8 * i);
            for (int i = 0; i < 16; ++i)
                OutValues[i] = static_cast<uint8_t>(palette[(bits >> (3 * i)) & 7] + 0.5f);
        }

    } // namespace

    uint32_t FTextureCompression::BytesPerBlock(ETextureCompression InFormat) {
        switch (InFormat) {
        case ETextureCompression::BC1:
        case ETextureCompression::BC4:
            return 8;
        case ETextureCompression::BC3:
        case ETextureCompression::BC5:
            return 16;
        case ETextureCompression::None:
            return 0;
        }
        return 0;
    }

    size_t FTextureCompression::MipByteSize(ETextureCompression InFormat, uint32_t InWidth, uint32_t InHeight) {
        if (InFormat == ETextureCompression::None)
            return static_cast<size_t>(InWidth) * InHeight * 4;
        return static_cast<size_t>(BlocksAcross(InWidth)) * BlocksAcross(InHeight) * BytesPerBlock(InFormat);
    }

    std::vector<uint8_t> FTextureCompression::Compress(ETextureCompression InFormat, const uint8_t* InRGBA8,
                                                       uint32_t InWidth, uint32_t InHeight) {
        if (!InRGBA8 || InWidth == 0 || InHeight == 0)
            return {};
        if (InFormat == ETextureCompression::None)
            return std::vector<uint8_t>(InRGBA8, InRGBA8 + static_cast<size_t>(InWidth) * InHeight * 4);

        const uint32_t blockBytes = BytesPerBlock(InFormat);
        const uint32_t blocksX = BlocksAcross(InWidth);
        const uint32_t blocksY = BlocksAcross(InHeight);
        std::vector<uint8_t> out(static_cast<size_t>(blocksX) * blocksY * blockBytes);
        for (uint32_t by = 0; by < blocksY; ++by) {
            for (uint32_t bx = 0; bx < blocksX; ++bx) {
                uint8_t pixels[16][4];
                ReadBlock(InRGBA8, InWidth, InHeight, bx, by, pixels);
                uint8_t* block = out.data() + (static_cast<size_t>(by) * blocksX + bx) * blockBytes;
                uint8_t channel[16];
                switch (InFormat) {
                case ETextureCompression::BC1:
                    EncodeColorBlock(pixels, block);
                    break;
                case ETextureCompression::BC3:
                    for (int i = 0; i < 16; ++i)
                        channel[i] = pixels[i][3];
                    EncodeValueBlock(channel, block);
                    EncodeColorBlock(pixels, block + 8);
                    break;
                case ETextureCompression::BC4:
                    for (int i = 0; i < 16; ++i)
                        channel[i] = pixels[i][0];
                    EncodeValueBlock(channel, block);
                    break;
                case ETextureCompression::BC5:
                    for (int i = 0; i < 16; ++i)
                        channel[i] = pixels[i][0];
                    EncodeValueBlock(channel, block);
                    for (int i = 0; i < 16; ++i)
                        channel[i] = pixels[i][1];
                    EncodeValueBlock(channel, block + 8);
                    break;
                case ETextureCompression::None:
                    break;
                }
            }
        }
        return out;
    }

    std::vector<uint8_t> FTextureCompression::Decompress(ETextureCompression InFormat, const uint8_t* InData,
                                                         size_t InSize, uint32_t InWidth, uint32_t InHeight) {
        if (!InData || InWidth == 0 || InHeight == 0 || InSize < MipByteSize(InFormat, InWidth, InHeight))
            return {};
        if (InFormat == ETextureCompression::None)
            return std::vector<uint8_t>(InData, InData + static_cast<size_t>(InWidth) * InHeight * 4);

        const uint32_t blockBytes = BytesPerBlock(InFormat);
        const uint32_t blocksX = BlocksAcross(InWidth);
        const uint32_t blocksY = BlocksAcross(InHeight);
        std::vector<uint8_t> out(static_cast<size_t>(InWidth) * InHeight * 4);
        for (uint32_t by = 0; by < blocksY; ++by) {
            for (uint32_t bx = 0; bx < blocksX; ++bx) {
                const uint8_t* block = InData + (static_cast<size_t>(by) * blocksX + bx) * blockBytes;
                uint8_t pixels[16][4];
                uint8_t first[16];
                uint8_t second[16];
                switch (InFormat) {
                case ETextureCompression::BC1:
                    DecodeColorBlock(block, false, pixels);
                    break;
                case ETextureCompression::BC3:
                    DecodeColorBlock(block + 8, true, pixels);
                    DecodeValueBlock(block, first);
                    for (int i = 0; i < 16; ++i)
                        pixels[i][3] = first[i];
                    break;
                case ETextureCompression::BC4:
                    DecodeValueBlock(block, first);
                    for (int i = 0; i < 16; ++i) {
                        pixels[i][0] = first[i];
                        pixels[i][1] = 0;
                        pixels[i][2] = 0;
                        pixels[i][3] = 255;
                    }
                    break;
                case ETextureCompression::BC5:
                    DecodeValueBlock(block, first);
                    DecodeValueBlock(block + 8, second);
                    for (int i = 0; i < 16; ++i) {
                        const float x = static_cast<float>(first[i]) / 255.0f * 2.0f - 1.0f;
                        const float y = static_cast<float>(second[i]) / 255.0f * 2.0f - 1.0f;
                        const float z = std::sqrt(std::max(0.0f, 1.0f - x * x - y * y));
                        pixels[i][0] = first[i];
                        pixels[i][1] = second[i];
                        pixels[i][2] = static_cast<uint8_t>((z * 0.5f + 0.5f) * 255.0f + 0.5f);
                        pixels[i][3] = 255;
                    }
                    break;
                case ETextureCompression::None:
                    break;
                }
                for (uint32_t y = 0; y < 4; ++y) {
                    const uint32_t py = by * 4 + y;
                    if (py >= InHeight)
                        break;
                    for (uint32_t x = 0; x < 4; ++x) {
                        const uint32_t px = bx * 4 + x;
                        if (px >= InWidth)
                            break;
                        std::memcpy(out.data() + (static_cast<size_t>(py) * InWidth + px) * 4, pixels[y * 4 + x], 4);
                    }
                }
            }
        }
        return out;
    }

} // namespace Leon
