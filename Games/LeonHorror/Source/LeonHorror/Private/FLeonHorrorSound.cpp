#include "LeonHorror/FLeonHorrorSound.hpp"

#include "Assets/UStaticMesh.hpp"
#include "Engine/UWorld.hpp"
#include "Gameplay/AActor.hpp"
#include "Gameplay/UGameplayStatics.hpp"
#include "Physics/ECollisionChannel.hpp"
#include "Physics/FHitResult.hpp"
#include "Scene/Components.hpp"

#include <cstdio>
#include <map>

namespace Leon::LeonHorrorSound {

    namespace {

        std::map<std::string, int>& Counts() {
            static std::map<std::string, int> counts;
            return counts;
        }

        std::string PathOf(const std::string& InCue) {
            return "/Game/Audio/" + InCue;
        }

    } // namespace

    void Play(const std::string& InCue, float InVolume) {
        if (InCue.empty())
            return;
        ++Counts()[InCue];
        UGameplayStatics::PlaySound2D(PathOf(InCue), InVolume);
    }

    void PlayAt(const std::string& InCue, const glm::vec3& InLocation, float InVolume, float InRadius) {
        if (InCue.empty())
            return;
        ++Counts()[InCue];
        UGameplayStatics::PlaySoundAtLocation(PathOf(InCue), InLocation, InVolume, InRadius);
    }

    void Music(const std::string& InCue, float InVolume) {
        if (InCue.empty())
            return;
        ++Counts()[InCue];
        UGameplayStatics::PlayMusic2D(PathOf(InCue), InVolume);
    }

    std::string Variation(const std::string& InPrefix, int InCount) {
        static std::map<std::string, int> last;
        static uint32_t seed = 0x9E3779B9u;
        if (InCount <= 1)
            return InPrefix + "_01";
        seed = seed * 1664525u + 1013904223u;
        int pick = static_cast<int>((seed >> 16) % static_cast<uint32_t>(InCount));
        int& previous = last[InPrefix];
        if (pick + 1 == previous)
            pick = (pick + 1) % InCount;
        previous = pick + 1;
        char suffix[8];
        std::snprintf(suffix, sizeof(suffix), "_%02d", pick + 1);
        return InPrefix + suffix;
    }

    std::string StepSurface(UWorld& InWorld, const glm::vec3& InFeet, AActor* InIgnore) {
        FHitResult hit;
        const glm::vec3 from = InFeet + glm::vec3(0.0f, 0.4f, 0.0f);
        const glm::vec3 to = InFeet - glm::vec3(0.0f, 0.6f, 0.0f);
        if (InWorld.LineTraceSingleByChannel(from, to, ECollisionChannel::WorldStatic, InIgnore, hit) && hit.Actor &&
            hit.Actor->HasComponent<FStaticMeshComponent>()) {
            const auto& mesh = hit.Actor->GetComponent<FStaticMeshComponent>();
            if (mesh.StaticMesh) {
                for (const FStaticMaterialSlot& slot : mesh.StaticMesh->GetMaterialSlots()) {
                    const std::string& material = slot.DefaultMaterialPath;
                    if (material.find("Tile") != std::string::npos || material.find("Concrete") != std::string::npos)
                        return "SFX_Step_Tile";
                }
            }
        }
        return "SFX_Step_Vinyl";
    }

    int CountPlayed(const std::string& InPrefix) {
        int total = 0;
        for (const auto& [cue, count] : Counts()) {
            if (cue.rfind(InPrefix, 0) == 0)
                total += count;
        }
        return total;
    }

    void ResetCounts() {
        Counts().clear();
    }

} // namespace Leon::LeonHorrorSound
