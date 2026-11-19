#include "LeonHorror/ALeonHorrorPower.hpp"

#include "LeonHorror/ALeonHorrorCharacter.hpp"
#include "LeonHorror/ALeonHorrorGameMode.hpp"
#include "LeonHorror/ULeonHorrorInventoryComponent.hpp"

#include "Assets/FMaterialInstance.hpp"
#include "Audio/FAudioDevice.hpp"
#include "Audio/USoundWave.hpp"
#include "Core/Cast.hpp"
#include "Engine/UWorld.hpp"
#include "Gameplay/UGameplayStatics.hpp"
#include "Scene/Components.hpp"

#include <algorithm>
#include <cmath>

namespace Leon {

    namespace {
        ALeonHorrorGameMode* GameModeOf(const AActor& InActor) {
            UWorld* world = InActor.GetWorld();
            return world ? Cast<ALeonHorrorGameMode>(world->GetGameMode()) : nullptr;
        }
    } // namespace

    // ---------------------------------------------------------------------------------------------
    // Ambient sound
    // ---------------------------------------------------------------------------------------------

    ALeonHorrorAmbientSound::ALeonHorrorAmbientSound(entt::entity InHandle, UWorld* InWorld, const std::string& InName)
        : AActor(InHandle, InWorld, InName) {
        SetClass("ALeonHorrorAmbientSound");
        SetCanEverTick(false);
    }

    void ALeonHorrorAmbientSound::OnPropertiesLoaded() {
        Sound = GetProperty("Sound", Sound);
        Volume = std::clamp(GetPropertyFloat("Volume", Volume), 0.0f, 4.0f);
        Radius = std::max(0.5f, GetPropertyFloat("Radius", Radius));
        bMusic = GetPropertyBool("Music", bMusic);
        bEnabled = GetPropertyBool("Enabled", bEnabled);
    }

    void ALeonHorrorAmbientSound::SetSound(const std::string& InPath, float InVolume, float InRadius) {
        Sound = InPath;
        Volume = InVolume;
        Radius = InRadius;
        Refresh();
    }

    void ALeonHorrorAmbientSound::BeginPlay() {
        Refresh();
    }

    void ALeonHorrorAmbientSound::EndPlay() {
        bEnabled = false;
        Refresh();
    }

    void ALeonHorrorAmbientSound::SetEnabled(bool bInEnabled) {
        bEnabled = bInEnabled;
        Refresh();
    }

    void ALeonHorrorAmbientSound::Refresh() {
        const bool bWanted = bEnabled && HasBegunPlay() && !Sound.empty();
        if (bWanted == bPlaying)
            return;
        bPlaying = bWanted;
        FAudioDevice& device = FAudioDevice::Get();
        if (!bWanted) {
            if (bMusic)
                device.StopMusic();
            else if (VoiceId != 0)
                device.StopVoice(VoiceId);
            VoiceId = 0;
            return;
        }
        // Without a sound card (tests, tools) it counts as playing and makes no noise.
        if (device.IsNullDevice())
            return;
        const TRef<USoundWave> wave = USoundWave::Load(Sound);
        if (!wave)
            return;
        if (bMusic)
            device.PlayMusic2D(wave, Volume);
        else
            VoiceId = device.PlayWave(wave, Volume, true, GetActorLocation(), Radius, true);
    }

    // ---------------------------------------------------------------------------------------------
    // Fuse box
    // ---------------------------------------------------------------------------------------------

    ALeonHorrorFuseBox::ALeonHorrorFuseBox(entt::entity InHandle, UWorld* InWorld, const std::string& InName)
        : ALeonHorrorInteractable(InHandle, InWorld, InName) {
        SetClass("ALeonHorrorFuseBox");
    }

    void ALeonHorrorFuseBox::OnPropertiesLoaded() {
        Group = GetProperty("Group", Group);
        RequiredItem = GetProperty("RequiredItem", RequiredItem);
    }

    bool ALeonHorrorFuseBox::IsLive() const {
        const ALeonHorrorGameMode* gameMode = GameModeOf(*this);
        return gameMode && gameMode->IsPowered(Group);
    }

    bool ALeonHorrorFuseBox::CanInteract(const APawn& InInstigator) const {
        return ALeonHorrorInteractable::CanInteract(InInstigator);
    }

    std::string ALeonHorrorFuseBox::GetInteractPrompt(const APawn& InInstigator) const {
        if (IsLive())
            return "The power is on";
        const auto* player = Cast<const ALeonHorrorCharacter>(&InInstigator);
        const auto* inventory = player ? player->GetInventory() : nullptr;
        if (inventory && inventory->HasItem(RequiredItem))
            return "Fit the " + LeonHorrorItems::DisplayName(RequiredItem);
        return "A fuse is missing";
    }

    bool ALeonHorrorFuseBox::Interact(APawn& InInstigator) {
        ALeonHorrorGameMode* gameMode = GameModeOf(*this);
        if (!gameMode || gameMode->IsPowered(Group))
            return false;
        auto* player = Cast<ALeonHorrorCharacter>(&InInstigator);
        auto* inventory = player ? player->GetInventory() : nullptr;
        if (!inventory || !inventory->RemoveItem(RequiredItem))
            return false;
        if (HasBegunPlay()) {
            UGameplayStatics::PlaySoundAtLocation("/Game/Audio/SFX_Fuse_Insert", GetInteractLocation(), 1.0f, 15.0f);
            UGameplayStatics::PlaySoundAtLocation("/Game/Audio/SFX_Power_On", GetInteractLocation(), 1.0f, 30.0f);
        }
        gameMode->SetPowerGroup(Group, true);
        return true;
    }

    // ---------------------------------------------------------------------------------------------
    // What having or not having power does. Lives here, with the rest of the electricity, though
    // the game mode owns the state.
    // ---------------------------------------------------------------------------------------------

    bool ALeonHorrorGameMode::IsPowered(const std::string& InGroup) const {
        return PoweredGroups.count(InGroup) > 0;
    }

    void ALeonHorrorGameMode::SetPowerGroup(const std::string& InGroup, bool bInPowered) {
        if (InGroup.empty())
            return;
        if (bInPowered)
            PoweredGroups.insert(InGroup);
        else
            PoweredGroups.erase(InGroup);
        ApplyPowerGroup(InGroup, bInPowered);
    }

    void ALeonHorrorGameMode::ApplyPowerGroup(const std::string& InGroup, bool bInPowered) {
        if (!World)
            return;
        for (const auto& actorRef : World->GetAllActors()) {
            AActor* actor = actorRef.get();
            if (!actor || actor->IsPendingKill() || actor->GetProperty("PowerGroup") != InGroup)
                continue;
            if (actor->HasComponent<FPointLightComponent>())
                actor->GetComponent<FPointLightComponent>().bEnabled = bInPowered;
            if (actor->HasComponent<FSpotLightComponent>())
                actor->GetComponent<FSpotLightComponent>().bEnabled = bInPowered;
            // A fitting glows through the material the map gave it; without power it does not.
            if (actor->HasComponent<FStaticMeshComponent>()) {
                for (const auto& material : actor->GetComponent<FStaticMeshComponent>().MaterialOverrides) {
                    if (!material)
                        continue;
                    if (bInPowered)
                        material->ClearEmissiveIntensityOverride();
                    else
                        material->SetEmissiveIntensity(0.0f);
                }
            }
            if (auto* sound = Cast<ALeonHorrorAmbientSound>(actor))
                sound->SetEnabled(bInPowered);
            if (auto* door = Cast<ALeonHorrorDoor>(actor))
                door->SetPowered(bInPowered);
        }
    }

    void ALeonHorrorGameMode::InitialisePower() {
        if (!World)
            return;
        FlickeringLights.clear();
        std::set<std::string> groups;
        for (const auto& actorRef : World->GetAllActors()) {
            AActor* actor = actorRef.get();
            if (!actor)
                continue;
            const std::string& group = actor->GetProperty("PowerGroup");
            if (!group.empty())
                groups.insert(group);
            const float flicker = actor->GetPropertyFloat("Flicker", 0.0f);
            if (flicker > 0.0f && actor->HasComponent<FPointLightComponent>()) {
                FFlickeringLight light;
                light.Actor = actor;
                light.BaseIntensity = actor->GetComponent<FPointLightComponent>().Light.Intensity;
                light.Amount = std::min(flicker, 1.0f);
                light.Phase = static_cast<float>(FlickeringLights.size()) * 1.73f;
                FlickeringLights.push_back(light);
            }
        }
        for (const std::string& group : groups)
            ApplyPowerGroup(group, IsPowered(group));
    }

    void ALeonHorrorGameMode::TickFlicker() {
        for (FFlickeringLight& light : FlickeringLights) {
            AActor* actor = light.Actor.Get();
            if (!actor || !actor->HasComponent<FPointLightComponent>())
                continue;
            // A tired tube: a steady shimmer, and now and then it drops out for a few frames. Built
            // from sines of unrelated periods so it never visibly repeats, and needs no random state.
            const float t = ElapsedPlaySeconds + light.Phase;
            const float shimmer = 0.5f + 0.5f * std::sin(t * 37.0f) * std::sin(t * 11.3f);
            const float drop = std::sin(t * 1.7f) * std::sin(t * 0.53f + 1.0f) * std::sin(t * 3.1f + 2.0f);
            float level = 1.0f - light.Amount * 0.25f * shimmer;
            if (drop > 1.0f - 0.55f * light.Amount)
                level *= 0.15f;
            actor->GetComponent<FPointLightComponent>().Light.Intensity = light.BaseIntensity * level;
        }
    }

} // namespace Leon
