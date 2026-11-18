#include <doctest/doctest.h>

#include "LeonHorrorTestSetup.hpp"

#include "Core/FProjectDescriptor.hpp"
#include "Engine/FMapSerializer.hpp"
#include "Gameplay/APawn.hpp"
#include "Gameplay/APlayerController.hpp"
#include "Gameplay/APlayerStart.hpp"

#include <filesystem>

namespace Leon {

    TEST_SUITE("LeonHorror boot") {

        TEST_CASE("the module registers the game mode class") {
            Test::RegisterLeonHorrorClasses();
            CHECK(UClassRegistry::Get().HasClass("ALeonHorrorGameMode"));
        }

        TEST_CASE("the project descriptor points at an existing map and a registered game mode") {
            Test::RegisterLeonHorrorClasses();
            FProjectDescriptor descriptor;
            REQUIRE(descriptor.Load("Games/LeonHorror/LeonHorror.lproject"));
            CHECK(descriptor.ProjectName == "LeonHorror");
            CHECK(UClassRegistry::Get().HasClass(descriptor.DefaultGameMode));
            Test::BindLeonHorrorProject();
            std::string mapFile = FProjectPaths::ResolveVirtualPath(descriptor.DefaultMap);
            if (!mapFile.ends_with(".lmap"))
                mapFile += ".lmap";
            CHECK(std::filesystem::exists(mapFile));
        }

        TEST_CASE("the game mode logs in one player and spawns a pawn that ticks") {
            Test::FHorrorWorld f;
            APlayerController* pc = f.World->GetFirstPlayerController();
            REQUIRE(pc);
            REQUIRE(pc->GetPawn());
            CHECK(f.World->GetPlayerControllers().size() == 1);

            f.TickSeconds(0.5f);
            CHECK(f.GM->GetElapsedPlaySeconds() == doctest::Approx(0.5f).epsilon(0.05));
        }

        TEST_CASE("the player spawns at the PlayerStart placed in the world") {
            Test::FHorrorWorld f(false);
            auto* start = f.World->SpawnActor<APlayerStart>("PlayerStart");
            start->SetActorLocation({4.0f, 2.0f, -6.0f});
            f.World->BeginPlay();

            APlayerController* pc = f.World->GetFirstPlayerController();
            REQUIRE(pc);
            REQUIRE(pc->GetPawn());
            const glm::vec3 delta = pc->GetPawn()->GetActorLocation() - start->GetActorLocation();
            CHECK(std::abs(delta.x) < 0.05f);
            CHECK(std::abs(delta.z) < 0.05f);
        }
    }

} // namespace Leon
