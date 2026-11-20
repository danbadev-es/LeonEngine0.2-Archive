#include <doctest/doctest.h>

#include "Audio/FAudioDevice.hpp"
#include "Audio/USoundWave.hpp"
#include "Gameplay/UAudioComponent.hpp"
#include "Core/FProjectPaths.hpp"

#include <cstdlib>
#include <filesystem>

#if defined(_WIN32)
#include <stdlib.h>
#endif

using namespace Leon;

TEST_CASE("Audio null device init and play no-crash") {
#if defined(_WIN32)
    _putenv_s("LEON_AUDIO_NULL", "1");
#else
    setenv("LEON_AUDIO_NULL", "1", 1);
#endif

    FAudioDevice::Get().Shutdown();
    CHECK(FAudioDevice::Get().Init());
    CHECK(FAudioDevice::Get().IsNullDevice());

    auto wave = CreateRef<USoundWave>("Empty");
    FAudioDevice::Get().PlaySound2D(wave, 1.0f);
    FAudioDevice::Get().PlayMusic2D(wave, 0.5f);
    FAudioDevice::Get().StopMusic();
    FAudioDevice::Get().Tick(0.016f);
    FAudioDevice::Get().Shutdown();
}

TEST_CASE("UAudioComponent play stop on null device") {
#if defined(_WIN32)
    _putenv_s("LEON_AUDIO_NULL", "1");
#else
    setenv("LEON_AUDIO_NULL", "1", 1);
#endif
    FAudioDevice::Get().Shutdown();
    REQUIRE(FAudioDevice::Get().Init());

    UAudioComponent comp("TestAudio");
    comp.Play();
    comp.Stop();
    FAudioDevice::Get().Shutdown();
}

TEST_CASE("USoundWave resolves /Game/Audio SFX without extension") {
    const std::string project = FProjectPaths::LocateProjectFile("Games/LeonTournament/LeonTournament.lproject");
    REQUIRE_FALSE(project.empty());
    FProjectPaths::SetProjectRoot(project);

    const std::string resolved = FProjectPaths::ResolveVirtualPath("/Game/Audio/SFX_RifleFire");
    CHECK(resolved.find(".wav") != std::string::npos);
    CHECK(std::filesystem::exists(resolved));

    auto wave = USoundWave::Load("/Game/Audio/SFX_RifleFire");
    REQUIRE(wave != nullptr);
    CHECK(wave->IsValid());
    CHECK(wave->GetAssetPath().find(".wav") != std::string::npos);

    auto again = USoundWave::Load("/Game/Audio/SFX_RifleFire");
    CHECK(again.get() == wave.get());
}

TEST_CASE("a short sound is decoded once and shared by its voices; a long one is streamed") {
    // A real mixer without a sound card: files are opened and decoded as in the game.
#if defined(_WIN32)
    _putenv_s("LEON_AUDIO_NULL", "0");
    _putenv_s("LEON_AUDIO_NODEVICE", "1");
#else
    setenv("LEON_AUDIO_NULL", "0", 1);
    setenv("LEON_AUDIO_NODEVICE", "1", 1);
#endif
    FAudioDevice& device = FAudioDevice::Get();
    device.Shutdown();
    REQUIRE(device.Init());
    REQUIRE_FALSE(device.IsNullDevice());

    const std::string project = FProjectPaths::LocateProjectFile("Games/LeonHorror/LeonHorror.lproject");
    REQUIRE_FALSE(project.empty());
    FProjectPaths::SetProjectRoot(project);
    auto shortWave = USoundWave::Load("/Game/Audio/SFX_Step_Tile_01");
    auto longWave = USoundWave::Load("/Game/Audio/MUS_Chase");
    REQUIRE(shortWave);
    REQUIRE(longWave);
    REQUIRE(std::filesystem::file_size(shortWave->GetAssetPath()) < FAudioDevice::kStreamAboveBytes);
    REQUIRE(std::filesystem::file_size(longWave->GetAssetPath()) > FAudioDevice::kStreamAboveBytes);

    const FAudioDevice::FStats before = device.GetStats();
    CHECK(device.Preload(shortWave));
    for (int i = 0; i < 5; ++i)
        CHECK(device.PlayWave(shortWave, 0.5f, false, glm::vec3(0.0f), 0.0f) != 0u);
    CHECK(device.GetStats().Decoded - before.Decoded == 1);
    CHECK(device.GetStats().Copies - before.Copies == 5);

    // A long file: opening it costs no whole decode, however long it is.
    const uint32_t music = device.PlayWave(longWave, 0.5f, false, glm::vec3(0.0f), 0.0f, true);
    CHECK(music != 0u);
    CHECK(device.GetStats().Streamed - before.Streamed == 1);
    CHECK(device.GetStats().Decoded - before.Decoded == 1);
    device.StopVoice(music);

    device.Shutdown();
#if defined(_WIN32)
    _putenv_s("LEON_AUDIO_NODEVICE", "0");
    _putenv_s("LEON_AUDIO_NULL", "1");
#else
    setenv("LEON_AUDIO_NODEVICE", "0", 1);
    setenv("LEON_AUDIO_NULL", "1", 1);
#endif
}
