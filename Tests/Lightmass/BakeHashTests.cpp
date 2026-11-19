#include <doctest/doctest.h>

#include "Assets/FMaterial.hpp"
#include "Assets/FMaterialInstance.hpp"
#include "Assets/UAssetManager.hpp"
#include "Assets/UStaticMesh.hpp"
#include "Scene/Components.hpp"
#include "Core/FProjectPaths.hpp"
#include "Engine/FLightmapBakeInputs.hpp"
#include "Engine/FMapSerializer.hpp"
#include "Engine/UWorld.hpp"
#include "GPU/HeadlessDevice.hpp"
#include "Gameplay/AActor.hpp"
#include "Gameplay/AReflectionProbe.hpp"
#include "Lightmass/FLightmass.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
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

        TEST_CASE("placing or moving a reflection probe leaves a bake valid") {
            auto world = UWorld::Create("Probed");
            AActor* floor = world->SpawnActor<AActor>("Floor");
            floor->AddComponent<FMeshComponent>().MeshType = "Plane";
            const uint64_t before = FLightmass::ComputeBakeInputHash(*world, FLightmassSettings{});

            // A probe takes light from the level and gives none back to the bake.
            auto* probe = world->SpawnActor<AReflectionProbe>("Probe");
            CHECK(FLightmass::ComputeBakeInputHash(*world, FLightmassSettings{}) == before);
            probe->SetActorLocation({3.0f, 1.0f, 2.0f});
            probe->GetComponent<FReflectionProbeComponent>().Extent = {5.0f, 2.0f, 5.0f};
            CHECK(FLightmass::ComputeBakeInputHash(*world, FLightmassSettings{}) == before);

            // Anything else that moves still counts.
            floor->SetActorLocation({0.0f, 0.5f, 0.0f});
            CHECK(FLightmass::ComputeBakeInputHash(*world, FLightmassSettings{}) != before);
        }

        TEST_CASE("laying a decal on a level, moving it or taking it away leaves a bake valid") {
            auto world = UWorld::Create("Stained");
            AActor* floor = world->SpawnActor<AActor>("Floor");
            floor->AddComponent<FMeshComponent>().MeshType = "Plane";
            const uint64_t before = FLightmass::ComputeBakeInputHash(*world, FLightmassSettings{});

            // A decal is a mark on a surface: it puts no light into the bake and blocks none.
            const TRef<FMaterial> material = FMaterial::Create("Stain");
            material->SetAlphaMode(EAlphaMode::Decal);
            AActor* stain = world->SpawnActor<AActor>("Stain");
            auto& quad = stain->AddComponent<FMeshComponent>();
            quad.MeshType = "Quad";
            quad.Mobility = EComponentMobility::Movable;
            stain->AddComponent<FMaterialComponent>(material->CreateInstance());
            CHECK(FLightmapBakeInputs::IsDecal(*stain));
            CHECK_FALSE(FLightmapBakeInputs::IsDecal(*floor));
            CHECK(FLightmass::ComputeBakeInputHash(*world, FLightmassSettings{}) == before);
            stain->SetActorLocation({1.0f, 0.001f, 2.0f});
            CHECK(FLightmass::ComputeBakeInputHash(*world, FLightmassSettings{}) == before);
            // Whatever its mobility says: the baker leaves a decal out either way.
            quad.Mobility = EComponentMobility::Static;
            CHECK(FLightmass::ComputeBakeInputHash(*world, FLightmassSettings{}) == before);

            // The same quad with a material that is not a decal is a surface of the level again.
            material->SetAlphaMode(EAlphaMode::Blend);
            CHECK_FALSE(FLightmapBakeInputs::IsDecal(*stain));
            CHECK(FLightmass::ComputeBakeInputHash(*world, FLightmassSettings{}) != before);
        }

        TEST_CASE("a decal marked Static gets no lightmap and throws no baked shadow") {
            namespace fs = std::filesystem;
            const fs::path project = fs::path("build") / "DecalBakeTest";
            std::error_code ec;
            fs::remove_all(project, ec);
            fs::create_directories(project / "Content" / "Maps", ec);
            fs::create_directories(project / "Content" / "Materials", ec);
            std::ofstream(project / "Content" / "Materials" / "M_Stain.lmat")
                << "Material:\n  AlbedoColor: [0.1, 0.1, 0.1]\n  AlphaMode: \"Decal\"\n";
            std::ofstream(project / "Content" / "Materials" / "M_Sheet.lmat")
                << "Material:\n  AlbedoColor: [0.1, 0.1, 0.1]\n  AlphaMode: \"Opaque\"\n";
            const std::string previousContent = UAssetManager::GetContentRoot();
            const std::string previousProject = FProjectPaths::ProjectDir();
            // The baker and the game find the materials of a map through its project.
            FProjectPaths::SetProjectRoot(project.string());
            UAssetManager::SetContentRoot((project / "Content").string());

            // A floor under a lamp and, when asked for, a dark sheet a metre wide hanging between the two.
            const auto writeMap = [&project](const char* InName, const char* InSheetMaterial) {
                auto world = UWorld::Create(InName);
                AActor* floor = world->SpawnActor<AActor>("Floor");
                auto& plane = floor->AddComponent<FMeshComponent>();
                plane.MeshType = "Plane";
                plane.MeshWidth = 4.0f;
                plane.MeshDepth = 4.0f;
                plane.Mobility = EComponentMobility::Static;
                plane.LightmapResolution = 16;
                AActor* lamp = world->SpawnActor<AActor>("Lamp");
                lamp->SetActorLocation({0.0f, 3.0f, 0.0f});
                FPointLightComponent point;
                point.Light.Intensity = 12.0f;
                point.Light.Radius = 12.0f;
                point.Mobility = ELightMobility::Static;
                lamp->AddComponent<FPointLightComponent>(point);
                if (InSheetMaterial) {
                    AActor* sheet = world->SpawnActor<AActor>("Sheet");
                    sheet->SetActorLocation({0.0f, 1.0f, 0.0f});
                    auto& box = sheet->AddComponent<FMeshComponent>();
                    box.MeshType = "Box";
                    box.MeshWidth = 1.0f;
                    box.MeshHeight = 0.02f;
                    box.MeshDepth = 1.0f;
                    box.Mobility = EComponentMobility::Static;
                    box.LightmapResolution = 8;
                    sheet->AddComponent<FMaterialComponent>().AssetPath = InSheetMaterial;
                }
                const std::string file = (project / "Content" / "Maps" / (std::string(InName) + ".lmap")).string();
                REQUIRE(FMapSerializer(world).Serialize(file));
                return file;
            };
            const auto readFile = [](const std::string& InPath) {
                std::ifstream in(InPath, std::ios::binary);
                return std::vector<char>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            };
            FLightmassSettings settings;
            ApplyLightingBuildQuality(ELightingBuildQuality::Preview, settings);
            settings.LightmapResolution = 16;
            settings.SamplesPerTexel = 2;
            settings.NumIndirectBounces = 1;
            settings.bLightProbes = false;
            const auto bake = [&settings, &readFile](const std::string& InMap) {
                const FLightmassBakeResult result = FLightmass::BakeMap(InMap, settings, true);
                REQUIRE_MESSAGE(result.bSuccess, result.Message);
                const std::vector<char> lightmap = readFile(result.LightmapPath);
                REQUIRE_FALSE(lightmap.empty());
                return lightmap;
            };

            const std::string bareMap = writeMap("Bare", nullptr);
            const std::string decalMap = writeMap("Stained", "/Game/Materials/M_Stain.lmat");
            const std::string sheetMap = writeMap("Covered", "/Game/Materials/M_Sheet.lmat");
            const std::vector<char> bare = bake(bareMap);
            const std::vector<char> stained = bake(decalMap);
            const std::vector<char> covered = bake(sheetMap);

            // The sheet as a decal: the lightmap of the level is the one it has without the sheet,
            // byte for byte. As an ordinary surface it has a chart of its own and shadows the floor.
            CHECK(stained == bare);
            CHECK(covered != bare);

            // The map baked with the decal in it is whole: nothing is missing a chart that should
            // have one, and the game trusts the bake.
            std::string message;
            CHECK_MESSAGE(FLightmass::ValidateMap(decalMap, message), message);
            {
                auto world = UWorld::Create("StainedLoaded");
                REQUIRE(FMapSerializer(world).Deserialize(decalMap));
                FLightmapBakeInputs::RefreshRuntimeTrust(*world);
                CHECK(world->AreLightmapsTrusted());
                CHECK(MeshOf(world, "Floor").LightmapIndex >= 0);
                CHECK(MeshOf(world, "Sheet").LightmapIndex < 0);
                CHECK(FLightmapBakeInputs::IsDecal(*world->FindActorByName("Sheet")));
            }
            {
                auto world = UWorld::Create("CoveredLoaded");
                REQUIRE(FMapSerializer(world).Deserialize(sheetMap));
                CHECK(MeshOf(world, "Sheet").LightmapIndex >= 0);
            }

            FProjectPaths::SetProjectRoot(previousProject);
            UAssetManager::SetContentRoot(previousContent);
            fs::remove_all(project, ec);
        }

        TEST_CASE("the hash does not depend on what loaded a mesh first") {
            // The asset manager hands out one mesh per file, and the mesh remembers the path of
            // whoever asked first. A game that reached the map through another one saw a different
            // hash than the baker did.
            auto hashWith = [](const char* InPathTheAssetRemembers) {
                auto world = UWorld::Create("SharedMesh");
                AActor* crate = world->SpawnActor<AActor>("Crate");
                auto mesh = UStaticMesh::Create("Cube");
                mesh->SetAssetPath(InPathTheAssetRemembers);
                auto& component = crate->AddComponent<FStaticMeshComponent>(mesh, "Engine/Meshes/Cube.lmesh");
                component.Mobility = EComponentMobility::Static;
                return FLightmass::ComputeBakeInputHash(*world, FLightmassSettings{});
            };
            CHECK(hashWith("Engine/Meshes/Cube.lmesh") == hashWith("Engine/Content/Meshes/Cube.lmesh"));
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
