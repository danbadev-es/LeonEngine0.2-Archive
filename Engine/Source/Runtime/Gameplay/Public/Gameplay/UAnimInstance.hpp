#pragma once

#include "Gameplay/UObject.hpp"
#include "Assets/FAnimTypes.hpp"
#include "Assets/UAnimSequence.hpp"
#include "Assets/UBlendSpace.hpp"
#include "Assets/USkeleton.hpp"
#include "Gameplay/FAnimStateMachine.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace Leon {

    /** How a one-shot animation is layered on top of the state machine. */
    struct FAnimOneShotParams {
        float BlendIn = 0.12f;
        float BlendOut = 0.18f;
        /** Only bones in the skeleton upper-body mask are affected; the legs keep following locomotion. */
        bool bUpperBodyOnly = false;
        float PlayRate = 1.0f;
    };

    /**
     * Generic animation evaluation host. Named float/bool parameters feed the state machine
     * and blend spaces. Game subclasses set parameters and build the graph.
     */
    class UAnimInstance : public UObject {
    public:
        UAnimInstance(const std::string& InName = "AnimInstance");
        ~UAnimInstance() override = default;

        void Initialize(const TRef<USkeleton>& InSkeleton);

        virtual void NativeInitializeAnimation();
        virtual void NativeUpdateAnimation(float InDeltaSeconds);
        virtual void Evaluate(FPose& OutPose);

        void ApplyRepState(const FAnimRepState& InState);
        FAnimRepState BuildRepState() const;

        void SetFloat(const std::string& InName, float InValue);
        float GetFloat(const std::string& InName, float InDefault = 0.0f) const;
        void SetBool(const std::string& InName, bool bInValue);
        bool GetBool(const std::string& InName, bool bDefault = false) const;

        TRef<USkeleton> GetSkeleton() const { return Skeleton; }
        FAnimStateMachine& GetStateMachine() { return StateMachine; }
        const FAnimStateMachine& GetStateMachine() const { return StateMachine; }

        void SetBlendParamNames(const std::string& InAxisX, const std::string& InAxisY);
        const std::string& GetBlendParamX() const { return BlendParamX; }
        const std::string& GetBlendParamY() const { return BlendParamY; }

        void SetUpperBodySequence(const TRef<UAnimSequence>& InSeq) { UpperBodySequence = InSeq; }
        TRef<UAnimSequence> GetUpperBodySequence() const { return UpperBodySequence; }

        void SetOverrideSequence(const TRef<UAnimSequence>& InSeq, bool bLoop = true);
        /** One-shot override (default non-looping); thin alias of SetOverrideSequence. */
        void PlayOneShotOverride(const TRef<UAnimSequence>& InSeq, bool bLoop = false);
        void ClearOverrideSequence();
        void ResetPoseState();

        /**
         * Plays a sequence once over whatever the graph is doing (attack, hit reaction, interaction),
         * blending in and out, then removes itself. Unlike the override it does not replace the state
         * machine or hold its last frame. There is no completion callback on purpose: gameplay timing
         * belongs to gameplay code, poll IsPlayingOneShot() if needed.
         */
        void PlayOneShot(const TRef<UAnimSequence>& InSeq, const FAnimOneShotParams& InParams = {});
        /** Fades the current one-shot out over its BlendOut time (or InBlendOut when >= 0). */
        void StopOneShot(float InBlendOut = -1.0f);
        bool IsPlayingOneShot() const { return OneShotSequence != nullptr; }
        float GetOneShotTime() const { return OneShotTime; }
        /** Current blend weight of the one-shot, 0..1. */
        float GetOneShotWeight() const;

        float GetAnimTime() const { return AnimTime; }

    protected:
        TRef<USkeleton> Skeleton;
        FAnimStateMachine StateMachine;
        TRef<UAnimSequence> UpperBodySequence;
        TRef<UAnimSequence> OverrideSequence;
        bool bOverrideLoop = true;
        float AnimTime = 0.0f;
        TRef<UAnimSequence> OneShotSequence;
        FAnimOneShotParams OneShotParams;
        float OneShotTime = 0.0f;
        float OneShotEndTime = 0.0f;
        std::vector<float> UpperBodyMask;
        std::string BlendParamX = "Speed";
        std::string BlendParamY = "Direction";
        std::unordered_map<std::string, float> FloatParams;
        std::unordered_map<std::string, bool> BoolParams;
    };

} // namespace Leon
