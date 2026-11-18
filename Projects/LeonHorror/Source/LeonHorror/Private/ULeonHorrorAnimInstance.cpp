#include "LeonHorror/ULeonHorrorAnimInstance.hpp"

#include "Assets/UAssetManager.hpp"

namespace Leon {

    ULeonHorrorAnimInstance::ULeonHorrorAnimInstance(const std::string& InName, ELeonHorrorAnimProfile InProfile)
        : UAnimInstance(InName), Profile(InProfile) {}

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

        addAllDirections(0.0f, "/Game/Animations/Idle.lanim");
        if (Profile == ELeonHorrorAnimProfile::Enemy) {
            addAllDirections(110.0f, "/Game/Animations/EnemyShamble.lanim");
            addAllDirections(600.0f, "/Game/Animations/EnemyShamble.lanim");
            return blend;
        }

        // Player: an authored slow walk forward; the run set covers strafing, backing up and sprinting.
        add(230.0f, 0.0f, "/Game/Animations/WalkForward.lanim");
        add(230.0f, 90.0f, "/Game/Animations/RunRight.lanim");
        add(230.0f, -90.0f, "/Game/Animations/RunLeft.lanim");
        add(230.0f, 180.0f, "/Game/Animations/RunBackward.lanim");
        add(230.0f, -180.0f, "/Game/Animations/RunBackward.lanim");
        add(460.0f, 0.0f, "/Game/Animations/RunForward.lanim");
        add(460.0f, 90.0f, "/Game/Animations/RunRight.lanim");
        add(460.0f, -90.0f, "/Game/Animations/RunLeft.lanim");
        add(460.0f, 180.0f, "/Game/Animations/RunBackward.lanim");
        add(460.0f, -180.0f, "/Game/Animations/RunBackward.lanim");
        return blend;
    }

    void ULeonHorrorAnimInstance::NativeInitializeAnimation() {
        if (!Skeleton || GraphSkeleton.get() == Skeleton.get())
            return;
        GraphSkeleton = Skeleton;

        Locomotion = BuildLocomotion();
        DeathSequence = LoadLinked("/Game/Animations/DeathFromTheFront.lanim", false);
        AimSequence = LoadLinked("/Game/Animations/PistolAimIdle.lanim", true);
        MeleeSequence = LoadLinked("/Game/Animations/MeleeSwing.lanim", false);
        FireSequence = LoadLinked("/Game/Animations/PistolFire.lanim", false);
        HitSequence = LoadLinked("/Game/Animations/HitReact.lanim", false);
        InteractSequence = LoadLinked("/Game/Animations/Interact.lanim", false);
        EnemyAttackSequence = LoadLinked("/Game/Animations/EnemyAttack.lanim", false);

        StateMachine = FAnimStateMachine{};
        SetBlendParamNames("Speed", "Direction");

        FAnimState locomotion;
        locomotion.Name = "Locomotion";
        locomotion.Type = EAnimNodeType::BlendSpace;
        locomotion.BlendSpace = Locomotion;
        locomotion.bLoop = true;
        StateMachine.AddState(locomotion);

        FAnimState death;
        death.Name = "Death";
        death.Sequence = DeathSequence;
        death.bLoop = false;
        StateMachine.AddState(death);

        StateMachine.AddTransition({"*", "Death", "Bool:bIsDead", 0.15f});
        StateMachine.AddTransition({"Death", "Locomotion", "NotBool:bIsDead", 0.1f});
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
        }
        return nullptr;
    }

    bool ULeonHorrorAnimInstance::PlayAction(ELeonHorrorAnimAction InAction) {
        auto sequence = GetActionSequence(InAction);
        if (!sequence)
            return false;
        FAnimOneShotParams params;
        params.BlendIn = 0.08f;
        params.BlendOut = 0.18f;
        // Arms act while the legs keep walking; a hit or an enemy lunge takes the whole body.
        params.bUpperBodyOnly = InAction == ELeonHorrorAnimAction::MeleeSwing ||
                                InAction == ELeonHorrorAnimAction::PistolFire ||
                                InAction == ELeonHorrorAnimAction::Interact;
        PlayOneShot(sequence, params);
        return true;
    }

    void ULeonHorrorAnimInstance::SetAiming(bool bInAiming) {
        bAiming = bInAiming;
        SetUpperBodySequence(bAiming ? AimSequence : nullptr);
    }

} // namespace Leon
