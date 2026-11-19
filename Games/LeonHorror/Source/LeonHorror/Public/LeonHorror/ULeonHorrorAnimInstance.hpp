#pragma once

#include "Gameplay/UAnimInstance.hpp"

namespace Leon {

    /** Which body the graph is built for; both use the mannequin skeleton. */
    enum class ELeonHorrorAnimProfile : uint8_t { Player = 0, Enemy = 1 };

    /** The clips of one kind of enemy. Paths of .lanim assets; an empty one leaves the default. */
    struct FLeonHorrorAnimSet {
        std::string Idle;
        std::string Walk;
        std::string Attack;
        std::string Hit;
        std::string Death;
        /** Ground speed the walk clip was authored at, in cm/s: where it sits in the locomotion blend. */
        float WalkSpeedCm = 110.0f;
    };

    /** Single actions layered over locomotion with UAnimInstance::PlayOneShot. */
    enum class ELeonHorrorAnimAction : uint8_t {
        MeleeSwing,
        PistolFire,
        HitReact,
        Interact,
        EnemyAttack,
        PickUp,    ///< from table height
        PickUpLow, ///< from the floor
        OpenDoor,
        Reload,
        UseMedkit,
    };

    /**
     * Animation graph for the horror characters: a locomotion blend driven by Speed / Direction, a
     * death state, an aim pose on the upper body and one-shot actions.
     *
     * Gameplay never waits on these clips: hit windows and cooldowns are timed in gameplay code, so the
     * game behaves the same if a clip is missing.
     */
    class ULeonHorrorAnimInstance : public UAnimInstance {
    public:
        explicit ULeonHorrorAnimInstance(const std::string& InName = "LeonHorrorAnimInstance",
                                         ELeonHorrorAnimProfile InProfile = ELeonHorrorAnimProfile::Player);

        void NativeInitializeAnimation() override;
        void NativeUpdateAnimation(float InDeltaSeconds) override;

        /** Returns false when the clip for this action is not available. */
        bool PlayAction(ELeonHorrorAnimAction InAction);
        /** Which way the player falls when killed: backwards when the blow came from the front. */
        void SetDeathFromFront(bool bInFromFront);
        /** Holds the pistol aim pose on the upper body while true. */
        void SetAiming(bool bInAiming);
        bool IsAiming() const { return bAiming; }

        /** The clips an enemy uses; the graph is rebuilt with them. Ignored by the player profile. */
        void SetEnemyClips(const FLeonHorrorAnimSet& InClips);
        const FLeonHorrorAnimSet& GetEnemyClips() const { return EnemyClips; }

        ELeonHorrorAnimProfile GetProfile() const { return Profile; }
        TRef<UAnimSequence> GetActionSequence(ELeonHorrorAnimAction InAction) const;

    private:
        TRef<UAnimSequence> LoadLinked(const std::string& InPath, bool bInLoop);
        TRef<UBlendSpace> BuildLocomotion();

        ELeonHorrorAnimProfile Profile;
        FLeonHorrorAnimSet EnemyClips;
        TRef<USkeleton> GraphSkeleton;
        TRef<UBlendSpace> Locomotion;
        TRef<UAnimSequence> DeathSequence;
        TRef<UAnimSequence> AimSequence;
        TRef<UAnimSequence> MeleeSequence;
        TRef<UAnimSequence> FireSequence;
        TRef<UAnimSequence> HitSequence;
        TRef<UAnimSequence> InteractSequence;
        TRef<UAnimSequence> EnemyAttackSequence;
        TRef<UAnimSequence> PickUpSequence;
        TRef<UAnimSequence> PickUpLowSequence;
        TRef<UAnimSequence> OpenDoorSequence;
        TRef<UAnimSequence> ReloadSequence;
        TRef<UAnimSequence> UseMedkitSequence;
        TRef<UAnimSequence> DeathFallingBackSequence;
        TRef<UBlendSpace> HurtLocomotion;
        bool bAiming = false;
    };

} // namespace Leon
