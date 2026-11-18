#include "RHI/FLegacyRenderDriver.hpp"

#include "RHI/FTexture.hpp"
#include "RHI/FTextureCube.hpp"
#include "RHI/IRHIDevice.hpp"
#include "RHI/IRenderDriver.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <string>
#include <vector>

namespace Leon {

    namespace {

        /** Vertex data kept in memory until the first draw needs it on the device. */
        class FLegacyVertexBuffer final : public FVertexBuffer {
        public:
            explicit FLegacyVertexBuffer(unsigned int InSize) : Bytes(InSize), bDynamic(true) {}
            FLegacyVertexBuffer(const void* InData, unsigned int InSize)
                : Bytes(static_cast<const uint8_t*>(InData), static_cast<const uint8_t*>(InData) + InSize) {}

            void Bind() const override {}
            void Unbind() const override {}
            void SetData(const void* InData, unsigned int InSize) override {
                if (!InData)
                    return;
                if (InSize > Bytes.size())
                    Bytes.resize(InSize);
                std::memcpy(Bytes.data(), InData, InSize);
                if (Buffer && bDynamic && InSize <= Buffer->GetDesc().Size) {
                    if (IRHIDevice* device = FRHI::GetDevice())
                        device->WriteBuffer(*Buffer, InData, InSize);
                } else {
                    Buffer.reset(); // grew, or was static: recreate on next use
                }
            }
            const FBufferLayout& GetLayout() const override { return Layout; }
            void SetLayout(const FBufferLayout& InLayout) override { Layout = InLayout; }

            IRHIBuffer* GetOrCreate() {
                IRHIDevice* device = FRHI::GetDevice();
                if (!device || Bytes.empty())
                    return nullptr;
                if (!Buffer) {
                    FRHIBufferDesc desc;
                    desc.Size = Bytes.size();
                    desc.Usage = ERHIBufferUsage::Vertex;
                    desc.Stride = Layout.GetStride();
                    desc.bDynamic = bDynamic;
                    desc.DebugName = "LegacyVertexBuffer";
                    Buffer = device->CreateBuffer(desc, Bytes.data());
                }
                return Buffer.get();
            }

        private:
            FBufferLayout Layout;
            std::vector<uint8_t> Bytes;
            TRef<IRHIBuffer> Buffer;
            bool bDynamic = false;
        };

        class FLegacyIndexBuffer final : public FIndexBuffer {
        public:
            FLegacyIndexBuffer(const uint32_t* InIndices, unsigned int InCount)
                : Indices(InIndices ? std::vector<uint32_t>(InIndices, InIndices + InCount) : std::vector<uint32_t>()),
                  Count(InCount) {}
            void Bind() const override {}
            void Unbind() const override {}
            unsigned int GetCount() const override { return Count; }

            IRHIBuffer* GetOrCreate() {
                IRHIDevice* device = FRHI::GetDevice();
                if (!device || Indices.empty())
                    return nullptr;
                if (!Buffer) {
                    FRHIBufferDesc desc;
                    desc.Size = Indices.size() * sizeof(uint32_t);
                    desc.Usage = ERHIBufferUsage::Index;
                    desc.Stride = sizeof(uint32_t);
                    desc.DebugName = "LegacyIndexBuffer";
                    Buffer = device->CreateBuffer(desc, Indices.data());
                }
                return Buffer.get();
            }

        private:
            std::vector<uint32_t> Indices;
            unsigned int Count;
            TRef<IRHIBuffer> Buffer;
        };

        class FNullVertexArray final : public FVertexArray {
        public:
            void Bind() const override {}
            void Unbind() const override {}
            void AddVertexBuffer(const TRef<FVertexBuffer>& InVertexBuffer) override {
                VertexBuffers.push_back(InVertexBuffer);
            }
            void SetIndexBuffer(const TRef<FIndexBuffer>& InIndexBuffer) override { IndexBuffer = InIndexBuffer; }
            const std::vector<TRef<FVertexBuffer>>& GetVertexBuffers() const override { return VertexBuffers; }
            const TRef<FIndexBuffer>& GetIndexBuffer() const override { return IndexBuffer; }

        private:
            std::vector<TRef<FVertexBuffer>> VertexBuffers;
            TRef<FIndexBuffer> IndexBuffer;
        };

        uint32_t NextNullId() {
            static uint32_t next = 1;
            return next++;
        }

        FLegacyImageLoader& ImageLoader() {
            static FLegacyImageLoader loader;
            return loader;
        }

        uint32_t MipCountFor(uint32_t InWidth, uint32_t InHeight) {
            uint32_t levels = 1;
            for (uint32_t size = std::max(InWidth, InHeight); size > 1; size /= 2)
                ++levels;
            return levels;
        }

        /** Halves an image with a 2x2 box filter. T is uint8_t or float; InChannels values per pixel. */
        template <typename T>
        std::vector<uint8_t> HalveImage(const std::vector<uint8_t>& InPixels, uint32_t InWidth, uint32_t InHeight,
                                        uint32_t InChannels) {
            const uint32_t outWidth = std::max(1u, InWidth / 2);
            const uint32_t outHeight = std::max(1u, InHeight / 2);
            std::vector<uint8_t> out(static_cast<size_t>(outWidth) * outHeight * InChannels * sizeof(T));
            const T* source = reinterpret_cast<const T*>(InPixels.data());
            T* target = reinterpret_cast<T*>(out.data());
            for (uint32_t y = 0; y < outHeight; ++y) {
                const uint32_t y0 = std::min(y * 2, InHeight - 1);
                const uint32_t y1 = std::min(y * 2 + 1, InHeight - 1);
                for (uint32_t x = 0; x < outWidth; ++x) {
                    const uint32_t x0 = std::min(x * 2, InWidth - 1);
                    const uint32_t x1 = std::min(x * 2 + 1, InWidth - 1);
                    for (uint32_t c = 0; c < InChannels; ++c) {
                        const double sum = static_cast<double>(source[(static_cast<size_t>(y0) * InWidth + x0) * InChannels + c]) +
                                           static_cast<double>(source[(static_cast<size_t>(y0) * InWidth + x1) * InChannels + c]) +
                                           static_cast<double>(source[(static_cast<size_t>(y1) * InWidth + x0) * InChannels + c]) +
                                           static_cast<double>(source[(static_cast<size_t>(y1) * InWidth + x1) * InChannels + c]);
                        target[(static_cast<size_t>(y) * outWidth + x) * InChannels + c] = static_cast<T>(sum * 0.25);
                    }
                }
            }
            return out;
        }

        /**
         * A 2D texture that keeps its pixels in memory until the device first needs them.
         * Formats: 8-bit RGBA (linear or sRGB) and float RGBA; the narrower float formats the old
         * interface offered (RG16F, RGBA16F) are stored as float RGBA.
         */
        class FLegacyTexture2D final : public FTexture2D {
        public:
            FLegacyTexture2D(uint32_t InWidth, uint32_t InHeight, ETextureFormat InFormat)
                : Width(std::max(InWidth, 1u)), Height(std::max(InHeight, 1u)), Id(NextNullId()) {
                Format = InFormat == ETextureFormat::RGBA8 ? ERHIFormat::RGBA8_UNorm : ERHIFormat::RGBA32_Float;
                bLoaded = true;
            }

            explicit FLegacyTexture2D(std::string InPath) : Path(std::move(InPath)), Id(NextNullId()) {
                FLegacyImage image;
                if (!ImageLoader() || !ImageLoader()(Path, image) || image.Mips.empty() || image.Width == 0 ||
                    image.Height == 0)
                    return;
                Width = image.Width;
                Height = image.Height;
                Format = image.Format;
                Mips = std::move(image.Mips);
                bLoaded = true;
            }

            uint32_t GetWidth() const override { return Width; }
            uint32_t GetHeight() const override { return Height; }
            uint32_t GetRendererID() const override { return Id; }
            const std::string& GetPath() const override { return Path; }
            bool IsLoaded() const override { return bLoaded; }
            void Bind(uint32_t) const override {}
            bool operator==(const FTexture& InOther) const override { return Id == InOther.GetRendererID(); }

            void SetData(void* InData, uint32_t InSize) override {
                const size_t expected = static_cast<size_t>(Width) * Height * 4;
                if (!InData || Format == ERHIFormat::RGBA32_Float || InSize < expected)
                    return;
                const auto* bytes = static_cast<const uint8_t*>(InData);
                SetMipZero(std::vector<uint8_t>(bytes, bytes + expected));
            }

            void SetDataFloat(const void* InData, uint32_t InSize) override {
                const size_t pixels = static_cast<size_t>(Width) * Height;
                const size_t channels = pixels ? InSize / (pixels * sizeof(float)) : 0;
                if (!InData || Format != ERHIFormat::RGBA32_Float || channels == 0 || channels > 4)
                    return;
                const auto* source = static_cast<const float*>(InData);
                std::vector<uint8_t> packed(pixels * 4 * sizeof(float));
                auto* target = reinterpret_cast<float*>(packed.data());
                for (size_t i = 0; i < pixels; ++i) {
                    for (size_t c = 0; c < 4; ++c)
                        target[i * 4 + c] = c < channels ? source[i * channels + c] : (c == 3 ? 1.0f : 0.0f);
                }
                SetMipZero(std::move(packed));
            }

            IRHITexture* GetOrCreate() const {
                IRHIDevice* device = FRHI::GetDevice();
                if (!device || !bLoaded)
                    return Texture.get();
                if (!Texture) {
                    FRHITextureDesc desc;
                    desc.Width = Width;
                    desc.Height = Height;
                    desc.MipLevels = static_cast<uint32_t>(std::max<size_t>(Mips.size(), 1));
                    desc.Format = Format;
                    desc.DebugName = Path.empty() ? std::string("Texture2D") : Path;
                    Texture = device->CreateTexture(desc);
                    if (Mips.empty())
                        Mips.emplace_back(static_cast<size_t>(RHIFormatRowBytes(Format, Width)) *
                                              RHIFormatRowCount(Format, Height),
                                          uint8_t{0});
                }
                if (Texture && !Mips.empty()) {
                    const uint32_t levels = std::min(static_cast<uint32_t>(Mips.size()), Texture->GetDesc().MipLevels);
                    for (uint32_t level = 0; level < levels; ++level) {
                        const uint32_t mipWidth = std::max(1u, Width >> level);
                        device->UploadTexture(*Texture, level, 0, Mips[level].data(),
                                              RHIFormatRowBytes(Format, mipWidth));
                    }
                    Mips.clear();
                    Mips.shrink_to_fit();
                }
                return Texture.get();
            }

        private:
            void SetMipZero(std::vector<uint8_t> InPixels) {
                // Written after the device texture exists: it has one level, so only that is replaced.
                Mips.clear();
                Mips.push_back(std::move(InPixels));
            }

            uint32_t Width = 1;
            uint32_t Height = 1;
            std::string Path;
            uint32_t Id;
            ERHIFormat Format = ERHIFormat::RGBA8_UNorm;
            bool bLoaded = false;
            /** Pixels waiting to be sent to the device; emptied once they are. */
            mutable std::vector<std::vector<uint8_t>> Mips;
            mutable TRef<IRHITexture> Texture;
        };

        /** A cube map: six square faces per level, 8-bit RGBA or float RGBA. */
        class FLegacyTextureCube final : public FTextureCube {
        public:
            FLegacyTextureCube(uint32_t InSize, bool bInHDR)
                : Size(std::max(InSize, 1u)), bHDR(bInHDR), Id(NextNullId()) {
                Levels.resize(MipCountFor(Size, Size));
            }

            uint32_t GetWidth() const override { return Size; }
            uint32_t GetHeight() const override { return Size; }
            uint32_t GetRendererID() const override { return Id; }
            void Bind(uint32_t) const override {}
            void Unbind() const override {}
            bool IsLoaded() const override { return true; }

            void SetFaceData(uint32_t InFaceIndex, const void* InData, uint32_t InWidth, uint32_t InHeight,
                             uint32_t InMipLevel, bool bInHDR) override {
                if (!InData || InFaceIndex >= 6 || InMipLevel >= Levels.size() || bInHDR != bHDR)
                    return;
                const size_t bytes = static_cast<size_t>(InWidth) * InHeight * BytesPerPixel();
                const auto* source = static_cast<const uint8_t*>(InData);
                Levels[InMipLevel][InFaceIndex].assign(source, source + bytes);
                UsedLevels = std::max(UsedLevels, InMipLevel + 1);
                bDirty = true;
            }

            void GenerateMipmaps() override {
                for (uint32_t level = 1; level < Levels.size(); ++level) {
                    const uint32_t previousSize = std::max(1u, Size >> (level - 1));
                    for (uint32_t face = 0; face < 6; ++face) {
                        const std::vector<uint8_t>& previous = Levels[level - 1][face];
                        if (previous.empty())
                            return;
                        Levels[level][face] = bHDR ? HalveImage<float>(previous, previousSize, previousSize, 4)
                                                   : HalveImage<uint8_t>(previous, previousSize, previousSize, 4);
                    }
                    UsedLevels = std::max(UsedLevels, level + 1);
                }
                bDirty = true;
            }

            IRHITexture* GetOrCreate() const {
                IRHIDevice* device = FRHI::GetDevice();
                if (!device)
                    return Texture.get();
                if (!Texture || Texture->GetDesc().MipLevels != UsedLevels) {
                    FRHITextureDesc desc;
                    desc.Width = Size;
                    desc.Height = Size;
                    desc.MipLevels = UsedLevels;
                    desc.Format = bHDR ? ERHIFormat::RGBA32_Float : ERHIFormat::RGBA8_UNorm;
                    desc.Dimension = ERHITextureDimension::TextureCube;
                    desc.DebugName = "TextureCube";
                    Texture = device->CreateTexture(desc);
                    bDirty = true;
                }
                if (Texture && bDirty) {
                    for (uint32_t level = 0; level < UsedLevels; ++level) {
                        const uint32_t levelSize = std::max(1u, Size >> level);
                        for (uint32_t face = 0; face < 6; ++face) {
                            const std::vector<uint8_t>& pixels = Levels[level][face];
                            if (pixels.size() == static_cast<size_t>(levelSize) * levelSize * BytesPerPixel())
                                device->UploadTexture(*Texture, level, face, pixels.data(), levelSize * BytesPerPixel());
                        }
                    }
                    bDirty = false;
                }
                return Texture.get();
            }

        private:
            uint32_t BytesPerPixel() const { return bHDR ? 16u : 4u; }

            uint32_t Size;
            bool bHDR;
            uint32_t Id;
            /** [level][face] pixels. Kept after upload: mip generation and re-creation read them. */
            std::vector<std::array<std::vector<uint8_t>, 6>> Levels;
            uint32_t UsedLevels = 1;
            mutable bool bDirty = false;
            mutable TRef<IRHITexture> Texture;
        };

        class FLegacyDriver final : public IRenderDriver {
        public:
            TRef<FVertexBuffer> CreateVertexBuffer(unsigned int InSize) override {
                return CreateRef<FLegacyVertexBuffer>(InSize);
            }
            TRef<FVertexBuffer> CreateVertexBuffer(const float* InVertices, unsigned int InSize) override {
                if (!InVertices)
                    return CreateRef<FLegacyVertexBuffer>(InSize);
                return CreateRef<FLegacyVertexBuffer>(static_cast<const void*>(InVertices), InSize);
            }
            TRef<FIndexBuffer> CreateIndexBuffer(const uint32_t* InIndices, unsigned int InCount) override {
                return CreateRef<FLegacyIndexBuffer>(InIndices, InCount);
            }
            TRef<FVertexArray> CreateVertexArray() override { return CreateRef<FNullVertexArray>(); }
            TRef<FTexture2D> CreateTexture2D(uint32_t InWidth, uint32_t InHeight) override {
                return CreateRef<FLegacyTexture2D>(InWidth, InHeight, ETextureFormat::RGBA8);
            }
            TRef<FTexture2D> CreateTexture2D(const std::string& InPath) override {
                return CreateRef<FLegacyTexture2D>(InPath);
            }
            TRef<FTexture2D> CreateTexture2DWithFormat(uint32_t InWidth, uint32_t InHeight,
                                                       ETextureFormat InFormat) override {
                return CreateRef<FLegacyTexture2D>(InWidth, InHeight, InFormat);
            }
            TRef<FTextureCube> CreateTextureCube(uint32_t InWidth, uint32_t, bool bInHDR) override {
                return CreateRef<FLegacyTextureCube>(InWidth, bInHDR);
            }
            TRef<FTextureCube> CreateTextureCube(const std::vector<std::string>&) override {
                // Cube maps from six image files are not used by the engine or the games.
                return CreateRef<FLegacyTextureCube>(1u, false);
            }
        };

    } // namespace

    IRHIBuffer* GetRHIVertexBuffer(const FVertexArray& InVertexArray) {
        const auto& buffers = InVertexArray.GetVertexBuffers();
        if (buffers.empty())
            return nullptr;
        auto* buffer = dynamic_cast<FLegacyVertexBuffer*>(buffers.front().get());
        return buffer ? buffer->GetOrCreate() : nullptr;
    }

    IRHIBuffer* GetRHIIndexBuffer(const FVertexArray& InVertexArray) {
        auto* buffer = dynamic_cast<FLegacyIndexBuffer*>(InVertexArray.GetIndexBuffer().get());
        return buffer ? buffer->GetOrCreate() : nullptr;
    }

    uint32_t GetRHIIndexCount(const FVertexArray& InVertexArray) {
        const auto& indices = InVertexArray.GetIndexBuffer();
        return indices ? indices->GetCount() : 0;
    }

    IRHITexture* GetRHITexture(const FTexture2D& InTexture) {
        const auto* texture = dynamic_cast<const FLegacyTexture2D*>(&InTexture);
        return texture ? texture->GetOrCreate() : nullptr;
    }

    IRHITexture* GetRHITexture(const FTextureCube& InTexture) {
        const auto* texture = dynamic_cast<const FLegacyTextureCube*>(&InTexture);
        return texture ? texture->GetOrCreate() : nullptr;
    }

    void SetLegacyImageLoader(FLegacyImageLoader InLoader) {
        ImageLoader() = std::move(InLoader);
    }

    FRHIVertexLayout MakeRHIVertexLayout(const FBufferLayout& InLayout) {
        FRHIVertexLayout layout;
        layout.Stride = InLayout.GetStride();
        for (const FBufferElement& element : InLayout.GetElements()) {
            FRHIVertexAttribute attribute;
            attribute.Offset = static_cast<uint32_t>(element.Offset);
            switch (element.Type) {
            case EShaderDataType::Float:
                attribute.Format = ERHIVertexFormat::Float;
                break;
            case EShaderDataType::Float2:
                attribute.Format = ERHIVertexFormat::Float2;
                break;
            case EShaderDataType::Float3:
                attribute.Format = ERHIVertexFormat::Float3;
                break;
            case EShaderDataType::Float4:
                attribute.Format = ERHIVertexFormat::Float4;
                break;
            // Bone indices: integers in the buffer, and integers in HLSL too (int4).
            case EShaderDataType::Int4:
            case EShaderDataType::Int4AsFloat:
                attribute.Format = ERHIVertexFormat::Int4;
                break;
            default:
                continue; // not used by any engine vertex format
            }
            layout.Attributes.push_back(attribute);
        }
        return layout;
    }

    FRHIVertexLayout GetRHIVertexLayout(const FVertexArray& InVertexArray) {
        const auto& buffers = InVertexArray.GetVertexBuffers();
        if (buffers.empty() || !buffers.front())
            return {};
        return MakeRHIVertexLayout(buffers.front()->GetLayout());
    }

    void FLegacyRenderDriver::Register() {
        if (!FRenderDriverRegistry::GetActiveDriver())
            FRenderDriverRegistry::RegisterDriver(CreateScope<FLegacyDriver>());
    }

} // namespace Leon
