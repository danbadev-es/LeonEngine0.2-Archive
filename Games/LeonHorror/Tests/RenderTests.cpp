#include <doctest/doctest.h>

#include "LeonHorrorTestSetup.hpp"

#include "LeonHorror/ALeonHorrorCharacter.hpp"
#include "LeonHorror/ALeonHorrorFlashlight.hpp"

#include "Engine/FMapSerializer.hpp"
#include "Gameplay/APlayerCameraManager.hpp"
#include "Gameplay/APlayerController.hpp"
#include "RHI/FLegacyRenderDriver.hpp"
#include "RHI/FRHIFrame.hpp"
#include "RHI/IRHIDevice.hpp"
#include "RHI/IRenderDriver.hpp"
#include "RHID3D12/FD3D12RHI.hpp"
#include "Renderer/FImmediateDrawRHI.hpp"
#include "RenderCore/FPerspectiveCamera.hpp"
#include "RenderCore/FShaderLibrary.hpp"
#include "Renderer/FWorldRenderer.hpp"

#include <stb_image.h>
#include <stb_image_write.h>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

// The game's first room as the player sees it when the game starts: the character from behind, the
// ceiling light and the flashlight. Drawn on WARP (the software Direct3D 12 adapter) with the debug
// layer on, and compared with a stored picture.
//
// This is the frame that was wrong on OpenGL without any test noticing: the character collapsed
// into its bind pose and the flashlight lit nothing.
//
//   LEON_UPDATE_REFERENCE=1   rewrites the stored picture with what is drawn now. Look at it first.
//   LEON_TEST_GPU=1           draws on the machine's graphics card (the comparison is then looser
//                             than WARP needs, and may still fail: the reference is WARP's).
//
// When the comparison fails, the frame drawn is left in build/LeonHorrorTests/ next to the name of
// the reference, to be looked at.

namespace Leon {

    namespace {

        constexpr uint32_t kWidth = 480;
        constexpr uint32_t kHeight = 270;
        const char* const kReferenceDir = "Games/LeonHorror/Tests/Reference";
        const char* const kActualDir = "build/LeonHorrorTests";

        struct FImage {
            std::vector<uint8_t> Pixels; // RGBA8, rows from the top

            float Luma(uint32_t InX, uint32_t InY) const {
                const uint8_t* pixel = &Pixels[(static_cast<size_t>(InY) * kWidth + InX) * 4];
                return (0.299f * pixel[0] + 0.587f * pixel[1] + 0.114f * pixel[2]) / 255.0f;
            }

            /** Mean luma of a rectangle given in fractions of the picture. */
            float MeanLuma(float InLeft, float InTop, float InRight, float InBottom) const {
                const auto x0 = static_cast<uint32_t>(InLeft * kWidth);
                const auto x1 = static_cast<uint32_t>(InRight * kWidth);
                const auto y0 = static_cast<uint32_t>(InTop * kHeight);
                const auto y1 = static_cast<uint32_t>(InBottom * kHeight);
                float sum = 0.0f;
                for (uint32_t y = y0; y < y1; ++y) {
                    for (uint32_t x = x0; x < x1; ++x)
                        sum += Luma(x, y);
                }
                return sum / static_cast<float>(std::max(1u, (x1 - x0) * (y1 - y0)));
            }
        };

        struct FImageDifference {
            /** Mean absolute difference per colour channel, in 0..255. */
            float Mean = 0.0f;
            /** Fraction of pixels with a channel further apart than 32. */
            float FarFraction = 0.0f;
        };

        FImageDifference Compare(const FImage& InA, const std::vector<uint8_t>& InB) {
            FImageDifference difference;
            double sum = 0.0;
            size_t far = 0;
            const size_t pixels = InA.Pixels.size() / 4;
            for (size_t i = 0; i < pixels; ++i) {
                int worst = 0;
                for (int channel = 0; channel < 3; ++channel) {
                    const int delta = std::abs(static_cast<int>(InA.Pixels[i * 4 + channel]) - static_cast<int>(InB[i * 4 + channel]));
                    sum += delta;
                    worst = std::max(worst, delta);
                }
                if (worst > 32)
                    ++far;
            }
            difference.Mean = static_cast<float>(sum / static_cast<double>(pixels * 3));
            difference.FarFraction = static_cast<float>(far) / static_cast<float>(pixels);
            return difference;
        }

        bool WritePng(const std::string& InFile, const FImage& InImage) {
            std::error_code ec;
            std::filesystem::create_directories(std::filesystem::path(InFile).parent_path(), ec);
            // Alpha carries luma for anti-aliasing, not coverage: store the picture opaque.
            std::vector<uint8_t> rgb;
            rgb.reserve(static_cast<size_t>(kWidth) * kHeight * 3);
            for (size_t i = 0; i < InImage.Pixels.size(); i += 4)
                rgb.insert(rgb.end(), {InImage.Pixels[i], InImage.Pixels[i + 1], InImage.Pixels[i + 2]});
            return stbi_write_png(InFile.c_str(), static_cast<int>(kWidth), static_cast<int>(kHeight), 3, rgb.data(),
                                  static_cast<int>(kWidth) * 3) != 0;
        }

        bool ReadPng(const std::string& InFile, std::vector<uint8_t>& OutPixels) {
            int width = 0;
            int height = 0;
            int channels = 0;
            stbi_set_flip_vertically_on_load(0);
            stbi_uc* data = stbi_load(InFile.c_str(), &width, &height, &channels, 4);
            if (!data)
                return false;
            const bool bSize = width == static_cast<int>(kWidth) && height == static_cast<int>(kHeight);
            if (bSize)
                OutPixels.assign(data, data + static_cast<size_t>(width) * height * 4);
            stbi_image_free(data);
            return bSize;
        }

        /** Compares a frame with its stored reference, or stores it when LEON_UPDATE_REFERENCE is set. */
        void CheckAgainstReference(const FImage& InImage, const std::string& InName) {
            const std::string reference = std::string(kReferenceDir) + "/" + InName + ".png";
            if (std::getenv("LEON_UPDATE_REFERENCE") != nullptr) {
                REQUIRE_MESSAGE(WritePng(reference, InImage), reference);
                MESSAGE("reference rewritten: " << reference);
                return;
            }
            std::vector<uint8_t> expected;
            REQUIRE_MESSAGE(ReadPng(reference, expected), "missing or wrong-sized reference: " << reference);
            const FImageDifference difference = Compare(InImage, expected);
            // WARP draws the same picture every time; the room for difference is for other versions
            // of it and for the compiler rounding the animation differently.
            const bool bRealGPU = std::getenv("LEON_TEST_GPU") != nullptr;
            // The far limit is what catches a wrong pose: the character covers about 3% of the
            // picture, and its arms alone, drawn somewhere else, change more than 0.3% of it.
            const float meanLimit = bRealGPU ? 6.0f : 1.0f;
            const float farLimit = bRealGPU ? 0.06f : 0.003f;
            const bool bSame = difference.Mean <= meanLimit && difference.FarFraction <= farLimit;
            if (!bSame) {
                const std::string actual = std::string(kActualDir) + "/" + InName + ".actual.png";
                WritePng(actual, InImage);
                MESSAGE("frame drawn: " << actual << "  reference: " << reference);
            }
            CHECK_MESSAGE(difference.Mean <= meanLimit, "mean difference per channel (0..255)");
            CHECK_MESSAGE(difference.FarFraction <= farLimit, "fraction of pixels that differ clearly");
        }

        /** The shipped map with the game running on it, on a real device. */
        struct FHorrorRenderFixture {
            TRef<UWorld> World;
            TRef<IRHITexture> Target;
            ALeonHorrorGameMode* GM = nullptr;
            ALeonHorrorCharacter* Player = nullptr;
            uint64_t ErrorsAtStart = 0;
            bool bReady = false;

            FHorrorRenderFixture() {
                // Whatever earlier tests loaded has no device resources: load it again on the device.
                UAssetManager::Clear();
                FD3D12RHI::Register();
                FRHIInitDesc init;
                init.bEnableDebugLayer = true;
                init.bUseSoftwareAdapter = std::getenv("LEON_TEST_GPU") == nullptr;
                if (!FRHI::Init(ERHIBackend::D3D12, init))
                    return;
                ErrorsAtStart = FD3D12RHI::GetValidationErrorCount();
                FLegacyRenderDriver::Register();

                Test::BindLeonHorrorProject();
                Test::RegisterLeonHorrorClasses();
                World = UWorld::Create("HorrorRenderWorld");
                std::string map = FProjectPaths::ResolveVirtualPath("/Game/Maps/Slice");
                if (!map.ends_with(".lmap"))
                    map += ".lmap";
                if (!FMapSerializer(World).Deserialize(map))
                    return;
                GM = World->SpawnActor<ALeonHorrorGameMode>("GM");
                World->SetGameMode(GM);
                World->InitWorld();
                GM->HUDClass = "None";
                GM->SetSaveFilePath("build/LeonHorrorSaves/render_test.lsav");

                FWorldRenderer* renderer = World->GetWorldRenderer();
                // The environment lighting takes seconds to build on the CPU and the room is indoors,
                // lit by its lamps: the procedural fallback is close enough.
                renderer->SetImageBasedLightingEnabled(false);
                renderer->OnViewportResize(kWidth, kHeight);

                World->BeginPlay();
                Player = GM->GetPlayerCharacter();
                if (!Player)
                    return;
                Player->SetInjectedInput(FLeonHorrorInputState{});
                if (APlayerController* controller = World->GetFirstPlayerController()) {
                    if (controller->GetPlayerCameraManager())
                        controller->GetPlayerCameraManager()->SetAspectRatio(static_cast<float>(kWidth) /
                                                                             static_cast<float>(kHeight));
                }
                Tick(1.0f); // lands on the floor, the camera settles, the idle animation is playing

                FRHITextureDesc target;
                target.Width = kWidth;
                target.Height = kHeight;
                target.Format = ERHIFormat::RGBA8_UNorm;
                target.Usage = RHITextureUsage_Sampled | RHITextureUsage_RenderTarget;
                target.DebugName = "TestOutput";
                Target = FRHI::GetDevice()->CreateTexture(target);
                bReady = Target != nullptr;
            }

            ~FHorrorRenderFixture() {
                // Everything that holds device resources goes before the device.
                World.reset();
                Target.reset();
                ShutdownImmediateDrawRHI();
                FShaderLibrary::Clear();
                UAssetManager::Clear();
                FRenderDriverRegistry::RegisterDriver(nullptr);
                FRHI::Shutdown();
            }

            void Tick(float InSeconds) {
                const int steps = std::max(1, static_cast<int>(InSeconds * 60.0f + 0.5f));
                for (int i = 0; i < steps; ++i)
                    World->Tick(FTimestep(1.0f / 60.0f));
            }

            /** The frame the game would show now, from the player's camera. */
            FImage Render(uint32_t InFrames = 2) {
                IRHIDevice* device = FRHI::GetDevice();
                FPerspectiveCamera camera(45.0f, static_cast<float>(kWidth) / static_cast<float>(kHeight), 0.1f, 1000.0f);
                if (APlayerController* controller = World->GetFirstPlayerController())
                    controller->GetPlayerViewPoint(camera);
                for (uint32_t frame = 0; frame < InFrames; ++frame) {
                    IRHICommandList& commands = device->BeginFrame();
                    FRHIFrame::Begin(commands, *Target);
                    World->OnRender(camera);
                    FRHIFrame::End();
                    device->EndFrame();
                }
                device->WaitIdle();
                FImage image;
                REQUIRE(device->ReadTexture(*Target, 0, 0, image.Pixels));
                REQUIRE(image.Pixels.size() == static_cast<size_t>(kWidth) * kHeight * 4);
                return image;
            }

            uint64_t NewValidationErrors() const { return FD3D12RHI::GetValidationErrorCount() - ErrorsAtStart; }
        };

    } // namespace

    TEST_SUITE("LeonHorror rendering") {

        TEST_CASE_FIXTURE(FHorrorRenderFixture, "the start room matches its reference picture, flashlight on and off") {
            REQUIRE(bReady);
            ALeonHorrorFlashlight* flashlight = Player->GetFlashlight();
            REQUIRE(flashlight != nullptr);

            flashlight->SetOn(true);
            Tick(0.1f);
            const FImage lit = Render();

            flashlight->SetOn(false);
            Tick(0.1f);
            const FImage unlit = Render();

            CHECK(NewValidationErrors() == 0);

            // Before any stored picture: the frame shows a room, and the flashlight lights what the
            // player looks at (the door at the far end, past the character's right shoulder).
            CHECK(unlit.MeanLuma(0.0f, 0.0f, 1.0f, 1.0f) > 0.02f);
            const float doorLit = lit.MeanLuma(0.46f, 0.46f, 0.58f, 0.60f);
            const float doorUnlit = unlit.MeanLuma(0.46f, 0.46f, 0.58f, 0.60f);
            CAPTURE(doorLit);
            CAPTURE(doorUnlit);
            CHECK(doorLit > doorUnlit * 1.3f);

            CheckAgainstReference(lit, "StartRoom_FlashlightOn");
            CheckAgainstReference(unlit, "StartRoom_FlashlightOff");
        }
    }

} // namespace Leon
