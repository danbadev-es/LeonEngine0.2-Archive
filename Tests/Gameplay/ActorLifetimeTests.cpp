#include "AI/UAIPerceptionComponent.hpp"
#include "Engine/UWorld.hpp"
#include "Gameplay/AAIController.hpp"
#include "Gameplay/AActor.hpp"
#include "Gameplay/ACharacter.hpp"
#include "Gameplay/AGameModeBase.hpp"
#include "Gameplay/TWeakObjectPtr.hpp"
#include "Gameplay/UActorComponent.hpp"
#include "Gameplay/UGameplayStatics.hpp"
#include "Gameplay/UHealthComponent.hpp"
#include "Gameplay/UInventoryComponent.hpp"
#include "Gameplay/UPrimitiveComponent.hpp"

#include <doctest/doctest.h>

#include <string>
#include <vector>

// Regression tests for actor lifetime: every case here used to mutate a container that was being
// iterated (undefined behaviour) or left a raw pointer to a freed actor.

namespace Leon {

    namespace {

        size_t CountActorsNamed(const UWorld& InWorld, const std::string& InName) {
            size_t count = 0;
            for (const auto& actor : InWorld.GetAllActors()) {
                if (actor && actor->GetName() == InName)
                    ++count;
            }
            return count;
        }

        /** Destroys a list of victims from its own EndPlay. */
        class AEndPlayDestroyer : public AActor {
        public:
            AEndPlayDestroyer(entt::entity InHandle, UWorld* InWorld, const std::string& InName = "Destroyer")
                : AActor(InHandle, InWorld, InName) {}
            void EndPlay() override {
                ++EndPlayCalls;
                for (AActor* victim : Victims) {
                    if (victim)
                        victim->Destroy();
                }
                Victims.clear();
                // Destroying ourselves from EndPlay must not re-enter EndPlay.
                if (bDestroySelf)
                    Destroy();
            }
            std::vector<AActor*> Victims;
            bool bDestroySelf = false;
            int EndPlayCalls = 0;
        };

        /** Spawns extra actors from BeginPlay (as a character spawning its weapon does). */
        class ABeginPlaySpawner : public AActor {
        public:
            ABeginPlaySpawner(entt::entity InHandle, UWorld* InWorld, const std::string& InName = "Spawner")
                : AActor(InHandle, InWorld, InName) {}
            void BeginPlay() override {
                for (int i = 0; i < SpawnCount; ++i)
                    Spawned.push_back(GetWorld()->SpawnActor<AActor>("SpawnedInBeginPlay"));
            }
            int SpawnCount = 64; // enough to force the actor vector to reallocate
            std::vector<AActor*> Spawned;
        };

        /** Component that destroys its owner on the Nth tick and counts ticks after that. */
        class UOwnerKillerComponent : public UActorComponent {
        public:
            explicit UOwnerKillerComponent(const std::string& InName = "Killer") : UActorComponent(InName) {}
            void Tick(float) override {
                ++TickCount;
                if (TickCount == 1 && Owner)
                    Owner->Destroy();
            }
            void EndPlay() override { ++EndPlayCount; }
            int TickCount = 0;
            int EndPlayCount = 0;
        };

        class UCountingComponent : public UActorComponent {
        public:
            explicit UCountingComponent(const std::string& InName = "Counter") : UActorComponent(InName) {}
            void Tick(float) override { ++TickCount; }
            int TickCount = 0;
        };

        struct FLifetimeWorld {
            TRef<UWorld> World;
            AGameModeBase* GM = nullptr;

            explicit FLifetimeWorld(bool bBeginPlay = true) {
                World = UWorld::Create("LifetimeWorld");
                GM = World->SpawnActor<AGameModeBase>("GM");
                World->SetGameMode(GM);
                World->InitWorld();
                GM->HUDClass = "None";
                GM->DefaultPawnClass = "None";
                if (bBeginPlay)
                    World->BeginPlay();
            }
        };

        TRef<UBoxComponent> AddOverlapBox(AActor* InActor, bool bGenerate) {
            auto box = InActor->AddActorComponent<UBoxComponent>("Box");
            box->UnregisterPhysics();
            InActor->SetRootComponent(box.get());
            box->SetBoxExtent({0.5f, 0.5f, 0.5f});
            box->SetGenerateOverlapEvents(bGenerate);
            box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
            box->SetCollisionResponseToAllChannels(ECollisionResponse::Overlap);
            box->RegisterPhysics();
            return box;
        }

    } // namespace

    TEST_SUITE("Actor lifetime") {

        TEST_CASE("destroying actors from EndPlay during world EndPlay is safe") {
            FLifetimeWorld f;
            // Victims are placed both before and after the destroyer in the actor list.
            AActor* before = f.World->SpawnActor<AActor>("VictimBefore");
            auto* destroyer = f.World->SpawnActor<AEndPlayDestroyer>("Destroyer");
            AActor* after = f.World->SpawnActor<AActor>("VictimAfter");
            destroyer->Victims = {before, after};
            destroyer->bDestroySelf = true;

            f.World->EndPlay();

            CHECK(CountActorsNamed(*f.World, "VictimBefore") == 0);
            CHECK(CountActorsNamed(*f.World, "VictimAfter") == 0);
            CHECK(CountActorsNamed(*f.World, "Destroyer") == 0);
            CHECK_FALSE(f.World->IsIteratingActors());
        }

        TEST_CASE("EndPlay runs once even when the actor destroys itself inside it") {
            FLifetimeWorld f;
            auto* destroyer = f.World->SpawnActor<AEndPlayDestroyer>("Destroyer");
            destroyer->bDestroySelf = true;
            const auto keep = f.World->GetAllActors(); // keep the object alive to read the counter
            f.World->EndPlay();
            CHECK(destroyer->EndPlayCalls == 1);
        }

        TEST_CASE("inventory destroying its items on world EndPlay is safe") {
            FLifetimeWorld f;
            AActor* owner = f.World->SpawnActor<AActor>("Owner");
            auto inv = owner->AddActorComponent<UInventoryComponent>("Inventory");
            inv->SetSlotCount(4);
            for (uint32_t i = 0; i < 4; ++i)
                CHECK(inv->GiveItem(i, f.World->SpawnActor<AActor>("Item")));

            f.World->EndPlay();

            CHECK(CountActorsNamed(*f.World, "Item") == 0);
            CHECK(CountActorsNamed(*f.World, "Owner") == 1);
        }

        TEST_CASE("spawning actors from BeginPlay during world BeginPlay is safe") {
            FLifetimeWorld f(false);
            auto* spawner = f.World->SpawnActor<ABeginPlaySpawner>("Spawner");
            f.World->SpawnActor<AActor>("Tail");

            f.World->BeginPlay();

            REQUIRE(spawner->Spawned.size() == 64);
            CHECK(CountActorsNamed(*f.World, "SpawnedInBeginPlay") == 64);
            for (AActor* spawned : spawner->Spawned) {
                REQUIRE(spawned);
                CHECK(spawned->HasBegunPlay());
            }
            AActor* tail = f.World->FindActorByName("Tail");
            REQUIRE(tail);
            CHECK(tail->HasBegunPlay());
        }

        TEST_CASE("a component destroying its owner from Tick stops the tick and frees nothing early") {
            FLifetimeWorld f;
            AActor* actor = f.World->SpawnActor<AActor>("Doomed");
            auto killer = actor->AddActorComponent<UOwnerKillerComponent>("Killer");
            auto counter = actor->AddActorComponent<UCountingComponent>("Counter");

            f.World->Tick(FTimestep(1.0f / 60.0f));

            // The killer ran once and got its EndPlay; the component after it never ticked.
            CHECK(killer->TickCount == 1);
            CHECK(killer->EndPlayCount == 1);
            CHECK(counter->TickCount == 0);
            // Removal was deferred to the end of the tick, then applied.
            CHECK(CountActorsNamed(*f.World, "Doomed") == 0);

            f.World->Tick(FTimestep(1.0f / 60.0f));
            CHECK(killer->TickCount == 1);
        }

        TEST_CASE("destroy outside any iteration stays immediate") {
            FLifetimeWorld f;
            f.World->SpawnActor<AActor>("Gone");
            REQUIRE(CountActorsNamed(*f.World, "Gone") == 1);
            f.World->DestroyActor(f.World->FindActorByName("Gone"));
            CHECK(CountActorsNamed(*f.World, "Gone") == 0);
        }

        TEST_CASE("destroy inside an iteration scope is deferred until the outermost scope ends") {
            FLifetimeWorld f;
            AActor* actor = f.World->SpawnActor<AActor>("Deferred");
            {
                UWorld::FActorIterationScope outer(*f.World);
                {
                    UWorld::FActorIterationScope inner(*f.World);
                    f.World->DestroyActor(actor);
                    CHECK(actor->IsPendingKill());
                    CHECK(CountActorsNamed(*f.World, "Deferred") == 1);
                }
                // Inner scope ended but the outer one is still alive.
                CHECK(CountActorsNamed(*f.World, "Deferred") == 1);
                CHECK(f.World->FindActorByName("Deferred") == nullptr); // pending-kill actors are not found
            }
            CHECK(CountActorsNamed(*f.World, "Deferred") == 0);
        }

        TEST_CASE("an actor destroying itself inside its own overlap callback is safe") {
            FLifetimeWorld f;
            AActor* pickup = f.World->SpawnActor<AActor>("Pickup");
            AActor* walker = f.World->SpawnActor<AActor>("Walker");
            auto pickupBox = AddOverlapBox(pickup, true);
            AddOverlapBox(walker, false);

            int firstCallbackCalls = 0;
            int secondCallbackCalls = 0;
            pickupBox->OnComponentBeginOverlap.Add(
                [&](UPrimitiveComponent*, AActor*, UPrimitiveComponent*, const FHitResult&) {
                    ++firstCallbackCalls;
                    pickup->Destroy(); // EndPlay clears the live callback vector while we are inside it
                });
            pickupBox->OnComponentBeginOverlap.Add(
                [&](UPrimitiveComponent*, AActor*, UPrimitiveComponent*, const FHitResult&) {
                    ++secondCallbackCalls;
                });

            pickup->SetActorLocation({0.0f, 0.0f, 0.0f});
            walker->SetActorLocation({0.2f, 0.0f, 0.0f});
            f.World->Tick(FTimestep(1.0f / 60.0f));

            CHECK(firstCallbackCalls == 1);
            // The owner was destroyed by the first callback; later callbacks for it are skipped.
            CHECK(secondCallbackCalls == 0);
            CHECK(CountActorsNamed(*f.World, "Pickup") == 0);
            CHECK(CountActorsNamed(*f.World, "Walker") == 1);

            // The surviving actor keeps ticking without touching the dead one.
            f.World->Tick(FTimestep(1.0f / 60.0f));
            CHECK(firstCallbackCalls == 1);
        }

        TEST_CASE("radial damage whose death callbacks destroy and spawn actors is safe") {
            FLifetimeWorld f;
            int deaths = 0;
            for (int i = 0; i < 8; ++i) {
                AActor* target = f.World->SpawnActor<AActor>("Target");
                target->SetActorLocation({static_cast<float>(i) * 0.25f, 0.0f, 0.0f});
                auto health = target->AddActorComponent<UHealthComponent>("Health");
                health->SetMaxHealth(10.0f);
                health->SetHealth(10.0f);
                UWorld* world = f.World.get();
                health->OnDeath.Add([&deaths, world, target](const FDamageInfo&) {
                    ++deaths;
                    world->SpawnActor<AActor>("Debris"); // grows the actor list mid-iteration
                    target->Destroy();                   // immediate erase outside a tick
                });
            }

            const int32_t hits = UGameplayStatics::ApplyRadialDamage(f.World.get(), 100.0f, {0.0f, 0.0f, 0.0f}, 50.0f,
                                                                     nullptr, nullptr, 100.0f, nullptr);

            CHECK(hits == 8);
            CHECK(deaths == 8);
            CHECK(CountActorsNamed(*f.World, "Target") == 0);
            CHECK(CountActorsNamed(*f.World, "Debris") == 8);
        }

        TEST_CASE("destroying an attached parent or child unlinks both sides") {
            FLifetimeWorld f;
            AActor* parent = f.World->SpawnActor<AActor>("Parent");
            AActor* childA = f.World->SpawnActor<AActor>("ChildA");
            AActor* childB = f.World->SpawnActor<AActor>("ChildB");
            childA->AttachToActor(parent);
            childB->AttachToActor(parent);
            REQUIRE(parent->GetAttachedActors().size() == 2);

            SUBCASE("destroying a child removes it from the parent") {
                f.World->DestroyActor(childA);
                REQUIRE(parent->GetAttachedActors().size() == 1);
                CHECK(parent->GetAttachedActors()[0] == childB);
            }

            SUBCASE("destroying the parent clears the children's parent pointer") {
                f.World->DestroyActor(parent);
                CHECK(childA->GetAttachParentActor() == nullptr);
                CHECK(childB->GetAttachParentActor() == nullptr);
            }
        }

        TEST_CASE("attachment refuses cycles") {
            FLifetimeWorld f;
            AActor* a = f.World->SpawnActor<AActor>("A");
            AActor* b = f.World->SpawnActor<AActor>("B");
            AActor* c = f.World->SpawnActor<AActor>("C");
            b->AttachToActor(a);
            c->AttachToActor(b);

            a->AttachToActor(c); // would make a -> c -> b -> a
            CHECK(a->GetAttachParentActor() == nullptr);
            a->AttachToActor(a);
            CHECK(a->GetAttachParentActor() == nullptr);
            CHECK(c->GetAttachParentActor() == b);
        }

        TEST_CASE("weak object pointer resolves to null once the actor is gone") {
            FLifetimeWorld f;
            AActor* actor = f.World->SpawnActor<AActor>("Tracked");
            FActorHandle handle(actor);
            CHECK(handle.Get() == actor);
            CHECK(handle.IsValid());
            CHECK_FALSE(handle.IsStale());

            SUBCASE("pending-kill counts as gone even while the object still exists") {
                UWorld::FActorIterationScope scope(*f.World);
                f.World->DestroyActor(actor);
                CHECK(CountActorsNamed(*f.World, "Tracked") == 1); // deferred: still in the world
                CHECK(handle.Get() == nullptr);
                CHECK(handle.IsStale());
            }

            SUBCASE("released object") {
                f.World->DestroyActor(actor); // immediate: the object is freed
                CHECK(handle.Get() == nullptr);
                CHECK_FALSE(handle.IsValid());
                CHECK(handle.IsStale());
                CHECK(handle.Pin() == nullptr);
            }

            SUBCASE("pin keeps the target alive across a destroy") {
                TRef<AActor> pinned = handle.Pin();
                REQUIRE(pinned);
                f.World->DestroyActor(actor);
                CHECK(pinned->IsPendingKill()); // still readable through the pinned reference
                CHECK(handle.Get() == nullptr);
            }

            SUBCASE("reset and null assignment") {
                handle = nullptr;
                CHECK(handle.Get() == nullptr);
                CHECK_FALSE(handle.IsStale());
            }
        }

        TEST_CASE("weak object pointer to an object not owned by a shared pointer is null") {
            UCountingComponent onStack("Stack");
            TWeakObjectPtr<UCountingComponent> ref(&onStack);
            CHECK(ref.Get() == nullptr);
        }

        TEST_CASE("overlap partner destroyed between frames ends the overlap without touching it") {
            FLifetimeWorld f;
            AActor* sensor = f.World->SpawnActor<AActor>("Sensor");
            AActor* visitor = f.World->SpawnActor<AActor>("Visitor");
            auto sensorBox = AddOverlapBox(sensor, true);
            AddOverlapBox(visitor, false);

            int begins = 0;
            int ends = 0;
            AActor* endOther = sensor; // sentinel: must be overwritten with null
            sensorBox->OnComponentBeginOverlap.Add(
                [&](UPrimitiveComponent*, AActor*, UPrimitiveComponent*, const FHitResult&) { ++begins; });
            sensorBox->OnComponentEndOverlap.Add(
                [&](UPrimitiveComponent*, AActor* InOther, UPrimitiveComponent* InOtherComp, const FHitResult&) {
                    ++ends;
                    endOther = InOther;
                    CHECK(InOtherComp == nullptr);
                });

            sensor->SetActorLocation({0.0f, 0.0f, 0.0f});
            visitor->SetActorLocation({0.2f, 0.0f, 0.0f});
            f.World->Tick(FTimestep(1.0f / 60.0f));
            REQUIRE(begins == 1);
            CHECK(sensorBox->GetOverlappingComponents().size() == 1);

            // Destroyed outside a tick: the visitor and its component are freed right here.
            f.World->DestroyActor(visitor);
            CHECK(sensorBox->GetOverlappingComponents().empty());

            f.World->Tick(FTimestep(1.0f / 60.0f));
            CHECK(ends == 1);
            CHECK(endOther == nullptr);
            CHECK(begins == 1);
        }

        TEST_CASE("perception drops a target that is destroyed between updates") {
            auto world = UWorld::Create("PerceptionLifetime");
            auto* self = world->SpawnActor<ACharacter>("Self");
            auto* other = world->SpawnActor<ACharacter>("Other");
            auto* ai = world->SpawnActor<AAIController>("AI");
            self->SetActorLocation({0.0f, 1.7f, 0.0f});
            other->SetActorLocation({0.0f, 1.7f, -8.0f});
            world->AddAIController(ai);
            ai->Possess(self);

            auto perception = ai->GetPerceptionComponent();
            REQUIRE(perception);
            perception->UpdatePerception();
            REQUIRE(perception->GetCurrentTarget() == other);
            REQUIRE(perception->GetPerceivedActors().size() == 1);

            world->DestroyActor(other); // freed immediately; no perception update in between

            CHECK(perception->GetCurrentTarget() == nullptr);
            CHECK(perception->GetPerceivedActors().empty());
            perception->UpdatePerception(); // used to dereference the stale pointer
            CHECK(perception->GetCurrentTarget() == nullptr);
        }

        TEST_CASE("components survive EndPlay and begin play again on the next BeginPlay") {
            FLifetimeWorld f;
            AActor* actor = f.World->SpawnActor<AActor>("Persistent");
            auto counter = actor->AddActorComponent<UCountingComponent>("Counter");
            REQUIRE(counter->HasBegunPlay());

            f.World->EndPlay();
            CHECK(actor->GetActorComponents().size() == 1);
            CHECK_FALSE(counter->HasBegunPlay());

            f.World->BeginPlay();
            CHECK(counter->HasBegunPlay());
            f.World->Tick(FTimestep(1.0f / 60.0f));
            CHECK(counter->TickCount == 1);
        }
    }

} // namespace Leon
