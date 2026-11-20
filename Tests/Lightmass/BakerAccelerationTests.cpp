#include <doctest/doctest.h>

#include "Lightmass/FLightBaker.hpp"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <vector>

// The baker finds what a ray hits through a tree of boxes and shares the texels out between
// threads. Neither may change the picture: the tree has to agree with trying every triangle, and
// one thread with many.

namespace Leon {

    namespace {

        /** Small repeatable generator: the tests must not depend on the standard library's. */
        struct FRandom {
            uint32_t State = 0x1234ABCDu;
            float Next() {
                State = State * 1664525u + 1013904223u;
                return static_cast<float>(State >> 8) / 16777216.0f;
            }
            glm::vec3 InBox(float InHalf) {
                return {(Next() * 2.0f - 1.0f) * InHalf, (Next() * 2.0f - 1.0f) * InHalf, (Next() * 2.0f - 1.0f) * InHalf};
            }
        };

        void AddTriangle(FLightBakerScene& InOutScene, const glm::vec3& InA, const glm::vec3& InB, const glm::vec3& InC,
                         bool bCastShadow = true, uint32_t InChart = 0) {
            const uint32_t base = static_cast<uint32_t>(InOutScene.Vertices.size());
            const glm::vec3 normal = glm::normalize(glm::cross(InB - InA, InC - InA));
            for (const glm::vec3& p : {InA, InB, InC}) {
                FBakeVertex vertex;
                vertex.Position = p;
                vertex.Normal = normal;
                InOutScene.Vertices.push_back(vertex);
            }
            InOutScene.Triangles.push_back({base, base + 1, base + 2, InChart, bCastShadow});
        }

        FLightBakerScene TriangleSoup(uint32_t InCount) {
            FLightBakerScene scene;
            FRandom random;
            for (uint32_t i = 0; i < InCount; ++i) {
                const glm::vec3 centre = random.InBox(10.0f);
                AddTriangle(scene, centre + random.InBox(0.6f), centre + random.InBox(0.6f), centre + random.InBox(0.6f),
                            (i % 3) != 0);
            }
            return scene;
        }

        /** A floor lit by a point light with a small blocker above it, in one chart. */
        FLightBakerScene LitFloor() {
            FLightBakerScene scene;
            auto floorVertex = [&](float InX, float InZ, float InU, float InV) {
                FBakeVertex vertex;
                vertex.Position = {InX, 0.0f, InZ};
                vertex.Normal = {0.0f, 1.0f, 0.0f};
                vertex.LightmapUV = {InU, InV};
                scene.Vertices.push_back(vertex);
            };
            floorVertex(-4.0f, -4.0f, 0.0f, 0.0f);
            floorVertex(4.0f, -4.0f, 1.0f, 0.0f);
            floorVertex(4.0f, 4.0f, 1.0f, 1.0f);
            floorVertex(-4.0f, 4.0f, 0.0f, 1.0f);
            scene.Triangles.push_back({0, 2, 1, 0, true});
            scene.Triangles.push_back({0, 3, 2, 0, true});
            // The blocker belongs to no chart (index past the end): it only casts.
            AddTriangle(scene, {-1.0f, 1.0f, -1.0f}, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, -1.0f}, true, 99);
            AddTriangle(scene, {-1.0f, 1.0f, -1.0f}, {-1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f}, true, 99);

            FLightmapChart chart;
            chart.Resolution = 32;
            chart.Scale = {1.0f, 1.0f};
            chart.Bias = {0.0f, 0.0f};
            scene.Charts.push_back(chart);
            scene.AtlasWidth = 32;
            scene.AtlasHeight = 32;

            FBakePointLight light;
            light.Light.Position = {0.0f, 3.0f, 0.0f};
            light.Light.Color = {1.0f, 0.9f, 0.8f};
            light.Light.Intensity = 20.0f;
            light.Light.Radius = 12.0f;
            scene.PointLights.push_back(light);
            return scene;
        }

    } // namespace

    TEST_SUITE("Lightmap baker acceleration") {

        TEST_CASE("the tree finds the same triangle as trying them all") {
            const FLightBakerScene brute = TriangleSoup(1500);
            FLightBakerScene treed = brute;
            FLightBaker::BuildAcceleration(treed);
            REQUIRE(treed.Acceleration != nullptr);
            REQUIRE(brute.Acceleration == nullptr);

            FRandom random;
            random.State = 0xBEEF1234u;
            int hits = 0;
            int disagreements = 0;
            for (int i = 0; i < 4000; ++i) {
                const glm::vec3 origin = random.InBox(12.0f);
                glm::vec3 direction = random.InBox(1.0f);
                if (glm::dot(direction, direction) < 1.0e-4f)
                    direction = {0.0f, 1.0f, 0.0f};
                direction = glm::normalize(direction);
                const float maxT = (i % 5 == 0) ? 3.0f : 1.0e6f; // short rays too

                float tA = 0.0f, tB = 0.0f;
                uint32_t triA = 0, triB = 0;
                glm::vec3 baryA(0.0f), baryB(0.0f);
                const bool hitA = FLightBaker::IntersectScene(brute, origin, direction, maxT, tA, triA, baryA);
                const bool hitB = FLightBaker::IntersectScene(treed, origin, direction, maxT, tB, triB, baryB);
                if (hitA != hitB || (hitA && (triA != triB || tA != tB)))
                    ++disagreements;
                hits += hitA ? 1 : 0;

                if (FLightBaker::IsOccluded(brute, origin, direction, maxT) !=
                    FLightBaker::IsOccluded(treed, origin, direction, maxT))
                    ++disagreements;
            }
            CHECK(disagreements == 0);
            CHECK(hits > 400); // the rays do meet the soup: the comparison is not vacuous
        }

        TEST_CASE("rays along the axes and from inside the geometry's box") {
            FLightBakerScene scene;
            // A wall facing +X at x = 2, made of two triangles.
            AddTriangle(scene, {2.0f, -1.0f, -1.0f}, {2.0f, 1.0f, 1.0f}, {2.0f, -1.0f, 1.0f});
            AddTriangle(scene, {2.0f, -1.0f, -1.0f}, {2.0f, 1.0f, -1.0f}, {2.0f, 1.0f, 1.0f});
            for (int i = 0; i < 40; ++i) // enough other triangles for the tree to have depth
                AddTriangle(scene, {-5.0f - i, 0.0f, 0.0f}, {-5.0f - i, 1.0f, 0.0f}, {-5.0f - i, 0.0f, 1.0f});
            FLightBaker::BuildAcceleration(scene);

            float t = 0.0f;
            uint32_t tri = 0;
            glm::vec3 bary(0.0f);
            // Straight along +X: two components of the direction are exactly zero.
            REQUIRE(FLightBaker::IntersectScene(scene, {0.0f, 0.2f, 0.3f}, {1.0f, 0.0f, 0.0f}, 100.0f, t, tri, bary));
            CHECK(t == doctest::Approx(2.0f));
            CHECK(tri < 2);
            // Stops short of the wall.
            CHECK_FALSE(FLightBaker::IntersectScene(scene, {0.0f, 0.2f, 0.3f}, {1.0f, 0.0f, 0.0f}, 1.5f, t, tri, bary));
            // Away from everything.
            CHECK_FALSE(FLightBaker::IntersectScene(scene, {3.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, 100.0f, t, tri, bary));
        }

        TEST_CASE("only surfaces that cast shadows block a light, wherever they are along the ray") {
            FLightBakerScene scene;
            // From the origin towards +Z: first a sheet that casts nothing, then one that does.
            AddTriangle(scene, {-1.0f, -1.0f, 1.0f}, {1.0f, -1.0f, 1.0f}, {0.0f, 2.0f, 1.0f}, false);
            AddTriangle(scene, {-1.0f, -1.0f, 2.0f}, {1.0f, -1.0f, 2.0f}, {0.0f, 2.0f, 2.0f}, true);
            const glm::vec3 origin(0.0f);
            const glm::vec3 forward(0.0f, 0.0f, 1.0f);

            for (int pass = 0; pass < 2; ++pass) {
                // The caster behind the transparent sheet still blocks.
                CHECK(FLightBaker::IsOccluded(scene, origin, forward, 10.0f));
                // A light between the two sheets is reached: only the non-caster is in the way.
                CHECK_FALSE(FLightBaker::IsOccluded(scene, origin, forward, 1.5f));
                FLightBaker::BuildAcceleration(scene); // and the same through the tree
            }
        }

        TEST_CASE("the baked picture does not depend on the number of threads") {
            const FLightBakerScene scene = LitFloor();
            FLightBakerSettings settings;
            settings.SamplesPerTexel = 8;
            settings.NumIndirectBounces = 2;

            std::vector<float> one, many;
            settings.NumThreads = 1;
            FLightBaker::Bake(scene, settings, one);
            settings.NumThreads = 7;
            FLightBaker::Bake(scene, settings, many);
            REQUIRE(one.size() == 32u * 32u * 4u);
            REQUIRE(one.size() == many.size());
            CHECK(one == many);

            // And the picture is a lit floor with a shadow under the blocker, not a blank.
            auto red = [&](int InX, int InY) { return one[(static_cast<size_t>(InY) * 32 + InX) * 4]; };
            // Off the blocker's diagonal: a ray through the exact seam of its two triangles may slip
            // between them, which says nothing about threads.
            const float underBlocker = red(18, 14);
            const float inTheOpen = red(26, 16);
            CHECK(inTheOpen > 0.5f);
            CHECK(underBlocker < inTheOpen * 0.5f);
        }

        TEST_CASE("the indirect light is smoothed within a chart, keeps its level, and does not leak into another chart") {
            // A floor under a low ceiling, lit only by a lamp shining up at a patch of the ceiling: the
            // floor sees the light only by its bounce, which a few rays a texel catch unevenly.
            FLightBakerScene scene;
            auto quad = [&](float InY, float InNormalY, uint32_t InChart, float InU0) {
                const uint32_t base = static_cast<uint32_t>(scene.Vertices.size());
                const float corners[4][2] = {{-4.0f, -4.0f}, {4.0f, -4.0f}, {4.0f, 4.0f}, {-4.0f, 4.0f}};
                for (const auto& c : corners) {
                    FBakeVertex vertex;
                    vertex.Position = {c[0], InY, c[1]};
                    vertex.Normal = {0.0f, InNormalY, 0.0f};
                    vertex.LightmapUV = {InU0 + (c[0] + 4.0f) / 16.0f, (c[1] + 4.0f) / 8.0f};
                    scene.Vertices.push_back(vertex);
                }
                if (InNormalY > 0.0f) {
                    scene.Triangles.push_back({base, base + 2, base + 1, InChart, true});
                    scene.Triangles.push_back({base, base + 3, base + 2, InChart, true});
                } else {
                    scene.Triangles.push_back({base, base + 1, base + 2, InChart, true});
                    scene.Triangles.push_back({base, base + 2, base + 3, InChart, true});
                }
            };
            quad(0.0f, 1.0f, 0, 0.0f);   // the floor: left half of the atlas
            quad(2.5f, -1.0f, 1, 0.5f);  // the ceiling: right half
            for (uint32_t i = 0; i < 2; ++i) {
                FLightmapChart chart;
                chart.Resolution = 32;
                chart.Scale = {1.0f, 1.0f};
                chart.Bias = {0.0f, 0.0f};
                scene.Charts.push_back(chart);
            }
            scene.AtlasWidth = 64;
            scene.AtlasHeight = 32;
            FBakeSpotLight lamp;
            lamp.Light.Position = {0.0f, 0.3f, 0.0f};
            lamp.Light.Direction = {0.0f, 1.0f, 0.0f};
            lamp.Light.Color = {1.0f, 1.0f, 1.0f};
            lamp.Light.Intensity = 40.0f;
            lamp.Light.Radius = 10.0f;
            lamp.Light.CutOff = 20.0f;
            lamp.Light.OuterCutOff = 30.0f;
            scene.SpotLights.push_back(lamp);

            FLightBakerSettings settings;
            settings.SamplesPerTexel = 8;
            settings.NumIndirectBounces = 1;
            settings.bAmbientOcclusion = false;
            std::vector<float> raw, smooth;
            settings.IndirectFilterPasses = 0;
            FLightBaker::Bake(scene, settings, raw);
            settings.IndirectFilterPasses = 3;
            FLightBaker::Bake(scene, settings, smooth);
            REQUIRE(raw.size() == 64u * 32u * 4u);

            // Over the floor (texels well inside its chart): how much a texel differs from the next,
            // and the mean.
            auto measure = [&](const std::vector<float>& InPixels, float& OutMean, float& OutRoughness) {
                double sum = 0.0, diff = 0.0;
                int count = 0;
                for (int y = 4; y < 28; ++y) {
                    for (int x = 4; x < 27; ++x) {
                        const float a = InPixels[(static_cast<size_t>(y) * 64 + x) * 4];
                        const float b = InPixels[(static_cast<size_t>(y) * 64 + x + 1) * 4];
                        sum += a;
                        diff += std::abs(a - b);
                        ++count;
                    }
                }
                OutMean = static_cast<float>(sum / count);
                OutRoughness = static_cast<float>(diff / count);
            };
            float rawMean = 0.0f, rawRough = 0.0f, smoothMean = 0.0f, smoothRough = 0.0f;
            measure(raw, rawMean, rawRough);
            measure(smooth, smoothMean, smoothRough);
            MESSAGE("floor mean " << rawMean << " -> " << smoothMean << ", texel to texel " << rawRough << " -> " << smoothRough);
            REQUIRE(rawMean > 0.01f);         // the floor is lit by the bounce
            CHECK(smoothRough < rawRough * 0.4f);
            CHECK(smoothMean == doctest::Approx(rawMean).epsilon(0.1));

            // The ceiling is lit directly where the lamp points; the floor's chart beside it in the
            // atlas must not have bled into it, nor it into the floor: the floor texels next to the
            // seam are no brighter than the floor's own mean allows.
            for (int y = 4; y < 28; ++y) {
                const float floorAtSeam = smooth[(static_cast<size_t>(y) * 64 + 31) * 4];
                CHECK(floorAtSeam < smoothMean * 3.0f + 0.05f);
            }
        }

        TEST_CASE("light on the far side of a thin surface does not come through it") {
            // The blocker is lit from above and seen from below. Its lit face used to be what the
            // floor under it received, as if the sheet glowed: brighter in its shadow than outside.
            const FLightBakerScene scene = LitFloor();
            FLightBakerSettings settings;
            settings.SamplesPerTexel = 32;
            settings.NumIndirectBounces = 1;
            settings.bAmbientOcclusion = false;
            std::vector<float> baked;
            FLightBaker::Bake(scene, settings, baked);
            auto red = [&](int InX, int InY) { return baked[(static_cast<size_t>(InY) * 32 + InX) * 4]; };
            const float underBlocker = red(18, 14);
            const float inTheOpen = red(26, 16);
            CHECK(inTheOpen > 0.5f);
            CHECK(underBlocker < inTheOpen * 0.25f);
        }

        TEST_CASE("vertices without a normal, or with normals that cancel out, bake like the triangle's plane") {
            FLightBakerSettings settings;
            settings.SamplesPerTexel = 8;
            settings.NumIndirectBounces = 2;
            settings.NumThreads = 2;
            const FLightBakerScene reference = LitFloor();
            std::vector<float> expected;
            FLightBaker::Bake(reference, settings, expected);

            // The floor exported without normals: every one of them is zero.
            FLightBakerScene bare = LitFloor();
            for (uint32_t i = 0; i < 4; ++i)
                bare.Vertices[i].Normal = glm::vec3(0.0f);
            // And the blocker with normals that cancel out half way along an edge: rays that reach it
            // there find no direction to shade it by.
            bare.Vertices[4].Normal = {0.0f, 1.0f, 0.0f};
            bare.Vertices[5].Normal = {0.0f, -1.0f, 0.0f};
            bare.Vertices[6].Normal = {0.0f, -1.0f, 0.0f};
            std::vector<float> baked;
            FLightBaker::Bake(bare, settings, baked);

            REQUIRE(baked.size() == expected.size());
            size_t notFinite = 0;
            float floorDifference = 0.0f;
            float brightest = 0.0f;
            for (size_t i = 0; i < baked.size(); ++i) {
                notFinite += std::isfinite(baked[i]) ? 0 : 1;
                brightest = std::max(brightest, expected[i]);
            }
            CHECK(notFinite == 0);
            // The floor is lit as with its real normals: straight up is the plane of its triangles. (The
            // blocker's faked normals change what bounces off it, which is a small part of the light.)
            for (size_t i = 0; i < baked.size(); i += 4)
                floorDifference = std::max(floorDifference, std::abs(baked[i] - expected[i]));
            REQUIRE(brightest > 1.0f);
            CHECK(floorDifference < 0.1f * brightest);
        }

        TEST_CASE("texels around a chart are filled several texels deep") {
            // The chart covers the middle of the atlas only; filtering reads beyond its edge.
            FLightBakerScene scene = LitFloor();
            scene.Charts[0].Scale = {0.5f, 0.5f};
            scene.Charts[0].Bias = {0.25f, 0.25f};
            scene.Triangles.resize(2); // no blocker: an evenly lit floor
            scene.Vertices.resize(4);
            FLightBakerSettings settings;
            settings.SamplesPerTexel = 1;
            settings.NumIndirectBounces = 0;
            settings.bAmbientOcclusion = false;
            std::vector<float> baked;
            FLightBaker::Bake(scene, settings, baked);

            auto texel = [&](int InX, int InY) { return &baked[(static_cast<size_t>(InY) * 32 + InX) * 4]; };
            // Inside the chart: covered and lit.
            CHECK(texel(16, 16)[3] == 1.0f);
            CHECK(texel(16, 16)[0] > 0.1f);
            // Three texels outside its left edge (the chart starts at x = 8): not covered, but lit.
            CHECK(texel(5, 16)[3] == 0.0f);
            CHECK(texel(5, 16)[0] > 0.05f);
            // Far from any chart nothing is invented.
            CHECK(texel(0, 0)[0] == 0.0f);
        }

        TEST_CASE("a scene of a hundred thousand triangles is traced in a moment") {
            // Trying every triangle for every ray made a bake of imported meshes a matter of hours.
            const FLightBakerScene scene = TriangleSoup(100000);
            FLightBaker::BuildAcceleration(scene);
            FRandom random;
            float t = 0.0f;
            uint32_t tri = 0;
            glm::vec3 bary(0.0f);
            int hits = 0;
            const auto start = std::chrono::steady_clock::now();
            for (int i = 0; i < 20000; ++i) {
                const glm::vec3 origin = random.InBox(12.0f);
                const glm::vec3 direction = glm::normalize(random.InBox(1.0f) + glm::vec3(0.001f));
                hits += FLightBaker::IntersectScene(scene, origin, direction, 1.0e6f, t, tri, bary) ? 1 : 0;
            }
            const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            CHECK(hits > 1000);
            // Without the tree these 20 000 rays are two thousand million triangle tests, minutes in a
            // debug build. The limit is far above what the tree needs, so a slow machine still passes.
            CHECK(seconds < 20.0);
        }
    }

} // namespace Leon
