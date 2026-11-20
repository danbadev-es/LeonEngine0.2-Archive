#include "Audio/FAudioDevice.hpp"
#include "Audio/USoundWave.hpp"
#include "Core/FLog.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

namespace Leon {
    namespace {

        struct FActiveVoice {
            uint32_t Id = 0;
            ma_sound Sound{};
            bool bValid = false;
        };

        ma_engine* GEngine = nullptr;
        std::mutex GVoiceMutex;
        std::unordered_map<uint32_t, FActiveVoice> GVoices;
        /**
         * One decoded copy of each short file, never played itself: every voice of that file is a
         * copy of it and shares its samples. Opening the file for every voice decoded the whole of it
         * on the game thread each time (tens of milliseconds for a long one).
         */
        std::unordered_map<std::string, std::unique_ptr<ma_sound>> GDecoded;

        bool IsLong(const std::string& InPath) {
            std::error_code ec;
            const uintmax_t size = std::filesystem::file_size(InPath, ec);
            return !ec && size > FAudioDevice::kStreamAboveBytes;
        }

        float DistanceAttenuation(float InDistance, float InRadius) {
            if (InRadius <= 1.0f)
                return 1.0f;
            const float t = std::clamp(InDistance / InRadius, 0.0f, 1.0f);
            return (1.0f - t) * (1.0f - t);
        }

    } // namespace

    FAudioDevice& FAudioDevice::Get() {
        static FAudioDevice Instance;
        return Instance;
    }

    bool FAudioDevice::Init() {
        if (bInitialized)
            return true;

        const char* nullEnv = std::getenv("LEON_AUDIO_NULL");
        if (nullEnv && nullEnv[0] == '1') {
            bNullDevice = true;
            bInitialized = true;
            LE_CORE_INFO("FAudioDevice: null device (LEON_AUDIO_NULL=1)");
            return true;
        }

        GEngine = new ma_engine();
        ma_engine_config config = ma_engine_config_init();
        // No output, but files are loaded, decoded and mixed as with a sound card: for tests.
        const char* noDeviceEnv = std::getenv("LEON_AUDIO_NODEVICE");
        if (noDeviceEnv && noDeviceEnv[0] == '1') {
            config.noDevice = MA_TRUE;
            config.channels = 2;
            config.sampleRate = 48000;
        }
        ma_result result = ma_engine_init(&config, GEngine);
        if (result != MA_SUCCESS) {
            delete GEngine;
            GEngine = nullptr;
            bNullDevice = true;
            bInitialized = true;
            LE_CORE_WARN("FAudioDevice: ma_engine_init failed ({0}); using null device", static_cast<int>(result));
            return true;
        }

        bNullDevice = false;
        bInitialized = true;
        ma_engine_set_volume(GEngine, MasterVolume);
        LE_CORE_INFO("FAudioDevice: initialized (miniaudio)");
        return true;
    }

    void FAudioDevice::Shutdown() {
        StopMusic();
        {
            std::lock_guard<std::mutex> lock(GVoiceMutex);
            for (auto& [id, voice] : GVoices) {
                if (voice.bValid) {
                    ma_sound_uninit(&voice.Sound);
                    voice.bValid = false;
                }
            }
            GVoices.clear();
            // After the voices: they share these copies' samples.
            for (auto& [path, sound] : GDecoded)
                ma_sound_uninit(sound.get());
            GDecoded.clear();
        }
        if (GEngine) {
            ma_engine_uninit(GEngine);
            delete GEngine;
            GEngine = nullptr;
        }
        bInitialized = false;
        bNullDevice = true;
    }

    void FAudioDevice::Tick(float /*InDeltaSeconds*/) {
        if (bNullDevice || !GEngine)
            return;

        std::lock_guard<std::mutex> lock(GVoiceMutex);
        std::vector<uint32_t> finished;
        for (auto& [id, voice] : GVoices) {
            if (!voice.bValid)
                continue;
            if (!ma_sound_is_playing(&voice.Sound)) {
                ma_sound_uninit(&voice.Sound);
                voice.bValid = false;
                finished.push_back(id);
            }
        }
        for (uint32_t id : finished)
            GVoices.erase(id);
    }

    void FAudioDevice::SetMasterVolume(float InVolume) {
        MasterVolume = std::clamp(InVolume, 0.0f, 1.0f);
        if (GEngine && !bNullDevice)
            ma_engine_set_volume(GEngine, MasterVolume);
    }

    void FAudioDevice::SetListener(const glm::vec3& InLocation, const glm::vec3& InForward, const glm::vec3& InUp) {
        ListenerLocation = InLocation;
        ListenerForward = InForward;
        ListenerUp = InUp;
        if (GEngine && !bNullDevice) {
            ma_engine_listener_set_position(GEngine, 0, InLocation.x, InLocation.y, InLocation.z);
            ma_engine_listener_set_direction(GEngine, 0, InForward.x, InForward.y, InForward.z);
            ma_engine_listener_set_world_up(GEngine, 0, InUp.x, InUp.y, InUp.z);
        }
    }

    void FAudioDevice::PlaySound2D(const TRef<USoundWave>& InSound, float InVolume) {
        PlayWave(InSound, InVolume, false, glm::vec3(0.0f), 0.0f, false);
    }

    void FAudioDevice::PlayMusic2D(const TRef<USoundWave>& InSound, float InVolume) {
        StopMusic();
        MusicVoiceId = PlayWave(InSound, InVolume, false, glm::vec3(0.0f), 0.0f, true);
    }

    void FAudioDevice::StopMusic() {
        if (MusicVoiceId != 0) {
            StopVoice(MusicVoiceId);
            MusicVoiceId = 0;
        }
    }

    void FAudioDevice::PlaySoundAtLocation(const TRef<USoundWave>& InSound, const glm::vec3& InLocation, float InVolume,
                                           float InAttenuationRadius) {
        PlayWave(InSound, InVolume, true, InLocation, InAttenuationRadius, false);
    }

    uint32_t FAudioDevice::PlayWave(const TRef<USoundWave>& InSound, float InVolume, bool bSpatial,
                                    const glm::vec3& InLocation, float InAttenuationRadius, bool bLoop) {
        if (!bInitialized || bNullDevice || !GEngine || !InSound || !InSound->IsValid())
            return 0;

        const std::string& path = InSound->GetAssetPath();
        if (path.empty())
            return 0;

        float volume = std::clamp(InVolume, 0.0f, 2.0f);
        if (bSpatial) {
            const float dist = glm::length(InLocation - ListenerLocation);
            volume *= DistanceAttenuation(dist, InAttenuationRadius);
            if (volume < 0.01f)
                return 0;
        }

        const uint32_t id = NextVoiceId++;
        std::lock_guard<std::mutex> lock(GVoiceMutex);
        FActiveVoice& voice = GVoices[id];
        voice.Id = id;
        voice.bValid = false;

        ma_result result = MA_ERROR;
        if (IsLong(path)) {
            // Music and long ambiences are read from disk as they play.
            result = ma_sound_init_from_file(GEngine, path.c_str(), MA_SOUND_FLAG_STREAM, nullptr, nullptr, &voice.Sound);
            if (result == MA_SUCCESS)
                ++Stats.Streamed;
        } else {
            ma_sound* decoded = nullptr;
            if (auto it = GDecoded.find(path); it != GDecoded.end()) {
                decoded = it->second.get();
            } else {
                auto sound = std::make_unique<ma_sound>();
                if (ma_sound_init_from_file(GEngine, path.c_str(), MA_SOUND_FLAG_DECODE, nullptr, nullptr, sound.get()) ==
                    MA_SUCCESS) {
                    decoded = sound.get();
                    GDecoded.emplace(path, std::move(sound));
                    ++Stats.Decoded;
                }
            }
            if (decoded) {
                result = ma_sound_init_copy(GEngine, decoded, 0, nullptr, &voice.Sound);
                if (result == MA_SUCCESS)
                    ++Stats.Copies;
            }
        }
        if (result != MA_SUCCESS) {
            GVoices.erase(id);
            LE_CORE_WARN("FAudioDevice: failed to play '{0}' ({1})", path, static_cast<int>(result));
            return 0;
        }

        ma_sound_set_volume(&voice.Sound, volume);
        if (bLoop)
            ma_sound_set_looping(&voice.Sound, MA_TRUE);
        if (bSpatial) {
            ma_sound_set_spatialization_enabled(&voice.Sound, MA_TRUE);
            ma_sound_set_position(&voice.Sound, InLocation.x, InLocation.y, InLocation.z);
        } else {
            ma_sound_set_spatialization_enabled(&voice.Sound, MA_FALSE);
        }

        result = ma_sound_start(&voice.Sound);
        if (result != MA_SUCCESS) {
            ma_sound_uninit(&voice.Sound);
            GVoices.erase(id);
            return 0;
        }

        voice.bValid = true;
        return id;
    }

    bool FAudioDevice::Preload(const TRef<USoundWave>& InSound) {
        if (!bInitialized || bNullDevice || !GEngine || !InSound || !InSound->IsValid())
            return false;
        const std::string& path = InSound->GetAssetPath();
        if (path.empty())
            return false;
        if (IsLong(path))
            return std::filesystem::exists(path);
        std::lock_guard<std::mutex> lock(GVoiceMutex);
        if (GDecoded.count(path) > 0)
            return true;
        auto sound = std::make_unique<ma_sound>();
        if (ma_sound_init_from_file(GEngine, path.c_str(), MA_SOUND_FLAG_DECODE, nullptr, nullptr, sound.get()) != MA_SUCCESS)
            return false;
        GDecoded.emplace(path, std::move(sound));
        ++Stats.Decoded;
        return true;
    }

    void FAudioDevice::StopVoice(uint32_t InVoiceId) {
        if (InVoiceId == 0 || bNullDevice)
            return;
        std::lock_guard<std::mutex> lock(GVoiceMutex);
        auto it = GVoices.find(InVoiceId);
        if (it == GVoices.end())
            return;
        if (it->second.bValid) {
            ma_sound_stop(&it->second.Sound);
            ma_sound_uninit(&it->second.Sound);
            it->second.bValid = false;
        }
        if (InVoiceId == MusicVoiceId)
            MusicVoiceId = 0;
        GVoices.erase(it);
    }

} // namespace Leon
