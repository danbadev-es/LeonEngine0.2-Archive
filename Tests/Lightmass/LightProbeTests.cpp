#include <doctest/doctest.h>

#include "Assets/UAssetManager.hpp"
#include "Core/FProjectPaths.hpp"
#include "Engine/FLightProbeAsset.hpp"
#include "Engine/FLightmapBakeInputs.hpp"
#include "Engine/FMapSerializer.hpp"
#include "Engine/UWorld.hpp"
#include "Gameplay/AActor.hpp"
#include "Gameplay/ANavMeshBoundsVolume.hpp"
#include "Lightmass/FLightBaker.hpp"
#include "Lightmass/FLightmass.hpp"
#include "RenderCore/FLightProbeGrid.hpp"
#include "Scene/Components.hpp"

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

// Light probes: the baked ambient light of a level for what has no lightmap. The baker measures the
// light arriving at the points of a grid; the game blends the probes around an object. These tests
// hold the measurement against a room whose light is known, and the file and its trust against the
// rules the lightmap follows.

namespace Leon {

    namespace {

        const glm::vec3 kGrey(0.6f);
        const glm::vec3 kRed(0.9f, 0.05f, 0.05f);

        /** A quad of the given corners (in order around it) that faces along InNormal. */
        void AddQuad(FLightBakerScene& InOutScene, const glm::vec3& InA, const glm::vec3& InB, const glm::vec3& InC,
                     const glm::vec3& InD, const glm::vec3& InNormal, const glm::vec3& InAlbedo) {
            const uint32_t base = static_cast<uint32_t>(InOutScene.Vertices.size());
            for (const glm::vec3& corner : {InA, InB, InC, InD}) {
                FBakeVertex vertex;
                vertex.Position = corner;
                vertex.Normal = InNormal;
                vertex.Albedo = InAlbedo;
                InOutScene.Vertices.push_back(vertex);
            }
            InOutScene.Triangles.push_back({base, base + 1, base + 2, 0, true});
            InOutScene.Triangles.push_back({base, base + 2, base + 3, 0, true});
        }

        /** The six faces of a box. bInwards: they face the inside (a room); otherwise the outside (a block). */
        void AddBox(FLightBakerScene& InOutScene, const glm::vec3& InLow, const glm::vec3& InHigh, bool bInwards,
                    const glm::vec3& InAlbedo, const glm::vec3& InLowXAlbedo) {
            const float side = bInwards ? 1.0f : -1.0f;
            const glm::vec3 l = InLow;
            const glm::vec3 h = InHigh;
            AddQuad(InOutScene, {l.x, l.y, l.z}, {l.x, l.y, h.z}, {l.x, h.y, h.z}, {l.x, h.y, l.z}, {side, 0, 0}, InLowXAlbedo);
            AddQuad(InOutScene, {h.x, l.y, l.z}, {h.x, l.y, h.z}, {h.x, h.y, h.z}, {h.x, h.y, l.z}, {-side, 0, 0}, InAlbedo);
            AddQuad(InOutScene, {l.x, l.y, l.z}, {h.x, l.y, l.z}, {h.x, l.y, h.z}, {l.x, l.y, h.z}, {0, side, 0}, InAlbedo);
            AddQuad(InOutScene, {l.x, h.y, l.z}, {h.x, h.y, l.z}, {h.x, h.y, h.z}, {l.x, h.y, h.z}, {0, -side, 0}, InAlbedo);
            AddQuad(InOutScene, {l.x, l.y, l.z}, {h.x, l.y, l.z}, {h.x, h.y, l.z}, {l.x, h.y, l.z}, {0, 0, side}, InAlbedo);
            AddQuad(InOutScene, {l.x, l.y, h.z}, {h.x, l.y, h.z}, {h.x, h.y, h.z}, {l.x, h.y, h.z}, {0, 0, -side}, InAlbedo);
        }

        /**
         * A closed room 4 x 3 x 4 m around the origin's column (floor at 0), grey but for the wall at
         * x = -2, which is red, with a lamp under the middle of the ceiling.
         */
        FLightBakerScene RedWallRoom(bool bInStaticLamp) {
            FLightBakerScene scene;
            AddBox(scene, {-2.0f, 0.0f, -2.0f}, {2.0f, 3.0f, 2.0f}, true, kGrey, kRed);
            FBakePointLight lamp;
            lamp.Light.Position = {0.0f, 2.5f, 0.0f};
            lamp.Light.Color = {1.0f, 1.0f, 1.0f};
            lamp.Light.Intensity = 10.0f;
            lamp.Light.Radius = 10.0f;
            lamp.bContributeDirect = bInStaticLamp;
            scene.PointLights.push_back(lamp);
            return scene;
        }

        /** Probes at x and z = -1.5, 0, 1.5 and y = 0.5, 1.5, 2.5: the middle one is the middle of the room. */
        FLightProbeBakeSettings RoomProbes() {
            FLightProbeBakeSettings settings;
            settings.BoundsMin = {-1.5f, 0.5f, -1.5f};
            settings.BoundsMax = {1.5f, 2.5f, 1.5f};
            settings.Spacing = 1.5f;
            settings.VerticalSpacing = 1.0f;
            settings.SamplesPerProbe = 512;
            return settings;
        }

        FLightBakerSettings BakeSettings(uint32_t InThreads = 0) {
            FLightBakerSettings settings;
            settings.NumIndirectBounces = 2;
            settings.NumThreads = InThreads;
            return settings;
        }

        float Redness(const glm::vec3& InColor) {
            return InColor.r / std::max(InColor.g, 1.0e-6f);
        }

        bool SameSample(const FLightProbeSample& InA, const FLightProbeSample& InB) {
            return InA.Red == InB.Red && InA.Green == InB.Green && InA.Blue == InB.Blue;
        }

    } // namespace

    TEST_SUITE("Light probes") {

        TEST_CASE("the grid is centred in its volume, as fine as asked for and never larger than allowed") {
            FLightProbeBakeSettings settings;
            settings.BoundsMin = {0.0f, 0.0f, 0.0f};
            settings.BoundsMax = {10.0f, 2.8f, 4.0f};
            FLightProbeGrid grid;
            FLightBaker::LayOutProbes(settings, grid);
            // 10 m at 1.5 m: seven probes with half a metre left on each side; 2.8 m at 1 m: three; 4 m: three.
            CHECK(grid.Counts == glm::uvec3(7, 3, 3));
            CHECK(grid.Spacing == glm::vec3(1.5f, 1.0f, 1.5f));
            CHECK(grid.Origin.x == doctest::Approx(0.5f));
            CHECK(grid.Origin.y == doctest::Approx(0.4f));
            CHECK(grid.Origin.z == doctest::Approx(0.5f));
            CHECK(grid.Probes.size() == 63);
            CHECK(grid.PositionOf(6, 2, 2).x == doctest::Approx(9.5f));

            // A volume thinner than the spacing gets one layer, in its middle.
            settings.BoundsMax.y = 0.6f;
            FLightBaker::LayOutProbes(settings, grid);
            CHECK(grid.Counts.y == 1);
            CHECK(grid.Origin.y == doctest::Approx(0.3f));

            // A hangar at the default spacing would be far too many: the grid is coarsened to fit.
            settings.BoundsMax = {400.0f, 60.0f, 400.0f};
            FLightBaker::LayOutProbes(settings, grid);
            CHECK(grid.ProbeCount() <= settings.MaxProbes);
            CHECK(grid.ProbeCount() > settings.MaxProbes / 2);
            CHECK(grid.Spacing.x > 1.5f);
            CHECK(grid.Spacing.y / grid.Spacing.x == doctest::Approx(1.0f / 1.5f));
        }

        TEST_CASE("a probe in a room with one red wall gets redder light from the wall's side") {
            // The lamp is Stationary: its own light is the game's business, the probes hold what bounces.
            const FLightBakerScene scene = RedWallRoom(false);
            FLightProbeGrid grid;
            FLightBaker::BakeProbes(scene, BakeSettings(), RoomProbes(), grid);
            REQUIRE(grid.Counts == glm::uvec3(3, 3, 3));
            for (const uint8_t valid : grid.Valid)
                CHECK(valid == 1);

            const FLightProbeSample& middle = grid.Probes[grid.IndexOf(1, 1, 1)];
            const glm::vec3 fromWall = middle.Irradiance({-1.0f, 0.0f, 0.0f}); // a surface facing the red wall
            const glm::vec3 fromOpposite = middle.Irradiance({1.0f, 0.0f, 0.0f});
            CAPTURE(fromWall.r);
            CAPTURE(fromWall.g);
            CAPTURE(fromOpposite.r);
            CAPTURE(fromOpposite.g);
            CHECK(fromWall.g > 0.01f);
            CHECK(fromOpposite.g > 0.01f);
            CHECK(Redness(fromWall) > 1.5f);
            CHECK(Redness(fromWall) > Redness(fromOpposite) * 1.3f);
            // Green and blue come off the grey walls alone, and the red wall sends hardly any.
            CHECK(fromWall.g < fromOpposite.g);
            CHECK(fromWall.g == doctest::Approx(fromWall.b).epsilon(0.02));

            // Nearer the red wall the light is redder than at the far side of the room.
            const glm::vec3 up(0.0f, 1.0f, 0.0f);
            CHECK(Redness(grid.Probes[grid.IndexOf(0, 1, 1)].Irradiance(up)) >
                  Redness(grid.Probes[grid.IndexOf(2, 1, 1)].Irradiance(up)) * 1.1f);

            // Without bounces nothing is left: the room is closed, and the lamp's own light is not baked.
            FLightBakerSettings direct = BakeSettings();
            direct.NumIndirectBounces = 0;
            FLightProbeGrid unlit;
            FLightBaker::BakeProbes(scene, direct, RoomProbes(), unlit);
            CHECK(unlit.Probes[unlit.IndexOf(1, 1, 1)].Irradiance(up) == glm::vec3(0.0f));
        }

        TEST_CASE("a Static light also lights the probe directly, a Stationary one only through what it lights") {
            FLightProbeGrid bounced;
            FLightProbeGrid whole;
            FLightBaker::BakeProbes(RedWallRoom(false), BakeSettings(), RoomProbes(), bounced);
            FLightBaker::BakeProbes(RedWallRoom(true), BakeSettings(), RoomProbes(), whole);
            const size_t middle = bounced.IndexOf(1, 1, 1);

            // The lamp is one metre straight above the middle probe: it adds its irradiance there, seen
            // through the two harmonics a probe has (1/4 + 1/2 cos), and nothing else changes.
            const float lamp = 10.0f * FLightBaker::PointAttenuation(1.0f, 10.0f);
            const glm::vec3 up(0.0f, 1.0f, 0.0f);
            const glm::vec3 side(1.0f, 0.0f, 0.0f);
            CHECK(whole.Probes[middle].Irradiance(up).g - bounced.Probes[middle].Irradiance(up).g ==
                  doctest::Approx(lamp * 0.75f).epsilon(0.001));
            CHECK(whole.Probes[middle].Irradiance(side).g - bounced.Probes[middle].Irradiance(side).g ==
                  doctest::Approx(lamp * 0.25f).epsilon(0.001));

            // Something in the way of the lamp takes that light away again.
            FLightBakerScene shaded = RedWallRoom(true);
            AddQuad(shaded, {-0.5f, 2.0f, -0.5f}, {0.5f, 2.0f, -0.5f}, {0.5f, 2.0f, 0.5f}, {-0.5f, 2.0f, 0.5f},
                    {0.0f, 1.0f, 0.0f}, kGrey);
            FLightProbeGrid inShadow;
            FLightBaker::BakeProbes(shaded, BakeSettings(), RoomProbes(), inShadow);
            CHECK(inShadow.Probes[middle].Irradiance(up).g < whole.Probes[middle].Irradiance(up).g - lamp * 0.5f);
        }

        TEST_CASE("a probe inside a solid is marked, holds its neighbours' light and is left out of the blend") {
            FLightBakerScene scene = RedWallRoom(true);
            // A pillar around the middle of the room, closed, facing outwards.
            AddBox(scene, {-0.4f, 0.0f, -0.4f}, {0.4f, 2.0f, 0.4f}, false, kGrey, kGrey);
            FLightProbeGrid grid;
            FLightBaker::BakeProbes(scene, BakeSettings(), RoomProbes(), grid);

            const size_t inside = grid.IndexOf(1, 1, 1);
            CHECK(grid.Valid[inside] == 0);
            CHECK(grid.Valid[grid.IndexOf(1, 0, 1)] == 0); // also in the pillar, half a metre up
            CHECK(grid.Valid[grid.IndexOf(1, 2, 1)] == 1); // above it
            CHECK(grid.Valid[grid.IndexOf(0, 1, 1)] == 1);
            size_t valid = 0;
            for (const uint8_t flag : grid.Valid)
                valid += flag;
            CHECK(valid == 25);

            // What the marked probe holds is light, not the darkness of the inside of a pillar.
            const glm::vec3 up(0.0f, 1.0f, 0.0f);
            CHECK(grid.Probes[inside].Irradiance(up).g > 0.05f);

            // Half way from the pillar to the probe beside it, only that probe counts.
            FLightProbeSample sample;
            REQUIRE(grid.Sample(grid.PositionOf(0, 1, 1) + glm::vec3(0.75f, 0.0f, 0.0f), sample));
            CHECK(SameSample(sample, grid.Probes[grid.IndexOf(0, 1, 1)]));
            // In the pillar itself no valid probe is in reach: the light copied into it is what is left.
            REQUIRE(grid.Sample(grid.PositionOf(1, 1, 1), sample));
            CHECK(SameSample(sample, grid.Probes[inside]));

            // A point outside the grid takes the light at the nearest point of it.
            FLightProbeSample corner;
            REQUIRE(grid.Sample({50.0f, 50.0f, 50.0f}, corner));
            CHECK(SameSample(corner, grid.Probes[grid.IndexOf(2, 2, 2)]));
            // Between two valid probes the light is half of each.
            REQUIRE(grid.Sample(0.5f * (grid.PositionOf(0, 2, 0) + grid.PositionOf(1, 2, 0)), sample));
            const glm::vec4 expected = 0.5f * (grid.Probes[grid.IndexOf(0, 2, 0)].Green + grid.Probes[grid.IndexOf(1, 2, 0)].Green);
            CHECK(sample.Green.w == doctest::Approx(expected.w));
            CHECK(sample.Green.x == doctest::Approx(expected.x));

            // No grid, no answer.
            CHECK_FALSE(FLightProbeGrid{}.Sample(glm::vec3(0.0f), sample));
        }

        TEST_CASE("the probes do not depend on the number of threads") {
            const FLightBakerScene scene = RedWallRoom(true);
            FLightProbeGrid one;
            FLightProbeGrid many;
            FLightBaker::BakeProbes(scene, BakeSettings(1), RoomProbes(), one);
            FLightBaker::BakeProbes(scene, BakeSettings(5), RoomProbes(), many);
            REQUIRE(one.Probes.size() == many.Probes.size());
            REQUIRE(!one.Probes.empty());
            CHECK(std::memcmp(one.Probes.data(), many.Probes.data(), one.Probes.size() * sizeof(FLightProbeSample)) == 0);
            CHECK(one.Valid == many.Valid);
        }

        TEST_CASE("uniform light from everywhere is the same irradiance on every side") {
            // No geometry at all and a sky of one colour: every direction brings the same radiance L,
            // and a surface facing anywhere receives pi * L.
            FLightBakerScene scene;
            scene.Environment.bEnabled = true;
            scene.Environment.Zenith = scene.Environment.Horizon = scene.Environment.Ground = {0.5f, 1.0f, 2.0f};
            scene.Environment.Intensity = 1.0f;
            FLightProbeBakeSettings settings;
            settings.BoundsMin = settings.BoundsMax = {0.0f, 1.0f, 0.0f};
            FLightProbeGrid grid;
            FLightBaker::BakeProbes(scene, BakeSettings(), settings, grid);
            REQUIRE(grid.Probes.size() == 1);
            const float pi = 3.14159265f;
            for (const glm::vec3& normal : {glm::vec3(0, 1, 0), glm::vec3(0, -1, 0), glm::vec3(1, 0, 0), glm::vec3(0, 0, -1)}) {
                const glm::vec3 irradiance = grid.Probes[0].Irradiance(normal);
                CHECK(irradiance.r == doctest::Approx(0.5f * pi).epsilon(0.02));
                CHECK(irradiance.g == doctest::Approx(1.0f * pi).epsilon(0.02));
                CHECK(irradiance.b == doctest::Approx(2.0f * pi).epsilon(0.02));
            }
        }

        TEST_CASE("the probes of a map are written to a file and read back") {
            namespace fs = std::filesystem;
            const fs::path folder = fs::path("build") / "LightProbeFileTest";
            std::error_code ec;
            fs::remove_all(folder, ec);
            const std::string file = (folder / "Room.lprobes").string();

            FLightProbeGrid grid;
            FLightBaker::BakeProbes(RedWallRoom(true), BakeSettings(), RoomProbes(), grid);
            grid.Valid[5] = 0;
            REQUIRE(FLightProbeAsset::Save(file, grid, 0xFEEDF00D12345678ull));

            FLightProbeGrid loaded;
            uint64_t hash = 0;
            REQUIRE(FLightProbeAsset::Load(file, loaded, hash));
            CHECK(hash == 0xFEEDF00D12345678ull);
            CHECK(loaded.Counts == grid.Counts);
            CHECK(loaded.Origin == grid.Origin);
            CHECK(loaded.Spacing == grid.Spacing);
            CHECK(loaded.Valid == grid.Valid);
            REQUIRE(loaded.Probes.size() == grid.Probes.size());
            // Half floats: three decimal digits.
            for (size_t i = 0; i < grid.Probes.size(); ++i) {
                for (int component = 0; component < 4; ++component) {
                    CHECK(loaded.Probes[i].Red[component] == doctest::Approx(grid.Probes[i].Red[component]).epsilon(0.002));
                    CHECK(loaded.Probes[i].Blue[component] == doctest::Approx(grid.Probes[i].Blue[component]).epsilon(0.002));
                }
            }
            // 56 bytes of header, a byte and twelve half floats per probe.
            CHECK(fs::file_size(file) == 56 + grid.Probes.size() * (1 + 24));

            // A file cut short, one that is something else, and none at all are refused whole.
            {
                std::ifstream in(file, std::ios::binary);
                std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                std::ofstream((folder / "Short.lprobes"), std::ios::binary).write(bytes.data(), static_cast<std::streamsize>(bytes.size() / 2));
                bytes[0] = 'X';
                std::ofstream((folder / "Other.lprobes"), std::ios::binary).write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            }
            for (const char* name : {"Short.lprobes", "Other.lprobes", "Missing.lprobes"}) {
                FLightProbeGrid refused;
                refused.Resize({1, 1, 1});
                CHECK_FALSE(FLightProbeAsset::Load((folder / name).string(), refused, hash));
                CHECK(refused.IsEmpty());
            }
            // An empty grid is not written.
            CHECK_FALSE(FLightProbeAsset::Save((folder / "Empty.lprobes").string(), FLightProbeGrid{}, 1));
            fs::remove_all(folder, ec);
        }

        TEST_CASE("the probe settings of a map are part of what its bake depends on, once they are changed") {
            auto world = UWorld::Create("ProbeHash");
            AActor* floor = world->SpawnActor<AActor>("Floor");
            floor->AddComponent<FMeshComponent>().MeshType = "Plane";

            // The settings a map carries reach the bake.
            FWorldSettingsComponent mapSettings;
            mapSettings.LightProbeSpacing = 2.5f;
            mapSettings.LightProbeVerticalSpacing = 0.75f;
            mapSettings.bLightProbes = false;
            FLightmassSettings applied;
            std::string unused;
            FLightmapBakeInputs::ApplyWorldSettings(mapSettings, applied, unused);
            CHECK(applied.LightProbeSpacing == 2.5f);
            CHECK(applied.LightProbeVerticalSpacing == 0.75f);
            CHECK_FALSE(applied.bLightProbes);

            const FLightmassSettings defaults;
            const uint64_t before = FLightmass::ComputeBakeInputHash(*world, defaults);
            FLightmassSettings changed = defaults;
            changed.LightProbeSpacing = 2.0f;
            const uint64_t wider = FLightmass::ComputeBakeInputHash(*world, changed);
            CHECK(wider != before);
            changed.LightProbeSpacing = 1.0f;
            CHECK(FLightmass::ComputeBakeInputHash(*world, changed) != wider);
            changed = defaults;
            changed.LightProbeVerticalSpacing = 0.5f;
            CHECK(FLightmass::ComputeBakeInputHash(*world, changed) != before);
            changed = defaults;
            changed.bLightProbes = false;
            CHECK(FLightmass::ComputeBakeInputHash(*world, changed) != before);
            // (That a map which leaves them alone keeps the hash it had before probes existed is held by
            // the maps baked back then: the game's tests still draw them with their lightmaps.)
        }

        TEST_CASE("a bake writes the probes next to the lightmap, and the game uses them only with that bake") {
            namespace fs = std::filesystem;
            const fs::path project = fs::path("build") / "LightProbeBakeTest";
            std::error_code ec;
            fs::remove_all(project, ec);
            fs::create_directories(project / "Content" / "Maps", ec);
            const std::string mapFile = (project / "Content" / "Maps" / "Ward.lmap").string();
            const fs::path probeFile = project / "Content" / "Lightmaps" / "Ward.lprobes";

            // A floor with a block on it under a lamp, and a volume that says where the game is played.
            {
                auto world = UWorld::Create("Ward");
                AActor* floor = world->SpawnActor<AActor>("Floor");
                auto& plane = floor->AddComponent<FMeshComponent>();
                plane.MeshType = "Plane";
                plane.MeshWidth = 8.0f;
                plane.MeshDepth = 8.0f;
                plane.Mobility = EComponentMobility::Static;
                plane.LightmapResolution = 8;
                AActor* block = world->SpawnActor<AActor>("Block");
                block->SetActorLocation({0.0f, 1.5f, 0.0f});
                auto& box = block->AddComponent<FMeshComponent>();
                box.MeshType = "Box";
                box.MeshWidth = 1.0f;
                box.MeshHeight = 3.0f;
                box.MeshDepth = 1.0f;
                box.Mobility = EComponentMobility::Static;
                box.LightmapResolution = 8;
                AActor* lamp = world->SpawnActor<AActor>("Lamp");
                lamp->SetActorLocation({2.0f, 2.5f, 2.0f});
                FPointLightComponent point;
                point.Light.Intensity = 12.0f;
                point.Light.Radius = 12.0f;
                point.Mobility = ELightMobility::Static;
                lamp->AddComponent<FPointLightComponent>(point);
                auto* play = world->SpawnActor<ANavMeshBoundsVolume>("PlayVolume");
                play->SetActorLocation({0.0f, 1.5f, 0.0f});
                play->SetActorScale({6.0f, 3.0f, 6.0f});
                REQUIRE(FMapSerializer(world).Serialize(mapFile));
            }

            FLightmassSettings settings;
            ApplyLightingBuildQuality(ELightingBuildQuality::Preview, settings);
            settings.LightmapResolution = 8;
            settings.SamplesPerTexel = 2;
            settings.NumIndirectBounces = 1;
            const FLightmassBakeResult bake = FLightmass::BakeMap(mapFile, settings, true);
            REQUIRE_MESSAGE(bake.bSuccess, bake.Message);
            REQUIRE(fs::exists(probeFile));
            // Validating a map points the asset manager at its folder; put it back afterwards.
            const std::string previousContent = UAssetManager::GetContentRoot();
            std::string message;
            CHECK_MESSAGE(FLightmass::ValidateMap(mapFile, message), message);
            CHECK(message.find("Light probes") != std::string::npos);
            // A second bake finds everything in place.
            CHECK(FLightmass::BakeMap(mapFile, settings, false).Message == "Lightmap cache valid");

            // The game finds its files through the project.
            const std::string previousProject = FProjectPaths::ProjectDir();
            FProjectPaths::SetProjectRoot(project.string());
            const auto loadWorld = [&mapFile]() {
                auto world = UWorld::Create("WardLoaded");
                REQUIRE(FMapSerializer(world).Deserialize(mapFile));
                FLightmapBakeInputs::RefreshRuntimeTrust(*world);
                return world;
            };
            {
                const auto world = loadWorld();
                CHECK(world->AreLightmapsTrusted());
                const FLightProbeGrid& probes = world->GetLightProbes();
                // The play volume is 6 x 3 x 6 m (clipped to the geometry, which starts at the floor):
                // five probes each way along the ground, and three or four layers.
                REQUIRE_FALSE(probes.IsEmpty());
                CHECK(probes.Counts.x == 5);
                CHECK(probes.Counts.z == 5);
                CHECK(probes.Counts.y >= 3);
                // The probe in the middle is inside the block.
                CHECK(probes.Valid[probes.IndexOf(2, 1, 2)] == 0);
                CHECK(probes.Valid[probes.IndexOf(0, 1, 0)] == 1);
                // In the open, on the lamp's side, there is light and it comes from where the lamp is.
                FLightProbeSample sample;
                REQUIRE(world->SampleLightProbes({2.0f, 1.0f, 0.5f}, sample));
                CHECK(sample.Irradiance({0.0f, 1.0f, 0.0f}).g > sample.Irradiance({0.0f, -1.0f, 0.0f}).g);
                CHECK(sample.Irradiance({0.0f, 1.0f, 0.0f}).g > 0.1f);
            }

            // The map changes after the bake: lightmaps and probes go together.
            std::string text;
            {
                std::ifstream in(mapFile, std::ios::binary);
                text.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            }
            {
                auto world = UWorld::Create("WardChanged");
                REQUIRE(FMapSerializer(world).DeserializeText(text));
                world->FindActorByName("Block")->SetActorLocation({1.0f, 1.5f, 0.0f});
                FLightmapBakeInputs::RefreshRuntimeTrust(*world);
                CHECK_FALSE(world->AreLightmapsTrusted());
                CHECK(world->GetLightProbes().IsEmpty());
                FLightProbeSample sample;
                CHECK_FALSE(world->SampleLightProbes({2.0f, 1.0f, 0.5f}, sample));
            }

            // A probe file left over from another bake: the lightmap is still good, the probes are not used.
            {
                FLightProbeGrid grid;
                uint64_t hash = 0;
                REQUIRE(FLightProbeAsset::Load(probeFile.string(), grid, hash));
                CHECK(hash == bake.BakeHash);
                REQUIRE(FLightProbeAsset::Save(probeFile.string(), grid, hash + 1));
                const auto world = loadWorld();
                CHECK(world->AreLightmapsTrusted());
                CHECK(world->GetLightProbes().IsEmpty());
                CHECK_FALSE(FLightmass::ValidateMap(mapFile, message));
                // And the baker does not take such a bake for finished.
                CHECK(FLightmass::BakeMap(mapFile, settings, false).Message != "Lightmap cache valid");
                CHECK_FALSE(loadWorld()->GetLightProbes().IsEmpty());
            }

            // A map that asks for no probes gets none, and says so.
            {
                auto world = UWorld::Create("WardWithout");
                REQUIRE(FMapSerializer(world).Deserialize(mapFile));
                for (const auto& actor : world->GetAllActors()) {
                    if (actor && actor->HasComponent<FWorldSettingsComponent>())
                        actor->GetComponent<FWorldSettingsComponent>().bLightProbes = false;
                }
                REQUIRE(FMapSerializer(world).Serialize(mapFile));
                const FLightmassBakeResult without = FLightmass::BakeMap(mapFile, settings, false);
                REQUIRE_MESSAGE(without.bSuccess, without.Message);
                CHECK(without.Message != "Lightmap cache valid"); // the settings changed: another bake
                const auto loaded = loadWorld();
                CHECK(loaded->AreLightmapsTrusted());
                CHECK(loaded->GetLightProbes().IsEmpty());
            }

            FProjectPaths::SetProjectRoot(previousProject);
            UAssetManager::SetContentRoot(previousContent);
            fs::remove_all(project, ec);
        }

        TEST_CASE("the probe keys of a map are read and written, and left out where they say nothing new") {
            auto src = UWorld::Create("ProbeKeys");
            AActor* env = src->SpawnActor<AActor>("Environment");
            auto& settings = env->AddComponent<FWorldSettingsComponent>();
            std::string text;
            REQUIRE(FMapSerializer(src).SerializeText(text));
            CHECK(text.find("LightProbe") == std::string::npos);

            settings.bLightProbes = false;
            settings.LightProbeSpacing = 2.25f;
            settings.LightProbeVerticalSpacing = 0.75f;
            settings.LightProbeAssetPath = "/Game/Lightmaps/ProbeKeys.lprobes";
            REQUIRE(FMapSerializer(src).SerializeText(text));
            CHECK(text.find("LightProbes: false") != std::string::npos);
            CHECK(text.find("LightProbeSpacing: 2.25") != std::string::npos);
            CHECK(text.find("LightProbeVerticalSpacing: 0.75") != std::string::npos);
            CHECK(text.find("LightProbeAsset: \"/Game/Lightmaps/ProbeKeys.lprobes\"") != std::string::npos);

            auto dst = UWorld::Create("ProbeKeysDst");
            REQUIRE(FMapSerializer(dst).DeserializeText(text));
            const FWorldSettingsComponent* restored = nullptr;
            for (const auto& actor : dst->GetAllActors()) {
                if (actor && actor->HasComponent<FWorldSettingsComponent>())
                    restored = &actor->GetComponent<FWorldSettingsComponent>();
            }
            REQUIRE(restored != nullptr);
            CHECK_FALSE(restored->bLightProbes);
            CHECK(restored->LightProbeSpacing == 2.25f);
            CHECK(restored->LightProbeVerticalSpacing == 0.75f);
            CHECK(restored->LightProbeAssetPath == "/Game/Lightmaps/ProbeKeys.lprobes");
        }
    }

} // namespace Leon
