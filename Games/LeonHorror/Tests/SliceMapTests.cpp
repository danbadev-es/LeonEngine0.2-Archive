#include <doctest/doctest.h>

#include "LeonHorrorTestSetup.hpp"

#include "LeonHorror/ALeonHorrorCharacter.hpp"
#include "LeonHorror/ALeonHorrorEnemy.hpp"
#include "LeonHorror/ALeonHorrorInteractable.hpp"
#include "LeonHorror/ULeonHorrorCombatComponent.hpp"
#include "LeonHorror/ULeonHorrorInventoryComponent.hpp"

#include "AI/UNavigationSystem.hpp"
#include "Engine/FMapSerializer.hpp"
#include "Gameplay/APlayerController.hpp"
#include "Assets/USkeletalMesh.hpp"
#include "Gameplay/APlayerStart.hpp"
#include "Gameplay/USkeletalMeshComponent.hpp"
#include "Gameplay/USpringArmComponent.hpp"
#include "Gameplay/UHealthComponent.hpp"

#include <cmath>
#include <filesystem>
#include <map>
#include <set>
#include <string>

namespace Leon {

    namespace {

        /** The shipped map, loaded into a headless world; optionally running the game on it. */
        struct FSliceWorld : Test::FMapWorld {
            explicit FSliceWorld(bool bInPlay) : FMapWorld("/Game/Maps/Slice", bInPlay) {}
        };

    } // namespace

    TEST_SUITE("LeonHorror slice map") {

        TEST_CASE("the map is well formed: unique GUIDs, one start, and what the level needs") {
            FSliceWorld map(false);
            std::set<std::string> guids;
            int starts = 0;
            int spawns = 0;
            int savePoints = 0;
            int exits = 0;
            int navBounds = 0;
            for (const auto& actor : map.World->GetAllActors()) {
                REQUIRE(actor);
                const std::string guid = actor->GetActorGuid().ToString();
                CHECK_MESSAGE(guids.insert(guid).second, "duplicate GUID on " << actor->GetName());
                starts += dynamic_cast<APlayerStart*>(actor.get()) ? 1 : 0;
                spawns += dynamic_cast<ALeonHorrorEnemySpawn*>(actor.get()) ? 1 : 0;
                savePoints += dynamic_cast<ALeonHorrorSavePoint*>(actor.get()) ? 1 : 0;
                exits += dynamic_cast<ALeonHorrorExit*>(actor.get()) ? 1 : 0;
                navBounds += actor->GetClass() == "ANavMeshBoundsVolume" ? 1 : 0;
            }
            CHECK(guids.size() > 40);
            CHECK(starts == 1);
            CHECK(spawns >= 1);
            CHECK(savePoints == 1);
            CHECK(exits == 1);
            CHECK(navBounds == 1);
        }

        TEST_CASE("every lock has its key lying somewhere, and both weapons can be found") {
            FSliceWorld map(false);
            std::map<std::string, int> items;
            std::set<std::string> locks;
            for (const auto& actor : map.World->GetAllActors()) {
                if (const auto* pickup = dynamic_cast<ALeonHorrorPickup*>(actor.get())) {
                    CHECK_MESSAGE(!pickup->GetItemId().empty(), pickup->GetName());
                    items[pickup->GetItemId()] += pickup->GetItemCount();
                } else if (const auto* door = dynamic_cast<ALeonHorrorDoor*>(actor.get())) {
                    if (!door->GetRequiredKey().empty()) {
                        CHECK(door->IsLocked());
                        locks.insert(door->GetRequiredKey());
                    }
                } else if (const auto* exit = dynamic_cast<ALeonHorrorExit*>(actor.get())) {
                    CHECK_FALSE(exit->GetRequiredKey().empty());
                    locks.insert(exit->GetRequiredKey());
                }
            }
            CHECK(locks.size() == 2);
            for (const std::string& key : locks)
                CHECK_MESSAGE(items[key] == 1, "key " << key);
            CHECK(items[LeonHorrorItems::Pipe] == 1);
            CHECK(items[LeonHorrorItems::Pistol] == 1);
            CHECK(items[LeonHorrorItems::PistolAmmo] >= 6);
            CHECK(items[LeonHorrorItems::Medkit] >= 1);
        }

        TEST_CASE("actor properties and fog survive the map being rewritten, as a lightmap bake does") {
            FSliceWorld map(false);
            std::string text;
            REQUIRE(FMapSerializer(map.World).SerializeText(text));
            TRef<UWorld> again = UWorld::Create("SliceRewritten");
            REQUIRE(FMapSerializer(again).DeserializeText(text));

            std::map<std::string, std::map<std::string, std::string>> before;
            for (const auto& actor : map.World->GetAllActors())
                before[actor->GetActorGuid().ToString()] = actor->GetProperties();
            size_t withProperties = 0;
            for (const auto& actor : again->GetAllActors()) {
                // The environment actor is rebuilt from the Environment block, not stored by GUID.
                if (actor->HasComponent<FSkyboxComponent>() || actor->HasComponent<FWorldSettingsComponent>())
                    continue;
                const auto found = before.find(actor->GetActorGuid().ToString());
                REQUIRE_MESSAGE(found != before.end(), actor->GetName());
                CHECK_MESSAGE(actor->GetProperties() == found->second, actor->GetName());
                withProperties += actor->GetProperties().empty() ? 0 : 1;
            }
            CHECK(again->GetAllActors().size() == map.World->GetAllActors().size());
            CHECK(withProperties >= 12);
            CHECK(text.find("FogEnabled: true") != std::string::npos);
        }

        TEST_CASE("the player lands on the floor of the start room and the enemy can reach it") {
            FSliceWorld slice(true);
            const glm::vec3 location = slice.Player->GetActorLocation();
            CHECK(location.y == doctest::Approx(slice.Player->GetCapsuleHalfHeight()).epsilon(0.15));
            CHECK(location.z > 0.0f);
            CHECK(location.z < 8.0f);

            auto* spawn = slice.Find<ALeonHorrorEnemySpawn>("EnemySpawn_Corridor");
            REQUIRE(spawn);
            REQUIRE(spawn->GetEnemy());
            CHECK(slice.World->GetAIControllers().size() == 1);

            UNavigationSystem* navigation = slice.World->GetNavigationSystem();
            REQUIRE(navigation);
            REQUIRE(navigation->IsBuilt());
            // Corridor end to corridor start is open from the beginning.
            CHECK(navigation->FindPath({0.0f, 0.0f, -22.0f}, {0.0f, 0.0f, -2.0f}).Status == ENavPathStatus::Valid);
            // The start room is behind a closed door until someone opens it.
            CHECK(navigation->FindPath({0.0f, 0.0f, -22.0f}, {0.0f, 0.0f, 5.0f}).Status != ENavPathStatus::Valid);
            auto* door = slice.Find<ALeonHorrorDoor>("Door_Start");
            REQUIRE(door);
            REQUIRE(slice.Use("Door_Start", 0.0f, 1.5f));
            CHECK(door->IsOpen());
            slice.Tick(0.6f);
            CHECK(navigation->FindPath({0.0f, 0.0f, -22.0f}, {0.0f, 0.0f, 5.0f}).Status == ENavPathStatus::Valid);
            // The office door is still shut: no complete path leads inside.
            CHECK(navigation->FindPath({0.0f, 0.0f, -2.0f}, {-6.0f, 0.0f, -11.0f}).Status != ENavPathStatus::Valid);
        }

        TEST_CASE("the camera sits behind the player at full arm length in the start room") {
            FSliceWorld slice(true);
            glm::vec3 camera, forward;
            slice.Player->GetViewPoint(camera, forward);
            auto arm = slice.Player->GetSpringArm();
            REQUIRE(arm);
            const glm::vec3 eye = slice.Player->GetPawnViewLocation();
            MESSAGE("eye " << eye.x << " " << eye.y << " " << eye.z << " camera " << camera.x << " " << camera.y
                           << " " << camera.z << " arm " << arm->GetCurrentArmLength() << " target " << arm->TargetArmLength);
            CHECK(arm->GetCurrentArmLength() == doctest::Approx(arm->TargetArmLength));
            CHECK(glm::length(camera - eye) > 1.8f);
            auto mesh = slice.Player->GetMesh();
            REQUIRE(mesh);
            CHECK(mesh->GetSkeletalMesh() != nullptr);
            CHECK_FALSE(mesh->IsHiddenInGame());

            // Standing still, the player is upright: head over the hips, not in the enemy's hunch.
            slice.Tick(1.0f);
            auto skeleton = mesh->GetSkeletalMesh()->GetSkeleton();
            REQUIRE(skeleton);
            const int32_t hips = skeleton->FindBoneIndex("mixamorig:Hips");
            const int32_t head = skeleton->FindBoneIndex("mixamorig:Head");
            REQUIRE(hips >= 0);
            REQUIRE(head >= 0);
            const auto& bones = mesh->GetComponentSpaceTransforms();
            REQUIRE(bones.size() > static_cast<size_t>(std::max(hips, head)));
            const glm::vec3 hipsPos(bones[hips][3]);
            const glm::vec3 headPos(bones[head][3]);
            MESSAGE("standing: hips " << hipsPos.x << " " << hipsPos.y << " " << hipsPos.z << " head " << headPos.x
                                      << " " << headPos.y << " " << headPos.z);
            CHECK(glm::length(glm::vec2(headPos.x - hipsPos.x, headPos.z - hipsPos.z)) < 0.10f);
        }

        TEST_CASE("the slice can be played from the first room to the exit") {
            FSliceWorld slice(true);
            auto* inventory = slice.Player->GetInventory();
            REQUIRE(inventory);
            // The enemy is not part of this walk.
            auto* spawn = slice.Find<ALeonHorrorEnemySpawn>("EnemySpawn_Corridor");
            REQUIRE(spawn);
            if (ALeonHorrorEnemy* enemy = spawn->GetEnemy())
                enemy->GetHealth()->ApplyDamage(1000.0f);

            // Start room: read, save, arm.
            REQUIRE(slice.Use("Note_Start", -2.0f, 4.5f));
            CHECK(slice.GM->GetActiveScreen() == ELeonHorrorScreen::Note);
            slice.GM->DismissNote();
            REQUIRE(slice.Use("SavePoint", -2.0f, 3.6f));
            CHECK(slice.GM->HasSaveFile());
            REQUIRE(slice.Use("Pickup_Pipe", 0.6f, 1.6f));
            CHECK(inventory->HasItem(LeonHorrorItems::Pipe));

            // The exit and the storage room are locked for now.
            CHECK_FALSE(slice.Use("Exit", 0.0f, -22.5f));
            CHECK_FALSE(slice.Use("Door_RoomB", 0.2f, -17.0f));
            CHECK_FALSE(slice.GM->IsSliceCompleted());

            // Office: storage key, handgun, ammunition.
            REQUIRE(slice.Use("Door_Start", 0.0f, 1.5f));
            REQUIRE(slice.Use("Door_RoomC", -0.2f, -11.0f));
            REQUIRE(slice.Use("Pickup_KeyStorage", -6.0f, -11.6f));
            REQUIRE(slice.Use("Pickup_Pistol", -6.0f, -10.6f));
            REQUIRE(slice.Use("Pickup_AmmoC", -4.5f, -12.3f));
            CHECK(inventory->HasItem("Key.Storage"));
            CHECK(inventory->GetCount(LeonHorrorItems::PistolAmmo) == 6);

            // Storage room: first aid and the exit key.
            REQUIRE(slice.Use("Door_RoomB", 0.2f, -17.0f));
            REQUIRE(slice.Use("Pickup_Medkit", 5.8f, -15.6f));
            REQUIRE(slice.Use("Pickup_KeyExit", 7.2f, -15.6f));
            REQUIRE(slice.Use("Pickup_AmmoB", 6.5f, -15.6f));
            CHECK(inventory->HasItem("Key.Exit"));

            // Out.
            REQUIRE(slice.Use("Exit", 0.0f, -22.5f));
            CHECK(slice.GM->IsSliceCompleted());
            CHECK(slice.GM->GetActiveScreen() == ELeonHorrorScreen::Completed);

            const FLeonHorrorSaveGame save = slice.GM->CaptureSave(*slice.Player);
            CHECK(save.CollectedPickups.size() == 7);
            CHECK(save.Doors.size() == 3);
            CHECK(save.DeadEnemies.size() == 1);
            CHECK(save.ReadNotes.size() == 1);
            std::error_code ec;
            std::filesystem::remove(slice.GM->GetSaveFilePath(), ec);
        }
    }

} // namespace Leon
