#include "Gameplay/UAnimInstance.hpp"
#include "Assets/FAnimRuntime.hpp"

#include <algorithm>

namespace Leon {

    UAnimInstance::UAnimInstance(const std::string& InName) : UObject(InName) {}

    void UAnimInstance::Initialize(const TRef<USkeleton>& InSkeleton) {
        Skeleton = InSkeleton;
        if (Skeleton)
            UpperBodyMask = Skeleton->BuildUpperBodyMask();
        AnimTime = 0.0f;
        NativeInitializeAnimation();
    }

    void UAnimInstance::NativeInitializeAnimation() {}

    void UAnimInstance::SetBlendParamNames(const std::string& InAxisX, const std::string& InAxisY) {
        BlendParamX = InAxisX;
        BlendParamY = InAxisY;
    }

    void UAnimInstance::SetFloat(const std::string& InName, float InValue) {
        FloatParams[InName] = InValue;
    }

    float UAnimInstance::GetFloat(const std::string& InName, float InDefault) const {
        auto it = FloatParams.find(InName);
        return it != FloatParams.end() ? it->second : InDefault;
    }

    void UAnimInstance::SetBool(const std::string& InName, bool bInValue) {
        BoolParams[InName] = bInValue;
    }

    bool UAnimInstance::GetBool(const std::string& InName, bool bDefault) const {
        auto it = BoolParams.find(InName);
        return it != BoolParams.end() ? it->second : bDefault;
    }

    void UAnimInstance::NativeUpdateAnimation(float InDeltaSeconds) {
        AnimTime += InDeltaSeconds;
        StateMachine.Update(InDeltaSeconds, *this);

        if (OneShotSequence) {
            OneShotTime += InDeltaSeconds * std::max(OneShotParams.PlayRate, 0.0f);
            if (OneShotTime >= OneShotEndTime)
                OneShotSequence = nullptr; // finished: the graph shows through again
        }
    }

    void UAnimInstance::PlayOneShot(const TRef<UAnimSequence>& InSeq, const FAnimOneShotParams& InParams) {
        OneShotSequence = InSeq;
        OneShotParams = InParams;
        OneShotTime = 0.0f;
        OneShotEndTime = InSeq ? InSeq->GetDuration() : 0.0f;
        if (OneShotSequence && Skeleton)
            OneShotSequence->LinkSkeleton(Skeleton);
        if (OneShotEndTime <= 0.0f)
            OneShotSequence = nullptr;
    }

    void UAnimInstance::StopOneShot(float InBlendOut) {
        if (!OneShotSequence)
            return;
        if (InBlendOut >= 0.0f)
            OneShotParams.BlendOut = InBlendOut;
        if (OneShotParams.BlendOut <= 0.0f) {
            OneShotSequence = nullptr;
            return;
        }
        // Fade from the current weight: shorten the remaining time to that fraction of the blend-out.
        const float weight = GetOneShotWeight();
        OneShotEndTime = std::min(OneShotEndTime, OneShotTime + OneShotParams.BlendOut * weight);
    }

    float UAnimInstance::GetOneShotWeight() const {
        if (!OneShotSequence)
            return 0.0f;
        float weight = 1.0f;
        if (OneShotParams.BlendIn > 0.0f)
            weight = std::min(weight, OneShotTime / OneShotParams.BlendIn);
        if (OneShotParams.BlendOut > 0.0f)
            weight = std::min(weight, (OneShotEndTime - OneShotTime) / OneShotParams.BlendOut);
        return std::clamp(weight, 0.0f, 1.0f);
    }

    void UAnimInstance::Evaluate(FPose& OutPose) {
        if (!Skeleton) {
            OutPose = {};
            return;
        }

        if (OverrideSequence) {
            FAnimRuntime::SampleSequence(*OverrideSequence, *Skeleton, AnimTime, bOverrideLoop, OutPose);
        } else {
            StateMachine.Evaluate(*Skeleton, *this, OutPose);
        }

        if (UpperBodySequence && !UpperBodyMask.empty()) {
            FPose upper;
            FAnimRuntime::SampleSequence(*UpperBodySequence, *Skeleton, AnimTime, true, upper);
            FPose layered;
            FAnimRuntime::LayeredBlend(OutPose, upper, UpperBodyMask, layered);
            OutPose = std::move(layered);
        }

        // One-shot on top of everything else.
        const float oneShotWeight = GetOneShotWeight();
        if (OneShotSequence && oneShotWeight > 0.0f) {
            FPose shot;
            FAnimRuntime::SampleSequence(*OneShotSequence, *Skeleton, OneShotTime, false, shot);
            FPose blended;
            if (OneShotParams.bUpperBodyOnly && !UpperBodyMask.empty()) {
                std::vector<float> weights = UpperBodyMask;
                for (float& w : weights)
                    w *= oneShotWeight;
                FAnimRuntime::LayeredBlend(OutPose, shot, weights, blended);
            } else {
                FAnimRuntime::BlendPoses(OutPose, shot, oneShotWeight, blended);
            }
            OutPose = std::move(blended);
        }
    }

    void UAnimInstance::ApplyRepState(const FAnimRepState& InState) {
        SetFloat("Speed", InState.Speed);
        SetFloat("Direction", InState.Direction);
        SetFloat("AimPitch", InState.AimPitch);
        SetBool("bIsFalling", InState.IsInAir());
        SetBool("bIsInAir", InState.IsInAir());
        SetBool("bIsCrouched", InState.IsCrouched());
    }

    FAnimRepState UAnimInstance::BuildRepState() const {
        FAnimRepState state;
        state.Speed = GetFloat("Speed");
        state.Direction = GetFloat("Direction");
        state.AimPitch = GetFloat("AimPitch");
        state.SetFlag(FAnimRepState::FlagInAir, GetBool("bIsFalling") || GetBool("bIsInAir"));
        state.SetFlag(FAnimRepState::FlagCrouched, GetBool("bIsCrouched"));
        return state;
    }

    void UAnimInstance::ResetPoseState() {
        AnimTime = 0.0f;
        ClearOverrideSequence();
        OneShotSequence = nullptr;
        SetUpperBodySequence(nullptr);
        SetBool("bIsDead", false);
        SetBool("bIsFalling", false);
        SetFloat("Speed", 0.0f);
        SetFloat("Direction", 0.0f);
        SetFloat("VerticalSpeed", 0.0f);
        StateMachine.ResetToDefault();
    }

    void UAnimInstance::SetOverrideSequence(const TRef<UAnimSequence>& InSeq, bool bLoop) {
        OverrideSequence = InSeq;
        bOverrideLoop = bLoop;
        AnimTime = 0.0f;
        if (OverrideSequence && Skeleton)
            OverrideSequence->LinkSkeleton(Skeleton);
    }

    void UAnimInstance::PlayOneShotOverride(const TRef<UAnimSequence>& InSeq, bool bLoop) {
        SetOverrideSequence(InSeq, bLoop);
    }

    void UAnimInstance::ClearOverrideSequence() {
        OverrideSequence = nullptr;
    }

} // namespace Leon
