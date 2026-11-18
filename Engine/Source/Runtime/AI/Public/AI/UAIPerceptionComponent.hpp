#pragma once

#include "Gameplay/UActorComponent.hpp"
#include "Gameplay/TWeakObjectPtr.hpp"
#include "Engine/ECollisionChannel.hpp"

#include <functional>
#include <glm/glm.hpp>
#include <vector>

namespace Leon {

    class AActor;
    class APawn;
    class UBlackboardComponent;
    class UWorld;

    /** Sight cone for UAIPerceptionComponent. Peripheral angle is half-FOV from forward. */
    struct FAISightConfig {
        float SightRadius = 42.0f;
        float PeripheralVisionAngleDegrees = 60.0f;
        /** Inside this radius, FOV is ignored (hearing / close awareness). */
        float CloseAwarenessRadius = 20.0f;
        float MemoryMaxAge = 4.0f;
        float UpdateInterval = 0.12f;
        float EyeHeightOffset = 0.25f;
        ECollisionChannel TraceChannel = ECollisionChannel::Visibility;
        /**
         * When true an actor only becomes the target while there is a clear trace to it. When false
         * (default) line of sight is just a scoring bonus, so targets can be picked through walls.
         */
        bool bRequireLineOfSight = false;
    };

    /**
     * @brief Generic sight perception. Teams, personalities, and weapon keys stay in the game.
     * Writes optional Blackboard keys: TargetActor, HasTarget, HasLineOfSight, TargetLocation,
     * LastKnownLocation, LastKnownTargetLocation, HasLastKnown, DistanceToTarget.
     */
    class UAIPerceptionComponent : public UActorComponent {
    public:
        using FSensePredicate = std::function<bool(AActor* InActor)>;

        UAIPerceptionComponent(const std::string& InName = "AIPerceptionComponent");

        void Tick(float DeltaSeconds) override;

        void SetSightConfig(const FAISightConfig& InConfig) { SightConfig = InConfig; }
        const FAISightConfig& GetSightConfig() const { return SightConfig; }

        /** Return true to include the actor. Null predicate senses all APawns except the owned pawn. */
        void SetSensePredicate(FSensePredicate InPredicate) { SensePredicate = std::move(InPredicate); }

        void SetBlackboard(UBlackboardComponent* InBoard) { Blackboard = InBoard; }

        void UpdatePerception();
        bool HasLineOfSight(AActor& InTarget) const;
        bool IsInSightCone(AActor& InTarget) const;

        /** Actors perceived by the last update that are still alive. */
        const std::vector<AActor*>& GetPerceivedActors() const;
        /** Null once the target is destroyed, even between perception updates. */
        AActor* GetCurrentTarget() const { return CurrentTarget.Get(); }

        void RememberActor(AActor* InActor);
        void ForgetLastKnown();

        /**
         * Hearing: if the sensing pawn is within InRadius of the noise, the noise position becomes the
         * last-known location to investigate. Does not select a target. Returns true when heard.
         */
        bool ReportNoise(const glm::vec3& InLocation, float InRadius);
        /** Reports a noise to every AI controller in the world; returns how many heard it. */
        static int32_t ReportNoiseEvent(UWorld& InWorld, const glm::vec3& InLocation, float InRadius,
                                        AActor* InInstigator = nullptr);
        const glm::vec3& GetLastKnownLocation() const { return LastKnownLocation; }
        float GetLastKnownAge() const { return LastKnownAge; }
        bool HasLastKnown() const;

        void WriteToBlackboard();

    private:
        APawn* GetSensePawn() const;
        glm::vec3 GetSenseOrigin() const;
        glm::vec3 GetSenseForward() const;
        bool PassesPredicate(AActor* InActor) const;
        static bool IsActorDead(AActor* InActor);
        float ScoreCandidate(AActor& InActor, AActor* InPrevious) const;
        void AgeMemory(float DeltaSeconds);

        FAISightConfig SightConfig;
        FSensePredicate SensePredicate;
        UBlackboardComponent* Blackboard = nullptr;
        std::vector<FActorHandle> PerceivedHandles;
        mutable std::vector<AActor*> PerceivedActorsCache; // rebuilt by GetPerceivedActors()
        FActorHandle CurrentTarget;
        glm::vec3 LastKnownLocation{0.0f};
        bool bLastKnownValid = false;
        float LastKnownAge = 1.0e6f;
        float UpdateAccumulator = 0.0f;
    };

} // namespace Leon
