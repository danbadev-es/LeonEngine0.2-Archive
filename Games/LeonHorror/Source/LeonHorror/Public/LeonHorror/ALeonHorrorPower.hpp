#pragma once

#include "LeonHorror/ALeonHorrorInteractable.hpp"

#include <string>

namespace Leon {

    /**
     * Electricity, as the level uses it.
     *
     * Anything in a map may carry a `PowerGroup` property: a light, a light fitting, a humming
     * machine, a door with an electric lock. While its group has no power the light is off, the
     * fitting does not glow, the sound is silent and the door stays shut. A fuse box feeds one
     * group once it is given its fuse. The game mode keeps which groups are live and puts it in
     * the save. Things without the property never lose power.
     *
     * A light with a `Flicker` property (0 to 1: how badly) wavers while it is on.
     */

    /**
     * A sound that belongs to a place: a looping hum, a drip, a draught. Properties: Sound (asset
     * path without extension), Volume (default 1), Radius in metres (default 10), Music (true: the
     * level's bed, played everywhere instead of from this spot), Enabled (default true), PowerGroup.
     */
    class ALeonHorrorAmbientSound : public AActor {
    public:
        ALeonHorrorAmbientSound() = default;
        ALeonHorrorAmbientSound(entt::entity InHandle, UWorld* InWorld, const std::string& InName = "AmbientSound");

        void OnPropertiesLoaded() override;
        void BeginPlay() override;
        void EndPlay() override;

        void SetSound(const std::string& InPath, float InVolume = 1.0f, float InRadius = 10.0f);
        const std::string& GetSound() const { return Sound; }
        bool IsMusic() const { return bMusic; }
        /** Starts the track again if this is the level's music (something else has been playing over it). */
        void RestartMusic();
        /** Starts or stops it; a sound that is off costs nothing. */
        void SetEnabled(bool bInEnabled);
        bool IsEnabled() const { return bEnabled; }
        /** Whether it would be heard now: enabled, in play, and given a sound. */
        bool IsPlaying() const { return bPlaying; }

    private:
        void Refresh();

        std::string Sound;
        float Volume = 1.0f;
        float Radius = 10.0f;
        bool bMusic = false;
        bool bEnabled = true;
        bool bPlaying = false;
        uint32_t VoiceId = 0;
    };

    /**
     * The distribution board. Properties: Group (the power group it feeds), RequiredItem (default
     * Fuse; it is used up). Using it with the fuse in hand turns the group on, for good.
     */
    class ALeonHorrorFuseBox : public ALeonHorrorInteractable {
    public:
        ALeonHorrorFuseBox() = default;
        ALeonHorrorFuseBox(entt::entity InHandle, UWorld* InWorld, const std::string& InName = "FuseBox");

        void OnPropertiesLoaded() override;
        bool CanInteract(const APawn& InInstigator) const override;
        std::string GetInteractPrompt(const APawn& InInstigator) const override;
        bool Interact(APawn& InInstigator) override;
        /** The board hangs on the wall above its origin on the floor. */
        glm::vec3 GetInteractLocation() const override { return GetActorLocation() + glm::vec3(0.0f, 1.45f, 0.0f); }

        void SetGroup(const std::string& InGroup) { Group = InGroup; }
        const std::string& GetGroup() const { return Group; }
        const std::string& GetRequiredItem() const { return RequiredItem; }
        bool IsLive() const;

    private:
        std::string Group = "Main";
        std::string RequiredItem = "Fuse";
    };

} // namespace Leon
