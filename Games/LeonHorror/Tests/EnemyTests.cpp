#include <doctest/doctest.h>

#include "LeonHorrorTestSetup.hpp"

#include "LeonHorror/ALeonHorrorCharacter.hpp"
#include "LeonHorror/ALeonHorrorEnemy.hpp"
#include "LeonHorror/ALeonHorrorInteractable.hpp"
#include "LeonHorror/ULeonHorrorCombatComponent.hpp"
#include "LeonHorror/ULeonHorrorInventoryComponent.hpp"

#include "AI/UAIPerceptionComponent.hpp"
#include "Gameplay/UCharacterMovementComponent.hpp"
#include "Engine/FMapSerializer.hpp"

namespace Leon {

    namespace {

        struct FEnemyFixture : Test::FHorrorWorld {
            ALeonHorrorCharacter* Player = nullptr;
            ALeonHorrorEnemySpawn* Spawn = nullptr;
            ALeonHorrorEnemy* Enemy = nullptr;
            ALeonHorrorEnemyController* Brain = nullptr;

            /** Player at the origin looking down -Z; the enemy InDistance metres ahead, facing InFacing. */
            explicit FEnemyFixture(float InDistance, const glm::vec3& InFacing = {0.0f, 0.0f, 1.0f}) {
                Player = GM->GetPlayerCharacter();
                REQUIRE(Player);
                Player->SetActorLocation({0.0f, Player->GetCapsuleHalfHeight(), 0.0f});
                Player->SetControlRotation({0.0f, -90.0f, 0.0f});
                Player->SetInjectedInput(FLeonHorrorInputState{});

                Spawn = World->SpawnActor<ALeonHorrorEnemySpawn>("Spawn");
                Spawn->SetActorLocation({0.0f, 0.0f, -InDistance});
                Enemy = Spawn->GetEnemy();
                if (!Enemy)
                    Enemy = Spawn->SpawnEnemy();
                REQUIRE(Enemy);
                // The spawner is placed after spawning in this fixture: move the enemy to it.
                Enemy->SetActorLocation({0.0f, Enemy->GetCapsuleHalfHeight(), -InDistance});
                Brain = dynamic_cast<ALeonHorrorEnemyController*>(Enemy->GetController());
                REQUIRE(Brain);
                Brain->SetHome(Enemy->GetActorLocation());
                Enemy->FaceDirection(InFacing);
            }

            float Distance() const {
                glm::vec3 d = Enemy->GetActorLocation() - Player->GetActorLocation();
                d.y = 0.0f;
                return glm::length(d);
            }
        };

    } // namespace

    TEST_SUITE("LeonHorror enemy") {

        TEST_CASE("a spawner creates one enemy with a controller and full health") {
            FEnemyFixture f(10.0f);
            CHECK(f.Enemy->GetClass() == "ALeonHorrorEnemy");
            CHECK(f.Enemy->GetHealth()->GetHealth() == doctest::Approx(f.Enemy->Tuning.Health));
            CHECK(f.Enemy->GetSpawnerGuid() == f.Spawn->GetActorGuid().ToString());
            CHECK(f.Spawn->SpawnEnemy() == f.Enemy); // asking again does not create a second one
            CHECK(f.World->GetAIControllers().size() == 1);
        }

        TEST_CASE("its speeds sit between the player's walk and sprint") {
            FEnemyFixture f(10.0f);
            CHECK(f.Enemy->Tuning.ChaseSpeed > f.Player->Tuning.WalkSpeed);
            CHECK(f.Enemy->Tuning.ChaseSpeed < f.Player->Tuning.SprintSpeed);
            CHECK(f.Enemy->Tuning.WanderSpeed < f.Player->Tuning.WalkSpeed);
        }

        TEST_CASE("it chases a player it can see") {
            FEnemyFixture f(9.0f); // facing +Z, toward the player
            const float before = f.Distance();
            f.TickSeconds(2.0f);
            CHECK(f.Brain->GetState() == ELeonHorrorEnemyState::Chase);
            CHECK(f.Distance() < before - 3.0f);
        }

        TEST_CASE("it does not notice a player behind its back at a distance") {
            FEnemyFixture f(9.0f, {0.0f, 0.0f, -1.0f}); // facing away
            // Keep it from turning round by itself: no wander movement.
            f.Enemy->Tuning.WanderRadius = 0.0f;
            f.TickSeconds(1.0f);
            CHECK(f.Brain->GetState() == ELeonHorrorEnemyState::Wander);
            CHECK(f.Brain->GetPerceptionComponent()->GetCurrentTarget() == nullptr);
        }

        TEST_CASE("a closed door hides the player; a gunshot draws it to where the shot was fired") {
            FEnemyFixture f(9.0f);
            auto* door = f.World->SpawnActor<ALeonHorrorDoor>("Door");
            door->SetDoorSize({6.0f, 4.0f, 0.2f});
            door->SetClosedLocation({0.0f, 2.0f, -4.5f});
            f.Enemy->Tuning.WanderRadius = 0.0f;
            f.TickSeconds(1.0f);
            CHECK(f.Brain->GetPerceptionComponent()->GetCurrentTarget() == nullptr);
            CHECK(f.Brain->GetState() == ELeonHorrorEnemyState::Wander);

            // The player fires: the enemy has not seen anything but heads for the sound.
            f.Player->GetInventory()->AddItem(LeonHorrorItems::Pistol);
            f.Player->GetCombat()->SetLoadedRounds(1);
            REQUIRE(f.Player->GetCombat()->StartAttack(true));
            f.TickSeconds(0.3f);
            CHECK(f.Brain->GetState() == ELeonHorrorEnemyState::Investigate);
            CHECK(f.Brain->GetPerceptionComponent()->GetCurrentTarget() == nullptr);
        }

        TEST_CASE("it investigates a noise, then gives up and wanders again") {
            FEnemyFixture f(30.0f, {0.0f, 0.0f, -1.0f}); // far away, facing away: cannot see the player
            const glm::vec3 noise = f.Enemy->GetActorLocation() + glm::vec3(5.0f, 0.0f, 0.0f);
            REQUIRE(f.Brain->GetPerceptionComponent()->ReportNoise(noise, 20.0f));
            f.TickSeconds(0.2f);
            CHECK(f.Brain->GetState() == ELeonHorrorEnemyState::Investigate);

            // Five metres at investigate speed takes under three seconds; then it pauses on the spot.
            f.TickSeconds(3.3f);
            glm::vec3 toNoise = f.Enemy->GetActorLocation() - noise;
            toNoise.y = 0.0f;
            CHECK(glm::length(toNoise) < 1.6f); // it went there
            f.TickSeconds(0.5f);
            CHECK(f.Brain->GetState() == ELeonHorrorEnemyState::Wander);
            CHECK_FALSE(f.Brain->GetPerceptionComponent()->HasLastKnown());
        }

        TEST_CASE("in range it winds up and then hurts the player, with a cooldown between blows") {
            FEnemyFixture f(1.2f);
            const float full = f.Player->GetHealth()->GetHealth();
            f.TickSeconds(0.3f); // perception runs on an interval: give it one full update
            CHECK(f.Brain->GetState() == ELeonHorrorEnemyState::Attack);
            CHECK(f.Brain->IsAttackWindingUp());
            CHECK(f.Player->GetHealth()->GetHealth() == doctest::Approx(full)); // not yet

            f.TickSeconds(f.Enemy->Tuning.AttackWindup + 0.1f);
            CHECK(f.Player->GetHealth()->GetHealth() == doctest::Approx(full - f.Enemy->Tuning.AttackDamage));

            // No second blow until the cooldown is over.
            f.TickSeconds(0.5f);
            CHECK(f.Player->GetHealth()->GetHealth() == doctest::Approx(full - f.Enemy->Tuning.AttackDamage));
            f.TickSeconds(f.Enemy->Tuning.AttackCooldown + f.Enemy->Tuning.AttackWindup);
            CHECK(f.Player->GetHealth()->GetHealth() == doctest::Approx(full - 2.0f * f.Enemy->Tuning.AttackDamage));
        }

        TEST_CASE("stepping back during the wind-up avoids the blow") {
            FEnemyFixture f(1.2f);
            const float full = f.Player->GetHealth()->GetHealth();
            f.TickSeconds(0.3f); // perception runs on an interval: give it one full update
            REQUIRE(f.Brain->IsAttackWindingUp());
            f.Player->SetActorLocation({0.0f, f.Player->GetCapsuleHalfHeight(), 6.0f});
            f.TickSeconds(f.Enemy->Tuning.AttackWindup + 0.1f);
            CHECK(f.Player->GetHealth()->GetHealth() == doctest::Approx(full));
        }

        TEST_CASE("a hit staggers it and cancels a lunge in progress") {
            FEnemyFixture f(1.2f);
            const float full = f.Player->GetHealth()->GetHealth();
            f.TickSeconds(0.3f); // perception runs on an interval: give it one full update
            REQUIRE(f.Brain->IsAttackWindingUp());

            f.Enemy->GetHealth()->ApplyDamage(10.0f);
            f.TickSeconds(0.1f);
            CHECK(f.Brain->GetState() == ELeonHorrorEnemyState::Staggered);
            CHECK_FALSE(f.Brain->IsAttackWindingUp());
            f.TickSeconds(f.Enemy->Tuning.AttackWindup);
            CHECK(f.Player->GetHealth()->GetHealth() == doctest::Approx(full));

            // It recovers and comes again.
            f.TickSeconds(f.Enemy->Tuning.StaggerSeconds + f.Enemy->Tuning.AttackCooldown +
                          f.Enemy->Tuning.AttackWindup + 0.3f);
            CHECK(f.Player->GetHealth()->GetHealth() < full);
        }

        TEST_CASE("it never ends up standing inside the player") {
            FEnemyFixture f(4.0f);
            f.TickSeconds(4.0f);
            CHECK(f.Distance() >= f.Enemy->GetCapsuleRadius() + f.Player->GetCapsuleRadius() - 0.05f);
        }

        TEST_CASE("the pipe kills it in three blows and the death is recorded by spawner GUID") {
            FEnemyFixture f(1.4f, {0.0f, 0.0f, -1.0f});
            f.Enemy->Tuning.AttackDamage = 0.0f; // this test is about the player's side of the fight
            f.Player->GetInventory()->AddItem(LeonHorrorItems::Pipe);
            auto* combat = f.Player->GetCombat();
            for (int blow = 0; blow < 3; ++blow) {
                CHECK_FALSE(f.Enemy->IsDead());
                // Keep it in reach: it may have shuffled during the cooldown.
                f.Enemy->SetActorLocation({0.0f, f.Enemy->GetCapsuleHalfHeight(), -1.4f});
                REQUIRE(combat->StartAttack(false));
                f.TickSeconds(combat->Tuning.MeleeWindup + 0.05f);
                f.TickSeconds(combat->Tuning.MeleeCooldown);
            }
            CHECK(f.Enemy->IsDead());
            CHECK(f.Brain->GetState() == ELeonHorrorEnemyState::Dead);
            CHECK(f.GM->GetDeadEnemies().count(f.Spawn->GetActorGuid().ToString()) == 1);

            // A dead enemy stays down and does no harm.
            const float health = f.Player->GetHealth()->GetHealth();
            const glm::vec3 where = f.Enemy->GetActorLocation();
            f.TickSeconds(3.0f);
            CHECK(f.Player->GetHealth()->GetHealth() == doctest::Approx(health));
            CHECK(glm::length(f.Enemy->GetActorLocation() - where) < 0.05f);
        }

        TEST_CASE("a spawner whose enemy was killed in the loaded save does not spawn it again") {
            // Spawners act on BeginPlay, so set the world up first and begin play afterwards.
            Test::FHorrorWorld f(false);
            auto* killed = f.World->SpawnActor<ALeonHorrorEnemySpawn>("Killed");
            auto* disabled = f.World->SpawnActor<ALeonHorrorEnemySpawn>("Disabled");
            auto* alive = f.World->SpawnActor<ALeonHorrorEnemySpawn>("Alive");
            disabled->SetEnabled(false);
            f.GM->MarkEnemyDead(killed->GetActorGuid().ToString());

            f.World->BeginPlay();

            CHECK(killed->GetEnemy() == nullptr);
            CHECK(disabled->GetEnemy() == nullptr);
            CHECK(alive->GetEnemy() != nullptr);
            CHECK(f.World->GetAIControllers().size() == 1);
        }

        TEST_CASE("the enemy itself is never written into a map; its spawner is") {
            FEnemyFixture f(10.0f);
            std::string yaml;
            REQUIRE(FMapSerializer(f.World).SerializeText(yaml));
            CHECK(yaml.find("ALeonHorrorEnemySpawn") != std::string::npos);
            CHECK(yaml.find("\"ALeonHorrorEnemy\"") == std::string::npos);
            CHECK(yaml.find("ALeonHorrorEnemyController") == std::string::npos);
        }
    }

} // namespace Leon
