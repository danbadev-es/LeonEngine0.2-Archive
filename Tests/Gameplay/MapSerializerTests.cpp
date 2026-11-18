#include "Engine/Components.hpp"
#include "Engine/FMapSerializer.hpp"
#include "Engine/UWorld.hpp"
#include "Gameplay/AActor.hpp"

#include <doctest/doctest.h>

#include <string>

// Robustness of the .lmap reader: a malformed or unsupported file must be refused as a whole and must
// leave the world it was being loaded into exactly as it was.

namespace Leon {

    namespace {

        size_t CountNamed(const UWorld& InWorld, const std::string& InName) {
            size_t count = 0;
            for (const auto& actor : InWorld.GetAllActors()) {
                if (actor && actor->GetName() == InName)
                    ++count;
            }
            return count;
        }

        /** World with one recognisable actor, standing in for the map the user had open. */
        TRef<UWorld> MakeOpenWorld() {
            auto world = UWorld::Create("OpenMap");
            AActor* keeper = world->SpawnActor<AActor>("Keeper");
            keeper->SetActorLocation({1.0f, 2.0f, 3.0f});
            return world;
        }

        void CheckWorldUntouched(const TRef<UWorld>& InWorld) {
            CHECK(InWorld->GetName() == "OpenMap");
            REQUIRE(CountNamed(*InWorld, "Keeper") == 1);
            CHECK(InWorld->FindActorByName("Keeper")->GetActorLocation().y == doctest::Approx(2.0f));
        }

    } // namespace

    TEST_SUITE("Map serializer reader") {

        TEST_CASE("a malformed scalar refuses the file and leaves the world untouched") {
            const char* kBad = R"(
Map:
  Name: "Broken"
  Version: "2.1"
Actors:
  - Name: "Light"
    Transform:
      Translation: [0, 0, 0]
      Rotation: [0, 0, 0]
      Scale: [1, 1, 1]
    PointLight:
      Enabled: true
      Color: [1, 1, 1]
      Intensity: eight
      Radius: 10
)";
            auto world = MakeOpenWorld();
            FMapSerializer s(world);
            CHECK_FALSE(s.DeserializeText(kBad));
            CheckWorldUntouched(world);
        }

        TEST_CASE("a malformed vector component refuses the file instead of silently becoming zero") {
            const char* kBad = R"(
Map:
  Name: "Broken"
Actors:
  - Name: "Prop"
    Transform:
      Translation: [4.0, oops, 6.0]
      Rotation: [0, 0, 0]
      Scale: [1, 1, 1]
)";
            auto world = MakeOpenWorld();
            FMapSerializer s(world);
            CHECK_FALSE(s.DeserializeText(kBad));
            CheckWorldUntouched(world);
        }

        TEST_CASE("a malformed world setting refuses the file") {
            const char* kBad = R"(
Map:
  Name: "Broken"
Environment:
  WorldSettings:
    LightmapResolution: sixty-four
Actors:
)";
            auto world = MakeOpenWorld();
            FMapSerializer s(world);
            CHECK_FALSE(s.DeserializeText(kBad));
            CheckWorldUntouched(world);
        }

        TEST_CASE("the map name is applied only when the load succeeds") {
            auto world = MakeOpenWorld();
            FMapSerializer s(world);

            // Name appears before the bad value: it must not leak into the world.
            CHECK_FALSE(s.DeserializeText("Map:\n  Name: \"Leaked\"\nActors:\n  - Name: \"A\"\n    TeamIndex: x\n"));
            CHECK(world->GetName() == "OpenMap");

            REQUIRE(s.DeserializeText("Map:\n  Name: \"Loaded\"\nActors:\n  - Name: \"A\"\n"));
            CHECK(world->GetName() == "Loaded");
            CHECK(CountNamed(*world, "Keeper") == 0);
            CHECK(CountNamed(*world, "A") == 1);
        }

        TEST_CASE("files without a version and with the current version load") {
            auto world = UWorld::Create("V");
            FMapSerializer s(world);
            CHECK(s.DeserializeText("Map:\n  Name: \"NoVersion\"\nActors:\n  - Name: \"A\"\n"));
            CHECK(s.DeserializeText("Map:\n  Name: \"Current\"\n  Version: \"2.1\"\nActors:\n  - Name: \"A\"\n"));
            CHECK(s.DeserializeText("Map:\n  Name: \"Minor\"\n  Version: \"2.9\"\nActors:\n  - Name: \"A\"\n"));
            CHECK(world->GetName() == "Minor");
        }

        TEST_CASE("a newer major version is rejected and the world is untouched") {
            auto world = MakeOpenWorld();
            FMapSerializer s(world);
            CHECK_FALSE(s.DeserializeText("Map:\n  Name: \"Future\"\n  Version: \"3.0\"\nActors:\n  - Name: \"A\"\n"));
            CheckWorldUntouched(world);
        }

        TEST_CASE("duplicate GUIDs are made unique on load") {
            const char* kDup = R"(
Map:
  Name: "Dups"
Actors:
  - Name: "First"
    GUID: "00000000000000aa00000000000000bb"
  - Name: "Second"
    GUID: "00000000000000aa00000000000000bb"
  - Name: "Twin"
  - Name: "Twin"
)";
            auto world = UWorld::Create("Dups");
            FMapSerializer s(world);
            REQUIRE(s.DeserializeText(kDup));

            AActor* first = world->FindActorByName("First");
            AActor* second = world->FindActorByName("Second");
            REQUIRE(first);
            REQUIRE(second);
            // The first occurrence keeps the authored GUID; the copy gets a new valid one.
            CHECK(first->GetActorGuid() == FUUID::FromString("00000000000000aa00000000000000bb"));
            CHECK(second->GetActorGuid().IsValid());
            CHECK_FALSE(second->GetActorGuid() == first->GetActorGuid());

            // Actors without a GUID derive one from their name; same-named actors must still differ.
            std::vector<FUUID> twins;
            for (const auto& actor : world->GetAllActors()) {
                if (actor && actor->GetName() == "Twin")
                    twins.push_back(actor->GetActorGuid());
            }
            REQUIRE(twins.size() == 2);
            CHECK(twins[0].IsValid());
            CHECK(twins[1].IsValid());
            CHECK_FALSE(twins[0] == twins[1]);
        }

        TEST_CASE("a valid file still round-trips after the hardening") {
            auto src = UWorld::Create("RoundTrip");
            AActor* lamp = src->SpawnActor<AActor>("Lamp");
            lamp->SetActorLocation({5.0f, 3.0f, -2.0f});
            lamp->AddComponent<FPointLightComponent>();

            std::string text;
            REQUIRE(FMapSerializer(src).SerializeText(text));

            auto dst = MakeOpenWorld();
            REQUIRE(FMapSerializer(dst).DeserializeText(text));
            CHECK(dst->GetName() == "RoundTrip");
            AActor* restored = dst->FindActorByName("Lamp");
            REQUIRE(restored);
            CHECK(restored->GetActorLocation().z == doctest::Approx(-2.0f));
            CHECK(restored->HasComponent<FPointLightComponent>());
            CHECK(restored->GetActorGuid() == lamp->GetActorGuid());
        }
    }

} // namespace Leon
