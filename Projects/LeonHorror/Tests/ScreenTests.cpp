#include <doctest/doctest.h>

#include "LeonHorrorTestSetup.hpp"

#include "LeonHorror/ALeonHorrorCharacter.hpp"
#include "LeonHorror/ALeonHorrorInteractable.hpp"
#include "LeonHorror/ALeonHorrorPlayerController.hpp"
#include "LeonHorror/ULeonHorrorInventoryComponent.hpp"

namespace Leon {

    namespace {

        struct FScreenFixture : Test::FHorrorWorld {
            ALeonHorrorCharacter* Player = nullptr;
            ALeonHorrorPlayerController* PC = nullptr;

            FScreenFixture() {
                Player = GM->GetPlayerCharacter();
                PC = dynamic_cast<ALeonHorrorPlayerController*>(World->GetFirstPlayerController());
                REQUIRE(Player);
                REQUIRE(PC);
            }

            void Press(bool FLeonHorrorMenuInput::*InField) {
                FLeonHorrorMenuInput input;
                input.*InField = true;
                PC->ApplyMenuInput(input);
            }
        };

    } // namespace

    TEST_SUITE("LeonHorror screens") {

        TEST_CASE("the inventory key opens and closes the inventory and pauses the world meanwhile") {
            FScreenFixture f;
            CHECK(f.GM->GetActiveScreen() == ELeonHorrorScreen::None);
            CHECK_FALSE(f.World->IsPaused());

            f.Press(&FLeonHorrorMenuInput::bInventoryPressed);
            CHECK(f.GM->GetActiveScreen() == ELeonHorrorScreen::Inventory);
            CHECK(f.World->IsPaused());

            // The world stands still: the play clock does not advance and the character cannot move.
            const float clock = f.GM->GetElapsedPlaySeconds();
            FLeonHorrorInputState forward;
            forward.Move = {0.0f, 1.0f};
            f.Player->SetInjectedInput(forward);
            const glm::vec3 before = f.Player->GetActorLocation();
            f.TickSeconds(1.0f);
            CHECK(f.GM->GetElapsedPlaySeconds() == doctest::Approx(clock));
            CHECK(glm::length(f.Player->GetActorLocation() - before) < 0.01f);

            f.Press(&FLeonHorrorMenuInput::bInventoryPressed);
            CHECK(f.GM->GetActiveScreen() == ELeonHorrorScreen::None);
            CHECK_FALSE(f.World->IsPaused());
            f.TickSeconds(0.5f);
            CHECK(glm::length(f.Player->GetActorLocation() - before) > 0.3f);
        }

        TEST_CASE("the pause key toggles the pause menu and also closes the inventory") {
            FScreenFixture f;
            f.Press(&FLeonHorrorMenuInput::bPausePressed);
            CHECK(f.GM->GetActiveScreen() == ELeonHorrorScreen::Pause);
            CHECK(f.World->IsPaused());
            f.Press(&FLeonHorrorMenuInput::bPausePressed);
            CHECK(f.GM->GetActiveScreen() == ELeonHorrorScreen::None);
            CHECK_FALSE(f.World->IsPaused());

            f.Press(&FLeonHorrorMenuInput::bInventoryPressed);
            f.Press(&FLeonHorrorMenuInput::bPausePressed);
            CHECK(f.GM->GetActiveScreen() == ELeonHorrorScreen::None);
        }

        TEST_CASE("a first aid kit heals, is consumed, and is not wasted at full health") {
            FScreenFixture f;
            auto* inventory = f.Player->GetInventory();
            inventory->AddItem(LeonHorrorItems::Medkit, 2);

            // Full health: nothing happens.
            CHECK_FALSE(f.GM->UseMedkit(*f.Player));
            CHECK(inventory->GetCount(LeonHorrorItems::Medkit) == 2);

            f.Player->GetHealth()->ApplyDamage(70.0f);
            f.Press(&FLeonHorrorMenuInput::bUseMedkitPressed); // quick use while playing
            CHECK(f.Player->GetHealth()->GetHealth() == doctest::Approx(80.0f));
            CHECK(inventory->GetCount(LeonHorrorItems::Medkit) == 1);

            // From the inventory screen too; healing is capped at the maximum.
            f.Press(&FLeonHorrorMenuInput::bInventoryPressed);
            f.Press(&FLeonHorrorMenuInput::bUseMedkitPressed);
            CHECK(f.Player->GetHealth()->GetHealth() == doctest::Approx(100.0f));
            CHECK(inventory->GetCount(LeonHorrorItems::Medkit) == 0);

            f.Player->GetHealth()->ApplyDamage(10.0f);
            CHECK_FALSE(f.GM->UseMedkit(*f.Player)); // none left
        }

        TEST_CASE("an open note is dismissed with confirm and takes priority over other screens") {
            FScreenFixture f;
            auto* note = f.World->SpawnActor<ALeonHorrorNote>("Note");
            note->SetContent("Page", "Text");
            f.GM->ShowNote(*note);
            CHECK(f.GM->GetActiveScreen() == ELeonHorrorScreen::Note);

            // The inventory key closes the note instead of opening the inventory over it.
            f.Press(&FLeonHorrorMenuInput::bInventoryPressed);
            CHECK(f.GM->GetActiveScreen() == ELeonHorrorScreen::None);
            CHECK_FALSE(f.GM->IsInventoryOpen());

            f.GM->ShowNote(*note);
            f.Press(&FLeonHorrorMenuInput::bConfirmPressed);
            CHECK_FALSE(f.GM->IsNoteOpen());
            CHECK_FALSE(f.World->IsPaused());
        }

        TEST_CASE("death shows the death screen without pausing; confirm is forwarded") {
            FScreenFixture f;
            f.Player->GetHealth()->ApplyDamage(500.0f);
            CHECK(f.GM->GetActiveScreen() == ELeonHorrorScreen::Dead);
            CHECK_FALSE(f.World->IsPaused());
            // The inventory cannot be opened once dead.
            f.Press(&FLeonHorrorMenuInput::bInventoryPressed);
            CHECK(f.GM->GetActiveScreen() == ELeonHorrorScreen::Dead);
            f.Press(&FLeonHorrorMenuInput::bConfirmPressed);
            CHECK(f.GM->GetEndScreenConfirmCount() == 1);
        }

        TEST_CASE("completing the slice pauses the world on the end screen") {
            FScreenFixture f;
            f.GM->NotifySliceCompleted();
            CHECK(f.GM->GetActiveScreen() == ELeonHorrorScreen::Completed);
            CHECK(f.World->IsPaused());
        }
    }

} // namespace Leon
