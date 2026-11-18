#include <doctest/doctest.h>

#include "LeonHorrorTestSetup.hpp"

#include "LeonHorror/ALeonHorrorCharacter.hpp"
#include "LeonHorror/ALeonHorrorEnemy.hpp"
#include "LeonHorror/ALeonHorrorInteractable.hpp"
#include "LeonHorror/FLeonHorrorSaveGame.hpp"
#include "LeonHorror/ULeonHorrorCombatComponent.hpp"
#include "LeonHorror/ULeonHorrorInventoryComponent.hpp"

#include <filesystem>
#include <fstream>

namespace Leon {

    namespace {

        const char* kPickupGuid = "00000000000000a100000000000000a1";
        const char* kKeepGuid = "00000000000000a200000000000000a2";
        const char* kDoorGuid = "00000000000000d100000000000000d1";
        const char* kSpawnGuid = "00000000000000e100000000000000e1";
        const char* kOtherSpawnGuid = "00000000000000e200000000000000e2";

        /** The same little level every time, as a map reload would produce it. */
        struct FLevel : Test::FHorrorWorld {
            ALeonHorrorCharacter* Player = nullptr;
            ALeonHorrorPickup* Pickup = nullptr;
            ALeonHorrorPickup* Keep = nullptr;
            ALeonHorrorDoor* Door = nullptr;
            ALeonHorrorEnemySpawn* Spawn = nullptr;
            ALeonHorrorEnemySpawn* OtherSpawn = nullptr;

            FLevel() : Test::FHorrorWorld(false) {
                Pickup = World->SpawnActor<ALeonHorrorPickup>("Ammo");
                Pickup->SetActorGuid(FUUID::FromString(kPickupGuid));
                Pickup->SetItem(LeonHorrorItems::PistolAmmo, 6);
                Pickup->SetActorLocation({20.0f, 1.0f, 0.0f});

                Keep = World->SpawnActor<ALeonHorrorPickup>("Medkit");
                Keep->SetActorGuid(FUUID::FromString(kKeepGuid));
                Keep->SetItem(LeonHorrorItems::Medkit, 1);
                Keep->SetActorLocation({22.0f, 1.0f, 0.0f});

                Door = World->SpawnActor<ALeonHorrorDoor>("Door");
                Door->SetActorGuid(FUUID::FromString(kDoorGuid));
                Door->SetRequiredKey("Key.Red");
                Door->SetClosedLocation({30.0f, 1.1f, 0.0f});

                Spawn = World->SpawnActor<ALeonHorrorEnemySpawn>("Spawn");
                Spawn->SetActorGuid(FUUID::FromString(kSpawnGuid));
                Spawn->SetActorLocation({0.0f, 0.0f, -40.0f});
                OtherSpawn = World->SpawnActor<ALeonHorrorEnemySpawn>("OtherSpawn");
                OtherSpawn->SetActorGuid(FUUID::FromString(kOtherSpawnGuid));
                OtherSpawn->SetActorLocation({0.0f, 0.0f, -60.0f});

                World->BeginPlay();
                Player = GM->GetPlayerCharacter();
                REQUIRE(Player);
                Player->SetInjectedInput(FLeonHorrorInputState{});
            }
        };

        std::string TempSavePath(const char* InName) {
            std::error_code ec;
            std::filesystem::create_directories("build/LeonHorrorSaves", ec);
            return std::string("build/LeonHorrorSaves/") + InName;
        }

    } // namespace

    TEST_SUITE("LeonHorror save game") {

        TEST_CASE("a save survives a round trip through text exactly") {
            FLeonHorrorSaveGame save;
            save.MapName = "Slice";
            save.PlayerLocation = {1.2345678f, 0.9f, -17.000002f};
            save.PlayerYaw = 33.333332f;
            save.PlayerPitch = -12.5f;
            save.Health = 61.5f;
            save.Stamina = 12.25f;
            save.bFlashlightOn = false;
            save.EquippedWeapon = 2;
            save.LoadedRounds = 5;
            save.Items = {{"Key.Red", 1}, {"Ammo.Pistol", 11}, {"Odd item name", 2}};
            save.CollectedPickups = {kPickupGuid};
            save.Doors[kDoorGuid] = {false, true};
            save.DeadEnemies = {kSpawnGuid};
            save.ReadNotes = {"00000000000000b100000000000000b1"};

            FLeonHorrorSaveGame loaded;
            REQUIRE(FLeonHorrorSaveGame::FromText(save.ToText(), loaded));
            CHECK(loaded.MapName == "Slice");
            CHECK(loaded.PlayerLocation.x == save.PlayerLocation.x);
            CHECK(loaded.PlayerLocation.z == save.PlayerLocation.z);
            CHECK(loaded.PlayerYaw == save.PlayerYaw);
            CHECK(loaded.PlayerPitch == save.PlayerPitch);
            CHECK(loaded.Health == save.Health);
            CHECK(loaded.Stamina == save.Stamina);
            CHECK_FALSE(loaded.bFlashlightOn);
            CHECK(loaded.EquippedWeapon == 2);
            CHECK(loaded.LoadedRounds == 5);
            CHECK(loaded.Items == save.Items);
            CHECK(loaded.CollectedPickups == save.CollectedPickups);
            CHECK(loaded.Doors == save.Doors);
            CHECK(loaded.DeadEnemies == save.DeadEnemies);
            CHECK(loaded.ReadNotes == save.ReadNotes);
        }

        TEST_CASE("text that is not a readable save is rejected and leaves the output untouched") {
            FLeonHorrorSaveGame out;
            out.Health = 77.0f;
            CHECK_FALSE(FLeonHorrorSaveGame::FromText("", out));
            CHECK_FALSE(FLeonHorrorSaveGame::FromText("SomethingElse 1\n", out));
            CHECK_FALSE(FLeonHorrorSaveGame::FromText("LeonHorrorSave 99\n", out)); // from a newer build
            CHECK_FALSE(FLeonHorrorSaveGame::FromText("LeonHorrorSave 1\nPlayer 1 2\n", out));
            CHECK_FALSE(FLeonHorrorSaveGame::FromText("LeonHorrorSave 1\nItem zero Key.Red\n", out));
            CHECK(out.Health == doctest::Approx(77.0f));
            // Unknown records are tolerated.
            CHECK(FLeonHorrorSaveGame::FromText("LeonHorrorSave 1\nFutureThing 1 2 3\nHealth 40\n", out));
            CHECK(out.Health == doctest::Approx(40.0f));
        }

        TEST_CASE("capturing a save reflects what the player did") {
            FLevel level;
            level.Player->GetInventory()->AddItem("Key.Red");
            level.Player->GetInventory()->AddItem(LeonHorrorItems::Pistol);
            level.Player->GetCombat()->SetLoadedRounds(3);
            REQUIRE(level.Pickup->Interact(*level.Player));
            REQUIRE(level.Door->Interact(*level.Player));
            REQUIRE(level.Spawn->GetEnemy());
            level.Spawn->GetEnemy()->GetHealth()->ApplyDamage(500.0f);
            level.Player->GetHealth()->ApplyDamage(35.0f);
            level.Player->SetActorLocation({5.0f, 0.9f, -6.0f});
            level.Player->SetControlRotation({-10.0f, 45.0f, 0.0f});

            const FLeonHorrorSaveGame save = level.GM->CaptureSave(*level.Player);
            CHECK(save.MapName == level.World->GetName());
            CHECK(save.PlayerLocation.x == doctest::Approx(5.0f));
            CHECK(save.PlayerYaw == doctest::Approx(45.0f));
            CHECK(save.Health == doctest::Approx(65.0f));
            CHECK(save.EquippedWeapon == static_cast<int32_t>(ELeonHorrorWeapon::Pistol));
            CHECK(save.LoadedRounds == 3);
            CHECK(save.Items.at("Key.Red") == 1);
            CHECK(save.Items.at(LeonHorrorItems::PistolAmmo) == 6);
            CHECK(save.CollectedPickups.count(kPickupGuid) == 1);
            CHECK(save.CollectedPickups.count(kKeepGuid) == 0);
            REQUIRE(save.Doors.count(kDoorGuid) == 1);
            CHECK(save.Doors.at(kDoorGuid).bOpen);
            CHECK_FALSE(save.Doors.at(kDoorGuid).bLocked);
            CHECK(save.DeadEnemies.count(kSpawnGuid) == 1);
            CHECK(save.DeadEnemies.count(kOtherSpawnGuid) == 0);
        }

        TEST_CASE("applying a save to a freshly loaded level restores the world and the player") {
            FLeonHorrorSaveGame save;
            {
                FLevel played;
                played.Player->GetInventory()->AddItem("Key.Red");
                played.Player->GetInventory()->AddItem(LeonHorrorItems::Pistol);
                played.Player->GetCombat()->Equip(ELeonHorrorWeapon::Pistol);
                played.Player->GetCombat()->SetLoadedRounds(4);
                REQUIRE(played.Pickup->Interact(*played.Player));
                REQUIRE(played.Door->Interact(*played.Player));
                played.Spawn->GetEnemy()->GetHealth()->ApplyDamage(500.0f);
                played.Player->GetHealth()->ApplyDamage(40.0f);
                played.Player->SetActorLocation({7.0f, 0.9f, -3.0f});
                played.Player->SetControlRotation({5.0f, 120.0f, 0.0f});
                FLeonHorrorInputState off;
                off.bFlashlightPressed = true;
                played.Player->ApplyInputState(off, 1.0f / 60.0f);
                // Through text, as a real load would.
                REQUIRE(FLeonHorrorSaveGame::FromText(played.GM->CaptureSave(*played.Player).ToText(), save));
            }

            FLevel fresh; // the map as authored: everything back, door locked, both enemies alive
            REQUIRE(fresh.World->FindActorByGuid(FUUID::FromString(kPickupGuid)) != nullptr);
            REQUIRE(fresh.Door->IsLocked());
            REQUIRE(fresh.Spawn->GetEnemy() != nullptr);

            fresh.GM->ApplySave(save);
            fresh.TickSeconds(0.05f);

            CHECK(fresh.World->FindActorByGuid(FUUID::FromString(kPickupGuid)) == nullptr);
            CHECK(fresh.World->FindActorByGuid(FUUID::FromString(kKeepGuid)) != nullptr);
            CHECK_FALSE(fresh.Door->IsLocked());
            CHECK(fresh.Door->IsOpen());
            CHECK(fresh.Door->GetCollider()->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
            CHECK(fresh.Spawn->GetEnemy() == nullptr);
            CHECK(fresh.OtherSpawn->GetEnemy() != nullptr);
            CHECK(fresh.World->GetAIControllers().size() == 1);

            CHECK(fresh.Player->GetActorLocation().x == doctest::Approx(7.0f));
            CHECK(fresh.Player->GetActorLocation().z == doctest::Approx(-3.0f));
            CHECK(fresh.Player->GetControlYaw() == doctest::Approx(120.0f));
            CHECK(fresh.Player->GetHealth()->GetHealth() == doctest::Approx(60.0f));
            CHECK_FALSE(fresh.Player->IsFlashlightOn());
            CHECK(fresh.Player->GetInventory()->GetCount("Key.Red") == 1);
            CHECK(fresh.Player->GetInventory()->GetCount(LeonHorrorItems::PistolAmmo) == 6);
            CHECK(fresh.Player->GetCombat()->GetEquipped() == ELeonHorrorWeapon::Pistol);
            CHECK(fresh.Player->GetCombat()->GetLoadedRounds() == 4);

            // The restored state is what a later save would record again.
            const FLeonHorrorSaveGame again = fresh.GM->CaptureSave(*fresh.Player);
            CHECK(again.CollectedPickups == save.CollectedPickups);
            CHECK(again.Doors == save.Doors);
            CHECK(again.DeadEnemies == save.DeadEnemies);
            CHECK(again.Items == save.Items);
        }

        TEST_CASE("a save point writes the file, and the file loads back") {
            FLevel level;
            const std::string path = TempSavePath("slot_test.lsav");
            std::error_code ec;
            std::filesystem::remove(path, ec);
            level.GM->SetSaveFilePath(path);
            CHECK_FALSE(level.GM->HasSaveFile());

            level.Player->GetInventory()->AddItem(LeonHorrorItems::Medkit, 2);
            auto* savePoint = level.World->SpawnActor<ALeonHorrorSavePoint>("SavePoint");
            REQUIRE(savePoint->Interact(*level.Player));
            CHECK(level.GM->GetSaveCount() == 1);
            REQUIRE(level.GM->HasSaveFile());
            CHECK_FALSE(std::filesystem::exists(path + ".tmp"));

            FLeonHorrorSaveGame loaded;
            REQUIRE(FLeonHorrorSaveGame::LoadFromFile(path, loaded));
            CHECK(loaded.Items.at(LeonHorrorItems::Medkit) == 2);
            CHECK(loaded.MapName == level.World->GetName());

            // A dead player cannot save.
            level.Player->GetHealth()->ApplyDamage(500.0f);
            CHECK_FALSE(level.GM->RequestSave(*level.Player));
            CHECK(level.GM->GetSaveCount() == 1);
            std::filesystem::remove(path, ec);
        }

        TEST_CASE("a corrupt save file is refused") {
            const std::string path = TempSavePath("corrupt.lsav");
            {
                std::ofstream out(path);
                out << "LeonHorrorSave 1\nPlayer not numbers\n";
            }
            FLeonHorrorSaveGame loaded;
            CHECK_FALSE(FLeonHorrorSaveGame::LoadFromFile(path, loaded));
            CHECK_FALSE(FLeonHorrorSaveGame::LoadFromFile(TempSavePath("does_not_exist.lsav"), loaded));
            std::error_code ec;
            std::filesystem::remove(path, ec);
        }
    }

} // namespace Leon
