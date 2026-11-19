#include <doctest/doctest.h>

#include "LeonHorrorTestSetup.hpp"

#include "LeonHorror/ALeonHorrorCharacter.hpp"
#include "LeonHorror/ALeonHorrorEnemy.hpp"
#include "LeonHorror/ALeonHorrorInteractable.hpp"
#include "LeonHorror/FLeonHorrorSound.hpp"
#include "LeonHorror/ULeonHorrorCombatComponent.hpp"
#include "LeonHorror/ULeonHorrorInventoryComponent.hpp"

#include "Gameplay/UHealthComponent.hpp"

#include <filesystem>
#include <set>
#include <string>

namespace Leon {

    namespace {

        /** The Slice level with the game running and the counts of what has been heard at zero. */
        struct FSoundWorld : Test::FMapWorld {
            FSoundWorld() : FMapWorld("/Game/Maps/Slice", true) {
                // Nothing in these tests is about the enemy unless a test wakes it.
                LeonHorrorSound::ResetCounts();
            }

            ALeonHorrorEnemy* Enemy() const {
                auto* spawn = Find<ALeonHorrorEnemySpawn>("EnemySpawn_Corridor");
                return spawn ? spawn->GetEnemy() : nullptr;
            }
        };

    } // namespace

    TEST_SUITE("LeonHorror sound") {

        TEST_CASE("a variation is one of the set and never the same twice running") {
            std::set<std::string> heard;
            std::string previous;
            for (int i = 0; i < 200; ++i) {
                const std::string cue = LeonHorrorSound::Variation("SFX_Step_Tile", 6);
                CHECK(cue != previous);
                CHECK(cue.rfind("SFX_Step_Tile_0", 0) == 0);
                heard.insert(cue);
                previous = cue;
            }
            CHECK(heard.size() == 6);
            CHECK(LeonHorrorSound::Variation("SFX_Save", 1) == "SFX_Save_01");
        }

        TEST_CASE("walking is heard a stride at a time, and standing still is silent") {
            FSoundWorld f;
            f.Tick(1.0f);
            CHECK(LeonHorrorSound::CountPlayed("SFX_Step_") == 0);

            const glm::vec3 from = f.Player->GetActorLocation();
            FLeonHorrorInputState walk;
            walk.Move = {0.0f, 1.0f};
            f.Player->SetInjectedInput(walk);
            f.Tick(2.0f);
            f.Player->SetInjectedInput(FLeonHorrorInputState{});
            glm::vec3 walked = f.Player->GetActorLocation() - from;
            walked.y = 0.0f;
            const float distance = glm::length(walked);
            REQUIRE(distance > 1.5f);
            const int steps = LeonHorrorSound::CountPlayed("SFX_Step_");
            // One for about every 0.8 m.
            CHECK(steps >= static_cast<int>(distance / 0.8f) - 1);
            CHECK(steps <= static_cast<int>(distance / 0.8f) + 1);

            // Being put somewhere else is not a step.
            LeonHorrorSound::ResetCounts();
            f.Player->SetActorLocation(from);
            f.Tick(0.2f);
            CHECK(LeonHorrorSound::CountPlayed("SFX_Step_") == 0);
        }

        TEST_CASE("the floor under the feet chooses the step") {
            FSoundWorld f;
            const glm::vec3 feet = f.Player->GetActorLocation() - glm::vec3(0.0f, f.Player->GetCapsuleHalfHeight(), 0.0f);
            const std::string surface = LeonHorrorSound::StepSurface(*f.World, feet, f.Player);
            CHECK((surface == "SFX_Step_Tile" || surface == "SFX_Step_Vinyl"));
            // Over nothing at all it is still a sound that exists.
            CHECK(LeonHorrorSound::StepSurface(*f.World, {500.0f, 50.0f, 500.0f}, f.Player) == "SFX_Step_Vinyl");
        }

        TEST_CASE("what is picked up, read and switched is heard") {
            FSoundWorld f;
            REQUIRE(f.Use("Pickup_Pipe", 0.6f, 1.6f));
            CHECK(LeonHorrorSound::CountPlayed("SFX_Pickup_Item") == 1);
            REQUIRE(f.Use("Note_Start", -2.0f, 4.5f));
            CHECK(LeonHorrorSound::CountPlayed("SFX_Paper") == 1);
            f.GM->DismissNote();
            REQUIRE(f.Use("SavePoint", -2.0f, 3.6f));
            CHECK(LeonHorrorSound::CountPlayed("SFX_Save") == 1);

            auto* key = f.World->SpawnActor<ALeonHorrorPickup>("TestKey");
            key->SetItem("Key.Test", 1);
            REQUIRE(key->Interact(*f.Player));
            CHECK(LeonHorrorSound::CountPlayed("SFX_Pickup_Key") == 1);
            auto* rounds = f.World->SpawnActor<ALeonHorrorPickup>("TestRounds");
            rounds->SetItem(LeonHorrorItems::PistolAmmo, 6);
            REQUIRE(rounds->Interact(*f.Player));
            CHECK(LeonHorrorSound::CountPlayed("SFX_Pickup_Ammo") == 1);

            FLeonHorrorInputState press;
            press.bFlashlightPressed = true;
            f.Player->SetInjectedInput(press);
            f.Tick(1.0f / 60.0f);
            f.Player->SetInjectedInput(FLeonHorrorInputState{});
            CHECK(LeonHorrorSound::CountPlayed("SFX_Flashlight_Off") == 1);
            std::error_code ec;
            std::filesystem::remove(f.GM->GetSaveFilePath(), ec);
        }

        TEST_CASE("the pipe swings, and rings off a wall when it finds nothing alive") {
            FSoundWorld f;
            if (ALeonHorrorEnemy* enemy = f.Enemy())
                enemy->GetHealth()->ApplyDamage(10000.0f);
            f.Player->GetInventory()->AddItem(LeonHorrorItems::Pipe);
            REQUIRE(f.Player->GetCombat()->Equip(ELeonHorrorWeapon::Pipe));
            // Face the nearest wall of the start room from arm's length.
            auto* door = f.Find<ALeonHorrorDoor>("Door_Start");
            REQUIRE(door);
            f.StandAndLookAt(0.0f, 1.2f, door->GetInteractLocation());
            LeonHorrorSound::ResetCounts();
            FLeonHorrorInputState swing;
            swing.bAttackPressed = true;
            f.Player->SetInjectedInput(swing);
            f.Tick(1.0f / 60.0f);
            f.Player->SetInjectedInput(FLeonHorrorInputState{});
            f.Tick(1.0f);
            CHECK(LeonHorrorSound::CountPlayed("SFX_Pipe_Swing") == 1);
            CHECK(LeonHorrorSound::CountPlayed("SFX_Pipe_Hit_Wall") == 1);
            CHECK(LeonHorrorSound::CountPlayed("SFX_Pipe_Hit_Flesh") == 0);
        }

        TEST_CASE("being hurt is heard, a heart beats when little life is left, and dying has its sound") {
            FSoundWorld f;
            if (ALeonHorrorEnemy* enemy = f.Enemy())
                enemy->GetHealth()->ApplyDamage(10000.0f);
            auto* health = f.Player->FindComponentByClass<UHealthComponent>();
            REQUIRE(health);
            LeonHorrorSound::ResetCounts();

            health->ApplyDamage(10.0f);
            CHECK(LeonHorrorSound::CountPlayed("SFX_Player_Hurt") == 1);
            f.Tick(2.0f);
            CHECK(LeonHorrorSound::CountPlayed("SFX_Heartbeat") == 0);

            health->ApplyDamage(health->GetHealth() - 0.2f * health->GetMaxHealth());
            f.Tick(3.0f);
            CHECK(LeonHorrorSound::CountPlayed("SFX_Heartbeat") >= 2);
            CHECK(LeonHorrorSound::CountPlayed("SFX_Heartbeat") <= 4);

            health->ApplyDamage(10000.0f);
            CHECK(f.Player->IsDead());
            CHECK(LeonHorrorSound::CountPlayed("SFX_Player_Death") == 1);
            CHECK(LeonHorrorSound::CountPlayed("STG_Death") == 1);
            const int beats = LeonHorrorSound::CountPlayed("SFX_Heartbeat");
            f.Tick(2.0f);
            CHECK(LeonHorrorSound::CountPlayed("SFX_Heartbeat") == beats); // the dead have no pulse
        }

        TEST_CASE("the chase track comes in when an enemy hunts and gives way once it is over") {
            // An empty world: the player at the origin, an enemy six metres ahead, looking at the player.
            Test::FHorrorWorld f;
            ALeonHorrorCharacter* player = f.GM->GetPlayerCharacter();
            REQUIRE(player);
            player->SetActorLocation({0.0f, player->GetCapsuleHalfHeight(), 0.0f});
            player->SetControlRotation({0.0f, -90.0f, 0.0f});
            player->SetInjectedInput(FLeonHorrorInputState{});
            f.TickSeconds(0.2f);
            LeonHorrorSound::ResetCounts();
            CHECK_FALSE(f.GM->IsChaseMusicPlaying());

            auto* spawn = f.World->SpawnActor<ALeonHorrorEnemySpawn>("Spawn");
            spawn->SetActorLocation({0.0f, 0.0f, -6.0f});
            ALeonHorrorEnemy* enemy = spawn->GetEnemy() ? spawn->GetEnemy() : spawn->SpawnEnemy();
            REQUIRE(enemy);
            enemy->SetActorLocation({0.0f, enemy->GetCapsuleHalfHeight(), -6.0f});
            enemy->FaceDirection({0.0f, 0.0f, 1.0f});

            for (int i = 0; i < 300 && !f.GM->IsChaseMusicPlaying(); ++i)
                f.TickSeconds(1.0f / 60.0f);
            REQUIRE(f.GM->IsChaseMusicPlaying());
            CHECK(LeonHorrorSound::CountPlayed("STG_Enemy_Spotted") == 1);
            CHECK(LeonHorrorSound::CountPlayed("MUS_Chase") == 1);
            // It comes for the player and is heard coming.
            f.TickSeconds(1.0f);
            CHECK(LeonHorrorSound::CountPlayed("SFX_EnemyStep") >= 1);

            // With the hunter dead the track stays a few seconds, then goes; it does not start twice.
            enemy->GetHealth()->ApplyDamage(10000.0f);
            f.TickSeconds(2.0f);
            CHECK(f.GM->IsChaseMusicPlaying());
            f.TickSeconds(4.0f);
            CHECK_FALSE(f.GM->IsChaseMusicPlaying());
            CHECK(LeonHorrorSound::CountPlayed("MUS_Chase") == 1);
        }
    }

} // namespace Leon
