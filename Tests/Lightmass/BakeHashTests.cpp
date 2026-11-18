#include <doctest/doctest.h>

#include "Assets/UAssetManager.hpp"
#include "Scene/Components.hpp"
#include "Engine/FMapSerializer.hpp"
#include "Engine/UWorld.hpp"
#include "GPU/HeadlessDevice.hpp"
#include "Gameplay/AActor.hpp"
#include "Lightmass/FLightmass.hpp"

#include <filesystem>
#include <string>
#include <system_error>

// A bake is trusted at run time only if the hash of its inputs, stored in the map, is the hash the
// game computes when it loads that map. The baker runs without a renderer and from another working
// directory, so the hash must not depend on either. It did, twice, and the game skipped the
// lightmaps of every map those cases touched without any test noticing.

namespace Leon {

    namespace {

        /** A map with a legacy cube stretched by its actor's scale, and a material from the project. */
        std::string MapWithScaledCube() {
            auto world = UWorld::Create("BakeHashSource");
            AActor* wall = world->SpawnActor<AActor>("Wall");
            wall->SetActorLocation({1.0f, 2.0f, 3.0f});
            wall->SetActorScale({4.0f, 1.0f, 0.5f});
            auto& mesh = wall->AddComponent<FMeshComponent>();
            mesh.MeshType = "Cube";
            mesh.MeshSize = 2.0f;
            mesh.Mobility = EComponentMobility::Static;
            AActor* mirrored = world->SpawnActor<AActor>("Mirrored");
            mirrored->SetActorScale({-1.0f, 1.0f, 1.0f});
            mirrored->AddComponent<FMeshComponent>().MeshType = "Cube";
            std::string text;
            REQUIRE(FMapSerializer(world).SerializeText(text));
            return text;
        }

        const FMeshComponent& MeshOf(const TRef<UWorld>& InWorld, const char* InName) {
            AActor* actor = InWorld->FindActorByName(InName);
            REQUIRE(actor != nullptr);
            REQUIRE(actor->HasComponent<FMeshComponent>());
            return actor->GetComponent<FMeshComponent>();
        }

    } // namespace

    TEST_SUITE("Lightmap bake hash") {

        TEST_CASE("a map loads into the same world with and without a renderer") {
            const std::string text = MapWithScaledCube();

            auto headless = UWorld::Create("Headless");
            REQUIRE(FMapSerializer(headless).DeserializeText(text));
            const uint64_t headlessHash = FLightmass::ComputeBakeInputHash(*headless, FLightmassSettings{});

            uint64_t renderedHash = 0;
            {
                TestGPU::FHeadlessDevice gl;
                REQUIRE(gl.IsValid());
                gl.UseEngineResources();
                auto rendered = UWorld::Create("Rendered");
                REQUIRE(FMapSerializer(rendered).DeserializeText(text));
                renderedHash = FLightmass::ComputeBakeInputHash(*rendered, FLightmassSettings{});

                // With a renderer the mesh also exists on the GPU; the data is the same.
                CHECK(MeshOf(rendered, "Wall").VertexArray != nullptr);
                CHECK(MeshOf(rendered, "Wall").MeshType == MeshOf(headless, "Wall").MeshType);
                CHECK(MeshOf(rendered, "Mirrored").MeshWidth == MeshOf(headless, "Mirrored").MeshWidth);
            }
            CHECK(headlessHash == renderedHash);

            // What the conversion is: the stretched cube becomes a box of that size with no scale left.
            const FMeshComponent& wall = MeshOf(headless, "Wall");
            CHECK(wall.MeshType == "Box");
            CHECK(wall.MeshWidth == doctest::Approx(8.0f));
            CHECK(wall.MeshHeight == doctest::Approx(2.0f));
            CHECK(wall.MeshDepth == doctest::Approx(1.0f));
            CHECK(headless->FindActorByName("Wall")->GetActorScale().x == doctest::Approx(1.0f));
        }

        TEST_CASE("the hash does not depend on where the project folder is") {
            auto world = UWorld::Create("Movable");
            AActor* floor = world->SpawnActor<AActor>("Floor");
            floor->AddComponent<FMeshComponent>().MeshType = "Plane";
            FMaterialComponent material;
            material.AssetPath = "/Game/Materials/M_Floor.lmat";
            floor->AddComponent<FMaterialComponent>(material);

            const std::string original = UAssetManager::GetContentRoot();
            UAssetManager::SetContentRoot("Projects/Somewhere/Content");
            const uint64_t there = FLightmass::ComputeBakeInputHash(*world, FLightmassSettings{});
            UAssetManager::SetContentRoot("C:/Elsewhere/Games/Other/Content");
            const uint64_t elsewhere = FLightmass::ComputeBakeInputHash(*world, FLightmassSettings{});
            UAssetManager::SetContentRoot(original);
            CHECK(there == elsewhere);

            // It still follows the asset: another material is another hash.
            floor->GetComponent<FMaterialComponent>().AssetPath = "/Game/Materials/M_Other.lmat";
            CHECK(FLightmass::ComputeBakeInputHash(*world, FLightmassSettings{}) != there);
        }

        TEST_CASE("a bake is valid for the map as the game then loads it") {
            namespace fs = std::filesystem;
            const fs::path folder = fs::path("build") / "BakeHashTest";
            std::error_code ec;
            fs::remove_all(folder, ec);
            fs::create_directories(folder / "Maps", ec);
            const std::string mapFile = (folder / "Maps" / "Small.lmap").string();

            // What saving a map rewrites: a light direction that is not unit length, a light with a
            // rotation, a cube stretched by its scale. The bake must be stamped for the file it wrote.
            {
                auto world = UWorld::Create("Small");
                AActor* floor = world->SpawnActor<AActor>("Floor");
                auto& plane = floor->AddComponent<FMeshComponent>();
                plane.MeshType = "Plane";
                plane.MeshWidth = 4.0f;
                plane.MeshDepth = 4.0f;
                plane.Mobility = EComponentMobility::Static;
                plane.LightmapResolution = 8;
                AActor* block = world->SpawnActor<AActor>("Block");
                block->SetActorLocation({0.0f, 0.5f, 0.0f});
                block->SetActorScale({2.0f, 1.0f, 0.5f});
                auto& cube = block->AddComponent<FMeshComponent>();
                cube.MeshType = "Cube";
                cube.Mobility = EComponentMobility::Static;
                cube.LightmapResolution = 8;
                AActor* lamp = world->SpawnActor<AActor>("Lamp");
                lamp->SetActorLocation({1.0f, 3.0f, 1.0f});
                lamp->SetActorRotation({20.0f, 35.0f, 0.0f});
                FSpotLightComponent spot;
                spot.Light.Direction = {0.75f, -0.45f, -0.45f};
                spot.Mobility = ELightMobility::Static;
                lamp->AddComponent<FSpotLightComponent>(spot);
                REQUIRE(FMapSerializer(world).Serialize(mapFile));
            }

            FLightmassSettings settings;
            ApplyLightingBuildQuality(ELightingBuildQuality::Preview, settings);
            settings.LightmapResolution = 8;
            settings.SamplesPerTexel = 1;
            settings.NumIndirectBounces = 0;
            const FLightmassBakeResult bake = FLightmass::BakeMap(mapFile, settings, true);
            REQUIRE_MESSAGE(bake.bSuccess, bake.Message);

            auto loaded = UWorld::Create("SmallLoaded");
            REQUIRE(FMapSerializer(loaded).Deserialize(mapFile));
            FLightmass::RefreshRuntimeLightmapTrust(*loaded);
            CHECK(loaded->AreLightmapsTrusted());

            std::string message;
            CHECK_MESSAGE(FLightmass::ValidateMap(mapFile, message), message);

            // And the check still bites: move the block and the same bake is stale.
            loaded->FindActorByName("Block")->SetActorLocation({1.0f, 0.5f, 0.0f});
            FLightmass::RefreshRuntimeLightmapTrust(*loaded);
            CHECK_FALSE(loaded->AreLightmapsTrusted());

            fs::remove_all(folder, ec);
        }
    }

} // namespace Leon
