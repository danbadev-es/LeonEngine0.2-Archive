#include <doctest/doctest.h>

#include "LeonHorrorTestSetup.hpp"

#include "LeonHorror/ALeonHorrorCharacter.hpp"
#include "LeonHorror/ALeonHorrorInteractable.hpp"
#include "LeonHorror/ULeonHorrorInventoryComponent.hpp"

#include "Engine/FMapSerializer.hpp"
#include "Gameplay/UPrimitiveComponent.hpp"

namespace Leon {

    namespace {

        struct FInteractFixture : Test::FHorrorWorld {
            ALeonHorrorCharacter* Player = nullptr;
            glm::vec3 Eye{0.0f};
            glm::vec3 Look{0.0f, 0.0f, -1.0f};

            FInteractFixture() {
                Player = GM->GetPlayerCharacter();
                REQUIRE(Player);
                Player->SetActorLocation({0.0f, Player->GetCapsuleHalfHeight(), 0.0f});
                Player->SetControlRotation({0.0f, -90.0f, 0.0f}); // looking down -Z
                Player->SetInjectedInput(FLeonHorrorInputState{});
                TickSeconds(0.05f);
                Eye = Player->GetPawnViewLocation();
                Look = Player->GetControlLookDirection();
            }

            /** A point InDistance metres in front of the eye, shifted sideways by InSide metres. */
            glm::vec3 InFront(float InDistance, float InSide = 0.0f) const {
                return Eye + Look * InDistance + glm::vec3(InSide, 0.0f, 0.0f);
            }

            ALeonHorrorPickup* SpawnPickup(const std::string& InName, const glm::vec3& InLocation,
                                           const std::string& InItem, int32_t InCount = 1) {
                auto* pickup = World->SpawnActor<ALeonHorrorPickup>(InName);
                pickup->SetActorLocation(InLocation);
                pickup->SetItem(InItem, InCount);
                return pickup;
            }

            void PressInteract() {
                FLeonHorrorInputState press;
                press.bInteractPressed = true;
                Player->ApplyInputState(press, 1.0f / 60.0f);
            }
        };

    } // namespace

    TEST_SUITE("LeonHorror inventory") {

        TEST_CASE("items are counted, removal is all-or-nothing and changes are announced") {
            ULeonHorrorInventoryComponent inventory;
            std::vector<std::string> changes;
            inventory.OnChanged.push_back([&](const std::string& InItem) { changes.push_back(InItem); });

            CHECK(inventory.AddItem(LeonHorrorItems::PistolAmmo, 6) == 6);
            CHECK(inventory.AddItem(LeonHorrorItems::PistolAmmo, 4) == 10);
            CHECK(inventory.GetCount(LeonHorrorItems::PistolAmmo) == 10);
            CHECK_FALSE(inventory.RemoveItem(LeonHorrorItems::PistolAmmo, 11));
            CHECK(inventory.GetCount(LeonHorrorItems::PistolAmmo) == 10);
            CHECK(inventory.RemoveItem(LeonHorrorItems::PistolAmmo, 10));
            CHECK_FALSE(inventory.HasItem(LeonHorrorItems::PistolAmmo));
            CHECK(inventory.GetItems().empty());
            CHECK_FALSE(inventory.RemoveItem("Nothing"));
            CHECK(inventory.AddItem("", 3) == 0);
            CHECK(inventory.AddItem("Thing", 0) == 0);
            CHECK(changes.size() == 3);
        }

        TEST_CASE("item ids have readable names") {
            CHECK(LeonHorrorItems::DisplayName("Key.Red") == "Red key");
            CHECK(LeonHorrorItems::DisplayName(LeonHorrorItems::Pistol) == "Handgun");
            CHECK(LeonHorrorItems::DisplayName("Unknown.Thing") == "Unknown.Thing");
        }
    }

    TEST_SUITE("LeonHorror interaction") {

        TEST_CASE("nothing in reach means no focus and no prompt") {
            FInteractFixture f;
            f.SpawnPickup("Far", f.InFront(8.0f), LeonHorrorItems::Medkit);
            f.TickSeconds(0.05f);
            CHECK(f.Player->GetFocusedInteractable() == nullptr);
            CHECK(f.Player->GetInteractPrompt().empty());
            CHECK_FALSE(f.Player->TryInteract());
        }

        TEST_CASE("the target is what the player looks at, not merely what is nearest") {
            FInteractFixture f;
            auto* centred = f.SpawnPickup("Centred", f.InFront(1.8f), LeonHorrorItems::Medkit);
            auto* nearSide = f.SpawnPickup("NearSide", f.InFront(1.0f, 0.45f), LeonHorrorItems::PistolAmmo);
            auto* behind = f.SpawnPickup("Behind", f.Eye - f.Look * 1.0f, LeonHorrorItems::Pipe);
            f.TickSeconds(0.05f);
            CHECK(f.Player->GetFocusedInteractable() == centred);
            (void)nearSide;

            // Turn around: the one behind is now the one in view.
            f.Player->SetControlRotation({0.0f, 90.0f, 0.0f});
            f.TickSeconds(0.05f);
            CHECK(f.Player->GetFocusedInteractable() == behind);
        }

        TEST_CASE("picking up an item adds it, removes the actor and is recorded by GUID") {
            FInteractFixture f;
            auto* pickup = f.SpawnPickup("Ammo", f.InFront(1.5f), LeonHorrorItems::PistolAmmo, 6);
            const std::string guid = pickup->GetActorGuid().ToString();
            f.TickSeconds(0.05f);
            REQUIRE(f.Player->GetFocusedInteractable() == pickup);
            CHECK(f.Player->GetInteractPrompt() == "Take Handgun bullets x6");

            f.PressInteract();
            CHECK(f.Player->GetInventory()->GetCount(LeonHorrorItems::PistolAmmo) == 6);
            CHECK(f.GM->GetCollectedPickups().count(guid) == 1);
            f.TickSeconds(0.05f);
            CHECK(f.World->FindActorByName("Ammo") == nullptr);
            CHECK(f.Player->GetFocusedInteractable() == nullptr);

            // Pressing again with nothing there does nothing.
            f.PressInteract();
            CHECK(f.Player->GetInventory()->GetCount(LeonHorrorItems::PistolAmmo) == 6);
        }

        TEST_CASE("a pickup reads its item from map properties") {
            FInteractFixture f;
            auto* pickup = f.World->SpawnActor<ALeonHorrorPickup>("FromMap");
            pickup->SetProperty("Item", "Key.Red");
            pickup->SetProperty("Count", "1");
            pickup->OnPropertiesLoaded();
            CHECK(pickup->GetItemId() == "Key.Red");
            CHECK(pickup->GetInteractPrompt(*f.Player) == "Take Red key");
        }

        TEST_CASE("a locked door needs its key; the key is kept after unlocking") {
            FInteractFixture f;
            auto* door = f.World->SpawnActor<ALeonHorrorDoor>("Door");
            door->SetClosedLocation(f.InFront(1.6f));
            door->SetRequiredKey("Key.Red");
            f.TickSeconds(0.05f);
            REQUIRE(f.Player->GetFocusedInteractable() == door);
            CHECK(door->IsLocked());
            CHECK(f.Player->GetInteractPrompt() == "Locked");

            f.PressInteract();
            CHECK(door->IsLocked());
            CHECK_FALSE(door->IsOpen());
            CHECK(door->GetCollider()->GetCollisionEnabled() != ECollisionEnabled::NoCollision);

            f.Player->GetInventory()->AddItem("Key.Blue");
            f.PressInteract();
            CHECK(door->IsLocked()); // the wrong key does not help

            f.Player->GetInventory()->AddItem("Key.Red");
            CHECK(f.Player->GetInteractPrompt() == "Unlock with Red key");
            f.PressInteract();
            CHECK_FALSE(door->IsLocked());
            CHECK(door->IsOpen());
            CHECK(f.Player->GetInventory()->HasItem("Key.Red"));
            CHECK(door->GetCollider()->GetCollisionEnabled() == ECollisionEnabled::NoCollision);

            const auto& states = f.GM->GetDoorStates();
            REQUIRE(states.count(door->GetActorGuid().ToString()) == 1);
            CHECK(states.at(door->GetActorGuid().ToString()).bOpen);
            CHECK_FALSE(states.at(door->GetActorGuid().ToString()).bLocked);
        }

        TEST_CASE("an unlocked door toggles, slides aside and blocks only while closed") {
            FInteractFixture f;
            auto* door = f.World->SpawnActor<ALeonHorrorDoor>("Door");
            door->SetDoorSize({1.2f, 2.2f, 0.2f});
            const glm::vec3 closed = f.InFront(1.6f);
            door->SetClosedLocation(closed);
            door->SetState(false, false);
            f.TickSeconds(0.05f);

            // Closed: a trace along the view is stopped by the door.
            FHitResult hit;
            const glm::vec3 beyond = f.InFront(4.0f);
            REQUIRE(f.World->LineTraceSingleByChannel(f.Eye, beyond, ECollisionChannel::WorldStatic, f.Player, hit));
            CHECK(hit.Actor == door);

            CHECK(f.Player->GetInteractPrompt() == "Open door");
            f.PressInteract();
            CHECK(door->IsOpen());
            f.TickSeconds(1.0f);
            // It has slid one door-width along X and no longer blocks.
            CHECK(door->GetActorLocation().x == doctest::Approx(closed.x + 1.2f).epsilon(0.01));
            CHECK_FALSE(f.World->LineTraceSingleByChannel(f.Eye, beyond, ECollisionChannel::WorldStatic, f.Player, hit));

            door->SetState(false, false);
            CHECK(door->GetActorLocation().x == doctest::Approx(closed.x).epsilon(0.01));
            CHECK(f.World->LineTraceSingleByChannel(f.Eye, beyond, ECollisionChannel::WorldStatic, f.Player, hit));
        }

        TEST_CASE("a closed door hides what is behind it from interaction") {
            FInteractFixture f;
            auto* door = f.World->SpawnActor<ALeonHorrorDoor>("Door");
            door->SetDoorSize({3.0f, 3.0f, 0.2f});
            door->SetClosedLocation(f.InFront(1.2f));
            door->SetState(false, false);
            auto* hidden = f.SpawnPickup("Hidden", f.InFront(2.0f), LeonHorrorItems::Medkit);
            f.TickSeconds(0.05f);
            CHECK(f.Player->GetFocusedInteractable() == door);

            door->SetState(false, true);
            f.TickSeconds(0.6f);
            CHECK(f.Player->FindInteractable() == hidden);
        }

        TEST_CASE("door data comes from map properties and the mesh box sizes the collider") {
            Test::RegisterLeonHorrorClasses();
            const char* kMap = R"(
Map:
  Name: "DoorMap"
  Version: "2.2"
Actors:
  - Name: "LockedDoor"
    Class: "ALeonHorrorDoor"
    GUID: "000000000000d00000000000000000d1"
    Properties:
      RequiredKey: "Key.Red"
      SlideX: "0"
      SlideY: "-2.5"
      SlideZ: "0"
    Transform:
      Translation: [4, 1.1, -3]
      Rotation: [0, 0, 0]
      Scale: [1, 1, 1]
    StaticMesh:
      Type: "Box"
      Width: 1.4
      Height: 2.2
      Depth: 0.16
      Mobility: Movable
)";
            auto world = UWorld::Create("DoorMapWorld");
            REQUIRE(FMapSerializer(world).DeserializeText(kMap));
            auto* door = dynamic_cast<ALeonHorrorDoor*>(world->FindActorByName("LockedDoor"));
            REQUIRE(door);
            CHECK(door->IsLocked());
            CHECK(door->GetRequiredKey() == "Key.Red");
            CHECK(door->GetCollider()->GetBoxExtent().x == doctest::Approx(0.7f));
            CHECK(door->GetCollider()->GetBoxExtent().y == doctest::Approx(1.1f));

            // Opening follows the authored slide: straight down into the floor.
            door->SetState(false, true);
            CHECK(door->GetActorLocation().y == doctest::Approx(1.1f - 2.5f));
            CHECK(door->GetActorLocation().x == doctest::Approx(4.0f));
        }

        TEST_CASE("reading a note pauses the world until it is dismissed and is recorded") {
            FInteractFixture f;
            auto* note = f.World->SpawnActor<ALeonHorrorNote>("Note");
            note->SetActorLocation(f.InFront(1.2f));
            note->SetProperty("Title", "Torn page");
            note->SetProperty("Text", "First line.\\nSecond line.");
            note->OnPropertiesLoaded();
            CHECK(note->GetText() == "First line.\nSecond line.");
            f.TickSeconds(0.05f);
            CHECK(f.Player->GetInteractPrompt() == "Read Torn page");

            f.PressInteract();
            CHECK(f.GM->IsNoteOpen());
            CHECK(f.GM->GetNoteTitle() == "Torn page");
            CHECK(f.World->IsPaused());
            CHECK(f.GM->GetReadNotes().count(note->GetActorGuid().ToString()) == 1);

            f.GM->DismissNote();
            CHECK_FALSE(f.GM->IsNoteOpen());
            CHECK_FALSE(f.World->IsPaused());
        }

        TEST_CASE("note text wraps at word boundaries and keeps paragraphs") {
            const auto lines = ALeonHorrorNote::WrapText("one two three four five\n\nsix", 9);
            REQUIRE(lines.size() == 5);
            CHECK(lines[0] == "one two");
            CHECK(lines[1] == "three");
            CHECK(lines[2] == "four five");
            CHECK(lines[3].empty());
            CHECK(lines[4] == "six");
            for (const auto& line : ALeonHorrorNote::WrapText("supercalifragilistic word", 8))
                CHECK(line.size() <= 8);
        }

        TEST_CASE("a save point asks the game mode to save") {
            FInteractFixture f;
            auto* save = f.World->SpawnActor<ALeonHorrorSavePoint>("Save");
            save->SetActorLocation(f.InFront(1.2f));
            f.TickSeconds(0.05f);
            CHECK(f.Player->GetInteractPrompt() == "Save");
            f.PressInteract();
            CHECK(f.GM->GetSaveCount() == 1);
        }

        TEST_CASE("the exit only works with its key and completes the slice") {
            FInteractFixture f;
            auto* exit = f.World->SpawnActor<ALeonHorrorExit>("Exit");
            exit->SetActorLocation(f.InFront(1.2f));
            exit->SetRequiredKey("Key.Exit");
            f.TickSeconds(0.05f);
            CHECK(f.Player->GetInteractPrompt() == "Locked");
            f.PressInteract();
            CHECK_FALSE(f.GM->IsSliceCompleted());

            f.Player->GetInventory()->AddItem("Key.Exit");
            CHECK(f.Player->GetInteractPrompt() == "Leave");
            f.PressInteract();
            CHECK(f.GM->IsSliceCompleted());
        }

        TEST_CASE("a dead player cannot interact") {
            FInteractFixture f;
            f.SpawnPickup("Item", f.InFront(1.2f), LeonHorrorItems::Medkit);
            f.TickSeconds(0.05f);
            REQUIRE(f.Player->GetFocusedInteractable() != nullptr);
            f.Player->GetHealth()->ApplyDamage(500.0f);
            f.TickSeconds(0.05f);
            CHECK(f.Player->GetFocusedInteractable() == nullptr);
            f.PressInteract();
            CHECK_FALSE(f.Player->GetInventory()->HasItem(LeonHorrorItems::Medkit));
        }
    }

} // namespace Leon
