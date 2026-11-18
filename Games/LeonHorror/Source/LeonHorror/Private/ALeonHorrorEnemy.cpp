#include "LeonHorror/ALeonHorrorEnemy.hpp"

#include "LeonHorror/ALeonHorrorCharacter.hpp"
#include "LeonHorror/ALeonHorrorGameMode.hpp"
#include "LeonHorror/ULeonHorrorAnimInstance.hpp"

#include "AI/UAIPerceptionComponent.hpp"
#include "Assets/USkeletalMesh.hpp"
#include "Application/FApplication.hpp"
#include "Engine/UWorld.hpp"
#include "Gameplay/UCharacterMovementComponent.hpp"
#include "Gameplay/UGameplayStatics.hpp"
#include "Gameplay/USkeletalMeshComponent.hpp"

#include <algorithm>
#include <cmath>

namespace Leon {

    namespace {
        constexpr const char* kEnemyMeshPath = "/Game/SkeletalMeshes/YBot.lskeletalmesh";
        constexpr float kEnemyHeightMetres = 1.82f;

        glm::vec3 Planar(const glm::vec3& InVector) {
            return {InVector.x, 0.0f, InVector.z};
        }
    } // namespace

    // ---------------------------------------------------------------------------------------------
    // Enemy body
    // ---------------------------------------------------------------------------------------------

    ALeonHorrorEnemy::ALeonHorrorEnemy(entt::entity InHandle, UWorld* InWorld, const std::string& InName)
        : ACharacter(InHandle, InWorld, InName) {
        SetClass("ALeonHorrorEnemy");
    }

    void ALeonHorrorEnemy::PostInitializeComponents() {
        ACharacter::PostInitializeComponents();
        if (!Health) {
            Health = AddActorComponent<UHealthComponent>("Health");
            Health->SetMaxHealth(Tuning.Health);
            Health->SetHealth(Tuning.Health);
        }
        SetThirdPerson(true); // always show the body
        if (auto movement = GetCharacterMovement()) {
            movement->SetMaxWalkSpeed(Tuning.WanderSpeed);
            movement->SetJumpMaxCount(1);
        }

        auto mesh = GetMesh();
        if (!mesh)
            return;
        if (!Anim)
            Anim = MakeRef<ULeonHorrorAnimInstance>("EnemyAnim", ELeonHorrorAnimProfile::Enemy);
        mesh->SetMeshAssetPath(kEnemyMeshPath);
        mesh->SetAnimInstance(Anim);
        float scale = 1.0f;
        float feetY = 0.0f;
        if (auto asset = mesh->GetSkeletalMesh()) {
            const float meshHeight = asset->GetBoundsMax().y - asset->GetBoundsMin().y;
            if (meshHeight > 1e-3f)
                scale = kEnemyHeightMetres / meshHeight;
            feetY = asset->GetBoundsMin().y;
        }
        mesh->SetRelativeScale3D(glm::vec3(scale));
        mesh->SetRelativeLocation(glm::vec3(0.0f, -GetCapsuleHalfHeight() - feetY * scale, 0.0f));
    }

    void ALeonHorrorEnemy::BeginPlay() {
        ACharacter::BeginPlay();
        if (Health) {
            Health->OnDamage.push_back([this](const FDamageInfo& InInfo) { HandleDamage(InInfo); });
            Health->OnDeath.push_back([this](const FDamageInfo& InInfo) { HandleDeath(InInfo); });
        }
    }

    void ALeonHorrorEnemy::UpdateAnimInstance(UAnimInstance& InAnim) const {
        ACharacter::UpdateAnimInstance(InAnim);
        InAnim.SetBool("bIsDead", bDead);
    }

    void ALeonHorrorEnemy::TickStagger(float DeltaSeconds) {
        if (StaggerRemaining > 0.0f)
            StaggerRemaining = std::max(0.0f, StaggerRemaining - DeltaSeconds);
    }

    void ALeonHorrorEnemy::FaceDirection(const glm::vec3& InDirection) {
        const glm::vec3 planar = Planar(InDirection);
        if (glm::length(planar) < 1e-4f)
            return;
        // Control yaw convention: forward = (cos yaw, 0, sin yaw).
        SetControlYaw(glm::degrees(std::atan2(planar.z, planar.x)));
    }

    void ALeonHorrorEnemy::HandleDamage(const FDamageInfo& InInfo) {
        (void)InInfo;
        if (bDead)
            return;
        StaggerRemaining = Tuning.StaggerSeconds;
        if (auto movement = GetCharacterMovement())
            movement->StopMovementImmediately();
        if (Anim)
            Anim->PlayAction(ELeonHorrorAnimAction::HitReact);
        if (FApplication::HasInstance())
            UGameplayStatics::PlaySoundAtLocation("/Game/Audio/SFX_DamageTaken", GetActorLocation(), 0.8f, 25.0f);
    }

    void ALeonHorrorEnemy::HandleDeath(const FDamageInfo& InInfo) {
        (void)InInfo;
        if (bDead)
            return;
        bDead = true;
        StaggerRemaining = 0.0f;
        if (auto movement = GetCharacterMovement()) {
            movement->StopMovementImmediately();
            movement->SetMovementMode(EMovementMode::None);
        }
        // A corpse neither blocks the corridor nor stops bullets.
        if (auto capsule = GetCapsuleComponent())
            capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        if (auto* gameMode = World ? dynamic_cast<ALeonHorrorGameMode*>(World->GetGameMode()) : nullptr)
            gameMode->NotifyEnemyKilled(*this);
    }

    // ---------------------------------------------------------------------------------------------
    // Controller
    // ---------------------------------------------------------------------------------------------

    ALeonHorrorEnemyController::ALeonHorrorEnemyController(entt::entity InHandle, UWorld* InWorld,
                                                           const std::string& InName)
        : AAIController(InHandle, InWorld, InName) {
        SetClass("ALeonHorrorEnemyController");
        // Different enemies must not wander in step.
        RandomState ^= static_cast<uint32_t>(static_cast<uint32_t>(InHandle) * 2654435761u);
    }

    void ALeonHorrorEnemyController::PostInitializeComponents() {
        AAIController::PostInitializeComponents();
        ConfigurePerception();
    }

    ALeonHorrorEnemy* ALeonHorrorEnemyController::GetEnemy() const {
        return dynamic_cast<ALeonHorrorEnemy*>(GetPawn());
    }

    void ALeonHorrorEnemyController::ConfigurePerception() {
        if (!Perception)
            return;
        const ALeonHorrorEnemy* enemy = GetEnemy();
        const FLeonHorrorEnemyTuning tuning = enemy ? enemy->Tuning : FLeonHorrorEnemyTuning{};
        FAISightConfig sight;
        sight.SightRadius = tuning.SightRadius;
        sight.PeripheralVisionAngleDegrees = tuning.SightHalfAngleDegrees;
        sight.CloseAwarenessRadius = tuning.CloseAwarenessRadius;
        sight.MemoryMaxAge = tuning.MemorySeconds;
        sight.EyeHeightOffset = 0.6f;
        sight.TraceChannel = ECollisionChannel::WorldStatic; // walls and closed doors hide the player
        sight.bRequireLineOfSight = true;
        Perception->SetSightConfig(sight);
        // Only the living player is of interest.
        Perception->SetSensePredicate([](AActor* InActor) {
            const auto* player = dynamic_cast<ALeonHorrorCharacter*>(InActor);
            return player && !player->IsDead();
        });
        bPerceptionConfigured = enemy != nullptr;
    }

    ALeonHorrorCharacter* ALeonHorrorEnemyController::GetVisibleTarget() const {
        return Perception ? dynamic_cast<ALeonHorrorCharacter*>(Perception->GetCurrentTarget()) : nullptr;
    }

    void ALeonHorrorEnemyController::MoveToward(ALeonHorrorEnemy& InEnemy, const glm::vec3& InDestination,
                                                float InSpeed, float InAcceptance) {
        if (auto movement = InEnemy.GetCharacterMovement())
            movement->SetMaxWalkSpeed(InSpeed);
        const glm::vec3 to = Planar(InDestination - InEnemy.GetActorLocation());
        if (glm::length(to) > InAcceptance)
            InEnemy.FaceDirection(to);
        MoveToLocation(InDestination, InAcceptance);
    }

    void ALeonHorrorEnemyController::KeepApartFrom(ALeonHorrorEnemy& InEnemy, const ALeonHorrorCharacter& InTarget) {
        // Characters do not block each other in the engine; stop the enemy standing inside the player.
        const float minimum = InEnemy.GetCapsuleRadius() + InTarget.GetCapsuleRadius();
        glm::vec3 away = Planar(InEnemy.GetActorLocation() - InTarget.GetActorLocation());
        const float distance = glm::length(away);
        if (distance >= minimum)
            return;
        away = distance > 1e-4f ? away / distance : glm::vec3(1.0f, 0.0f, 0.0f);
        InEnemy.SetActorLocation(InEnemy.GetActorLocation() + away * (minimum - distance));
    }

    void ALeonHorrorEnemyController::LandAttack(ALeonHorrorEnemy& InEnemy) {
        ALeonHorrorCharacter* target = AttackTarget.Get();
        AttackTarget = nullptr;
        if (!target || target->IsDead())
            return;
        // Stepping out of reach during the wind-up avoids the blow.
        if (glm::length(Planar(target->GetActorLocation() - InEnemy.GetActorLocation())) > InEnemy.Tuning.AttackReach)
            return;
        FDamageInfo info;
        info.DamageAmount = InEnemy.Tuning.AttackDamage;
        info.DamageType = EDamageType::Point;
        info.Instigator = &InEnemy;
        info.Causer = &InEnemy;
        info.HitActor = target;
        info.HitLocation = target->GetActorLocation();
        if (UHealthComponent* health = target->GetHealth())
            health->ApplyDamage(info);
        if (FApplication::HasInstance())
            UGameplayStatics::PlaySound2D("/Game/Audio/SFX_DamageTaken", 1.0f);
    }

    void ALeonHorrorEnemyController::TickChase(ALeonHorrorEnemy& InEnemy, ALeonHorrorCharacter& InTarget,
                                               float DeltaSeconds) {
        (void)DeltaSeconds;
        const glm::vec3 to = Planar(InTarget.GetActorLocation() - InEnemy.GetActorLocation());
        const float distance = glm::length(to);
        InEnemy.FaceDirection(to);

        if (distance <= InEnemy.Tuning.AttackRange) {
            State = ELeonHorrorEnemyState::Attack;
            StopMovement();
            if (AttackCooldownRemaining <= 0.0f && AttackWindupRemaining <= 0.0f) {
                AttackTarget = &InTarget;
                AttackWindupRemaining = InEnemy.Tuning.AttackWindup;
                AttackCooldownRemaining = InEnemy.Tuning.AttackCooldown;
                if (auto* anim = InEnemy.GetHorrorAnim())
                    anim->PlayAction(ELeonHorrorAnimAction::EnemyAttack);
            }
        } else {
            State = ELeonHorrorEnemyState::Chase;
            MoveToward(InEnemy, InTarget.GetActorLocation(), InEnemy.Tuning.ChaseSpeed, InEnemy.Tuning.AttackRange * 0.8f);
        }
        KeepApartFrom(InEnemy, InTarget);
    }

    void ALeonHorrorEnemyController::TickInvestigate(ALeonHorrorEnemy& InEnemy, float DeltaSeconds) {
        State = ELeonHorrorEnemyState::Investigate;
        InvestigateSeconds += DeltaSeconds;
        const glm::vec3 place = Perception->GetLastKnownLocation();
        const float distance = glm::length(Planar(place - InEnemy.GetActorLocation()));
        if (distance < 1.2f || InvestigateSeconds > InEnemy.Tuning.InvestigateTimeout) {
            // Nothing here: forget it and go back to wandering from where it stands.
            Perception->ForgetLastKnown();
            InvestigateSeconds = 0.0f;
            StopMovement();
            bHasWanderGoal = false;
            WanderWait = 1.5f;
            return;
        }
        MoveToward(InEnemy, place, InEnemy.Tuning.InvestigateSpeed, 0.8f);
    }

    void ALeonHorrorEnemyController::TickWander(ALeonHorrorEnemy& InEnemy, float DeltaSeconds) {
        State = ELeonHorrorEnemyState::Wander;
        if (!bHasHome)
            SetHome(InEnemy.GetActorLocation());

        if (WanderWait > 0.0f) {
            WanderWait -= DeltaSeconds;
            return;
        }
        if (!bHasWanderGoal) {
            const auto nextRandom = [this]() {
                RandomState = RandomState * 1664525u + 1013904223u;
                return static_cast<float>((RandomState >> 8) & 0xFFFFu) / 65535.0f;
            };
            const float angle = nextRandom() * 6.2831853f;
            const float radius = InEnemy.Tuning.WanderRadius * (0.35f + 0.65f * nextRandom());
            WanderGoal = Home + glm::vec3(std::cos(angle), 0.0f, std::sin(angle)) * radius;
            WanderGoal.y = InEnemy.GetActorLocation().y;
            bHasWanderGoal = true;
        }
        if (glm::length(Planar(WanderGoal - InEnemy.GetActorLocation())) < 0.7f) {
            bHasWanderGoal = false;
            WanderWait = 2.0f;
            StopMovement();
            return;
        }
        MoveToward(InEnemy, WanderGoal, InEnemy.Tuning.WanderSpeed, 0.5f);
    }

    void ALeonHorrorEnemyController::Tick(float DeltaSeconds) {
        AAIController::Tick(DeltaSeconds);
        ALeonHorrorEnemy* enemy = GetEnemy();
        if (!enemy || !Perception)
            return;
        if (!bPerceptionConfigured)
            ConfigurePerception(); // the pawn (and its tuning) arrives after the controller is created

        if (enemy->IsDead()) {
            State = ELeonHorrorEnemyState::Dead;
            AttackWindupRemaining = 0.0f;
            StopMovement();
            return;
        }

        if (AttackCooldownRemaining > 0.0f)
            AttackCooldownRemaining = std::max(0.0f, AttackCooldownRemaining - DeltaSeconds);

        enemy->TickStagger(DeltaSeconds);
        if (enemy->GetStaggerRemaining() > 0.0f) {
            // A hit interrupts whatever it was doing, including a lunge that had not landed yet.
            State = ELeonHorrorEnemyState::Staggered;
            AttackWindupRemaining = 0.0f;
            AttackTarget = nullptr;
            StopMovement();
            return;
        }

        if (AttackWindupRemaining > 0.0f) {
            AttackWindupRemaining -= DeltaSeconds;
            State = ELeonHorrorEnemyState::Attack;
            StopMovement();
            if (AttackWindupRemaining <= 0.0f) {
                AttackWindupRemaining = 0.0f;
                LandAttack(*enemy);
            }
            return;
        }

        if (ALeonHorrorCharacter* target = GetVisibleTarget()) {
            InvestigateSeconds = 0.0f;
            TickChase(*enemy, *target, DeltaSeconds);
        } else if (Perception->HasLastKnown()) {
            TickInvestigate(*enemy, DeltaSeconds);
        } else {
            TickWander(*enemy, DeltaSeconds);
        }
    }

    // ---------------------------------------------------------------------------------------------
    // Spawner
    // ---------------------------------------------------------------------------------------------

    ALeonHorrorEnemySpawn::ALeonHorrorEnemySpawn(entt::entity InHandle, UWorld* InWorld, const std::string& InName)
        : AActor(InHandle, InWorld, InName) {
        SetClass("ALeonHorrorEnemySpawn");
        SetCanEverTick(false);
    }

    void ALeonHorrorEnemySpawn::OnPropertiesLoaded() {
        WanderRadius = GetPropertyFloat("WanderRadius", WanderRadius);
        bEnabled = GetPropertyBool("Enabled", bEnabled);
    }

    void ALeonHorrorEnemySpawn::BeginPlay() {
        SpawnEnemy();
    }

    ALeonHorrorEnemy* ALeonHorrorEnemySpawn::SpawnEnemy() {
        if (!World || !bEnabled || Enemy.IsValid())
            return Enemy.Get();
        const std::string guid = GetActorGuid().ToString();
        auto* gameMode = dynamic_cast<ALeonHorrorGameMode*>(World->GetGameMode());
        if (gameMode && gameMode->GetDeadEnemies().count(guid) > 0)
            return nullptr; // killed before the save that was loaded

        auto* enemy = World->SpawnActor<ALeonHorrorEnemy>(GetName() + "_Enemy");
        if (!enemy)
            return nullptr;
        enemy->SetSpawnerGuid(guid);
        enemy->Tuning.WanderRadius = WanderRadius;
        glm::vec3 location = GetActorLocation();
        location.y += enemy->GetCapsuleHalfHeight();
        enemy->SetFloorZ(GetActorLocation().y);
        enemy->SetActorLocation(location);

        auto* controller = World->SpawnActor<ALeonHorrorEnemyController>(GetName() + "_Controller");
        if (controller) {
            World->AddAIController(controller);
            controller->Possess(enemy);
            controller->SetHome(location);
        }
        Enemy = enemy;
        return enemy;
    }

} // namespace Leon
