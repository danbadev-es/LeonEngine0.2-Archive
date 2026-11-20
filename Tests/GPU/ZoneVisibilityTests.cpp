#include <doctest/doctest.h>

#include "GPU/WorldRenderFixture.hpp"

#include "Gameplay/APortal.hpp"
#include "Gameplay/AVisibilityZone.hpp"
#include "RHI/FFrameProfiler.hpp"
#include "RHI/FRenderer.hpp"

#include <chrono>
#include <cstdlib>
#include <string>

// Rooms and doorways in whole frames (FZoneVisibility.hpp, FWorldRendererRHI.cpp). The level, seen
// from above (x right, z down), floor at y = 0, rooms 3 m high:
//
//   z=-8          +-------+
//                 |   D   |        sealed: no doorway
//   z=-4  +-------+-------+-------+
//         |   A   |   B   |   C   |   doorways (1 m x 2 m) at x = -2 and x = 2, z = -2
//   z= 0  +-------+-------+-------+
//        x=-6    -2       2       6
//
// Procedural boxes are culled by a cube around them as large as their largest side, so the walls
// count as standing in every room they come near: they are always drawn. The props are small.

namespace Leon {

    namespace {

        struct FRoomsFixture : FWorldRenderFixture {
            TRef<FMaterialInstance> Paint;
            TRef<FMaterialInstance> PropPaint;

            FRoomsFixture() {
                if (!bReady)
                    return;
                SwitchFixtureLightsOff();
                UsePlainPostProcess();
                World->FindActorByName("Box")->GetComponent<FMeshComponent>().bVisible = false;
                Paint = FloorMaterial->GetParent()->CreateInstance("Paint");
                Paint->SetAlbedoColor({0.6f, 0.6f, 0.55f});
                Paint->SetRoughness(0.9f);
                PropPaint = FloorMaterial->GetParent()->CreateInstance("PropPaint");
                PropPaint->SetAlbedoColor({0.2f, 0.5f, 0.8f});
                PropPaint->SetRoughness(0.6f);

                AActor* environment = World->SpawnActor<AActor>("Environment");
                auto& sky = environment->AddComponent<FSkyboxComponent>();
                sky.bEnabled = false;
                sky.EnvironmentIntensity = 0.02f;

                const float t = 0.2f;
                Block("BackWall", {-2.0f, 1.5f, -8.0f}, 4.0f + t, 3.0f, t);
                Block("MiddleWall", {0.0f, 1.5f, -4.0f}, 12.0f + t, 3.0f, t);
                Block("FrontWall", {0.0f, 1.5f, 0.0f}, 12.0f + t, 3.0f, t);
                Block("EndWestWall", {-6.0f, 1.5f, -2.0f}, t, 3.0f, 4.0f);
                Block("EndEastWall", {6.0f, 1.5f, -2.0f}, t, 3.0f, 4.0f);
                Block("DWestWall", {-2.0f, 1.5f, -6.0f}, t, 3.0f, 4.0f);
                Block("DEastWall", {2.0f, 1.5f, -6.0f}, t, 3.0f, 4.0f);
                Block("Ceiling", {0.0f, 3.0f, -4.0f}, 12.0f, t, 8.0f);
                for (const float x : {-2.0f, 2.0f}) {
                    // A partition with a doorway 1 m wide, 2 m high, centred at z = -2.
                    Block("Partition", {x, 1.5f, -3.25f}, t, 3.0f, 1.5f);
                    Block("Partition", {x, 1.5f, -0.75f}, t, 3.0f, 1.5f);
                    Block("Lintel", {x, 2.5f, -2.0f}, t, 1.0f, 1.0f);
                }

                Zone("Zone_A", {-4.0f, 1.5f, -2.0f});
                Zone("Zone_B", {0.0f, 1.5f, -2.0f});
                Zone("Zone_C", {4.0f, 1.5f, -2.0f});
                Zone("Zone_D", {0.0f, 1.5f, -6.0f});
                Portal("Portal_AB", -2.0f, "Door_AB");
                Portal("Portal_BC", 2.0f, "Door_BC");
            }

            AActor* Block(const std::string& InName, const glm::vec3& InCenter, float InWidth, float InHeight, float InDepth,
                          const TRef<FMaterialInstance>& InMaterial = nullptr) {
                AActor* block = World->SpawnActor<AActor>(InName);
                block->SetActorLocation(InCenter);
                auto& mesh = block->AddComponent<FMeshComponent>(FMeshPrimitives::CreateBox(InWidth, InHeight, InDepth));
                mesh.MeshType = "Box";
                mesh.MeshWidth = InWidth;
                mesh.MeshHeight = InHeight;
                mesh.MeshDepth = InDepth;
                mesh.Mobility = EComponentMobility::Static;
                block->AddComponent<FMaterialComponent>(InMaterial ? InMaterial : Paint);
                return block;
            }

            /** A box 0.5 m on a side standing on the floor (culled as a 1 m cube). */
            AActor* Prop(const std::string& InName, float InX, float InZ) {
                return Block(InName, {InX, 0.25f, InZ}, 0.5f, 0.5f, 0.5f, PropPaint);
            }

            void Zone(const std::string& InName, const glm::vec3& InCenter) {
                AVisibilityZone* zone = World->SpawnActor<AVisibilityZone>(InName);
                zone->SetActorLocation(InCenter);
                // The room between the middles of its walls, from under the floor to above the ceiling.
                zone->GetComponent<FVisibilityZoneComponent>().Extent = {2.0f, 2.0f, 2.0f};
            }

            void Portal(const std::string& InName, float InX, const std::string& InDoor) {
                APortal* portal = World->SpawnActor<APortal>(InName);
                portal->SetActorLocation({InX, 1.0f, -2.0f});
                portal->SetActorRotation({0.0f, 90.0f, 0.0f}); // the rectangle across the partition
                auto& opening = portal->GetComponent<FPortalComponent>();
                opening.Size = {1.0f, 2.0f};
                opening.Doors = {InDoor};
            }

            FPointLightComponent& Bulb(const glm::vec3& InPosition, float InRadius) {
                AActor* lamp = World->SpawnActor<AActor>("Bulb");
                lamp->SetActorLocation(InPosition);
                FPointLight point;
                point.Color = {1.0f, 0.9f, 0.8f};
                point.Intensity = 12.0f;
                point.Radius = InRadius;
                FPointLightComponent& component = lamp->AddComponent<FPointLightComponent>(point);
                component.bCastShadows = true;
                component.Mobility = ELightMobility::Stationary;
                return component;
            }

            void Look(const glm::vec3& InPosition, float InYaw, float InPitch = -5.0f) {
                Camera.SetPosition(InPosition);
                Camera.SetRotation(InPitch, InYaw);
            }

            FWorldRenderer& Renderer() { return *World->GetWorldRenderer(); }
            const FFrameTiming& Frame() const { return FFrameProfiler::Working(); }
        };

        /** Pixels where InB has a channel more than InLevels above (InSign 1) or below (-1) InA's. */
        uint32_t PixelsMoved(const FImage& InA, const FImage& InB, int InLevels, int InSign) {
            uint32_t moved = 0;
            for (size_t i = 0; i < InA.Pixels.size(); i += 4) {
                for (size_t c = 0; c < 3; ++c) {
                    const int delta = static_cast<int>(InB.Pixels[i + c]) - static_cast<int>(InA.Pixels[i + c]);
                    if (delta * InSign > InLevels) {
                        ++moved;
                        break;
                    }
                }
            }
            return moved;
        }

    } // namespace

    TEST_SUITE("World renderer - zones") {

        TEST_CASE_FIXTURE(FRoomsFixture, "What is in a room the camera cannot see into is not drawn; it is when the doorway comes into view") {
            REQUIRE(bReady);
            Prop("B_OnTheLine", 0.0f, -2.0f);
            Prop("B_BehindTheWall", -1.2f, -3.4f);
            Prop("C_OnTheLine", 4.0f, -2.0f);

            // In A's corner by the partition, looking along it: B and C are in front of the camera,
            // behind the wall, and the doorway is far to the side.
            Look({-2.5f, 1.5f, -3.6f}, 0.0f);
            Render(1);
            CHECK(Frame().VisibleZones == 1);
            CHECK(Frame().ZoneCulledActors == 3);

            // Back, facing the doorway: through it B, and through the next one C.
            Look({-5.5f, 1.5f, -2.0f}, 0.0f);
            Render(1);
            CHECK(Frame().VisibleZones == 3);
            // Only the prop in B that the wall beside the doorway hides.
            CHECK(Frame().ZoneCulledActors == 1);

            // A map that turns zones off draws them all.
            Renderer().SetZoneVisibilityEnabled(false);
            Render(1);
            CHECK(Frame().VisibleZones == 0);
            CHECK(Frame().ZoneCulledActors == 0);
            CHECK(NewValidationErrors() == 0);
        }

        TEST_CASE_FIXTURE(FRoomsFixture, "A shut door cuts its doorway; what straddles rooms or stands outside them is still drawn") {
            REQUIRE(bReady);
            Prop("B_OnTheLine", 0.0f, -2.0f);
            Prop("B_BehindTheWall", -1.2f, -3.4f);
            Prop("C_OnTheLine", 4.0f, -2.0f);
            Look({-5.5f, 1.5f, -2.0f}, 0.0f);

            CHECK(World->SetPortalDoorClosed("Door_AB", true) == 1);
            Render(1);
            CHECK(Frame().VisibleZones == 1);
            CHECK(Frame().ZoneCulledActors == 3);

            // Across the wall between B and C, and beyond C's end wall: neither is held by one room.
            Prop("Across_BC", 2.0f, -3.5f);
            Prop("Outside", 9.0f, -2.0f);
            Render(1);
            CHECK(Frame().ZoneCulledActors == 3);

            // The door opens again; the far one shuts: B is seen, C is not.
            World->SetPortalDoorClosed("Door_AB", false);
            World->SetPortalDoorClosed("Door_BC", true);
            Render(1);
            CHECK(Frame().VisibleZones == 2);
            CHECK(Frame().ZoneCulledActors == 2); // B's hidden prop and C's
            CHECK(NewValidationErrors() == 0);
        }

        TEST_CASE_FIXTURE(FRoomsFixture, "Zones change nothing that is seen, and a light in a sealed room is left out") {
            REQUIRE(bReady);
            for (int i = 0; i < 4; ++i) {
                Prop("A_Prop", -5.0f + 0.9f * static_cast<float>(i), -3.4f);
                Prop("B_Prop", -1.2f + 0.8f * static_cast<float>(i), -3.3f + 0.4f * static_cast<float>(i & 1));
                Prop("C_Prop", 3.0f + 0.7f * static_cast<float>(i), -1.0f - 0.6f * static_cast<float>(i));
                Prop("D_Prop", -1.0f + 0.7f * static_cast<float>(i), -6.5f);
            }
            Bulb({-4.0f, 2.6f, -2.0f}, 6.0f);
            Bulb({0.5f, 2.6f, -2.5f}, 6.0f);
            Bulb({4.0f, 2.6f, -2.0f}, 6.0f);
            Bulb({0.0f, 2.6f, -6.0f}, 6.0f); // in D: shines on the middle wall, which shadows B

            const auto compare = [&](const std::string& InView) {
                Renderer().SetZoneVisibilityEnabled(false);
                const FImage without = Render(2);
                const uint32_t shadowDrawsWithout = Frame().ShadowDrawCalls;
                Renderer().SetZoneVisibilityEnabled(true);
                const FImage with = Render(2);
                CAPTURE(InView);
                CAPTURE(Frame().VisibleZones);
                CAPTURE(Frame().ZoneCulledActors);
                CAPTURE(Frame().ZoneSkippedLights);
                CAPTURE(shadowDrawsWithout);
                CAPTURE(Frame().ShadowDrawCalls);
                CHECK(Frame().ShadowDrawCalls < shadowDrawsWithout);
                // What is seen is the same. The one thing zones may take away is light that a lamp
                // shut in D let through the wall: where the wall meets the floor and the partitions,
                // nearer to its shadowing surface than the shadow bias, and where the shadow fades out
                // on surfaces it grazes (Shadows.hlsli). They never add light.
                const uint32_t brighter = PixelsMoved(without, with, 2, 1);
                const uint32_t darker = PixelsMoved(without, with, 2, -1);
                CAPTURE(brighter);
                CAPTURE(darker);
                CHECK(brighter == 0);
                CHECK(darker < kWidth * kHeight / 100);
                if (Frame().ZoneSkippedLights == 0)
                    CHECK(darker == 0);
                return Frame().ZoneSkippedLights;
            };
            Look({-5.5f, 1.5f, -2.0f}, 0.0f);
            CHECK(compare("A, through both doorways") == 1);
            Look({-5.5f, 1.6f, -0.5f}, -20.0f);
            compare("A, across to B's back corner");
            Look({1.0f, 1.5f, -1.0f}, 180.0f);
            compare("B, looking back into A");
            Look({5.5f, 1.5f, -3.5f}, 160.0f);
            compare("C, through to B");
            CHECK(NewValidationErrors() == 0);
        }

        TEST_CASE_FIXTURE(FRoomsFixture, "A level of many rooms: what zones save, counted") {
            REQUIRE(bReady);
            // A row of rooms is not much of a level: a grid of 4 x 4 rooms of 6 m, each with a lamp that
            // casts shadows and a dozen props, every neighbour joined by a doorway. The fixture's rooms
            // stay where they are, out of the way at z > -8 and out of sight.
            World->FindActorByName("Floor")->GetComponent<FMeshComponent>().bVisible = false;
            const float room = 6.0f;
            const glm::vec3 origin(-12.0f, 0.0f, -40.0f);
            Block("GridFloor", origin + glm::vec3(12.0f, -0.1f, 12.0f), 24.0f, 0.2f, 24.0f);
            Block("GridCeiling", origin + glm::vec3(12.0f, 3.1f, 12.0f), 24.0f, 0.2f, 24.0f);
            for (int i = 0; i <= 4; ++i) {
                const float line = room * static_cast<float>(i);
                for (int j = 0; j < 4; ++j) {
                    const float mid = room * (static_cast<float>(j) + 0.5f);
                    const bool bOuter = i == 0 || i == 4;
                    // A wall along x at z = line and one along z at x = line, each with a doorway inside the grid.
                    for (const int along : {0, 1}) {
                        const glm::vec3 center =
                            along == 0 ? origin + glm::vec3(mid, 1.5f, line) : origin + glm::vec3(line, 1.5f, mid);
                        const glm::vec3 across = along == 0 ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 0.0f, 1.0f);
                        const auto wall = [&](const glm::vec3& InCenter, float InLength) {
                            Block("Wall", InCenter, along == 0 ? InLength : 0.2f, 3.0f, along == 0 ? 0.2f : InLength);
                        };
                        if (bOuter) {
                            wall(center, room + 0.2f);
                            continue;
                        }
                        wall(center - across * 1.75f, 2.5f);
                        wall(center + across * 1.75f, 2.5f);
                        Block("Lintel", center + glm::vec3(0.0f, 1.0f, 0.0f), along == 0 ? 1.0f : 0.2f, 1.0f,
                              along == 0 ? 0.2f : 1.0f);
                        APortal* portal = World->SpawnActor<APortal>("GridPortal");
                        portal->SetActorLocation(center + glm::vec3(0.0f, -0.5f, 0.0f));
                        portal->SetActorRotation({0.0f, along == 0 ? 0.0f : 90.0f, 0.0f});
                        portal->GetComponent<FPortalComponent>().Size = {1.0f, 2.0f};
                    }
                }
            }
            for (int x = 0; x < 4; ++x) {
                for (int z = 0; z < 4; ++z) {
                    const glm::vec3 corner = origin + glm::vec3(room * static_cast<float>(x), 0.0f, room * static_cast<float>(z));
                    AVisibilityZone* zone = World->SpawnActor<AVisibilityZone>("GridZone");
                    zone->SetActorLocation(corner + glm::vec3(room * 0.5f, 1.5f, room * 0.5f));
                    zone->GetComponent<FVisibilityZoneComponent>().Extent = {room * 0.5f, 2.0f, room * 0.5f};
                    for (int p = 0; p < 12; ++p)
                        Prop("GridProp", corner.x + 1.0f + 0.37f * static_cast<float>(p % 4) * 3.0f,
                             corner.z + 1.0f + 1.3f * static_cast<float>(p / 4));
                    Bulb(corner + glm::vec3(room * 0.5f, 2.6f, room * 0.5f), 7.0f);
                }
            }
            // The flashlight, from a corner room down the grid.
            const glm::vec3 eye = origin + glm::vec3(1.5f, 1.6f, 3.0f);
            AddSpotLight("Flashlight", eye + glm::vec3(0.2f, -0.3f, 0.0f), eye + glm::vec3(10.0f, -1.0f, 0.0f), 40.0f, 26.0f,
                         11.0f, 26.0f)
                .Mobility = ELightMobility::Movable;
            Look(eye, 0.0f);

            struct FCount {
                uint32_t DrawCalls = 0;
                uint32_t Triangles = 0;
                uint32_t ShadowDraws = 0;
                uint32_t Zones = 0;
                uint32_t Culled = 0;
                uint32_t Skipped = 0;
            };
            const auto count = [&](bool bInZones) {
                Renderer().SetZoneVisibilityEnabled(bInZones);
                Render(2);
                FCount counted;
                counted.DrawCalls = FRenderer::GetStats().DrawCalls;
                counted.Triangles = FRenderer::GetStats().TriangleCount;
                counted.ShadowDraws = Frame().ShadowDrawCalls;
                counted.Zones = Frame().VisibleZones;
                counted.Culled = Frame().ZoneCulledActors;
                counted.Skipped = Frame().ZoneSkippedLights;
                return counted;
            };
            const FCount without = count(false);
            const FCount with = count(true);
            MESSAGE("16 rooms, 24 doorways, 16 shadowed lamps + flashlight; zones off: ", without.DrawCalls, " draws (",
                    without.Triangles, " triangles), ", without.ShadowDraws, " shadow draws; zones on: ", with.DrawCalls,
                    " draws (", with.Triangles, " triangles), ", with.ShadowDraws, " shadow draws, ", with.Zones,
                    " rooms seen, ", with.Culled, " actors and ", with.Skipped, " shadowed lamps left out");
            CHECK(with.Zones < 16);
            CHECK(with.Culled > 0);
            CHECK(with.Triangles < without.Triangles);
            CHECK(with.DrawCalls <= without.DrawCalls);
            CHECK(with.ShadowDraws < without.ShadowDraws);

            // On the graphics card, what it costs the CPU.
            if (std::getenv("LEON_TEST_GPU") != nullptr) {
                IRHIDevice* device = FRHI::GetDevice();
                for (const bool bZones : {false, true}) {
                    Renderer().SetZoneVisibilityEnabled(bZones);
                    Render(10);
                    float total = 0.0f;
                    for (int frame = 0; frame < 100; ++frame) {
                        IRHICommandList& commands = device->BeginFrame();
                        FRHIFrame::Begin(commands, *Target);
                        const auto start = std::chrono::steady_clock::now();
                        Renderer().Render(Camera);
                        total += std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start).count();
                        FRHIFrame::End();
                        device->EndFrame();
                        FRHIFrame::RunAfterFrameTasks();
                    }
                    device->WaitIdle();
                    MESSAGE(std::string(bZones ? "zones on" : "zones off"), ": CPU render ", total / 100.0f, " ms a frame (mean of 100)");
                }
            }
            CHECK(NewValidationErrors() == 0);
        }
    }

} // namespace Leon
