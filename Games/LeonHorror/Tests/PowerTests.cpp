#include <doctest/doctest.h>

#include "LeonHorrorTestSetup.hpp"

#include "LeonHorror/ALeonHorrorCharacter.hpp"
#include "LeonHorror/ALeonHorrorInteractable.hpp"
#include "LeonHorror/ALeonHorrorPower.hpp"
#include "LeonHorror/FLeonHorrorSaveGame.hpp"
#include "LeonHorror/ULeonHorrorInventoryComponent.hpp"

#include "Assets/FMaterial.hpp"
#include "Assets/FMaterialInstance.hpp"
#include "Scene/Components.hpp"

#include <algorithm>

namespace Leon {

    namespace {

        /** A room wing without power: a light, a sound and an electric door on one group, and its fuse box. */
        struct FPowerFixture : Test::FHorrorWorld {
            ALeonHorrorCharacter* Player = nullptr;
            AActor* Lamp = nullptr;
            AActor* OtherLamp = nullptr;
            ALeonHorrorAmbientSound* Hum = nullptr;
            ALeonHorrorDoor* Door = nullptr;
            ALeonHorrorFuseBox* Box = nullptr;

            FPowerFixture() {
                Player = GM->GetPlayerCharacter();
                REQUIRE(Player);

                Lamp = World->SpawnActor<AActor>("Lamp");
                Lamp->SetProperty("PowerGroup", "North");
                FPointLightComponent light;
                light.Light.Intensity = 6.0f;
                Lamp->AddComponent<FPointLightComponent>(light);

                // On another circuit: nothing that happens to North may touch it.
                OtherLamp = World->SpawnActor<AActor>("OtherLamp");
                OtherLamp->AddComponent<FPointLightComponent>(light);

                Hum = World->SpawnActor<ALeonHorrorAmbientSound>("Hum");
                Hum->SetProperty("PowerGroup", "North");
                Hum->SetProperty("Sound", "/Game/Audio/AMB_Fluorescent_Hum");
                Hum->OnPropertiesLoaded();

                Door = World->SpawnActor<ALeonHorrorDoor>("ElectricDoor");
                Door->SetProperty("PowerGroup", "North");
                Door->OnPropertiesLoaded();

                Box = World->SpawnActor<ALeonHorrorFuseBox>("FuseBox");
                Box->SetProperty("Group", "North");
                Box->OnPropertiesLoaded();

                // The game mode looks at the map on its first tick, which this world is already past.
                TickSeconds(0.05f);
                GM->RescanPower();
            }

            bool LampOn() const { return Lamp->GetComponent<FPointLightComponent>().bEnabled; }
        };

    } // namespace

    TEST_SUITE("LeonHorror power") {

        TEST_CASE("a wing starts without power: lights off, sounds silent, electric doors shut") {
            FPowerFixture f;
            CHECK_FALSE(f.GM->IsPowered("North"));
            CHECK_FALSE(f.LampOn());
            CHECK(f.OtherLamp->GetComponent<FPointLightComponent>().bEnabled);
            CHECK_FALSE(f.Hum->IsPlaying());

            CHECK(f.Door->NeedsPower());
            CHECK(f.Door->GetInteractPrompt(*f.Player) == "No power");
            CHECK_FALSE(f.Door->Interact(*f.Player));
            CHECK_FALSE(f.Door->IsOpen());
        }

        TEST_CASE("the fuse box needs its fuse, uses it up and brings the wing back") {
            FPowerFixture f;
            CHECK(f.Box->GetInteractPrompt(*f.Player) == "A fuse is missing");
            CHECK_FALSE(f.Box->Interact(*f.Player));
            CHECK_FALSE(f.GM->IsPowered("North"));

            f.Player->GetInventory()->AddItem("Fuse");
            CHECK(f.Box->GetInteractPrompt(*f.Player) == "Fit the Fuse");
            REQUIRE(f.Box->Interact(*f.Player));
            CHECK_FALSE(f.Player->GetInventory()->HasItem("Fuse"));

            CHECK(f.GM->IsPowered("North"));
            CHECK(f.Box->IsLive());
            CHECK(f.LampOn());
            CHECK(f.Hum->IsPlaying());
            CHECK(f.Box->GetInteractPrompt(*f.Player) == "The power is on");
            // A second fuse is not swallowed by a board that is already live.
            f.Player->GetInventory()->AddItem("Fuse");
            CHECK_FALSE(f.Box->Interact(*f.Player));
            CHECK(f.Player->GetInventory()->HasItem("Fuse"));

            // And the electric door now works like any other.
            CHECK(f.Door->GetInteractPrompt(*f.Player) == "Open door");
            CHECK(f.Door->Interact(*f.Player));
            CHECK(f.Door->IsOpen());
        }

        TEST_CASE("a fitting stops glowing with its group and glows again with it") {
            FPowerFixture f;
            AActor* fitting = f.World->SpawnActor<AActor>("Fitting");
            fitting->SetProperty("PowerGroup", "North");
            auto base = MakeRef<FMaterial>("Diffuser");
            base->SetEmissiveIntensity(6.0f);
            auto instance = base->CreateInstance();
            FStaticMeshComponent mesh;
            mesh.MaterialOverrides.push_back(instance);
            fitting->AddComponent<FStaticMeshComponent>(mesh);
            REQUIRE(instance->GetEmissiveIntensity() == doctest::Approx(6.0f));

            f.GM->SetPowerGroup("North", false);
            CHECK(instance->GetEmissiveIntensity() == doctest::Approx(0.0f));
            f.GM->SetPowerGroup("North", true);
            CHECK(instance->GetEmissiveIntensity() == doctest::Approx(6.0f));
        }

        TEST_CASE("which groups are live is saved and restored") {
            FPowerFixture f;
            f.GM->SetPowerGroup("North", true);
            const FLeonHorrorSaveGame save = f.GM->CaptureSave(*f.Player);
            CHECK(save.PoweredGroups.count("North") == 1);

            FLeonHorrorSaveGame read;
            REQUIRE(FLeonHorrorSaveGame::FromText(save.ToText(), read));
            CHECK(read.PoweredGroups == save.PoweredGroups);

            // Loading a save made before the fuse was fitted takes the power away again.
            f.GM->ApplySave(FLeonHorrorSaveGame{});
            CHECK_FALSE(f.GM->IsPowered("North"));
            CHECK_FALSE(f.LampOn());
            f.GM->ApplySave(read);
            CHECK(f.GM->IsPowered("North"));
            CHECK(f.LampOn());
            CHECK(f.Hum->IsPlaying());
        }

        TEST_CASE("a flickering light wavers around its strength and never exceeds it") {
            FPowerFixture f;
            AActor* tube = f.World->SpawnActor<AActor>("Tube");
            tube->SetProperty("Flicker", "0.8");
            FPointLightComponent light;
            light.Light.Intensity = 5.0f;
            tube->AddComponent<FPointLightComponent>(light);
            // The game mode gathered its lights before this one existed: have it look again.
            f.GM->RescanPower();

            float low = 1.0e9f;
            float high = 0.0f;
            for (int i = 0; i < 600; ++i) {
                f.TickSeconds(1.0f / 60.0f);
                const float intensity = tube->GetComponent<FPointLightComponent>().Light.Intensity;
                low = std::min(low, intensity);
                high = std::max(high, intensity);
            }
            CHECK(high <= 5.0f + 1.0e-4f);
            CHECK(high > 4.0f);
            CHECK(low < 2.0f); // it does drop out now and then
            CHECK(low > 0.0f);
            // A steady light is left alone.
            CHECK(f.OtherLamp->GetComponent<FPointLightComponent>().Light.Intensity == doctest::Approx(6.0f));
        }

        TEST_CASE("an ambient sound plays while enabled and in play") {
            FPowerFixture f;
            auto* drip = f.World->SpawnActor<ALeonHorrorAmbientSound>("Drip");
            CHECK_FALSE(drip->IsPlaying()); // nothing to play yet
            drip->SetSound("/Game/Audio/AMB_Drip", 0.8f, 12.0f);
            CHECK(drip->IsPlaying());
            drip->SetEnabled(false);
            CHECK_FALSE(drip->IsPlaying());
            drip->SetEnabled(true);
            CHECK(drip->IsPlaying());
        }
    }

} // namespace Leon
