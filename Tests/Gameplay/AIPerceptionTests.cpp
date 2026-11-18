#include <doctest/doctest.h>

#include "Engine/UWorld.hpp"
#include "Gameplay/ACharacter.hpp"
#include "Gameplay/AAIController.hpp"
#include "Gameplay/UPrimitiveComponent.hpp"
#include "AI/UAIPerceptionComponent.hpp"

TEST_SUITE("AI Perception") {
    TEST_CASE("actor in FOV with clear trace is perceived") {
        auto world = Leon::UWorld::Create("PercWorld");
        auto* self = world->SpawnActor<Leon::ACharacter>("Self");
        auto* other = world->SpawnActor<Leon::ACharacter>("Other");
        auto* ai = world->SpawnActor<Leon::AAIController>("AI");
        self->SetActorLocation({0.0f, 1.7f, 0.0f});
        other->SetActorLocation({0.0f, 1.7f, -8.0f});
        world->AddAIController(ai);
        ai->Possess(self);

        auto perception = ai->GetPerceptionComponent();
        REQUIRE(perception);
        perception->UpdatePerception();
        CHECK(ai->HasLineOfSightTo(*other));
        CHECK(perception->GetCurrentTarget() == other);
        bool bFound = false;
        for (Leon::AActor* actor : ai->GetPerceivedActors()) {
            if (actor == other)
                bFound = true;
        }
        CHECK(bFound);
    }

    TEST_CASE("wall blocks line of sight") {
        auto world = Leon::UWorld::Create("PercWall");
        auto* wall = world->SpawnActor<Leon::AActor>("Wall");
        wall->SetActorLocation({0.0f, 1.5f, -4.0f});
        wall->SetActorScale({6.0f, 3.0f, 0.8f});
        auto box = wall->AddActorComponent<Leon::UBoxComponent>("Box");
        box->SetBoxExtent({0.5f, 0.5f, 0.5f});
        box->SetCollisionObjectType(Leon::ECollisionChannel::WorldStatic);

        auto* self = world->SpawnActor<Leon::ACharacter>("Self");
        auto* other = world->SpawnActor<Leon::ACharacter>("Other");
        auto* ai = world->SpawnActor<Leon::AAIController>("AI");
        self->SetActorLocation({0.0f, 1.7f, 0.0f});
        other->SetActorLocation({0.0f, 1.7f, -8.0f});
        world->AddAIController(ai);
        ai->Possess(self);

        CHECK_FALSE(ai->HasLineOfSightTo(*other));
        auto perception = ai->GetPerceptionComponent();
        REQUIRE(perception);
        CHECK_FALSE(perception->HasLineOfSight(*other));
    }

    TEST_CASE("a wall hides the target only when line of sight is required") {
        auto world = Leon::UWorld::Create("PercRequireLos");
        auto* wall = world->SpawnActor<Leon::AActor>("Wall");
        wall->SetActorLocation({0.0f, 1.5f, -4.0f});
        wall->SetActorScale({6.0f, 3.0f, 0.8f});
        auto box = wall->AddActorComponent<Leon::UBoxComponent>("Box");
        box->SetBoxExtent({0.5f, 0.5f, 0.5f});
        box->SetCollisionObjectType(Leon::ECollisionChannel::WorldStatic);

        auto* self = world->SpawnActor<Leon::ACharacter>("Self");
        auto* other = world->SpawnActor<Leon::ACharacter>("Other");
        auto* ai = world->SpawnActor<Leon::AAIController>("AI");
        self->SetActorLocation({0.0f, 1.7f, 0.0f});
        other->SetActorLocation({0.0f, 1.7f, -8.0f});
        world->AddAIController(ai);
        ai->Possess(self);
        auto perception = ai->GetPerceptionComponent();
        REQUIRE(perception);
        REQUIRE_FALSE(perception->HasLineOfSight(*other));

        // Default: line of sight is only a bonus, the actor behind the wall is still targeted.
        perception->UpdatePerception();
        CHECK(perception->GetCurrentTarget() == other);

        Leon::FAISightConfig config = perception->GetSightConfig();
        config.bRequireLineOfSight = true;
        perception->SetSightConfig(config);
        perception->UpdatePerception();
        CHECK(perception->GetCurrentTarget() == nullptr);

        // Step out from behind the wall: now it is seen.
        other->SetActorLocation({0.0f, 1.7f, -2.0f});
        perception->UpdatePerception();
        CHECK(perception->GetCurrentTarget() == other);
    }

    TEST_CASE("a noise is heard only within its radius and becomes the place to investigate") {
        auto world = Leon::UWorld::Create("PercNoise");
        auto* self = world->SpawnActor<Leon::ACharacter>("Listener");
        auto* ai = world->SpawnActor<Leon::AAIController>("AI");
        self->SetActorLocation({0.0f, 1.7f, 0.0f});
        world->AddAIController(ai);
        ai->Possess(self);
        auto perception = ai->GetPerceptionComponent();
        REQUIRE(perception);
        CHECK_FALSE(perception->HasLastKnown());

        // Too far to hear.
        CHECK_FALSE(perception->ReportNoise({30.0f, 1.7f, 0.0f}, 10.0f));
        CHECK_FALSE(perception->HasLastKnown());

        CHECK(perception->ReportNoise({6.0f, 1.7f, -3.0f}, 10.0f));
        CHECK(perception->HasLastKnown());
        CHECK(perception->GetLastKnownLocation().x == doctest::Approx(6.0f));
        CHECK(perception->GetLastKnownLocation().z == doctest::Approx(-3.0f));
        // Hearing gives a place to look at, not a target.
        CHECK(perception->GetCurrentTarget() == nullptr);

        perception->ForgetLastKnown();
        CHECK_FALSE(perception->HasLastKnown());

        // A noise exactly at the world origin is still a valid place.
        self->SetActorLocation({2.0f, 1.7f, 0.0f});
        CHECK(perception->ReportNoise({0.0f, 0.0f, 0.0f}, 10.0f));
        CHECK(perception->HasLastKnown());
    }

    TEST_CASE("a noise event reaches every listener in range except the one that made it") {
        auto world = Leon::UWorld::Create("PercNoiseEvent");
        auto spawnListener = [&](const char* InName, const glm::vec3& InLocation) {
            auto* pawn = world->SpawnActor<Leon::ACharacter>(InName);
            auto* controller = world->SpawnActor<Leon::AAIController>(std::string(InName) + "AI");
            pawn->SetActorLocation(InLocation);
            world->AddAIController(controller);
            controller->Possess(pawn);
            return controller;
        };
        auto* nearA = spawnListener("NearA", {3.0f, 1.7f, 0.0f});
        auto* nearB = spawnListener("NearB", {-4.0f, 1.7f, 2.0f});
        auto* far = spawnListener("Far", {40.0f, 1.7f, 0.0f});

        const int heard = Leon::UAIPerceptionComponent::ReportNoiseEvent(*world, {0.0f, 1.7f, 0.0f}, 8.0f,
                                                                           nearB->GetPawn());
        CHECK(heard == 1);
        CHECK(nearA->GetPerceptionComponent()->HasLastKnown());
        CHECK_FALSE(nearB->GetPerceptionComponent()->HasLastKnown()); // the instigator does not hear itself
        CHECK_FALSE(far->GetPerceptionComponent()->HasLastKnown());
    }
}
