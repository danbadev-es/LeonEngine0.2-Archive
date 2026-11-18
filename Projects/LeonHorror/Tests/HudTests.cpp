#include <doctest/doctest.h>

#include "LeonHorrorTestSetup.hpp"

#include "LeonHorror/ALeonHorrorCharacter.hpp"
#include "LeonHorror/ALeonHorrorHUD.hpp"
#include "LeonHorror/ALeonHorrorInteractable.hpp"
#include "LeonHorror/ALeonHorrorPlayerController.hpp"
#include "LeonHorror/ULeonHorrorCombatComponent.hpp"
#include "LeonHorror/ULeonHorrorInventoryComponent.hpp"

#include "Gameplay/UHealthComponent.hpp"

#include <filesystem>

namespace Leon {

    namespace {

        struct FHudFixture : Test::FHorrorWorld {
            ALeonHorrorCharacter* Player = nullptr;
            ALeonHorrorPlayerController* PC = nullptr;
            ALeonHorrorHUD* Hud = nullptr;
            std::string SavePath;

            FHudFixture() : Test::FHorrorWorld(false) {
                GM->HUDClass = "ALeonHorrorHUD";
                World->BeginPlay();
                Player = GM->GetPlayerCharacter();
                PC = dynamic_cast<ALeonHorrorPlayerController*>(World->GetFirstPlayerController());
                REQUIRE(Player);
                REQUIRE(PC);
                Hud = PC->GetHUD<ALeonHorrorHUD>();
                Player->SetInjectedInput(FLeonHorrorInputState{});

                std::error_code ec;
                std::filesystem::create_directories("build/LeonHorrorSaves", ec);
                SavePath = "build/LeonHorrorSaves/hud_test.lsav";
                std::filesystem::remove(SavePath, ec);
                GM->SetSaveFilePath(SavePath);
            }
            ~FHudFixture() {
                std::error_code ec;
                std::filesystem::remove(SavePath, ec);
            }

            FLeonHorrorHudModel Model() const { return ALeonHorrorHUD::BuildModel(*GM, Player); }

            void Press(bool FLeonHorrorMenuInput::*InField) {
                FLeonHorrorMenuInput input;
                input.*InField = true;
                PC->ApplyMenuInput(input);
            }
        };

    } // namespace

    TEST_SUITE("LeonHorror HUD") {

        TEST_CASE("the game mode spawns the horror HUD and drawing without a window is harmless") {
            FHudFixture f;
            REQUIRE(f.Hud != nullptr);
            f.Hud->DrawHUD();
            CHECK(f.Hud->BuildModel().Screen == ELeonHorrorScreen::None);
            CHECK(f.Hud->BuildModel().bShowStatus);
        }

        TEST_CASE("the overlay shows health, stamina and the equipped weapon with its ammunition") {
            FHudFixture f;
            FLeonHorrorHudModel model = f.Model();
            CHECK(model.bShowStatus);
            CHECK(model.HealthPercent == doctest::Approx(1.0f));
            CHECK_FALSE(model.bShowStamina);
            CHECK(model.WeaponText.empty());
            CHECK_FALSE(model.bShowCrosshair);

            f.Player->GetHealth()->ApplyDamage(25.0f);
            f.Player->SetStamina(40.0f);
            f.Player->GetInventory()->AddItem(LeonHorrorItems::Pipe);
            f.Player->GetCombat()->Equip(ELeonHorrorWeapon::Pipe);
            model = f.Model();
            CHECK(model.HealthPercent == doctest::Approx(0.75f));
            CHECK(model.bShowStamina);
            CHECK(model.StaminaPercent == doctest::Approx(0.4f));
            CHECK(model.WeaponText == "STEEL PIPE");

            f.Player->GetInventory()->AddItem(LeonHorrorItems::Pistol);
            f.Player->GetInventory()->AddItem(LeonHorrorItems::PistolAmmo, 9);
            f.Player->GetCombat()->Equip(ELeonHorrorWeapon::Pistol);
            f.Player->GetCombat()->SetLoadedRounds(4);
            CHECK(f.Model().WeaponText == "HANDGUN  4 / 9");
        }

        TEST_CASE("the interaction prompt of the thing in front of the player is shown") {
            FHudFixture f;
            CHECK(f.Model().Prompt.empty());
            auto* pickup = f.World->SpawnActor<ALeonHorrorPickup>("Medkit");
            pickup->SetItem(LeonHorrorItems::Medkit, 1);
            f.Player->SetActorLocation({0.0f, f.Player->GetCapsuleHalfHeight(), 0.0f});
            f.Player->SetControlRotation({0.0f, -90.0f, 0.0f}); // looking down -Z
            f.TickSeconds(0.05f);
            pickup->SetActorLocation(f.Player->GetPawnViewLocation() + f.Player->GetControlLookDirection() * 1.2f);
            f.TickSeconds(0.05f);
            CHECK(f.Model().Prompt == f.Player->GetInteractPrompt());
            CHECK_FALSE(f.Model().Prompt.empty());
        }

        TEST_CASE("the inventory screen lists what is carried and hides the overlay") {
            FHudFixture f;
            f.Player->GetInventory()->AddItem(LeonHorrorItems::Medkit, 2);
            f.Player->GetInventory()->AddItem("Key.Red");
            f.Press(&FLeonHorrorMenuInput::bInventoryPressed);
            const FLeonHorrorHudModel model = f.Model();
            CHECK(model.Screen == ELeonHorrorScreen::Inventory);
            CHECK_FALSE(model.bShowStatus);
            CHECK(model.ScreenTitle == "INVENTORY");
            bool bMedkits = false;
            bool bKey = false;
            for (const std::string& line : model.ScreenLines) {
                bMedkits = bMedkits || line == LeonHorrorItems::DisplayName(LeonHorrorItems::Medkit) + "  x2";
                bKey = bKey || line == LeonHorrorItems::DisplayName("Key.Red");
            }
            CHECK(bMedkits);
            CHECK(bKey);
        }

        TEST_CASE("a note is shown wrapped, with its title") {
            FHudFixture f;
            auto* note = f.World->SpawnActor<ALeonHorrorNote>("Note");
            note->SetContent("Torn page", "The red key is in the room at the end of the corridor, behind the bed, "
                                       "where nobody thought to look for it.");
            REQUIRE(note->Interact(*f.Player));
            const FLeonHorrorHudModel model = f.Model();
            CHECK(model.Screen == ELeonHorrorScreen::Note);
            CHECK(model.ScreenTitle == "Torn page");
            REQUIRE(model.ScreenLines.size() >= 2);
            for (const std::string& line : model.ScreenLines)
                CHECK(line.size() <= ALeonHorrorHUD::NoteColumns);
        }

        TEST_CASE("saving raises a message that goes away") {
            FHudFixture f;
            REQUIRE(f.Hud != nullptr);
            REQUIRE(f.GM->RequestSave(*f.Player));
            f.TickSeconds(0.1f);
            CHECK(f.Hud->GetToast() == "GAME SAVED");
            CHECK(f.Hud->BuildModel().Toast == "GAME SAVED");
            f.TickSeconds(ALeonHorrorHUD::ToastSeconds + 0.2f);
            CHECK(f.Hud->GetToast().empty());
        }

        TEST_CASE("dying shows the death screen, and confirming asks to load the last save") {
            FHudFixture f;
            REQUIRE(f.GM->RequestSave(*f.Player));
            f.Player->GetHealth()->ApplyDamage(500.0f);
            f.TickSeconds(0.1f);
            const FLeonHorrorHudModel model = f.Model();
            CHECK(model.Screen == ELeonHorrorScreen::Dead);
            CHECK(model.ScreenTitle == "YOU DIED");
            CHECK(model.ScreenFooter == "[E] Load the last save");
            CHECK_FALSE(f.World->IsPaused()); // the fall plays out

            CHECK(f.GM->GetLastTravelRequest() == ELeonHorrorTravel::None);
            f.Press(&FLeonHorrorMenuInput::bConfirmPressed);
            CHECK(f.GM->GetLastTravelRequest() == ELeonHorrorTravel::LoadSave);
        }

        TEST_CASE("dying without a save restarts the map") {
            FHudFixture f;
            f.Player->GetHealth()->ApplyDamage(500.0f);
            f.TickSeconds(0.1f);
            CHECK(f.Model().ScreenFooter == "[E] Start again");
            f.Press(&FLeonHorrorMenuInput::bConfirmPressed);
            CHECK(f.GM->GetLastTravelRequest() == ELeonHorrorTravel::Restart);
        }

        TEST_CASE("the pause menu can load the last save or quit") {
            FHudFixture f;
            REQUIRE(f.GM->RequestSave(*f.Player));
            f.Press(&FLeonHorrorMenuInput::bPausePressed);
            CHECK(f.Model().ScreenTitle == "PAUSED");
            // Load and quit keys do nothing outside the pause menu, so they are safe during play.
            f.Press(&FLeonHorrorMenuInput::bLoadPressed);
            CHECK(f.GM->GetLastTravelRequest() == ELeonHorrorTravel::LoadSave);
            CHECK(f.GM->GetActiveScreen() == ELeonHorrorScreen::None);

            f.Press(&FLeonHorrorMenuInput::bQuitPressed);
            CHECK_FALSE(f.GM->IsQuitRequested());
            f.Press(&FLeonHorrorMenuInput::bPausePressed);
            f.Press(&FLeonHorrorMenuInput::bQuitPressed);
            CHECK(f.GM->IsQuitRequested());
        }

        TEST_CASE("finishing the slice shows the end screen, and confirming restarts") {
            FHudFixture f;
            f.GM->NotifySliceCompleted();
            const FLeonHorrorHudModel model = f.Model();
            CHECK(model.Screen == ELeonHorrorScreen::Completed);
            CHECK(model.ScreenTitle == "END OF THE PROTOTYPE");
            f.Press(&FLeonHorrorMenuInput::bConfirmPressed);
            CHECK(f.GM->GetLastTravelRequest() == ELeonHorrorTravel::Restart);
        }
    }

} // namespace Leon
