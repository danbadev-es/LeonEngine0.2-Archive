#pragma once

#include "Gameplay/UAnimInstance.hpp"

namespace Leon {

    /** Which body the graph is built for; both use the mannequin skeleton. */
    enum class ELeonHorrorAnimProfile : uint8_t { Player = 0, Enemy = 1 };

    /** Single actions layered over locomotion with UAnimInstance::PlayOneShot. */
    enum class ELeonHorrorAnimAction : uint8_t { MeleeSwing, PistolFire, HitReact, Interact, EnemyAttack };

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

        /** Returns false when the clip for this action is not available. */
        bool PlayAction(ELeonHorrorAnimAction InAction);
        /** Holds the pistol aim pose on the upper body while true. */
        void SetAiming(bool bInAiming);
        bool IsAiming() const { return bAiming; }

        ELeonHorrorAnimProfile GetProfile() const { return Profile; }
        TRef<UAnimSequence> GetActionSequence(ELeonHorrorAnimAction InAction) const;

    private:
        TRef<UAnimSequence> LoadLinked(const std::string& InPath, bool bInLoop);
        TRef<UBlendSpace> BuildLocomotion();

        ELeonHorrorAnimProfile Profile;
        TRef<USkeleton> GraphSkeleton;
        TRef<UBlendSpace> Locomotion;
        TRef<UAnimSequence> DeathSequence;
        TRef<UAnimSequence> AimSequence;
        TRef<UAnimSequence> MeleeSequence;
        TRef<UAnimSequence> FireSequence;
        TRef<UAnimSequence> HitSequence;
        TRef<UAnimSequence> InteractSequence;
        TRef<UAnimSequence> EnemyAttackSequence;
        bool bAiming = false;
    };

} // namespace Leon
