#include "Scene/Components.hpp"
#include "Engine/FMapSerializer.hpp"
#include "Engine/UWorld.hpp"
#include "Gameplay/AActor.hpp"
#include "Gameplay/ABlockingVolume.hpp"
#include "Gameplay/APlayerStart.hpp"
#include "Gameplay/ATriggerVolume.hpp"
#include "Gameplay/UClassRegistry.hpp"

#include <doctest/doctest.h>

#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

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

    namespace {
        /** Reads its authored data in the load hook, as a door or a pickup does. */
        class ATestPropertyActor : public AActor {
        public:
            ATestPropertyActor(entt::entity InHandle, UWorld* InWorld, const std::string& InName = "PropertyActor")
                : AActor(InHandle, InWorld, InName) {
                SetClass("ATestPropertyActor");
            }
            void OnPropertiesLoaded() override {
                ++LoadedCalls;
                RequiredKey = GetProperty("RequiredKey", "none");
                Amount = GetPropertyInt("Amount", -1);
                bLocked = GetPropertyBool("Locked", false);
                // The hook must run after components and transform are restored.
                LocationAtLoad = GetActorLocation();
                bHadLightAtLoad = HasComponent<FPointLightComponent>();
            }
            int LoadedCalls = 0;
            std::string RequiredKey;
            int32_t Amount = 0;
            bool bLocked = false;
            glm::vec3 LocationAtLoad{0.0f};
            bool bHadLightAtLoad = false;
        };
    } // namespace

    TEST_SUITE("Actor properties") {

        TEST_CASE("keys are validated and typed getters fall back to defaults") {
            auto world = UWorld::Create("Props");
            AActor* actor = world->SpawnActor<AActor>("A");
            CHECK(actor->SetProperty("RequiredKey", "red"));
            CHECK(actor->SetProperty("Door.Open_2", "true"));
            CHECK_FALSE(actor->SetProperty("", "x"));
            CHECK_FALSE(actor->SetProperty("2fast", "x"));
            CHECK_FALSE(actor->SetProperty("has space", "x"));
            CHECK_FALSE(actor->SetProperty("colon:key", "x"));
            CHECK_FALSE(actor->SetProperty("quote\"key", "x"));
            CHECK(actor->GetProperties().size() == 2);

            CHECK(actor->GetProperty("RequiredKey") == "red");
            CHECK(actor->GetProperty("Missing", "fallback") == "fallback");
            CHECK(actor->GetPropertyBool("Door.Open_2"));
            CHECK(actor->GetPropertyBool("Missing", true));
            actor->SetProperty("Amount", "12");
            actor->SetProperty("Speed", "1.5");
            actor->SetProperty("Junk", "abc");
            CHECK(actor->GetPropertyInt("Amount") == 12);
            CHECK(actor->GetPropertyFloat("Speed") == doctest::Approx(1.5f));
            CHECK(actor->GetPropertyInt("Junk", 7) == 7);
            CHECK(actor->GetPropertyFloat("Junk", 2.0f) == doctest::Approx(2.0f));

            actor->RemoveProperty("Amount");
            CHECK_FALSE(actor->HasProperty("Amount"));
        }

        TEST_CASE("properties round-trip through the map and the load hook sees the finished actor") {
            UClassRegistry::Get().RegisterClass<ATestPropertyActor>("ATestPropertyActor");

            auto src = UWorld::Create("PropsMap");
            auto* door = src->SpawnActor<ATestPropertyActor>("Door");
            door->SetActorLocation({3.0f, 0.0f, -7.0f});
            door->AddComponent<FPointLightComponent>();
            door->SetProperty("RequiredKey", "red");
            door->SetProperty("Amount", "3");
            door->SetProperty("Locked", "true");
            // Values that would break a line-based format, and a key equal to a section name.
            door->SetProperty("Note", "Line one\nHe said \"run\" \\ now");
            door->SetProperty("Map", "not the header");
            door->SetProperty("Transform", "not a section");
            door->SetProperty("Empty", "");
            src->SpawnActor<AActor>("Plain"); // no properties: no block is written

            std::string yaml;
            REQUIRE(FMapSerializer(src).SerializeText(yaml));
            CHECK(yaml.find("Properties:") != std::string::npos);

            auto dst = UWorld::Create("PropsDst");
            REQUIRE(FMapSerializer(dst).DeserializeText(yaml));
            CHECK(dst->GetName() == "PropsMap"); // the "Map" property did not hijack the header

            auto* restored = dynamic_cast<ATestPropertyActor*>(dst->FindActorByName("Door"));
            REQUIRE(restored);
            CHECK(restored->GetProperties().size() == 7);
            CHECK(restored->GetProperty("Note") == "Line one\nHe said \"run\" \\ now");
            CHECK(restored->GetProperty("Map") == "not the header");
            CHECK(restored->GetProperty("Transform") == "not a section");
            CHECK(restored->HasProperty("Empty"));
            CHECK(restored->GetProperty("Empty").empty());

            CHECK(restored->LoadedCalls == 1);
            CHECK(restored->RequiredKey == "red");
            CHECK(restored->Amount == 3);
            CHECK(restored->bLocked);
            CHECK(restored->LocationAtLoad.x == doctest::Approx(3.0f));
            CHECK(restored->LocationAtLoad.z == doctest::Approx(-7.0f));
            CHECK(restored->bHadLightAtLoad);

            AActor* plain = dst->FindActorByName("Plain");
            REQUIRE(plain);
            CHECK(plain->GetProperties().empty());
            // The transform after the Properties block was still parsed.
            CHECK(restored->GetActorLocation().z == doctest::Approx(-7.0f));
        }

        TEST_CASE("an invalid property key in a map file is ignored with the rest loaded") {
            const char* kMap = "Map:\n  Name: \"K\"\n  Version: \"2.2\"\nActors:\n  - Name: \"A\"\n"
                               "    Properties:\n      Good: \"1\"\n      9bad: \"2\"\n";
            auto world = UWorld::Create("K");
            REQUIRE(FMapSerializer(world).DeserializeText(kMap));
            AActor* a = world->FindActorByName("A");
            REQUIRE(a);
            CHECK(a->GetProperties().size() == 1);
            CHECK(a->GetProperty("Good") == "1");
        }
    }

    TEST_SUITE("Map serializer writer") {

        TEST_CASE("floats round-trip exactly instead of being cut to six digits") {
            auto src = UWorld::Create("Precision");
            AActor* actor = src->SpawnActor<AActor>("Precise");
            // None of these survive a 6-significant-digit text representation.
            const glm::vec3 location{1234.5678f, -0.123456789f, 1.0f / 3.0f};
            const glm::vec3 rotation{0.000123456f, 359.99997f, 12.345678f};
            actor->SetActorLocation(location);
            actor->SetActorRotation(rotation);

            std::string text;
            REQUIRE(FMapSerializer(src).SerializeText(text));
            auto dst = UWorld::Create("PrecisionDst");
            REQUIRE(FMapSerializer(dst).DeserializeText(text));
            AActor* restored = dst->FindActorByName("Precise");
            REQUIRE(restored);
            // Bit-exact: repeated saves no longer drift.
            CHECK(restored->GetActorLocation().x == location.x);
            CHECK(restored->GetActorLocation().y == location.y);
            CHECK(restored->GetActorLocation().z == location.z);
            CHECK(restored->GetActorRotation().x == rotation.x);
            CHECK(restored->GetActorRotation().y == rotation.y);
            CHECK(restored->GetActorRotation().z == rotation.z);

            // Saving the reloaded world writes the same actor records (stable fixed point). Only the actor
            // section is compared: loading materialises an environment actor, which changes the header.
            std::string again;
            REQUIRE(FMapSerializer(dst).SerializeText(again));
            const auto actorSection = [](const std::string& InText) {
                const size_t at = InText.find("\nActors:");
                return at == std::string::npos ? std::string() : InText.substr(at);
            };
            REQUIRE_FALSE(actorSection(text).empty());
            CHECK(actorSection(again) == actorSection(text));
        }

        TEST_CASE("round values keep their short text form") {
            // Existing maps must not be rewritten as noise such as 0.100000001.
            auto src = UWorld::Create("Short");
            AActor* actor = src->SpawnActor<AActor>("Round");
            actor->SetActorLocation({1.0f, 0.125f, -0.25f});
            actor->SetActorRotation({64.0f, 0.1f, 0.0f});
            std::string text;
            REQUIRE(FMapSerializer(src).SerializeText(text));
            CHECK(text.find("Translation: [1, 0.125, -0.25]") != std::string::npos);
            CHECK(text.find("Rotation: [64, 0.1, 0]") != std::string::npos);
            CHECK(text.find("Scale: [1, 1, 1]") != std::string::npos);
        }

        TEST_CASE("quotes, backslashes and line breaks in strings survive a round trip") {
            auto src = UWorld::Create("Map \"with\" quotes");
            AActor* named = src->SpawnActor<AActor>("Door \"A\" \\ east");
            named->SetFolderPath("Props/He said \"hi\"");
            AActor* note = src->SpawnActor<AActor>("Note");
            auto& text = note->AddComponent<FTextComponent>();
            text.Text = "First line\nSecond \"quoted\" line\\end";

            std::string yaml;
            REQUIRE(FMapSerializer(src).SerializeText(yaml));
            CHECK(yaml.find("Version: \"2.2\"") != std::string::npos);

            auto dst = UWorld::Create("Dst");
            REQUIRE(FMapSerializer(dst).DeserializeText(yaml));
            CHECK(dst->GetName() == "Map \"with\" quotes");
            AActor* restoredNamed = dst->FindActorByName("Door \"A\" \\ east");
            REQUIRE(restoredNamed);
            CHECK(restoredNamed->GetFolderPath() == "Props/He said \"hi\"");
            AActor* restoredNote = dst->FindActorByName("Note");
            REQUIRE(restoredNote);
            REQUIRE(restoredNote->HasComponent<FTextComponent>());
            CHECK(restoredNote->GetComponent<FTextComponent>().Text == "First line\nSecond \"quoted\" line\\end");
            // The line break did not split the record: exactly the two authored actors came back.
            CHECK(CountNamed(*dst, "Note") == 1);
        }

        TEST_CASE("a 2.1 file keeps its raw backslashes") {
            // In a 2.1 file "\n" and "\t" are a backslash followed by a letter (Windows paths), not escapes.
            const char* kOld = "Map:\n  Name: \"Old\"\n  Version: \"2.1\"\nActors:\n"
                               "  - Name: \"C:\\Art\\new\\table\"\n";
            const std::string expected = "C:\\Art\\new\\table";
            auto world = UWorld::Create("Old");
            REQUIRE(FMapSerializer(world).DeserializeText(kOld));
            REQUIRE(world->FindActorByName(expected));

            // Re-saving upgrades to 2.2 and escapes them, so the value is still preserved.
            std::string yaml;
            REQUIRE(FMapSerializer(world).SerializeText(yaml));
            CHECK(yaml.find("C:\\\\Art\\\\new\\\\table") != std::string::npos);
            auto again = UWorld::Create("Again");
            REQUIRE(FMapSerializer(again).DeserializeText(yaml));
            CHECK(again->FindActorByName(expected) != nullptr);
        }

        TEST_CASE("box collision, trigger state and player start state are persisted") {
            auto src = UWorld::Create("Volumes");
            AActor* blocker = src->SpawnActor<AActor>("Blocker");
            blocker->AddComponent<FBoxCollisionComponent>(glm::vec3(-2.0f, 0.0f, -1.5f), glm::vec3(2.0f, 3.25f, 1.5f),
                                                          false, ECollisionChannel::WorldDynamic);
            auto* trigger = src->SpawnActor<ATriggerVolume>("Trigger");
            trigger->SetEnabled(false);
            auto* start = src->SpawnActor<APlayerStart>("Start");
            start->SetEnabled(false);
            auto* startOn = src->SpawnActor<APlayerStart>("StartOn");

            std::string yaml;
            REQUIRE(FMapSerializer(src).SerializeText(yaml));

            auto dst = UWorld::Create("VolumesDst");
            REQUIRE(FMapSerializer(dst).DeserializeText(yaml));

            AActor* restoredBlocker = dst->FindActorByName("Blocker");
            REQUIRE(restoredBlocker);
            REQUIRE(restoredBlocker->HasComponent<FBoxCollisionComponent>());
            const auto& box = restoredBlocker->GetComponent<FBoxCollisionComponent>();
            CHECK(box.LocalMin.x == -2.0f);
            CHECK(box.LocalMax.y == 3.25f);
            CHECK(box.LocalMax.z == 1.5f);
            CHECK_FALSE(box.bBlockMovement);
            CHECK(box.Channel == ECollisionChannel::WorldDynamic);

            auto* restoredTrigger = dynamic_cast<ATriggerVolume*>(dst->FindActorByName("Trigger"));
            REQUIRE(restoredTrigger);
            CHECK_FALSE(restoredTrigger->IsEnabled());
            auto* restoredStart = dynamic_cast<APlayerStart*>(dst->FindActorByName("Start"));
            REQUIRE(restoredStart);
            CHECK_FALSE(restoredStart->IsEnabled());
            auto* restoredStartOn = dynamic_cast<APlayerStart*>(dst->FindActorByName("StartOn"));
            REQUIRE(restoredStartOn);
            CHECK(restoredStartOn->IsEnabled());
            (void)startOn;
        }

        TEST_CASE("mesh collision off and blocking volumes are persisted") {
            auto src = UWorld::Create("Infrastructure");
            AActor* decoration = src->SpawnActor<AActor>("Decoration");
            decoration->SetActorLocation({0.0f, 1.0f, 6.0f});
            decoration->AddComponent<FMeshComponent>().bCollision = false;
            AActor* solid = src->SpawnActor<AActor>("Solid");
            solid->SetActorLocation({20.0f, 1.0f, 6.0f});
            solid->AddComponent<FMeshComponent>();
            auto* volume = src->SpawnActor<ABlockingVolume>("WallCollision");
            volume->SetActorLocation({0.0f, 1.0f, 6.0f});
            volume->SetActorScale({4.0f, 2.0f, 0.5f});

            std::string yaml;
            REQUIRE(FMapSerializer(src).SerializeText(yaml));
            // The switch is written only where it is off.
            CHECK(yaml.find("Collision: false") != std::string::npos);
            CHECK(yaml.find("Collision: true") == std::string::npos);

            auto dst = UWorld::Create("InfrastructureDst");
            REQUIRE(FMapSerializer(dst).DeserializeText(yaml));

            AActor* restoredDecoration = dst->FindActorByName("Decoration");
            REQUIRE(restoredDecoration);
            REQUIRE(restoredDecoration->HasComponent<FMeshComponent>());
            CHECK_FALSE(restoredDecoration->GetComponent<FMeshComponent>().bCollision);
            AActor* restoredSolid = dst->FindActorByName("Solid");
            REQUIRE(restoredSolid);
            REQUIRE(restoredSolid->HasComponent<FMeshComponent>());
            CHECK(restoredSolid->GetComponent<FMeshComponent>().bCollision);

            auto* restoredVolume = dynamic_cast<ABlockingVolume*>(dst->FindActorByName("WallCollision"));
            REQUIRE(restoredVolume);
            CHECK(restoredVolume->GetActorScale().x == doctest::Approx(4.0f));
            CHECK(restoredVolume->GetActorScale().z == doctest::Approx(0.5f));
        }

        TEST_CASE("fog settings are persisted with the environment only when fog is enabled") {
            auto src = UWorld::Create("Foggy");
            AActor* env = src->SpawnActor<AActor>("Environment");
            auto& sky = env->AddComponent<FSkyboxComponent>();

            std::string withoutFog;
            REQUIRE(FMapSerializer(src).SerializeText(withoutFog));
            CHECK(withoutFog.find("FogEnabled") == std::string::npos); // existing maps are not rewritten

            sky.bFogEnabled = true;
            sky.FogColor = {0.2f, 0.25f, 0.3f};
            sky.FogDensity = 0.125f;
            sky.FogStartDistance = 1.5f;
            sky.FogHeightFalloff = 0.75f;
            sky.FogBaseHeight = -3.0f;
            sky.FogMaxOpacity = 0.9f;

            std::string yaml;
            REQUIRE(FMapSerializer(src).SerializeText(yaml));
            auto dst = UWorld::Create("FoggyDst");
            REQUIRE(FMapSerializer(dst).DeserializeText(yaml));

            const FSkyboxComponent* restored = nullptr;
            for (const auto& actor : dst->GetAllActors()) {
                if (actor && actor->HasComponent<FSkyboxComponent>())
                    restored = &actor->GetComponent<FSkyboxComponent>();
            }
            REQUIRE(restored);
            CHECK(restored->bFogEnabled);
            CHECK(restored->FogColor.g == 0.25f);
            CHECK(restored->FogDensity == 0.125f);
            CHECK(restored->FogStartDistance == 1.5f);
            CHECK(restored->FogHeightFalloff == 0.75f);
            CHECK(restored->FogBaseHeight == -3.0f);
            CHECK(restored->FogMaxOpacity == 0.9f);
        }

        TEST_CASE("saving to disk is atomic and keeps a backup of the previous file") {
            namespace fs = std::filesystem;
            const std::string path = "build/atomic_save_test.lmap";
            std::error_code ec;
            fs::remove(path, ec);
            fs::remove(path + ".bak", ec);
            fs::remove(path + ".tmp", ec);

            auto first = UWorld::Create("FirstVersion");
            first->SpawnActor<AActor>("OnlyInFirst");
            REQUIRE(FMapSerializer(first).Serialize(path));
            CHECK(fs::exists(path));
            CHECK_FALSE(fs::exists(path + ".tmp"));
            CHECK_FALSE(fs::exists(path + ".bak")); // nothing to back up on the first save

            auto second = UWorld::Create("SecondVersion");
            second->SpawnActor<AActor>("OnlyInSecond");
            REQUIRE(FMapSerializer(second).Serialize(path));
            CHECK_FALSE(fs::exists(path + ".tmp"));
            REQUIRE(fs::exists(path + ".bak"));

            auto current = UWorld::Create("Current");
            REQUIRE(FMapSerializer(current).Deserialize(path));
            CHECK(current->FindActorByName("OnlyInSecond") != nullptr);
            auto backup = UWorld::Create("Backup");
            REQUIRE(FMapSerializer(backup).Deserialize(path + ".bak"));
            CHECK(backup->FindActorByName("OnlyInFirst") != nullptr);

            SUBCASE("a save that cannot be written leaves the existing map intact") {
                auto third = UWorld::Create("Third");
                third->SpawnActor<AActor>("OnlyInThird");
                // The target directory does not exist, so the temp file cannot be created.
                CHECK_FALSE(FMapSerializer(third).Serialize("build/no_such_dir/atomic_save_test.lmap"));
                auto stillThere = UWorld::Create("Still");
                REQUIRE(FMapSerializer(stillThere).Deserialize(path));
                CHECK(stillThere->FindActorByName("OnlyInSecond") != nullptr);
            }

            fs::remove(path, ec);
            fs::remove(path + ".bak", ec);
        }
    }

} // namespace Leon
