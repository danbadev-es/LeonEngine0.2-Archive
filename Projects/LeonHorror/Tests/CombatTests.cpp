#include <doctest/doctest.h>

#include "LeonHorrorTestSetup.hpp"

#include "LeonHorror/ALeonHorrorCharacter.hpp"
#include "LeonHorror/ALeonHorrorInteractable.hpp"
#include "LeonHorror/ULeonHorrorCombatComponent.hpp"
#include "LeonHorror/ULeonHorrorInventoryComponent.hpp"

#include "AI/UAIPerceptionComponent.hpp"
#include "Gameplay/AAIController.hpp"
#include "Gameplay/ACharacter.hpp"
#include "Gameplay/UHealthComponent.hpp"

namespace Leon {

    namespace {

        struct FCombatFixture : Test::FHorrorWorld {
            ALeonHorrorCharacter* Player = nullptr;
            ULeonHorrorCombatComponent* Combat = nullptr;
            ULeonHorrorInventoryComponent* Inventory = nullptr;

            FCombatFixture() {
                Player = GM->GetPlayerCharacter();
                REQUIRE(Player);
                Combat = Player->GetCombat();
                Inventory = Player->GetInventory();
                REQUIRE(Combat);
                REQUIRE(Inventory);
                Player->SetActorLocation({0.0f, Player->GetCapsuleHalfHeight(), 0.0f});
                Player->SetControlRotation({0.0f, -90.0f, 0.0f}); // looking down -Z
                Player->SetInjectedInput(FLeonHorrorInputState{});
                TickSeconds(0.05f);
            }

            /** A standing target with 100 health, InDistance metres ahead and InSide metres to the right. */
            ACharacter* SpawnTarget(const std::string& InName, float InDistance, float InSide = 0.0f) {
                auto* target = World->SpawnActor<ACharacter>(InName);
                target->SetActorLocation({InSide, target->GetCapsuleHalfHeight(), -InDistance});
                auto health = target->AddActorComponent<UHealthComponent>("Health");
                health->SetMaxHealth(100.0f);
                health->SetHealth(100.0f);
                TickSeconds(0.05f);
                return target;
            }

            static float HealthOf(AActor* InActor) {
                return InActor->FindComponentByClass<UHealthComponent>()->GetHealth();
            }

            void Hold(const FLeonHorrorInputState& InState, float InSeconds) {
                Player->SetInjectedInput(InState);
                TickSeconds(InSeconds);
                Player->SetInjectedInput(FLeonHorrorInputState{});
            }
        };

        FLeonHorrorInputState Aim() {
            FLeonHorrorInputState state;
            state.bAimHeld = true;
            return state;
        }

    } // namespace

    TEST_SUITE("LeonHorror combat") {

        TEST_CASE("weapons must be carried to be equipped; the first one found is drawn") {
            FCombatFixture f;
            CHECK(f.Combat->GetEquipped() == ELeonHorrorWeapon::None);
            CHECK_FALSE(f.Combat->Equip(ELeonHorrorWeapon::Pipe));
            CHECK_FALSE(f.Combat->StartAttack(false));

            f.Inventory->AddItem(LeonHorrorItems::Pipe);
            CHECK(f.Combat->GetEquipped() == ELeonHorrorWeapon::Pipe);
            f.Inventory->AddItem(LeonHorrorItems::Pistol);
            CHECK(f.Combat->GetEquipped() == ELeonHorrorWeapon::Pipe); // keeps what is in hand
            CHECK(f.Combat->Equip(ELeonHorrorWeapon::Pistol));
            CHECK(f.Combat->GetEquipped() == ELeonHorrorWeapon::Pistol);
        }

        TEST_CASE("number keys switch weapons through the input state") {
            FCombatFixture f;
            f.Inventory->AddItem(LeonHorrorItems::Pipe);
            f.Inventory->AddItem(LeonHorrorItems::Pistol);
            FLeonHorrorInputState state;
            state.bSelectPistolPressed = true;
            f.Player->ApplyInputState(state, 1.0f / 60.0f);
            CHECK(f.Combat->GetEquipped() == ELeonHorrorWeapon::Pistol);
            state = {};
            state.bSelectMeleePressed = true;
            f.Player->ApplyInputState(state, 1.0f / 60.0f);
            CHECK(f.Combat->GetEquipped() == ELeonHorrorWeapon::Pipe);
        }

        TEST_CASE("the pipe hits after its wind-up, only in front and only within reach") {
            FCombatFixture f;
            f.Inventory->AddItem(LeonHorrorItems::Pipe);
            ACharacter* inFront = f.SpawnTarget("InFront", 1.4f);
            ACharacter* behind = f.SpawnTarget("Behind", -1.4f);
            ACharacter* tooFar = f.SpawnTarget("TooFar", 4.0f);

            REQUIRE(f.Combat->StartAttack(false));
            CHECK(f.Combat->IsSwinging());
            // Nothing lands at the moment of the press.
            f.TickSeconds(f.Combat->Tuning.MeleeWindup * 0.5f);
            CHECK(f.HealthOf(inFront) == doctest::Approx(100.0f));

            f.TickSeconds(f.Combat->Tuning.MeleeWindup);
            CHECK(f.HealthOf(inFront) == doctest::Approx(100.0f - f.Combat->Tuning.MeleeDamage));
            CHECK(f.HealthOf(behind) == doctest::Approx(100.0f));
            CHECK(f.HealthOf(tooFar) == doctest::Approx(100.0f));
            CHECK(f.Combat->GetMeleeHits() == 1);
        }

        TEST_CASE("the pipe cannot be swung again until the cooldown has passed") {
            FCombatFixture f;
            f.Inventory->AddItem(LeonHorrorItems::Pipe);
            ACharacter* target = f.SpawnTarget("Target", 1.4f);
            REQUIRE(f.Combat->StartAttack(false));
            f.TickSeconds(f.Combat->Tuning.MeleeWindup + 0.05f);
            CHECK_FALSE(f.Combat->StartAttack(false));
            f.TickSeconds(f.Combat->Tuning.MeleeCooldown);
            CHECK(f.Combat->StartAttack(false));
            f.TickSeconds(f.Combat->Tuning.MeleeWindup + 0.05f);
            CHECK(f.HealthOf(target) == doctest::Approx(100.0f - 2.0f * f.Combat->Tuning.MeleeDamage));
        }

        TEST_CASE("the pipe does not hit through a closed door") {
            FCombatFixture f;
            f.Inventory->AddItem(LeonHorrorItems::Pipe);
            auto* door = f.World->SpawnActor<ALeonHorrorDoor>("Door");
            door->SetDoorSize({3.0f, 3.0f, 0.2f});
            door->SetClosedLocation({0.0f, 1.5f, -0.8f});
            ACharacter* target = f.SpawnTarget("Behind", 1.6f);

            REQUIRE(f.Combat->StartAttack(false));
            f.TickSeconds(f.Combat->Tuning.MeleeWindup + 0.1f);
            CHECK(f.HealthOf(target) == doctest::Approx(100.0f));
        }

        TEST_CASE("the handgun fires only while aiming, spends a round and damages what it hits") {
            FCombatFixture f;
            f.Inventory->AddItem(LeonHorrorItems::Pistol);
            f.Combat->SetLoadedRounds(3);
            ACharacter* target = f.SpawnTarget("Target", 6.0f);

            // Not aiming: nothing happens, no round is spent.
            CHECK_FALSE(f.Combat->StartAttack(false));
            CHECK(f.Combat->GetLoadedRounds() == 3);

            FLeonHorrorInputState fire = Aim();
            fire.bAttackPressed = true;
            f.Player->ApplyInputState(fire, 1.0f / 60.0f);
            CHECK(f.Combat->GetShotsFired() == 1);
            CHECK(f.Combat->GetLoadedRounds() == 2);
            CHECK(f.Combat->GetLastHitActor() == target);
            CHECK(f.HealthOf(target) == doctest::Approx(100.0f - f.Combat->Tuning.PistolDamage));

            // Rate of fire: an immediate second press is ignored.
            f.Player->ApplyInputState(fire, 1.0f / 60.0f);
            CHECK(f.Combat->GetShotsFired() == 1);
        }

        TEST_CASE("a shot that misses still costs the round") {
            FCombatFixture f;
            f.Inventory->AddItem(LeonHorrorItems::Pistol);
            f.Combat->SetLoadedRounds(2);
            ACharacter* aside = f.SpawnTarget("Aside", 6.0f, 3.0f);
            REQUIRE(f.Combat->StartAttack(true));
            CHECK(f.Combat->GetLoadedRounds() == 1);
            CHECK(f.Combat->GetLastHitActor() == nullptr);
            CHECK(f.HealthOf(aside) == doctest::Approx(100.0f));
        }

        TEST_CASE("an empty handgun clicks instead of firing and never reloads by itself") {
            FCombatFixture f;
            f.Inventory->AddItem(LeonHorrorItems::Pistol);
            f.Inventory->AddItem(LeonHorrorItems::PistolAmmo, 5);
            f.Combat->SetLoadedRounds(0);

            CHECK_FALSE(f.Combat->StartAttack(true));
            CHECK(f.Combat->GetDryFires() == 1);
            CHECK(f.Combat->GetShotsFired() == 0);
            f.TickSeconds(3.0f);
            CHECK(f.Combat->GetLoadedRounds() == 0);
            CHECK(f.Inventory->GetCount(LeonHorrorItems::PistolAmmo) == 5);
        }

        TEST_CASE("reloading takes time and moves only the spare rounds that exist") {
            FCombatFixture f;
            f.Inventory->AddItem(LeonHorrorItems::Pistol);
            f.Combat->SetLoadedRounds(1);

            // No spare rounds: nothing to do.
            CHECK_FALSE(f.Combat->StartReload());

            f.Inventory->AddItem(LeonHorrorItems::PistolAmmo, 3);
            REQUIRE(f.Combat->StartReload());
            CHECK(f.Combat->IsReloading());
            CHECK_FALSE(f.Combat->StartAttack(true)); // busy
            f.TickSeconds(f.Combat->Tuning.PistolReloadSeconds * 0.5f);
            CHECK(f.Combat->GetLoadedRounds() == 1);

            f.TickSeconds(f.Combat->Tuning.PistolReloadSeconds);
            CHECK_FALSE(f.Combat->IsReloading());
            CHECK(f.Combat->GetLoadedRounds() == 4); // 1 + the 3 that were spare, not a full clip
            CHECK(f.Inventory->GetCount(LeonHorrorItems::PistolAmmo) == 0);
        }

        TEST_CASE("a full reload leaves the remainder in the inventory, and a full gun refuses to reload") {
            FCombatFixture f;
            f.Inventory->AddItem(LeonHorrorItems::Pistol);
            f.Inventory->AddItem(LeonHorrorItems::PistolAmmo, 20);
            f.Combat->SetLoadedRounds(2);
            REQUIRE(f.Combat->StartReload());
            f.TickSeconds(f.Combat->Tuning.PistolReloadSeconds + 0.1f);
            CHECK(f.Combat->GetLoadedRounds() == f.Combat->Tuning.PistolClipSize);
            CHECK(f.Inventory->GetCount(LeonHorrorItems::PistolAmmo) == 20 - (f.Combat->Tuning.PistolClipSize - 2));
            CHECK_FALSE(f.Combat->StartReload());
        }

        TEST_CASE("a gunshot is heard by nearby enemies; the pipe is much quieter") {
            FCombatFixture f;
            f.Inventory->AddItem(LeonHorrorItems::Pipe);
            f.Inventory->AddItem(LeonHorrorItems::Pistol);

            auto spawnListener = [&](const char* InName, const glm::vec3& InLocation) {
                auto* pawn = f.World->SpawnActor<ACharacter>(InName);
                auto* controller = f.World->SpawnActor<AAIController>(std::string(InName) + "AI");
                pawn->SetActorLocation(InLocation);
                f.World->AddAIController(controller);
                controller->Possess(pawn);
                auto perception = controller->GetPerceptionComponent();
                // Blind listeners: this test is about hearing, and the player is close enough to be seen.
                perception->SetSensePredicate([](AActor*) { return false; });
                return perception;
            };
            // Behind the player and out of the line of fire.
            auto nearby = spawnListener("Nearby", {4.0f, 0.9f, 12.0f});
            auto distant = spawnListener("Distant", {4.0f, 0.9f, 80.0f});

            REQUIRE(f.Combat->StartAttack(false));
            f.TickSeconds(f.Combat->Tuning.MeleeWindup + 0.1f);
            CHECK_FALSE(nearby->HasLastKnown()); // 12 m away: a swing is not heard

            f.Combat->Equip(ELeonHorrorWeapon::Pistol);
            f.Combat->SetLoadedRounds(1);
            f.TickSeconds(1.0f);
            REQUIRE(f.Combat->StartAttack(true));
            CHECK(nearby->HasLastKnown());
            CHECK(nearby->GetLastKnownLocation().z == doctest::Approx(f.Player->GetActorLocation().z).epsilon(0.05));
            CHECK_FALSE(distant->HasLastKnown());
        }

        TEST_CASE("a dead player cannot attack") {
            FCombatFixture f;
            f.Inventory->AddItem(LeonHorrorItems::Pipe);
            f.Player->GetHealth()->ApplyDamage(500.0f);
            CHECK_FALSE(f.Combat->StartAttack(false));
        }
    }

} // namespace Leon
