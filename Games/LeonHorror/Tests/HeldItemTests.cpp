#include <doctest/doctest.h>

#include "LeonHorrorTestSetup.hpp"

#include "LeonHorror/ALeonHorrorCharacter.hpp"
#include "LeonHorror/ALeonHorrorHeldItem.hpp"
#include "LeonHorror/ULeonHorrorCombatComponent.hpp"
#include "LeonHorror/ULeonHorrorInventoryComponent.hpp"

#include "Gameplay/USkeletalMeshComponent.hpp"
#include "Scene/Components.hpp"

#include <glm/gtc/matrix_transform.hpp>

namespace Leon {

    namespace {

        /** Which way the item's own forward (-Z) points in the world. */
        glm::vec3 ForwardOf(const AActor& InActor) {
            return glm::normalize(glm::vec3(InActor.GetTransform().GetTransform() * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));
        }

    } // namespace

    TEST_SUITE("LeonHorror held items") {

        TEST_CASE("a held item goes where the bone is and turns with it, whatever the bone's scale") {
            Test::FHorrorWorld f;
            auto* item = f.World->SpawnActor<ALeonHorrorHeldItem>("Item");
            item->SetGrip({0.0f, 0.1f, 0.0f}, {0.0f, 0.0f, 0.0f});

            // A bone at (2, 1, -3), turned a quarter about Y, in a skeleton authored in centimetres.
            const glm::mat4 bone = glm::translate(glm::mat4(1.0f), {2.0f, 1.0f, -3.0f}) *
                                   glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), {0.0f, 1.0f, 0.0f}) *
                                   glm::scale(glm::mat4(1.0f), glm::vec3(0.01f));
            item->FollowBone(bone);
            // The grip is along the bone's Y, which the turn about Y leaves alone: 10 cm, not 1 mm.
            const glm::vec3 location = item->GetActorLocation();
            CHECK(location.x == doctest::Approx(2.0f).epsilon(0.001));
            CHECK(location.y == doctest::Approx(1.1f).epsilon(0.001));
            CHECK(location.z == doctest::Approx(-3.0f).epsilon(0.001));
            // -Z turned a quarter about Y points along -X; and the item is not scaled down.
            const glm::vec3 forward = ForwardOf(*item);
            CHECK(forward.x == doctest::Approx(-1.0f).epsilon(0.001));
            CHECK(std::abs(forward.z) < 1.0e-3f);
            CHECK(item->GetTransform().Scale.x == doctest::Approx(1.0f));

            // Aimed: at the bone, pointing where it is told, upright.
            const glm::vec3 aim = glm::normalize(glm::vec3(0.3f, -0.2f, -1.0f));
            item->FollowBoneAimed(bone, aim);
            CHECK(glm::dot(ForwardOf(*item), aim) > 0.9999f);
            const glm::vec3 up = glm::vec3(item->GetTransform().GetTransform() * glm::vec4(0.0f, 1.0f, 0.0f, 0.0f));
            CHECK(up.y > 0.9f);
        }

        TEST_CASE("an item with no mesh shows nothing, and one that is hidden stays hidden") {
            Test::FHorrorWorld f;
            auto* item = f.World->SpawnActor<ALeonHorrorHeldItem>("Item");
            CHECK_FALSE(item->IsShown());
            item->SetMesh("/Game/Meshes/SM_Held_Pipe.lmesh");
            REQUIRE(item->HasComponent<FStaticMeshComponent>());
            const auto& mesh = item->GetComponent<FStaticMeshComponent>();
            CHECK(item->IsShown());
            CHECK(mesh.bVisible);
            CHECK(mesh.StaticMesh != nullptr);
            CHECK(mesh.Mobility == EComponentMobility::Movable);
            CHECK_FALSE(mesh.bCollision);
            CHECK_FALSE(mesh.bCastShadows);
            item->SetShown(false);
            CHECK_FALSE(item->GetComponent<FStaticMeshComponent>().bVisible);
            item->SetShown(true);
            CHECK(item->GetComponent<FStaticMeshComponent>().bVisible);
        }

        TEST_CASE("the player carries the flashlight in the left hand and the weapon in use in the right") {
            Test::FHorrorWorld f;
            ALeonHorrorCharacter* player = f.GM->GetPlayerCharacter();
            REQUIRE(player);
            f.TickSeconds(0.3f);

            ALeonHorrorHeldItem* light = player->GetHeldLight();
            ALeonHorrorHeldItem* weapon = player->GetHeldWeapon();
            REQUIRE(light);
            REQUIRE(weapon);
            CHECK(light->IsShown());
            CHECK(light->GetMeshPath() == "/Game/Meshes/SM_Held_Flashlight.lmesh");
            CHECK_FALSE(weapon->IsShown()); // empty-handed to begin with

            auto mesh = player->GetMesh();
            REQUIRE(mesh);
            glm::vec3 leftHand, rightHand;
            REQUIRE(mesh->GetBoneLocation("mixamorig:LeftHand", leftHand));
            REQUIRE(mesh->GetBoneLocation("mixamorig:RightHand", rightHand));
            CHECK(glm::length(light->GetActorLocation() - leftHand) < 0.15f);
            // It points where the player looks, not where the hand happens to hang.
            CHECK(glm::dot(ForwardOf(*light), player->GetControlLookDirection()) > 0.999f);

            player->GetInventory()->AddItem(LeonHorrorItems::Pipe);
            REQUIRE(player->GetCombat()->Equip(ELeonHorrorWeapon::Pipe));
            f.TickSeconds(0.1f);
            REQUIRE(mesh->GetBoneLocation("mixamorig:RightHand", rightHand));
            CHECK(weapon->IsShown());
            CHECK(weapon->GetMeshPath() == "/Game/Meshes/SM_Held_Pipe.lmesh");
            CHECK(glm::length(weapon->GetActorLocation() - rightHand) < 0.15f);

            player->GetInventory()->AddItem(LeonHorrorItems::Pistol);
            REQUIRE(player->GetCombat()->Equip(ELeonHorrorWeapon::Pistol));
            f.TickSeconds(0.1f);
            CHECK(weapon->IsShown());
            CHECK(weapon->GetMeshPath() == "/Game/Meshes/SM_Held_Pistol.lmesh");

            player->GetCombat()->Equip(ELeonHorrorWeapon::None);
            f.TickSeconds(0.1f);
            CHECK_FALSE(weapon->IsShown());
        }

        TEST_CASE("with a wall right behind, the body and what it carries are not drawn; stepping away brings them back") {
            Test::FMapWorld slice("/Game/Maps/Slice", true);
            ALeonHorrorCharacter* player = slice.Player;
            auto mesh = player->GetMesh();
            REQUIRE(mesh);
            CHECK_FALSE(player->IsBodyHiddenFromCamera());
            CHECK_FALSE(mesh->IsHiddenInGame());

            // The start room's back wall is behind the player at the start; go and lean on it.
            glm::vec3 camera, forward;
            player->GetViewPoint(camera, forward);
            const glm::vec3 start = player->GetActorLocation();
            glm::vec3 back = camera - start;
            back.y = 0.0f;
            back = glm::normalize(back);
            float hiddenAt = -1.0f;
            for (float moved = 0.0f; moved < 12.0f && hiddenAt < 0.0f; moved += 0.25f) {
                player->SetActorLocation(start + back * moved);
                slice.Tick(0.4f);
                if (player->IsBodyHiddenFromCamera())
                    hiddenAt = moved;
            }
            REQUIRE(hiddenAt > 0.0f);
            CHECK(mesh->IsHiddenInGame());
            CHECK_FALSE(player->GetHeldLight()->IsShown());

            player->SetActorLocation(start);
            slice.Tick(1.0f);
            CHECK_FALSE(player->IsBodyHiddenFromCamera());
            CHECK_FALSE(mesh->IsHiddenInGame());
            CHECK(player->GetHeldLight()->IsShown());
        }
    }

} // namespace Leon
