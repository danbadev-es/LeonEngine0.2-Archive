#include "LeonHorror/ULeonHorrorAnimInstance.hpp"

#include "LeonHorror/FLeonHorrorArchetypes.hpp"

#include "Assets/UAssetManager.hpp"

namespace Leon {

    ULeonHorrorAnimInstance::ULeonHorrorAnimInstance(const std::string& InName, ELeonHorrorAnimProfile InProfile)
        : UAnimInstance(InName), Profile(InProfile) {
        EnemyClips.Idle = "/Game/Animations/EnemyIdle.lanim";
        EnemyClips.Walk = "/Game/Animations/EnemyShamble.lanim";
        EnemyClips.Attack = "/Game/Animations/EnemyAttack.lanim";
        EnemyClips.Hit = "/Game/Animations/HitReact.lanim";
        EnemyClips.Death = "/Game/Animations/DeathFromTheFront.lanim";
    }

    void ULeonHorrorAnimInstance::SetEnemyClips(const FLeonHorrorAnimSet& InClips) {
        const auto take = [](std::string& InOutPath, const std::string& InNew) {
            if (!InNew.empty())
                InOutPath = InNew;
        };
        take(EnemyClips.Idle, InClips.Idle);
        take(EnemyClips.Walk, InClips.Walk);
        take(EnemyClips.Attack, InClips.Attack);
        take(EnemyClips.Hit, InClips.Hit);
        take(EnemyClips.Death, InClips.Death);
        EnemyClips.WalkSpeedCm = InClips.WalkSpeedCm > 1.0f ? InClips.WalkSpeedCm : EnemyClips.WalkSpeedCm;
        // The graph was built for the old clips: have it built again.
        GraphSkeleton.reset();
        NativeInitializeAnimation();
    }

    TRef<UAnimSequence> ULeonHorrorAnimInstance::LoadLinked(const std::string& InPath, bool bInLoop) {
        auto sequence = UAssetManager::GetAnimSequence(InPath);
        if (!sequence)
            return nullptr;
        if (Skeleton)
            sequence->LinkSkeleton(Skeleton);
        sequence->SetLooping(bInLoop);
        return sequence;
    }

    TRef<UBlendSpace> ULeonHorrorAnimInstance::BuildLocomotion() {
        // Axes: Speed in cm/s (ACharacter writes world m/s * 100), Direction in degrees
        // (0 forward, +90 right, 180 back, -90 left).
        auto blend = UBlendSpace::Create(Profile == ELeonHorrorAnimProfile::Player ? "BS_HorrorPlayer"
                                                                                  : "BS_HorrorEnemy");
        blend->Set2D(true);
        blend->SetAxisRange({0.0f, -180.0f}, {600.0f, 180.0f});

        const auto add = [&](float InSpeed, float InDirection, const char* InPath) {
            if (auto sequence = LoadLinked(InPath, true))
                blend->AddSample({InSpeed, InDirection}, sequence);
        };
        const auto addAllDirections = [&](float InSpeed, const char* InPath) {
            for (float direction : {0.0f, 90.0f, -90.0f, 180.0f, -180.0f})
                add(InSpeed, direction, InPath);
        };

        if (Profile == ELeonHorrorAnimProfile::Enemy) {
            addAllDirections(0.0f, EnemyClips.Idle.c_str());
            addAllDirections(EnemyClips.WalkSpeedCm, EnemyClips.Walk.c_str());
            addAllDirections(600.0f, EnemyClips.Walk.c_str());
            return blend;
        }

        // Player: an authored walk forward; the run set covers strafing (while aiming), backing up
        // and sprinting. Speeds match FLeonHorrorPlayerTuning (walk 2.6 m/s, sprint 5 m/s).
        addAllDirections(0.0f, "/Game/Animations/IdleRelaxed.lanim");
        // PlayerWalk carries the flashlight in front and matches 260 cm/s; WalkForward is the old one.
        add(260.0f, 0.0f,
            LeonHorrorArchetypes::ExistingAsset("/Game/Animations/PlayerWalk.lanim", "/Game/Animations/WalkForward.lanim").c_str());
        add(260.0f, 90.0f, "/Game/Animations/RunRight.lanim");
        add(260.0f, -90.0f, "/Game/Animations/RunLeft.lanim");
        add(260.0f, 180.0f, "/Game/Animations/RunBackward.lanim");
        add(260.0f, -180.0f, "/Game/Animations/RunBackward.lanim");
        add(500.0f, 0.0f, "/Game/Animations/RunForward.lanim");
        add(500.0f, 90.0f, "/Game/Animations/RunRight.lanim");
        add(500.0f, -90.0f, "/Game/Animations/RunLeft.lanim");
        add(500.0f, 180.0f, "/Game/Animations/RunBackward.lanim");
        add(500.0f, -180.0f, "/Game/Animations/RunBackward.lanim");
        return blend;
    }

    void ULeonHorrorAnimInstance::NativeUpdateAnimation(float InDeltaSeconds) {
        // An enemy has one walk, authored for its walking speed. Chasing, it covers more ground a
        // second with the same stride, so the stride has to come faster or the feet slide.
        float rate = 1.0f;
        if (Profile == ELeonHorrorAnimProfile::Enemy && EnemyClips.WalkSpeedCm > 1.0f)
            rate = glm::clamp(GetFloat("Speed") / EnemyClips.WalkSpeedCm, 1.0f, 3.0f);
        SetFloat("LocomotionRate", rate);
        UAnimInstance::NativeUpdateAnimation(InDeltaSeconds);
    }

    void ULeonHorrorAnimInstance::NativeInitializeAnimation() {
        if (!Skeleton || GraphSkeleton.get() == Skeleton.get())
            return;
        GraphSkeleton = Skeleton;

        Locomotion = BuildLocomotion();
        const bool bEnemy = Profile == ELeonHorrorAnimProfile::Enemy;
        DeathSequence = LoadLinked(bEnemy ? EnemyClips.Death : std::string("/Game/Animations/DeathFromTheFront.lanim"), false);
        AimSequence = LoadLinked("/Game/Animations/PistolAimIdle.lanim", true);
        MeleeSequence = LoadLinked("/Game/Animations/MeleeSwing.lanim", false);
        FireSequence = LoadLinked("/Game/Animations/PistolFire.lanim", false);
        HitSequence = LoadLinked(bEnemy ? EnemyClips.Hit : std::string("/Game/Animations/HitReact.lanim"), false);
        InteractSequence = LoadLinked("/Game/Animations/Interact.lanim", false);
        EnemyAttackSequence = LoadLinked(EnemyClips.Attack, false);
        if (!bEnemy) {
            PickUpSequence = LoadLinked("/Game/Animations/PlayerPickup.lanim", false);
            PickUpLowSequence = LoadLinked("/Game/Animations/PlayerPickupLow.lanim", false);
            OpenDoorSequence = LoadLinked("/Game/Animations/PlayerOpenDoor.lanim", false);
            ReloadSequence = LoadLinked("/Game/Animations/PlayerReload.lanim", false);
            UseMedkitSequence = LoadLinked("/Game/Animations/PlayerUseMedkit.lanim", false);
            DeathFallingBackSequence = LoadLinked("/Game/Animations/PlayerDeathBackward.lanim", false);
            // Badly hurt, the player stands and walks bent over a hand pressed to the side.
            auto idle = LoadLinked("/Game/Animations/PlayerHurtIdle.lanim", true);
            auto walk = LoadLinked("/Game/Animations/PlayerHurtWalk.lanim", true);
            if (idle && walk) {
                HurtLocomotion = UBlendSpace::Create("BS_HorrorPlayerHurt");
                HurtLocomotion->Set2D(true);
                HurtLocomotion->SetAxisRange({0.0f, -180.0f}, {600.0f, 180.0f});
                for (float direction : {0.0f, 90.0f, -90.0f, 180.0f, -180.0f}) {
                    HurtLocomotion->AddSample({0.0f, direction}, idle);
                    HurtLocomotion->AddSample({260.0f, direction}, walk);
                }
            }
        }

        StateMachine = FAnimStateMachine{};
        SetBlendParamNames("Speed", "Direction");

        FAnimState locomotion;
        locomotion.Name = "Locomotion";
        locomotion.Type = EAnimNodeType::BlendSpace;
        locomotion.BlendSpace = Locomotion;
        locomotion.bLoop = true;
        locomotion.RateParam = "LocomotionRate";
        StateMachine.AddState(locomotion);

        FAnimState death;
        death.Name = "Death";
        death.Sequence = DeathSequence;
        death.bLoop = false;
        StateMachine.AddState(death);

        if (HurtLocomotion) {
            FAnimState hurt;
            hurt.Name = "LocomotionHurt";
            hurt.Type = EAnimNodeType::BlendSpace;
            hurt.BlendSpace = HurtLocomotion;
            hurt.bLoop = true;
            StateMachine.AddState(hurt);
        }

        StateMachine.AddTransition({"*", "Death", "Bool:bIsDead", 0.15f});
        StateMachine.AddTransition({"Death", "Locomotion", "NotBool:bIsDead", 0.1f});
        if (HurtLocomotion) {
            StateMachine.AddTransition({"Locomotion", "LocomotionHurt", "Bool:bHurt", 0.4f});
            StateMachine.AddTransition({"LocomotionHurt", "Locomotion", "NotBool:bHurt", 0.4f});
        }
        StateMachine.SetDefaultState("Locomotion");
        StateMachine.ResetToDefault();
    }

    TRef<UAnimSequence> ULeonHorrorAnimInstance::GetActionSequence(ELeonHorrorAnimAction InAction) const {
        switch (InAction) {
        case ELeonHorrorAnimAction::MeleeSwing:
            return MeleeSequence;
        case ELeonHorrorAnimAction::PistolFire:
            return FireSequence;
        case ELeonHorrorAnimAction::HitReact:
            return HitSequence;
        case ELeonHorrorAnimAction::Interact:
            return InteractSequence;
        case ELeonHorrorAnimAction::EnemyAttack:
            return EnemyAttackSequence;
        case ELeonHorrorAnimAction::PickUp:
            return PickUpSequence;
        case ELeonHorrorAnimAction::PickUpLow:
            return PickUpLowSequence;
        case ELeonHorrorAnimAction::OpenDoor:
            return OpenDoorSequence;
        case ELeonHorrorAnimAction::Reload:
            return ReloadSequence;
        case ELeonHorrorAnimAction::UseMedkit:
            return UseMedkitSequence;
        }
        return nullptr;
    }

    bool ULeonHorrorAnimInstance::PlayAction(ELeonHorrorAnimAction InAction) {
        auto sequence = GetActionSequence(InAction);
        // A specific clip that is missing falls back to the general one.
        if (!sequence && (InAction == ELeonHorrorAnimAction::PickUp || InAction == ELeonHorrorAnimAction::PickUpLow ||
                          InAction == ELeonHorrorAnimAction::OpenDoor))
            sequence = InteractSequence;
        if (!sequence)
            return false;
        FAnimOneShotParams params;
        params.BlendIn = 0.08f;
        params.BlendOut = 0.18f;
        // Arms act while the legs keep walking; a hit or an enemy lunge takes the whole body.
        // Picking something up bends the whole body; the rest is done with the arms.
        params.bUpperBodyOnly = InAction == ELeonHorrorAnimAction::MeleeSwing ||
                                InAction == ELeonHorrorAnimAction::PistolFire ||
                                InAction == ELeonHorrorAnimAction::Interact ||
                                InAction == ELeonHorrorAnimAction::OpenDoor ||
                                InAction == ELeonHorrorAnimAction::Reload ||
                                InAction == ELeonHorrorAnimAction::UseMedkit;
        PlayOneShot(sequence, params);
        return true;
    }

    void ULeonHorrorAnimInstance::SetDeathFromFront(bool bInFromFront) {
        if (Profile != ELeonHorrorAnimProfile::Player || !DeathFallingBackSequence)
            return;
        const TRef<UAnimSequence> wanted = bInFromFront ? DeathFallingBackSequence : DeathSequence;
        for (FAnimState& state : StateMachine.GetStates()) {
            if (state.Name == "Death")
                state.Sequence = wanted;
        }
    }

    void ULeonHorrorAnimInstance::SetAiming(bool bInAiming) {
        bAiming = bInAiming;
        SetUpperBodySequence(bAiming ? AimSequence : nullptr);
    }

} // namespace Leon
