#pragma once

#include "Gameplay/UActorComponent.hpp"

#include <glm/glm.hpp>
#include <string>

namespace Leon {

    class ALeonHorrorCharacter;
    class AActor;

    enum class ELeonHorrorWeapon : uint8_t { None = 0, Pipe = 1, Pistol = 2 };

    /** Combat numbers, in metres, seconds and health points. */
    struct FLeonHorrorCombatTuning {
        float MeleeDamage = 34.0f;
        float MeleeRange = 2.0f;
        /** Half-angle of the swing in front of the player. */
        float MeleeArcDegrees = 55.0f;
        /** Seconds from the button press to the blow landing. */
        float MeleeWindup = 0.28f;
        float MeleeCooldown = 0.9f;
        float MeleeNoiseRadius = 7.0f;

        float PistolDamage = 45.0f;
        float PistolRange = 45.0f;
        float PistolCooldown = 0.45f;
        int32_t PistolClipSize = 8;
        float PistolReloadSeconds = 1.7f;
        /** A gunshot is heard this far away. */
        float PistolNoiseRadius = 30.0f;
    };

    /**
     * The player's weapons: a steel pipe and a handgun with scarce ammunition.
     *
     * Timing lives here, not in animation clips: the blow lands MeleeWindup seconds after the press and
     * a reload finishes after PistolReloadSeconds, whether or not a clip is playing. Loaded rounds are
     * held by the component; spare rounds are the "Ammo.Pistol" count in the inventory. Nothing reloads
     * automatically.
     */
    class ULeonHorrorCombatComponent : public UActorComponent {
    public:
        explicit ULeonHorrorCombatComponent(const std::string& InName = "Combat");

        void BeginPlay() override;
        void Tick(float DeltaSeconds) override;

        /** Equips a weapon the owner carries. Returns false (and changes nothing) if it does not. */
        bool Equip(ELeonHorrorWeapon InWeapon);
        ELeonHorrorWeapon GetEquipped() const { return Equipped; }

        /**
         * Attack with the equipped weapon. The pipe swings; the handgun fires only while aiming.
         * Returns true when an attack actually started (a click on an empty gun returns false).
         */
        bool StartAttack(bool bInAiming);
        /** Starts reloading the handgun. False if it is full, there are no spare rounds, or it is busy. */
        bool StartReload();

        int32_t GetLoadedRounds() const { return LoadedRounds; }
        void SetLoadedRounds(int32_t InRounds);
        int32_t GetSpareRounds() const;
        bool IsReloading() const { return ReloadRemaining > 0.0f; }
        bool IsSwinging() const { return SwingRemaining > 0.0f; }
        bool CanAttack() const { return Cooldown <= 0.0f && !IsReloading() && !IsSwinging(); }

        // Counters for the HUD and for tests.
        int32_t GetShotsFired() const { return ShotsFired; }
        int32_t GetDryFires() const { return DryFires; }
        int32_t GetMeleeHits() const { return MeleeHits; }
        AActor* GetLastHitActor() const { return LastHitActor; }

        FLeonHorrorCombatTuning Tuning;

    private:
        ALeonHorrorCharacter* GetPlayer() const;
        void ResolveMeleeHit();
        void FirePistol();
        void FinishReload();
        void OnInventoryChanged(const std::string& InItemId);
        bool OwnerHas(ELeonHorrorWeapon InWeapon) const;

        ELeonHorrorWeapon Equipped = ELeonHorrorWeapon::None;
        int32_t LoadedRounds = 0;
        float Cooldown = 0.0f;
        float SwingRemaining = 0.0f;
        float ReloadRemaining = 0.0f;
        int32_t ShotsFired = 0;
        int32_t DryFires = 0;
        int32_t MeleeHits = 0;
        AActor* LastHitActor = nullptr; // informational only; valid for the frame it was set
    };

} // namespace Leon
