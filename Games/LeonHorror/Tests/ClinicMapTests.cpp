#include <doctest/doctest.h>

#include "LeonHorrorTestSetup.hpp"

#include "LeonHorror/ALeonHorrorCharacter.hpp"
#include "LeonHorror/ALeonHorrorEnemy.hpp"
#include "LeonHorror/ALeonHorrorInteractable.hpp"
#include "LeonHorror/ALeonHorrorPower.hpp"
#include "LeonHorror/ULeonHorrorInventoryComponent.hpp"

#include "AI/UNavigationSystem.hpp"
#include "Gameplay/APlayerStart.hpp"
#include "Gameplay/UHealthComponent.hpp"

#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>

namespace Leon {

    namespace {

        /**
         * The clinic, loaded into a headless world. Places are given as on the floor plan
         * (Scripts/clinic_layout.py): metres east and metres north of the south-west corner.
         */
        struct FClinicWorld : Test::FMapWorld {
            explicit FClinicWorld(bool bInPlay) : FMapWorld("/Game/Maps/Clinic", bInPlay) {}

            bool UseFrom(const std::string& InName, float InEast, float InNorth) { return Use(InName, InEast, -InNorth); }

            void KillEveryEnemy() {
                for (const auto& actor : World->GetAllActors()) {
                    auto* spawn = dynamic_cast<ALeonHorrorEnemySpawn*>(actor.get());
                    if (spawn && spawn->GetEnemy())
                        spawn->GetEnemy()->GetHealth()->ApplyDamage(10000.0f);
                }
            }
        };

        std::string ReadMapText() {
            std::ifstream file(Test::MapFile("/Game/Maps/Clinic"), std::ios::binary);
            std::stringstream text;
            text << file.rdbuf();
            return text.str();
        }

        /** Every value written as `InKey: "/Game/..."` in the map. */
        std::set<std::string> VirtualPathsAfter(const std::string& InText, const std::string& InKey) {
            std::set<std::string> paths;
            const std::string needle = InKey + ": \"/Game/";
            for (size_t at = InText.find(needle); at != std::string::npos; at = InText.find(needle, at + 1)) {
                const size_t begin = at + InKey.size() + 3;
                paths.insert(InText.substr(begin, InText.find('"', begin) - begin));
            }
            return paths;
        }

    } // namespace

    TEST_SUITE("LeonHorror clinic map") {

        TEST_CASE("the map is well formed and holds what the level needs") {
            FClinicWorld map(false);
            std::set<std::string> guids;
            std::map<std::string, int> spawns;
            int starts = 0;
            int savePoints = 0;
            int exits = 0;
            int navBounds = 0;
            int fuseBoxes = 0;
            int notes = 0;
            int doors = 0;
            for (const auto& actor : map.World->GetAllActors()) {
                REQUIRE(actor);
                CHECK_MESSAGE(guids.insert(actor->GetActorGuid().ToString()).second, "duplicate GUID on " << actor->GetName());
                starts += dynamic_cast<APlayerStart*>(actor.get()) ? 1 : 0;
                savePoints += dynamic_cast<ALeonHorrorSavePoint*>(actor.get()) ? 1 : 0;
                exits += dynamic_cast<ALeonHorrorExit*>(actor.get()) ? 1 : 0;
                fuseBoxes += dynamic_cast<ALeonHorrorFuseBox*>(actor.get()) ? 1 : 0;
                notes += dynamic_cast<ALeonHorrorNote*>(actor.get()) ? 1 : 0;
                doors += dynamic_cast<ALeonHorrorDoor*>(actor.get()) ? 1 : 0;
                navBounds += actor->GetClass() == "ANavMeshBoundsVolume" ? 1 : 0;
                if (const auto* spawn = dynamic_cast<ALeonHorrorEnemySpawn*>(actor.get()))
                    ++spawns[spawn->GetType()];
            }
            CHECK(guids.size() > 300);
            CHECK(starts == 1);
            CHECK(savePoints == 2);
            CHECK(exits == 1);
            CHECK(navBounds == 1);
            CHECK(fuseBoxes == 1);
            CHECK(notes >= 5);
            CHECK(doors >= 19); // fifteen doorways, four of them with two leaves
            // Two kinds of enemy, and nothing the spawner would not know how to make.
            CHECK(spawns["Patient"] >= 3);
            CHECK(spawns["Orderly"] >= 2);
            CHECK(spawns.size() == 2);
        }

        TEST_CASE("everything the map names is in the project") {
            const std::string text = ReadMapText();
            REQUIRE_FALSE(text.empty());
            size_t checked = 0;
            for (const char* key : {"Asset", "LightmapAsset"}) {
                for (const std::string& path : VirtualPathsAfter(text, key)) {
                    CHECK_MESSAGE(std::filesystem::exists(FProjectPaths::ResolveVirtualPath(path)), path);
                    ++checked;
                }
            }
            for (const std::string& path : VirtualPathsAfter(text, "Sound")) {
                // "/Game/Audio/Name": the sound loader adds the extension.
                const std::string file = FProjectPaths::ProjectContentDir() + path.substr(5) + ".wav";
                CHECK_MESSAGE(std::filesystem::exists(file), file);
                ++checked;
            }
            CHECK(checked > 60);
        }

        TEST_CASE("every lock has its key, the board has its fuse, and both weapons can be found") {
            FClinicWorld map(false);
            std::map<std::string, int> items;
            std::set<std::string> locks;
            std::set<std::string> boards;
            std::set<std::string> electricDoors;
            for (const auto& actor : map.World->GetAllActors()) {
                if (const auto* pickup = dynamic_cast<ALeonHorrorPickup*>(actor.get())) {
                    CHECK_MESSAGE(!pickup->GetItemId().empty(), pickup->GetName());
                    items[pickup->GetItemId()] += pickup->GetItemCount();
                } else if (const auto* door = dynamic_cast<ALeonHorrorDoor*>(actor.get())) {
                    if (!door->GetRequiredKey().empty()) {
                        CHECK(door->IsLocked());
                        locks.insert(door->GetRequiredKey());
                    }
                    if (door->NeedsPower())
                        electricDoors.insert(door->GetProperty("PowerGroup"));
                } else if (const auto* board = dynamic_cast<ALeonHorrorFuseBox*>(actor.get())) {
                    boards.insert(board->GetGroup());
                    CHECK(items.count(board->GetRequiredItem()) + 1 > 0);
                    locks.insert(board->GetRequiredItem());
                }
            }
            CHECK(locks == std::set<std::string>{"Key.Consult", "Key.Pharmacy", "Key.Storage", "Key.Bay", "Fuse"});
            for (const std::string& key : locks)
                CHECK_MESSAGE(items[key] == 1, "key " << key);
            // A door that waits for power is on a group some board can bring back.
            CHECK(electricDoors == boards);
            CHECK(items[LeonHorrorItems::Pipe] == 1);
            CHECK(items[LeonHorrorItems::Pistol] == 1);
            CHECK(items[LeonHorrorItems::PistolAmmo] >= 24);
            CHECK(items[LeonHorrorItems::Medkit] >= 4);
        }

        TEST_CASE("the player starts on the reception floor, and its door is all that keeps the corridor out") {
            FClinicWorld clinic(true);
            const glm::vec3 location = clinic.Player->GetActorLocation();
            CHECK(location.y == doctest::Approx(clinic.Player->GetCapsuleHalfHeight()).epsilon(0.15));
            CHECK(location.x > 0.0f);
            CHECK(location.x < 8.4f);
            CHECK(-location.z > 0.0f);
            CHECK(-location.z < 6.0f);
            CHECK(clinic.World->GetAIControllers().size() == 6);

            UNavigationSystem* navigation = clinic.World->GetNavigationSystem();
            REQUIRE(navigation);
            REQUIRE(navigation->IsBuilt());
            const glm::vec3 reception{2.2f, 0.0f, -2.3f};
            const glm::vec3 waiting{11.4f, 0.0f, -3.0f};
            const glm::vec3 corridor{7.2f, 0.0f, -12.0f};
            // The waiting room is through an open archway.
            CHECK(navigation->FindPath(reception, waiting).Status == ENavPathStatus::Valid);
            CHECK(navigation->FindPath(reception, corridor).Status != ENavPathStatus::Valid);
            REQUIRE(clinic.UseFrom("Door_Corridor_A", 6.8f, 5.0f));
            clinic.Tick(1.5f);
            CHECK(clinic.Find<ALeonHorrorDoor>("Door_Corridor_B")->IsOpen());
            CHECK(navigation->FindPath(reception, corridor).Status == ENavPathStatus::Valid);
        }

        TEST_CASE("with every door open, an enemy can walk from reception into every room") {
            FClinicWorld clinic(true);
            for (const auto& actor : clinic.World->GetAllActors()) {
                if (auto* door = dynamic_cast<ALeonHorrorDoor*>(actor.get()))
                    door->SetState(false, true);
            }
            clinic.Tick(0.2f);
            UNavigationSystem* navigation = clinic.World->GetNavigationSystem();
            REQUIRE(navigation);
            REQUIRE(navigation->IsBuilt());
            // A clear spot of floor in each room, as (east, north) on the plan.
            const std::map<std::string, glm::vec2> rooms = {
                {"Waiting", {11.4f, 3.0f}},  {"Corridor", {7.2f, 15.0f}}, {"Consult1", {3.6f, 8.0f}},  {"Consult2", {3.6f, 12.4f}},
                {"Pharmacy", {2.0f, 17.6f}}, {"Staff", {3.4f, 21.6f}},    {"Ward", {11.2f, 9.6f}},     {"XRay", {10.0f, 14.2f}},
                {"Toilets", {11.4f, 18.4f}}, {"Storage", {11.4f, 21.6f}}, {"Service", {7.2f, 25.2f}},  {"Theatre", {4.6f, 28.0f}},
                {"Morgue", {8.0f, 29.6f}},   {"Plant", {12.2f, 28.0f}},   {"Bay", {16.2f, 26.0f}},
            };
            for (const auto& [name, spot] : rooms) {
                CHECK_MESSAGE(navigation->IsWalkable({spot.x, 0.0f, -spot.y}), name);
                CHECK_MESSAGE(navigation->FindPath({2.2f, 0.0f, -2.3f}, {spot.x, 0.0f, -spot.y}).Status == ENavPathStatus::Valid, name);
            }
        }

        TEST_CASE("the clinic can be played from reception to the ambulance bay") {
            FClinicWorld clinic(true);
            auto* inventory = clinic.Player->GetInventory();
            REQUIRE(inventory);
            // This is a walk through the locks; the fights have their own tests.
            clinic.KillEveryEnemy();

            // Reception: read, save, arm.
            REQUIRE(clinic.UseFrom("Note_Reception", 4.3f, 2.4f));
            CHECK(clinic.GM->GetActiveScreen() == ELeonHorrorScreen::Note);
            clinic.GM->DismissNote();
            REQUIRE(clinic.UseFrom("SavePoint_Reception", 2.9f, 4.7f));
            CHECK(clinic.GM->HasSaveFile());
            REQUIRE(clinic.UseFrom("Pickup_Pipe", 6.9f, 1.5f));
            CHECK(inventory->HasItem(LeonHorrorItems::Pipe));
            REQUIRE(clinic.UseFrom("Door_Corridor_A", 6.8f, 5.0f));

            // Three doors off the corridor are locked, and the north wing has no power.
            CHECK_FALSE(clinic.UseFrom("Door_Consult2", 7.0f, 12.8f));
            CHECK_FALSE(clinic.UseFrom("Door_Pharmacy", 7.0f, 17.0f));
            CHECK_FALSE(clinic.UseFrom("Door_Storage", 7.4f, 22.0f));
            CHECK_FALSE(clinic.GM->IsPowered("North"));

            // Staff room: the key to consulting room 2 and the handgun.
            REQUIRE(clinic.UseFrom("Door_Staff", 7.0f, 21.5f));
            REQUIRE(clinic.UseFrom("Pickup_KeyConsult", 2.6f, 20.4f));
            REQUIRE(clinic.UseFrom("Pickup_Pistol", 1.4f, 21.8f));
            REQUIRE(clinic.UseFrom("Pickup_Ammo_Staff", 1.4f, 22.2f));
            CHECK(inventory->HasItem(LeonHorrorItems::Pistol));

            // Consulting room 2: the pharmacy key.
            REQUIRE(clinic.UseFrom("Door_Consult2", 7.0f, 12.8f));
            REQUIRE(clinic.UseFrom("Pickup_KeyPharmacy", 1.0f, 13.3f));
            REQUIRE(clinic.UseFrom("Door_Pharmacy", 7.0f, 17.0f));
            REQUIRE(clinic.UseFrom("Pickup_Medkit_Pharmacy", 4.3f, 16.6f));

            // Ward: the store room key, on the far bed.
            REQUIRE(clinic.UseFrom("Door_Ward_A", 7.4f, 9.1f));
            REQUIRE(clinic.UseFrom("Pickup_KeyStorage", 13.4f, 10.75f));

            // Store room: the fuse.
            REQUIRE(clinic.UseFrom("Door_Storage", 7.4f, 22.0f));
            REQUIRE(clinic.UseFrom("Pickup_Fuse", 11.2f, 22.7f));
            CHECK(inventory->HasItem("Fuse"));

            // Service corridor: the morgue door will not move without power, the bay is locked.
            REQUIRE(clinic.UseFrom("Door_Service_A", 6.8f, 23.0f));
            auto* morgueDoor = clinic.Find<ALeonHorrorDoor>("Door_Morgue");
            REQUIRE(morgueDoor);
            CHECK_FALSE(clinic.UseFrom("Door_Morgue", 8.0f, 25.3f));
            CHECK(morgueDoor->GetInteractPrompt(*clinic.Player) == "No power");
            CHECK_FALSE(clinic.UseFrom("Door_Bay", 13.3f, 25.5f));

            // Plant room: the fuse goes in and the wing comes back.
            REQUIRE(clinic.UseFrom("Door_Plant", 12.2f, 25.3f));
            REQUIRE(clinic.UseFrom("FuseBox_North", 11.2f, 29.0f));
            CHECK(clinic.GM->IsPowered("North"));
            CHECK_FALSE(inventory->HasItem("Fuse"));

            // Morgue: the bay key.
            REQUIRE(clinic.UseFrom("Door_Morgue", 8.0f, 25.3f));
            REQUIRE(clinic.UseFrom("Pickup_KeyBay", 8.0f, 30.6f));

            // Out through the bay.
            REQUIRE(clinic.UseFrom("Door_Bay", 13.3f, 25.5f));
            clinic.Tick(1.5f);
            UNavigationSystem* navigation = clinic.World->GetNavigationSystem();
            REQUIRE(navigation);
            CHECK(navigation->FindPath({2.2f, 0.0f, -2.3f}, {16.2f, 0.0f, -26.0f}).Status == ENavPathStatus::Valid);
            CHECK_FALSE(clinic.GM->IsSliceCompleted());
            REQUIRE(clinic.UseFrom("Exit", 16.9f, 26.0f));
            CHECK(clinic.GM->IsSliceCompleted());
            CHECK(clinic.GM->GetActiveScreen() == ELeonHorrorScreen::Completed);

            const FLeonHorrorSaveGame save = clinic.GM->CaptureSave(*clinic.Player);
            CHECK(save.PoweredGroups.count("North") == 1);
            CHECK(save.DeadEnemies.size() == 6);
            CHECK(save.ReadNotes.size() == 1);
            std::error_code ec;
            std::filesystem::remove(clinic.GM->GetSaveFilePath(), ec);
        }
    }

} // namespace Leon
