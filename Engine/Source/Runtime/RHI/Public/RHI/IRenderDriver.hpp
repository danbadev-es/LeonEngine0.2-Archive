#pragma once

#include "Core/Base.hpp"
#include "RHI/FBuffer.hpp"
#include "RHI/FShader.hpp"
#include "RHI/FTexture.hpp"
#include "RHI/FVertexArray.hpp"

#include <string>
#include <vector>

namespace Leon {

    class FTextureCube;

    /**
     * Creates the objects behind FVertexBuffer, FIndexBuffer, FVertexArray, FShader, FTexture2D and
     * FTextureCube. Executables that draw register one (FLegacyRenderDriver); without one the Create
     * functions of those classes return null, which is how tools and tests run without render objects.
     */
    class IRenderDriver {
    public:
        virtual ~IRenderDriver() = default;

        virtual TRef<FVertexBuffer> CreateVertexBuffer(unsigned int InSize) = 0;
        virtual TRef<FVertexBuffer> CreateVertexBuffer(const float* InVertices, unsigned int InSize) = 0;
        virtual TRef<FIndexBuffer> CreateIndexBuffer(const uint32_t* InIndices, unsigned int InCount) = 0;
        virtual TRef<FVertexArray> CreateVertexArray() = 0;
        virtual TRef<FShader> CreateShader(const std::string& InFilePath) = 0;
        virtual TRef<FShader> CreateShader(const std::string& InName, const std::string& InVertexSrc,
                                           const std::string& InFragmentSrc) = 0;
        virtual TRef<FTexture2D> CreateTexture2D(uint32_t InWidth, uint32_t InHeight) = 0;
        virtual TRef<FTexture2D> CreateTexture2D(const std::string& InPath) = 0;
        virtual TRef<FTexture2D> CreateTexture2DWithFormat(uint32_t InWidth, uint32_t InHeight,
                                                           ETextureFormat InFormat) = 0;
        virtual TRef<FTextureCube> CreateTextureCube(uint32_t InWidth, uint32_t InHeight, bool bInHDR = false) = 0;
        virtual TRef<FTextureCube> CreateTextureCube(const std::vector<std::string>& InFacePaths) = 0;
    };

    class FRenderDriverRegistry {
    public:
        /** Replaces the driver in use. */
        static void RegisterDriver(TScope<IRenderDriver> InDriver);
        /** Null when no driver was registered. */
        static IRenderDriver* GetActiveDriver();
    };

} // namespace Leon
