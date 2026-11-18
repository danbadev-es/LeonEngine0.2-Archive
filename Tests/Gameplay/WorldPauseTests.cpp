#include "Engine/UWorld.hpp"
#include "Gameplay/AActor.hpp"
#include "Gameplay/AGameModeBase.hpp"
#include "Gameplay/AHUD.hpp"
#include "Gameplay/APlayerController.hpp"

#include <doctest/doctest.h>

namespace Leon {

    namespace {
        class ATickCounter : public AActor {
        public:
            ATickCounter(entt::entity InHandle, UWorld* InWorld, const std::string& InName = "Counter")
                : AActor(InHandle, InWorld, InName) {}
            void Tick(float InDelta) override {
                ++Ticks;
                Seconds += InDelta;
            }
            int Ticks = 0;
            float Seconds = 0.0f;
        };

        struct FPauseWorld {
            TRef<UWorld> World;
            FPauseWorld() {
                World = UWorld::Create("PauseWorld");
                auto* gm = World->SpawnActor<AGameModeBase>("GM");
                World->SetGameMode(gm);
                World->InitWorld();
                gm->HUDClass = "None";
                gm->DefaultPawnClass = "None";
                World->BeginPlay();
            }
            void Tick(int InFrames = 1) {
                for (int i = 0; i < InFrames; ++i)
                    World->Tick(FTimestep(1.0f / 60.0f));
            }
        };
    } // namespace

    TEST_SUITE("World pause") {

        TEST_CASE("pausing stops ordinary actors and resuming continues them") {
            FPauseWorld f;
            auto* counter = f.World->SpawnActor<ATickCounter>("Counter");
            f.Tick(3);
            CHECK(counter->Ticks == 3);

            f.World->SetPaused(true);
            CHECK(f.World->IsPaused());
            f.Tick(5);
            CHECK(counter->Ticks == 3);

            f.World->SetPaused(false);
            f.Tick(2);
            CHECK(counter->Ticks == 5);
        }

        TEST_CASE("actors that opted in keep ticking while paused") {
            FPauseWorld f;
            auto* ui = f.World->SpawnActor<ATickCounter>("UI");
            ui->SetTickWhenPaused(true);
            auto* world = f.World->SpawnActor<ATickCounter>("World");

            f.World->SetPaused(true);
            f.Tick(4);
            CHECK(ui->Ticks == 4);
            CHECK(world->Ticks == 0);
        }

        TEST_CASE("player controllers and HUDs tick while paused by default") {
            FPauseWorld f;
            APlayerController* pc = f.World->GetFirstPlayerController();
            REQUIRE(pc);
            CHECK(pc->ShouldTickWhenPaused());
            auto* hud = f.World->SpawnActor<AHUD>("HUD");
            CHECK(hud->ShouldTickWhenPaused());
            CHECK_FALSE(f.World->SpawnActor<AActor>("Plain")->ShouldTickWhenPaused());
        }

        TEST_CASE("timers do not advance while paused") {
            FPauseWorld f;
            int fired = 0;
            FTimerHandle handle;
            f.World->GetTimerManager().SetTimer(handle, [&fired]() { ++fired; }, 0.05f, false);

            f.World->SetPaused(true);
            f.Tick(30); // 0.5 s of wall time, ten times the timer delay
            CHECK(fired == 0);

            f.World->SetPaused(false);
            f.Tick(6);
            CHECK(fired == 1);
        }

        TEST_CASE("destroying an actor while paused still removes it") {
            FPauseWorld f;
            auto* doomed = f.World->SpawnActor<ATickCounter>("Doomed");
            f.World->SetPaused(true);
            {
                UWorld::FActorIterationScope scope(*f.World);
                f.World->DestroyActor(doomed);
            }
            f.Tick(1);
            CHECK(f.World->FindActorByName("Doomed") == nullptr);
        }

        TEST_CASE("clearing the world resets the pause state") {
            FPauseWorld f;
            f.World->SetPaused(true);
            f.World->Clear();
            CHECK_FALSE(f.World->IsPaused());
        }
    }

} // namespace Leon
