#pragma once

#include "Gameplay/AAIController.hpp"
#include "Gameplay/ACharacter.hpp"
#include "Gameplay/TWeakObjectPtr.hpp"
#include "Gameplay/UHealthComponent.hpp"

#include <string>

namespace Leon {

    class ALeonHorrorCharacter;
    class ULeonHorrorAnimInstance;

    /** Enemy numbers, in metres, seconds and health points. */
    struct FLeonHorrorEnemyTuning {
        float Health = 100.0f;
        /** Slower than the player walks: it can always be outpaced while it has not noticed you. */
        float WanderSpeed = 0.9f;
        /** Faster than the player walks (2.3) and slower than the player sprints (4.6). */
        float ChaseSpeed = 2.9f;
        float InvestigateSpeed = 1.8f;

        float SightRadius = 15.0f;
        float SightHalfAngleDegrees = 55.0f;
        /** Within this distance it notices you whatever way it faces. */
        float CloseAwarenessRadius = 1.8f;
        float MemorySeconds = 6.0f;

        float AttackRange = 1.5f;
        /** The lunge still connects if the player has only just stepped back. */
        float AttackReach = 2.0f;
        float AttackDamage = 22.0f;
        /** Seconds from starting the lunge to the blow landing. */
        float AttackWindup = 0.45f;
        float AttackCooldown = 1.6f;
        /** Seconds it is stopped by a hit. */
        float StaggerSeconds = 0.55f;

        float WanderRadius = 4.0f;
        float InvestigateTimeout = 7.0f;
    };

    enum class ELeonHorrorEnemyState : uint8_t { Wander, Investigate, Chase, Attack, Staggered, Dead };

    /** The body: health, animation and the reactions to being hit. Decisions are in the controller. */
    class ALeonHorrorEnemy : public ACharacter {
    public:
        ALeonHorrorEnemy() = default;
        ALeonHorrorEnemy(entt::entity InHandle, UWorld* InWorld, const std::string& InName = "Enemy");

        void PostInitializeComponents() override;
        void BeginPlay() override;
        void UpdateAnimInstance(UAnimInstance& InAnim) const override;

        UHealthComponent* GetHealth() const { return Health.get(); }
        ULeonHorrorAnimInstance* GetHorrorAnim() const { return Anim.get(); }
        bool IsDead() const { return bDead; }
        /** Seconds of stagger left after the last hit. */
        float GetStaggerRemaining() const { return StaggerRemaining; }
        void TickStagger(float DeltaSeconds);

        /** GUID of the spawner that created it; identifies it in a save. Empty when spawned from code. */
        const std::string& GetSpawnerGuid() const { return SpawnerGuid; }
        void SetSpawnerGuid(const std::string& InGuid) { SpawnerGuid = InGuid; }

        /** Turns the body to face a planar direction (the view cone follows the body). */
        void FaceDirection(const glm::vec3& InDirection);

        FLeonHorrorEnemyTuning Tuning;

    private:
        void HandleDamage(const FDamageInfo& InInfo);
        void HandleDeath(const FDamageInfo& InInfo);

        TRef<UHealthComponent> Health;
        TRef<ULeonHorrorAnimInstance> Anim;
        std::string SpawnerGuid;
        float StaggerRemaining = 0.0f;
        bool bDead = false;
    };

    /**
     * The mind. A small explicit state machine rather than the engine behaviour tree: that tree is
     * re-evaluated from the root every tick and keeps no per-agent state, so anything that takes time
     * (a wind-up, a pause between wander points) has to be tracked here anyway.
     *
     *   sees the player       -> Chase, and Attack when close
     *   heard or lost sight   -> Investigate the last known place, then give up
     *   otherwise             -> Wander around its home
     */
    class ALeonHorrorEnemyController : public AAIController {
    public:
        ALeonHorrorEnemyController() = default;
        ALeonHorrorEnemyController(entt::entity InHandle, UWorld* InWorld, const std::string& InName = "EnemyController");

        void PostInitializeComponents() override;
        void Tick(float DeltaSeconds) override;

        ELeonHorrorEnemyState GetState() const { return State; }
        void SetHome(const glm::vec3& InHome) { Home = InHome; bHasHome = true; }
        ALeonHorrorEnemy* GetEnemy() const;
        bool IsAttackWindingUp() const { return AttackWindupRemaining > 0.0f; }

    private:
        void ConfigurePerception();
        ALeonHorrorCharacter* GetVisibleTarget() const;
        void TickChase(ALeonHorrorEnemy& InEnemy, ALeonHorrorCharacter& InTarget, float DeltaSeconds);
        void TickInvestigate(ALeonHorrorEnemy& InEnemy, float DeltaSeconds);
        void TickWander(ALeonHorrorEnemy& InEnemy, float DeltaSeconds);
        void MoveToward(ALeonHorrorEnemy& InEnemy, const glm::vec3& InDestination, float InSpeed, float InAcceptance);
        void LandAttack(ALeonHorrorEnemy& InEnemy);
        void KeepApartFrom(ALeonHorrorEnemy& InEnemy, const ALeonHorrorCharacter& InTarget);

        ELeonHorrorEnemyState State = ELeonHorrorEnemyState::Wander;
        TWeakObjectPtr<ALeonHorrorCharacter> AttackTarget;
        glm::vec3 Home{0.0f};
        bool bHasHome = false;
        glm::vec3 WanderGoal{0.0f};
        bool bHasWanderGoal = false;
        /** Starts with a pause: it stands where it was placed until it has had a first look around. */
        float WanderWait = 1.0f;
        float AttackCooldownRemaining = 0.0f;
        float AttackWindupRemaining = 0.0f;
        float InvestigateSeconds = 0.0f;
        uint32_t RandomState = 0x9E3779B9u;
        bool bPerceptionConfigured = false;
    };

    /**
     * Where an enemy appears. A plain actor so it can be placed and saved in a map: the map writer
     * skips characters as runtime actors. Properties: WanderRadius (metres), Enabled (true/false).
     */
    class ALeonHorrorEnemySpawn : public AActor {
    public:
        ALeonHorrorEnemySpawn() = default;
        ALeonHorrorEnemySpawn(entt::entity InHandle, UWorld* InWorld, const std::string& InName = "EnemySpawn");

        void OnPropertiesLoaded() override;
        void BeginPlay() override;

        /** Spawns the enemy unless disabled or recorded as killed. Returns it, or null. */
        ALeonHorrorEnemy* SpawnEnemy();
        ALeonHorrorEnemy* GetEnemy() const { return Enemy.Get(); }
        void SetWanderRadius(float InRadius) { WanderRadius = InRadius; }
        void SetEnabled(bool bInEnabled) { bEnabled = bInEnabled; }

    private:
        TWeakObjectPtr<ALeonHorrorEnemy> Enemy;
        float WanderRadius = 4.0f;
        bool bEnabled = true;
    };

} // namespace Leon
