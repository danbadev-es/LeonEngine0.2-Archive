#include "AI/UAIPerceptionComponent.hpp"
#include "AI/UBlackboardComponent.hpp"
#include "Gameplay/AAIController.hpp"
#include "Gameplay/AActor.hpp"
#include "Gameplay/ACharacter.hpp"
#include "Gameplay/AController.hpp"
#include "Gameplay/APawn.hpp"
#include "Gameplay/UHealthComponent.hpp"
#include "Engine/UWorld.hpp"
#include "Physics/FHitResult.hpp"

#include <algorithm>
#include <cmath>

namespace Leon {

    namespace {
        float AngleDegrees(const glm::vec3& A, const glm::vec3& B) {
            const float la = glm::length(A);
            const float lb = glm::length(B);
            if (la < 1e-5f || lb < 1e-5f)
                return 180.0f;
            return glm::degrees(std::acos(std::clamp(glm::dot(A / la, B / lb), -1.0f, 1.0f)));
        }

        float PlanarDistance(const glm::vec3& A, const glm::vec3& B) {
            glm::vec3 d = A - B;
            d.y = 0.0f;
            return glm::length(d);
        }
    } // namespace

    UAIPerceptionComponent::UAIPerceptionComponent(const std::string& InName) : UActorComponent(InName) {
        // Spread perception updates across frames so N bots do not trace on the same tick.
        const uint32_t salt = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(this) >> 4);
        UpdateAccumulator = static_cast<float>(salt % 120) * 0.001f;
    }

    void UAIPerceptionComponent::Tick(float DeltaSeconds) {
        AgeMemory(DeltaSeconds);
        UpdateAccumulator += DeltaSeconds;
        if (UpdateAccumulator < SightConfig.UpdateInterval)
            return;
        UpdateAccumulator = 0.0f;
        UpdatePerception();
    }

    void UAIPerceptionComponent::AgeMemory(float DeltaSeconds) {
        LastKnownAge += DeltaSeconds;
        if (LastKnownAge >= SightConfig.MemoryMaxAge)
            LastKnownAge = SightConfig.MemoryMaxAge;
    }

    APawn* UAIPerceptionComponent::GetSensePawn() const {
        if (auto* controller = dynamic_cast<AController*>(GetOwner()))
            return controller->GetPawn();
        return dynamic_cast<APawn*>(GetOwner());
    }

    glm::vec3 UAIPerceptionComponent::GetSenseOrigin() const {
        APawn* pawn = GetSensePawn();
        if (!pawn)
            return glm::vec3(0.0f);
        glm::vec3 origin = pawn->GetActorLocation();
        origin.y += SightConfig.EyeHeightOffset;
        return origin;
    }

    glm::vec3 UAIPerceptionComponent::GetSenseForward() const {
        APawn* pawn = GetSensePawn();
        if (!pawn)
            return glm::vec3(0.0f, 0.0f, -1.0f);
        if (auto* character = dynamic_cast<ACharacter*>(pawn))
            return character->GetControlLookDirection();
        return pawn->GetActorForwardVector();
    }

    bool UAIPerceptionComponent::IsActorDead(AActor* InActor) {
        if (!InActor)
            return true;
        if (auto* health = InActor->FindComponentByClass<UHealthComponent>())
            return health->IsDead();
        return false;
    }

    bool UAIPerceptionComponent::PassesPredicate(AActor* InActor) const {
        APawn* self = GetSensePawn();
        if (!InActor || InActor->IsPendingKill() || InActor == self)
            return false;
        if (IsActorDead(InActor))
            return false;
        if (SensePredicate)
            return SensePredicate(InActor);
        return dynamic_cast<APawn*>(InActor) != nullptr;
    }

    bool UAIPerceptionComponent::HasLineOfSight(AActor& InTarget) const {
        APawn* self = GetSensePawn();
        UWorld* world = self ? self->GetWorld() : (GetOwner() ? GetOwner()->GetWorld() : nullptr);
        if (!self || !world)
            return false;
        glm::vec3 origin = GetSenseOrigin();
        glm::vec3 chest = InTarget.GetActorLocation();
        glm::vec3 to = chest - origin;
        if (glm::length(to) < 0.15f)
            return true;
        FHitResult hit;
        if (!world->LineTraceSingleByChannel(origin, chest, SightConfig.TraceChannel, self, hit))
            return true;
        return hit.Actor == &InTarget;
    }

    bool UAIPerceptionComponent::IsInSightCone(AActor& InTarget) const {
        APawn* self = GetSensePawn();
        if (!self)
            return false;
        const glm::vec3 origin = self->GetActorLocation();
        glm::vec3 to = InTarget.GetActorLocation() - origin;
        const float dist = glm::length(to);
        if (dist > SightConfig.SightRadius)
            return false;
        if (dist <= SightConfig.CloseAwarenessRadius)
            return true;
        glm::vec3 planar = to;
        planar.y = 0.0f;
        const glm::vec3 forward = GetSenseForward();
        const float ang = AngleDegrees(glm::vec3(forward.x, 0.0f, forward.z), planar);
        return ang <= SightConfig.PeripheralVisionAngleDegrees;
    }

    float UAIPerceptionComponent::ScoreCandidate(AActor& InActor, AActor* InPrevious) const {
        APawn* self = GetSensePawn();
        if (!self)
            return -1.0e9f;
        glm::vec3 to = InActor.GetActorLocation() - self->GetActorLocation();
        const float dist = glm::length(to);
        glm::vec3 planar = to;
        planar.y = 0.0f;
        const glm::vec3 forward = GetSenseForward();
        const float ang = AngleDegrees(glm::vec3(forward.x, 0.0f, forward.z), planar);
        float score = (1.0f - dist / std::max(SightConfig.SightRadius, 0.01f)) * 40.0f + (1.0f - ang / 90.0f) * 20.0f;
        if (&InActor == InPrevious)
            score += 25.0f;
        return score;
    }

    bool UAIPerceptionComponent::HasLastKnown() const {
        // An explicit flag: the world origin is a legitimate place to have seen or heard something.
        return bLastKnownValid && LastKnownAge < SightConfig.MemoryMaxAge;
    }

    void UAIPerceptionComponent::RememberActor(AActor* InActor) {
        if (!InActor)
            return;
        LastKnownLocation = InActor->GetActorLocation();
        bLastKnownValid = true;
        LastKnownAge = 0.0f;
        CurrentTarget = InActor;
    }

    bool UAIPerceptionComponent::ReportNoise(const glm::vec3& InLocation, float InRadius) {
        APawn* self = GetSensePawn();
        if (!self || InRadius <= 0.0f)
            return false;
        const glm::vec3 delta = InLocation - self->GetActorLocation();
        if (glm::dot(delta, delta) > InRadius * InRadius)
            return false;
        LastKnownLocation = InLocation;
        bLastKnownValid = true;
        LastKnownAge = 0.0f;
        WriteToBlackboard();
        return true;
    }

    int32_t UAIPerceptionComponent::ReportNoiseEvent(UWorld& InWorld, const glm::vec3& InLocation, float InRadius,
                                                     AActor* InInstigator) {
        int32_t heard = 0;
        // Copy: a listener may react by spawning or destroying controllers.
        const std::vector<AAIController*> controllers = InWorld.GetAIControllers();
        for (AAIController* controller : controllers) {
            if (!controller || controller->IsPendingKill())
                continue;
            if (InInstigator && controller->GetPawn() == InInstigator)
                continue;
            auto perception = controller->GetPerceptionComponent();
            if (perception && perception->ReportNoise(InLocation, InRadius))
                ++heard;
        }
        return heard;
    }

    void UAIPerceptionComponent::ForgetLastKnown() {
        LastKnownLocation = glm::vec3(0.0f);
        bLastKnownValid = false;
        LastKnownAge = SightConfig.MemoryMaxAge;
        CurrentTarget = nullptr;
        PerceivedHandles.clear();
        WriteToBlackboard();
    }

    const std::vector<AActor*>& UAIPerceptionComponent::GetPerceivedActors() const {
        PerceivedActorsCache.clear();
        for (const FActorHandle& handle : PerceivedHandles) {
            if (AActor* actor = handle.Get())
                PerceivedActorsCache.push_back(actor);
        }
        return PerceivedActorsCache;
    }

    void UAIPerceptionComponent::WriteToBlackboard() {
        if (!Blackboard)
            return;
        AActor* target = CurrentTarget.Get();
        Blackboard->SetValueAsObject("TargetActor", target);
        Blackboard->SetValueAsBool("HasTarget", target != nullptr);
        if (target) {
            Blackboard->SetValueAsVector("TargetLocation", target->GetActorLocation());
            const bool bLos = HasLineOfSight(*target);
            Blackboard->SetValueAsBool("HasLineOfSight", bLos);
            APawn* self = GetSensePawn();
            Blackboard->SetValueAsFloat(
                "DistanceToTarget", self ? PlanarDistance(self->GetActorLocation(), target->GetActorLocation()) : 0.0f);
        } else {
            Blackboard->SetValueAsBool("HasLineOfSight", false);
            Blackboard->SetValueAsFloat("DistanceToTarget", 0.0f);
        }
        Blackboard->SetValueAsBool("HasLastKnown", HasLastKnown());
        Blackboard->SetValueAsVector("LastKnownLocation", LastKnownLocation);
        Blackboard->SetValueAsVector("LastKnownTargetLocation", LastKnownLocation);
    }

    void UAIPerceptionComponent::UpdatePerception() {
        PerceivedHandles.clear();
        APawn* self = GetSensePawn();
        UWorld* world = self ? self->GetWorld() : (GetOwner() ? GetOwner()->GetWorld() : nullptr);
        if (!self || !world) {
            CurrentTarget = nullptr;
            WriteToBlackboard();
            return;
        }

        // Get() is already null if the previous target was destroyed since the last update.
        AActor* previous = CurrentTarget.Get();
        if (previous && (IsActorDead(previous) || !PassesPredicate(previous)))
            previous = nullptr;

        struct FCandidate {
            AActor* Actor = nullptr;
            float Score = 0.0f;
        };
        FCandidate top[4];
        int topCount = 0;
        auto consider = [&](AActor* other, float score) {
            if (topCount < 4) {
                top[topCount++] = {other, score};
            } else {
                int worst = 0;
                for (int i = 1; i < 4; ++i) {
                    if (top[i].Score < top[worst].Score)
                        worst = i;
                }
                if (score > top[worst].Score)
                    top[worst] = {other, score};
            }
        };

        for (const auto& actor : world->GetAllActors()) {
            AActor* other = actor.get();
            if (!PassesPredicate(other) || !IsInSightCone(*other))
                continue;
            PerceivedHandles.emplace_back(other);
            consider(other, ScoreCandidate(*other, previous));
        }

        AActor* best = nullptr;
        float bestScore = -1.0e9f;
        for (int i = 0; i < topCount; ++i) {
            float score = top[i].Score;
            const bool bLos = HasLineOfSight(*top[i].Actor);
            if (SightConfig.bRequireLineOfSight && !bLos)
                continue;
            if (bLos)
                score += 100.0f;
            if (score > bestScore) {
                bestScore = score;
                best = top[i].Actor;
            }
        }

        CurrentTarget = best;
        if (best && HasLineOfSight(*best)) {
            LastKnownLocation = best->GetActorLocation();
            bLastKnownValid = true;
            LastKnownAge = 0.0f;
        }
        WriteToBlackboard();
    }

} // namespace Leon
