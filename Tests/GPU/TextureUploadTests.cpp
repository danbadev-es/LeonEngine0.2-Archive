#include <doctest/doctest.h>
#include "GPU/HeadlessDevice.hpp"

#include <glm/gtc/packing.hpp>

#include <cmath>
#include <vector>

TEST_SUITE("GPU - Texture Upload & Half-Float Cubemap Integration") {

    TEST_CASE("Half-float cube faces come back as they were uploaded") {
        Leon::TestGPU::FHeadlessDevice gl;
        REQUIRE(gl.IsValid());
        Leon::IRHIDevice& device = *gl.GetDevice();

        const uint32_t cubeSize = 16;
        Leon::FRHITextureDesc desc;
        desc.Width = cubeSize;
        desc.Height = cubeSize;
        desc.Format = Leon::ERHIFormat::RGBA16_Float;
        desc.Dimension = Leon::ERHITextureDimension::TextureCube;
        desc.DebugName = "TestHalfCube";
        auto cube = device.CreateTexture(desc);
        REQUIRE(cube != nullptr);

        // A different value per face, so a face landing in the wrong slice shows.
        const auto faceValue = [](uint32_t InFace) { return 3.1415f + static_cast<float>(InFace); };
        for (uint32_t face = 0; face < 6; ++face) {
            const std::vector<uint16_t> texels(cubeSize * cubeSize * 4, glm::packHalf1x16(faceValue(face)));
            device.UploadTexture(*cube, 0, face, texels.data(), cubeSize * 8);
        }
        device.WaitIdle();

        for (uint32_t face = 0; face < 6; ++face) {
            std::vector<uint8_t> bytes;
            REQUIRE(device.ReadTexture(*cube, 0, face, bytes));
            REQUIRE(bytes.size() == static_cast<size_t>(cubeSize) * cubeSize * 8);
            const auto* halves = reinterpret_cast<const uint16_t*>(bytes.data());
            for (size_t i = 0; i < static_cast<size_t>(cubeSize) * cubeSize * 4; ++i) {
                const float value = glm::unpackHalf1x16(halves[i]);
                CHECK(!std::isnan(value));
                CHECK(!std::isinf(value));
                // Half floats carry 11 bits of mantissa.
                CHECK(value == doctest::Approx(faceValue(face)).epsilon(0.005f));
            }
        }
        CHECK(gl.ValidationErrors() == 0);
    }
}
