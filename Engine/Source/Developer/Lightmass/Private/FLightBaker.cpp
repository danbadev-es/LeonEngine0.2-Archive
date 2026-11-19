#include "Lightmass/FLightBaker.hpp"
#include "AssetPipeline/FLightmapUV.hpp"
#include "RenderCore/FLightAttenuation.hpp"
#include "RenderCore/FIBLMath.hpp"
#include "Core/FLog.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <thread>

namespace Leon {
    namespace {

        constexpr float kPI = 3.14159265358979323846f;
        constexpr float kEpsilon = 1e-4f;

        uint32_t HashCombine(uint64_t Seed, uint32_t X, uint32_t Y, uint32_t Sample) {
            uint64_t h = Seed;
            h ^= static_cast<uint64_t>(X) + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
            h ^= static_cast<uint64_t>(Y) + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
            h ^= static_cast<uint64_t>(Sample) + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
            return static_cast<uint32_t>(h);
        }

        float RadicalInverseVdC(uint32_t bits) {
            bits = (bits << 16u) | (bits >> 16u);
            bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
            bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
            bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
            bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
            return static_cast<float>(bits) * 2.3283064365386963e-10f;
        }

        glm::vec2 HammersleyLocal(uint32_t i, uint32_t N) {
            return {static_cast<float>(i) / static_cast<float>(N), RadicalInverseVdC(i)};
        }

        bool RayTriangle(const glm::vec3& Orig, const glm::vec3& Dir, const glm::vec3& V0, const glm::vec3& V1,
                         const glm::vec3& V2, float& OutT, glm::vec3& OutBary) {
            glm::vec3 e1 = V1 - V0;
            glm::vec3 e2 = V2 - V0;
            glm::vec3 pvec = glm::cross(Dir, e2);
            float det = glm::dot(e1, pvec);
            if (std::abs(det) < 1e-8f)
                return false;
            float invDet = 1.0f / det;
            glm::vec3 tvec = Orig - V0;
            float u = glm::dot(tvec, pvec) * invDet;
            if (u < 0.0f || u > 1.0f)
                return false;
            glm::vec3 qvec = glm::cross(tvec, e1);
            float v = glm::dot(Dir, qvec) * invDet;
            if (v < 0.0f || u + v > 1.0f)
                return false;
            float t = glm::dot(e2, qvec) * invDet;
            if (t <= kEpsilon)
                return false;
            OutT = t;
            OutBary = {1.0f - u - v, u, v};
            return true;
        }

        /** How often SurfaceNormal had to fall back during the bake in progress; reported at its end. */
        std::atomic<uint64_t> GNormalFallbacks{0};

        bool IsFinite(const glm::vec3& InValue) {
            return std::isfinite(InValue.x) && std::isfinite(InValue.y) && std::isfinite(InValue.z);
        }

        /**
         * The normal of a triangle at a point of it, from the normals of its corners. Corners without
         * a normal (a mesh exported without them) or with normals that cancel out would give a vector
         * of no length, and normalising that is not a number: it then poisons every texel that sees the
         * surface. Such a point takes the triangle's own plane instead.
         */
        glm::vec3 SurfaceNormal(const FBakeVertex& InV0, const FBakeVertex& InV1, const FBakeVertex& InV2,
                                const glm::vec3& InBary) {
            const glm::vec3 shading = FLightmapUV::Interpolate(InV0.Normal, InV1.Normal, InV2.Normal, InBary);
            const float shadingLength = glm::dot(shading, shading);
            if (std::isfinite(shadingLength) && shadingLength > 1.0e-12f)
                return shading / std::sqrt(shadingLength);
            GNormalFallbacks.fetch_add(1, std::memory_order_relaxed);
            const glm::vec3 plane = glm::cross(InV1.Position - InV0.Position, InV2.Position - InV0.Position);
            const float planeLength = glm::dot(plane, plane);
            if (std::isfinite(planeLength) && planeLength > 1.0e-20f)
                return plane / std::sqrt(planeLength);
            return {0.0f, 1.0f, 0.0f};
        }

    } // namespace

    float FLightBaker::PointAttenuation(float InDistance, float InRadius) {
        return DistanceAttenuationUE4(InDistance, InRadius);
    }

    float FLightBaker::SpotConeFactor(const glm::vec3& InLightDir, const glm::vec3& InToLight, float InCutOffDeg,
                                      float InOuterCutOffDeg) {
        return SpotConeAttenuation(InLightDir, InToLight, InCutOffDeg, InOuterCutOffDeg);
    }

    glm::vec3 FLightBaker::CosineSampleHemisphere(const glm::vec3& InNormal, float InU1, float InU2) {
        return ::Leon::CosineSampleHemisphere(glm::vec2(InU1, InU2), InNormal);
    }

    /**
     * A bounding volume hierarchy over the scene's triangles. A node with a count is a leaf holding
     * that many entries of Order; otherwise its two children are Nodes[First] and Nodes[First + 1].
     */
    struct FBakeAcceleration {
        struct FNode {
            glm::vec3 Min{0.0f};
            uint32_t First = 0;
            glm::vec3 Max{0.0f};
            uint32_t Count = 0;
        };
        std::vector<FNode> Nodes;
        std::vector<uint32_t> Order;
        size_t TriangleCount = 0;
        size_t VertexCount = 0;
    };

    namespace {

        constexpr uint32_t kLeafTriangles = 4;
        constexpr int kTreeStack = 64;

        void BuildNode(FBakeAcceleration& InOutTree, uint32_t InNode, const std::vector<glm::vec3>& InCentroids,
                       const std::vector<glm::vec3>& InMins, const std::vector<glm::vec3>& InMaxs) {
            const uint32_t first = InOutTree.Nodes[InNode].First;
            const uint32_t count = InOutTree.Nodes[InNode].Count;
            glm::vec3 low(1.0e30f);
            glm::vec3 high(-1.0e30f);
            glm::vec3 centroidLow(1.0e30f);
            glm::vec3 centroidHigh(-1.0e30f);
            for (uint32_t i = first; i < first + count; ++i) {
                const uint32_t tri = InOutTree.Order[i];
                low = glm::min(low, InMins[tri]);
                high = glm::max(high, InMaxs[tri]);
                centroidLow = glm::min(centroidLow, InCentroids[tri]);
                centroidHigh = glm::max(centroidHigh, InCentroids[tri]);
            }
            InOutTree.Nodes[InNode].Min = low;
            InOutTree.Nodes[InNode].Max = high;
            if (count <= kLeafTriangles)
                return;

            // Split in two halves along the axis the centres spread most.
            const glm::vec3 extent = centroidHigh - centroidLow;
            int axis = 0;
            if (extent.y > extent.x)
                axis = 1;
            if (extent.z > extent[axis])
                axis = 2;
            if (extent[axis] <= 0.0f)
                return; // every centre in one place: nothing to gain from splitting
            const uint32_t middle = first + count / 2;
            std::nth_element(
                InOutTree.Order.begin() + first, InOutTree.Order.begin() + middle,
                InOutTree.Order.begin() + first + count,
                [&](uint32_t InA, uint32_t InB) { return InCentroids[InA][axis] < InCentroids[InB][axis]; });

            const uint32_t left = static_cast<uint32_t>(InOutTree.Nodes.size());
            InOutTree.Nodes.emplace_back();
            InOutTree.Nodes.emplace_back();
            InOutTree.Nodes[left].First = first;
            InOutTree.Nodes[left].Count = middle - first;
            InOutTree.Nodes[left + 1].First = middle;
            InOutTree.Nodes[left + 1].Count = first + count - middle;
            InOutTree.Nodes[InNode].First = left;
            InOutTree.Nodes[InNode].Count = 0;
            BuildNode(InOutTree, left, InCentroids, InMins, InMaxs);
            BuildNode(InOutTree, left + 1, InCentroids, InMins, InMaxs);
        }

        /** Distance at which the ray enters the box, or a negative number when it misses before InMaxT. */
        float RayBox(const FBakeAcceleration::FNode& InNode, const glm::vec3& InOrigin, const glm::vec3& InInvDir,
                     float InMaxT) {
            const glm::vec3 t0 = (InNode.Min - InOrigin) * InInvDir;
            const glm::vec3 t1 = (InNode.Max - InOrigin) * InInvDir;
            const glm::vec3 nearT = glm::min(t0, t1);
            const glm::vec3 farT = glm::max(t0, t1);
            const float enter = std::max(std::max(nearT.x, nearT.y), std::max(nearT.z, 0.0f));
            const float exit = std::min(std::min(farT.x, farT.y), std::min(farT.z, InMaxT));
            // A little slack: a triangle lying in the face of its box must not be missed by rounding.
            return enter <= exit * 1.00001f + 1.0e-5f ? enter : -1.0f;
        }

        glm::vec3 SafeInverse(const glm::vec3& InDir) {
            const auto inverse = [](float InValue) {
                return std::abs(InValue) > 1.0e-12f ? 1.0f / InValue : (InValue < 0.0f ? -1.0e12f : 1.0e12f);
            };
            return {inverse(InDir.x), inverse(InDir.y), inverse(InDir.z)};
        }

        const FBakeAcceleration* CurrentTree(const FLightBakerScene& InScene) {
            const FBakeAcceleration* tree = InScene.Acceleration.get();
            if (tree && tree->TriangleCount == InScene.Triangles.size() &&
                tree->VertexCount == InScene.Vertices.size() && !tree->Nodes.empty())
                return tree;
            return nullptr;
        }

    } // namespace

    void FLightBaker::BuildAcceleration(const FLightBakerScene& InScene) {
        auto tree = std::make_shared<FBakeAcceleration>();
        const size_t count = InScene.Triangles.size();
        tree->TriangleCount = count;
        tree->VertexCount = InScene.Vertices.size();
        tree->Order.resize(count);
        std::vector<glm::vec3> centroids(count);
        std::vector<glm::vec3> mins(count);
        std::vector<glm::vec3> maxs(count);
        for (size_t i = 0; i < count; ++i) {
            const FBakeTriangle& tri = InScene.Triangles[i];
            const glm::vec3& a = InScene.Vertices[tri.I0].Position;
            const glm::vec3& b = InScene.Vertices[tri.I1].Position;
            const glm::vec3& c = InScene.Vertices[tri.I2].Position;
            tree->Order[i] = static_cast<uint32_t>(i);
            mins[i] = glm::min(glm::min(a, b), c);
            maxs[i] = glm::max(glm::max(a, b), c);
            centroids[i] = (a + b + c) / 3.0f;
        }
        if (count > 0) {
            tree->Nodes.reserve(count / 2 + 2);
            tree->Nodes.emplace_back();
            tree->Nodes[0].First = 0;
            tree->Nodes[0].Count = static_cast<uint32_t>(count);
            BuildNode(*tree, 0, centroids, mins, maxs);
        }
        InScene.Acceleration = std::move(tree);
    }

    bool FLightBaker::IntersectScene(const FLightBakerScene& InScene, const glm::vec3& InOrigin, const glm::vec3& InDir,
                                     float InMaxT, float& OutT, uint32_t& OutTri, glm::vec3& OutBary) {
        bool hit = false;
        float bestT = InMaxT;
        const auto testTriangle = [&](uint32_t InIndex) {
            const auto& tri = InScene.Triangles[InIndex];
            float t = 0.0f;
            glm::vec3 bary;
            if (!RayTriangle(InOrigin, InDir, InScene.Vertices[tri.I0].Position, InScene.Vertices[tri.I1].Position,
                             InScene.Vertices[tri.I2].Position, t, bary))
                return;
            // On a tie the lower index wins, as when every triangle is tried in order.
            if (t < bestT || (hit && t == bestT && InIndex < OutTri)) {
                bestT = t;
                OutT = t;
                OutTri = InIndex;
                OutBary = bary;
                hit = true;
            }
        };

        const FBakeAcceleration* tree = CurrentTree(InScene);
        if (!tree) {
            for (uint32_t ti = 0; ti < InScene.Triangles.size(); ++ti)
                testTriangle(ti);
            return hit;
        }

        const glm::vec3 invDir = SafeInverse(InDir);
        uint32_t stack[kTreeStack];
        int top = 0;
        stack[top++] = 0;
        while (top > 0) {
            const FBakeAcceleration::FNode& node = tree->Nodes[stack[--top]];
            if (RayBox(node, InOrigin, invDir, bestT) < 0.0f)
                continue;
            if (node.Count > 0) {
                for (uint32_t i = node.First; i < node.First + node.Count; ++i)
                    testTriangle(tree->Order[i]);
                continue;
            }
            if (top + 2 > kTreeStack)
                continue; // deeper than any balanced tree gets
            // The nearer child goes on last, so it is looked at first and shortens the ray.
            const float left = RayBox(tree->Nodes[node.First], InOrigin, invDir, bestT);
            const float right = RayBox(tree->Nodes[node.First + 1], InOrigin, invDir, bestT);
            if (left >= 0.0f && right >= 0.0f) {
                stack[top++] = left < right ? node.First + 1 : node.First;
                stack[top++] = left < right ? node.First : node.First + 1;
            } else if (left >= 0.0f) {
                stack[top++] = node.First;
            } else if (right >= 0.0f) {
                stack[top++] = node.First + 1;
            }
        }
        return hit;
    }

    bool FLightBaker::IsOccluded(const FLightBakerScene& InScene, const glm::vec3& InOrigin, const glm::vec3& InDir,
                                 float InMaxT) {
        const auto blocks = [&](uint32_t InIndex) {
            const auto& tri = InScene.Triangles[InIndex];
            if (!tri.bCastShadow)
                return false;
            float t = 0.0f;
            glm::vec3 bary;
            return RayTriangle(InOrigin, InDir, InScene.Vertices[tri.I0].Position, InScene.Vertices[tri.I1].Position,
                               InScene.Vertices[tri.I2].Position, t, bary) &&
                   t < InMaxT;
        };

        const FBakeAcceleration* tree = CurrentTree(InScene);
        if (!tree) {
            for (uint32_t ti = 0; ti < InScene.Triangles.size(); ++ti) {
                if (blocks(ti))
                    return true;
            }
            return false;
        }

        const glm::vec3 invDir = SafeInverse(InDir);
        uint32_t stack[kTreeStack];
        int top = 0;
        stack[top++] = 0;
        while (top > 0) {
            const FBakeAcceleration::FNode& node = tree->Nodes[stack[--top]];
            if (RayBox(node, InOrigin, invDir, InMaxT) < 0.0f)
                continue;
            if (node.Count > 0) {
                for (uint32_t i = node.First; i < node.First + node.Count; ++i) {
                    if (blocks(tree->Order[i]))
                        return true;
                }
            } else if (top + 2 <= kTreeStack) {
                stack[top++] = node.First;
                stack[top++] = node.First + 1;
            }
        }
        return false;
    }

    static bool IsShadowed(const FLightBakerScene& Scene, const glm::vec3& Origin, const glm::vec3& Dir, float MaxT) {
        // Any caster on the way to the light, not only the nearest surface: a surface that casts no
        // shadow in front of one that does used to let the light through.
        return FLightBaker::IsOccluded(Scene, Origin, Dir, MaxT);
    }

    static glm::vec3 SampleBakeEnvironment(const FLightBakerScene& Scene, const glm::vec3& Dir) {
        if (!Scene.Environment.bEnabled)
            return glm::vec3(0.0f);
        glm::vec3 li;
        if (!Scene.Environment.HDRRGBA.empty() && Scene.Environment.HDRWidth > 0 && Scene.Environment.HDRHeight > 0) {
            li = SampleEquirectangular(Scene.Environment.HDRRGBA.data(), Scene.Environment.HDRWidth,
                                       Scene.Environment.HDRHeight, Dir);
        } else {
            li = SampleAtmosphericSky(Dir, Scene.Environment.Zenith, Scene.Environment.Horizon,
                                      Scene.Environment.Ground);
        }
        return li * Scene.Environment.Intensity;
    }

    static glm::vec3 EvaluateDirectIrradiance(const FLightBakerScene& Scene, const glm::vec3& Pos,
                                              const glm::vec3& Normal, bool bDirectLightingPass) {
        glm::vec3 irradiance(0.0f);

        for (const auto& dl : Scene.DirectionalLights) {
            if (bDirectLightingPass && !dl.bContributeDirect)
                continue;
            glm::vec3 L = glm::normalize(-dl.Light.Direction);
            float NdotL = LambertNdotL(Normal, L);
            if (NdotL <= 0.0f)
                continue;
            if (IsShadowed(Scene, Pos + Normal * kEpsilon * 2.0f, L, 1e6f))
                continue;
            glm::vec3 radiance = dl.Light.Color * dl.Light.Intensity;
            irradiance += radiance * NdotL;
        }

        for (const auto& pl : Scene.PointLights) {
            if (bDirectLightingPass && !pl.bContributeDirect)
                continue;
            glm::vec3 toLight = pl.Light.Position - Pos;
            float dist = glm::length(toLight);
            if (dist < 1e-5f || dist > pl.Light.Radius)
                continue;
            glm::vec3 L = toLight / dist;
            float NdotL = LambertNdotL(Normal, L);
            if (NdotL <= 0.0f)
                continue;
            if (IsShadowed(Scene, Pos + Normal * kEpsilon * 2.0f, L, dist - kEpsilon))
                continue;
            float atten = FLightBaker::PointAttenuation(dist, pl.Light.Radius);
            glm::vec3 radiance = pl.Light.Color * pl.Light.Intensity * atten;
            irradiance += radiance * NdotL;
        }

        for (const auto& sl : Scene.SpotLights) {
            if (bDirectLightingPass && !sl.bContributeDirect)
                continue;
            glm::vec3 toLight = sl.Light.Position - Pos;
            float dist = glm::length(toLight);
            if (dist < 1e-5f || dist > sl.Light.Radius)
                continue;
            glm::vec3 L = toLight / dist;
            float NdotL = LambertNdotL(Normal, L);
            if (NdotL <= 0.0f)
                continue;
            float cone =
                FLightBaker::SpotConeFactor(sl.Light.Direction, toLight, sl.Light.CutOff, sl.Light.OuterCutOff);
            if (cone <= 0.0f)
                continue;
            if (IsShadowed(Scene, Pos + Normal * kEpsilon * 2.0f, L, dist - kEpsilon))
                continue;
            float atten = FLightBaker::PointAttenuation(dist, sl.Light.Radius) * cone;
            glm::vec3 radiance = sl.Light.Color * sl.Light.Intensity * atten;
            irradiance += radiance * NdotL;
        }

        return irradiance;
    }

    /** The normal of the side a ray arriving along InRayDir sees. */
    static glm::vec3 FacingNormal(const glm::vec3& InNormal, const glm::vec3& InRayDir) {
        return glm::dot(InNormal, InRayDir) > 0.0f ? -InNormal : InNormal;
    }

    /**
     * What the surface sends back towards a ray that reached it along InRayDir. A thin surface is lit
     * on the side the ray sees: the light that falls on its other face does not come through.
     */
    static glm::vec3 SurfaceOutgoingRadiance(const FLightBakerScene& Scene, const FBakeVertex& V0,
                                             const FBakeVertex& V1, const FBakeVertex& V2, const glm::vec3& Bary,
                                             const glm::vec3& InRayDir) {
        glm::vec3 albedo = FLightmapUV::Interpolate(V0.Albedo, V1.Albedo, V2.Albedo, Bary);
        float metallic = V0.Metallic * Bary.x + V1.Metallic * Bary.y + V2.Metallic * Bary.z;
        albedo *= (1.0f - metallic);
        glm::vec3 emissive = FLightmapUV::Interpolate(V0.Emissive, V1.Emissive, V2.Emissive, Bary);
        glm::vec3 pos = FLightmapUV::Interpolate(V0.Position, V1.Position, V2.Position, Bary);
        glm::vec3 n = FacingNormal(SurfaceNormal(V0, V1, V2, Bary), InRayDir);
        glm::vec3 E = EvaluateDirectIrradiance(Scene, pos, n, false);
        return emissive + (albedo / kPI) * E;
    }

    void FLightBaker::Bake(const FLightBakerScene& InScene, const FLightBakerSettings& InSettings,
                           std::vector<float>& OutRGBA32F) {
        const uint32_t W = InScene.AtlasWidth;
        const uint32_t H = InScene.AtlasHeight;
        OutRGBA32F.assign(static_cast<size_t>(W) * H * 4, 0.0f);
        if (W == 0 || H == 0)
            return;

        GNormalFallbacks.store(0);
        std::vector<float> coverage(static_cast<size_t>(W) * H, 0.0f);
        std::vector<glm::vec3> positions(static_cast<size_t>(W) * H, glm::vec3(0.0f));
        std::vector<glm::vec3> normals(static_cast<size_t>(W) * H, glm::vec3(0.0f, 1.0f, 0.0f));
        std::vector<glm::vec3> albedos(static_cast<size_t>(W) * H, glm::vec3(0.0f));
        std::vector<glm::vec3> emissives(static_cast<size_t>(W) * H, glm::vec3(0.0f));
        std::vector<int> chartOf(static_cast<size_t>(W) * H, -1);

        // Rasterize triangles into atlas using chart-local UV1
        for (uint32_t ti = 0; ti < InScene.Triangles.size(); ++ti) {
            const auto& tri = InScene.Triangles[ti];
            if (tri.ChartIndex >= InScene.Charts.size())
                continue;
            const auto& chart = InScene.Charts[tri.ChartIndex];
            const auto& v0 = InScene.Vertices[tri.I0];
            const auto& v1 = InScene.Vertices[tri.I1];
            const auto& v2 = InScene.Vertices[tri.I2];

            auto toAtlas = [&](const glm::vec2& uv) -> glm::vec2 { return uv * chart.Scale + chart.Bias; };

            glm::vec2 a = toAtlas(v0.LightmapUV);
            glm::vec2 b = toAtlas(v1.LightmapUV);
            glm::vec2 c = toAtlas(v2.LightmapUV);

            glm::vec2 minUV = glm::min(glm::min(a, b), c);
            glm::vec2 maxUV = glm::max(glm::max(a, b), c);
            int x0 = std::max(0, static_cast<int>(std::floor(minUV.x * W)) - 1);
            int y0 = std::max(0, static_cast<int>(std::floor(minUV.y * H)) - 1);
            int x1 = std::min(static_cast<int>(W) - 1, static_cast<int>(std::ceil(maxUV.x * W)) + 1);
            int y1 = std::min(static_cast<int>(H) - 1, static_cast<int>(std::ceil(maxUV.y * H)) + 1);

            for (int y = y0; y <= y1; ++y) {
                for (int x = x0; x <= x1; ++x) {
                    glm::vec2 p((static_cast<float>(x) + 0.5f) / static_cast<float>(W),
                                (static_cast<float>(y) + 0.5f) / static_cast<float>(H));
                    glm::vec3 bary;
                    if (!FLightmapUV::ComputeBarycentric(p, a, b, c, bary))
                        continue;
                    if (bary.x < -1e-3f || bary.y < -1e-3f || bary.z < -1e-3f)
                        continue;
                    size_t idx = static_cast<size_t>(y) * W + static_cast<size_t>(x);
                    coverage[idx] = 1.0f;
                    chartOf[idx] = static_cast<int>(tri.ChartIndex);
                    positions[idx] = FLightmapUV::Interpolate(v0.Position, v1.Position, v2.Position, bary);
                    normals[idx] = SurfaceNormal(v0, v1, v2, bary);
                    glm::vec3 albedo = FLightmapUV::Interpolate(v0.Albedo, v1.Albedo, v2.Albedo, bary);
                    float metallic = v0.Metallic * bary.x + v1.Metallic * bary.y + v2.Metallic * bary.z;
                    albedos[idx] = albedo * (1.0f - metallic);
                    emissives[idx] = FLightmapUV::Interpolate(v0.Emissive, v1.Emissive, v2.Emissive, bary);
                }
            }
        }

        const uint32_t samples = std::max(1u, InSettings.SamplesPerTexel);
        std::vector<glm::vec3> direct(static_cast<size_t>(W) * H, glm::vec3(0.0f));
        std::vector<glm::vec3> indirect(static_cast<size_t>(W) * H, glm::vec3(0.0f));

        BuildAcceleration(InScene);

        // Every texel is worked out from the scene alone and written to its own slot, so rows can be
        // shared out between threads and the picture is the same however many there are.
        const auto bakeRow = [&](uint32_t y) {
            for (uint32_t x = 0; x < W; ++x) {
                size_t idx = static_cast<size_t>(y) * W + x;
                if (coverage[idx] <= 0.0f)
                    continue;

                glm::vec3 pos = positions[idx];
                glm::vec3 n = normals[idx];
                glm::vec3 albedo = albedos[idx];
                (void)albedo;
                // Receptor emissive stays runtime-only. Incoming emissive from other surfaces is in GI Li.
                direct[idx] = EvaluateDirectIrradiance(InScene, pos, n, true);

                glm::vec3 skyAccum(0.0f);
                glm::vec3 indir(0.0f);
                // Ray visibility is already in direct/GI. Material AO stays a runtime artistic term.
                const bool bTraceGI = InSettings.NumIndirectBounces > 0;
                for (uint32_t s = 0; s < samples; ++s) {
                    uint32_t h = HashCombine(InSettings.Seed, x, y, s);
                    glm::vec2 xi = HammersleyLocal(h % samples, samples);
                    xi.x = std::fmod(xi.x + RadicalInverseVdC(h), 1.0f);
                    xi.y = std::fmod(xi.y + RadicalInverseVdC(h ^ 0xA5A5A5A5u), 1.0f);

                    glm::vec3 dir = CosineSampleHemisphere(n, xi.x, xi.y);
                    float tHit;
                    uint32_t hitTri;
                    glm::vec3 bary;
                    bool hit = IntersectScene(InScene, pos + n * kEpsilon * 2.0f, dir, 1e6f, tHit, hitTri, bary);

                    if (!hit) {
                        skyAccum += kPI * SampleBakeEnvironment(InScene, dir);
                        continue;
                    }
                    if (!bTraceGI)
                        continue;

                    const auto& tri = InScene.Triangles[hitTri];
                    const auto& hv0 = InScene.Vertices[tri.I0];
                    const auto& hv1 = InScene.Vertices[tri.I1];
                    const auto& hv2 = InScene.Vertices[tri.I2];
                    glm::vec3 hitPos = FLightmapUV::Interpolate(hv0.Position, hv1.Position, hv2.Position, bary);
                    glm::vec3 hitN = FacingNormal(SurfaceNormal(hv0, hv1, hv2, bary), dir);
                    glm::vec3 hitAlbedo =
                        FLightmapUV::Interpolate(hv0.Albedo, hv1.Albedo, hv2.Albedo, bary) *
                        (1.0f - (hv0.Metallic * bary.x + hv1.Metallic * bary.y + hv2.Metallic * bary.z));

                    glm::vec3 LoHit = SurfaceOutgoingRadiance(InScene, hv0, hv1, hv2, bary, dir);
                    glm::vec3 Epath = kPI * LoHit;
                    glm::vec3 curPos = hitPos;
                    glm::vec3 curN = hitN;
                    glm::vec3 throughput = kPI * hitAlbedo;

                    for (uint32_t bounce = 1; bounce < InSettings.NumIndirectBounces; ++bounce) {
                        uint32_t hb = HashCombine(InSettings.Seed, x, y, s + bounce * 1024u);
                        glm::vec2 xi2 = HammersleyLocal(hb % samples, samples);
                        glm::vec3 dir2 = CosineSampleHemisphere(curN, xi2.x, xi2.y);
                        float t2;
                        uint32_t tri2;
                        glm::vec3 bary2;
                        if (!IntersectScene(InScene, curPos + curN * kEpsilon * 2.0f, dir2, 1e6f, t2, tri2, bary2)) {
                            Epath += throughput * SampleBakeEnvironment(InScene, dir2);
                            break;
                        }
                        const auto& tref = InScene.Triangles[tri2];
                        const auto& w0 = InScene.Vertices[tref.I0];
                        const auto& w1 = InScene.Vertices[tref.I1];
                        const auto& w2 = InScene.Vertices[tref.I2];
                        glm::vec3 LoNext = SurfaceOutgoingRadiance(InScene, w0, w1, w2, bary2, dir2);
                        Epath += throughput * LoNext;
                        curPos = FLightmapUV::Interpolate(w0.Position, w1.Position, w2.Position, bary2);
                        curN = FacingNormal(SurfaceNormal(w0, w1, w2, bary2), dir2);
                        float met = w0.Metallic * bary2.x + w1.Metallic * bary2.y + w2.Metallic * bary2.z;
                        glm::vec3 nextAlbedo =
                            FLightmapUV::Interpolate(w0.Albedo, w1.Albedo, w2.Albedo, bary2) * (1.0f - met);
                        throughput *= nextAlbedo;
                    }

                    indir += Epath;
                }

                if (samples > 0) {
                    indir /= static_cast<float>(samples);
                    skyAccum /= static_cast<float>(samples);
                }
                direct[idx] += skyAccum;
                indirect[idx] = indir * InSettings.IndirectIntensity;

                if (InSettings.bAmbientOcclusion && InSettings.AORadius > 0.0f) {
                    float occluded = 0.0f;
                    const uint32_t aoSamples = std::max(1u, samples);
                    for (uint32_t s = 0; s < aoSamples; ++s) {
                        uint32_t h = HashCombine(InSettings.Seed ^ 0xA0A0A0A0u, x, y, s);
                        glm::vec2 xi = HammersleyLocal(h % aoSamples, aoSamples);
                        glm::vec3 dir = CosineSampleHemisphere(n, xi.x, xi.y);
                        float tHit;
                        uint32_t hitTri;
                        glm::vec3 bary;
                        if (IntersectScene(InScene, pos + n * kEpsilon * 2.0f, dir, InSettings.AORadius, tHit, hitTri,
                                           bary))
                            occluded += 1.0f;
                    }
                    float vis = 1.0f - std::clamp(InSettings.AOIntensity * (occluded / static_cast<float>(aoSamples)),
                                                  0.0f, 1.0f);
                    direct[idx] *= vis;
                    indirect[idx] *= vis;
                }
            }
        };

        uint32_t threadCount = InSettings.NumThreads > 0 ? InSettings.NumThreads : std::thread::hardware_concurrency();
        threadCount = std::clamp(threadCount, 1u, std::max(H, 1u));
        std::atomic<uint32_t> nextRow{0};
        const auto worker = [&]() {
            for (uint32_t y = nextRow.fetch_add(1); y < H; y = nextRow.fetch_add(1))
                bakeRow(y);
        };
        std::vector<std::thread> threads;
        threads.reserve(threadCount - 1);
        for (uint32_t i = 1; i < threadCount; ++i)
            threads.emplace_back(worker);
        worker();
        for (std::thread& thread : threads)
            thread.join();

        // Texels outside the charts take the light of their neighbours, a few texels deep: bilinear
        // filtering reads past the edge of a chart, and would otherwise fetch black.
        std::vector<glm::vec3> filtered(static_cast<size_t>(W) * H, glm::vec3(0.0f));
        std::vector<uint8_t> filled(static_cast<size_t>(W) * H, 0);
        size_t notFinite = 0;
        for (size_t idx = 0; idx < filtered.size(); ++idx) {
            filtered[idx] = direct[idx] + indirect[idx];
            filled[idx] = coverage[idx] > 0.0f ? 1 : 0;
            // A texel that is not a number must never reach the atlas: one of them on screen is spread
            // over the whole picture by the bloom. It is filled from its neighbours like a texel outside
            // every chart, and the count is reported so the cause gets looked at.
            if (!IsFinite(filtered[idx])) {
                filtered[idx] = glm::vec3(0.0f);
                filled[idx] = 0;
                ++notFinite;
            }
        }
        if (notFinite > 0)
            LE_CORE_WARN("FLightBaker: {0} texels came out as not-a-number or infinite; filled from their neighbours",
                         notFinite);
        if (const uint64_t fallbacks = GNormalFallbacks.load(); fallbacks > 0)
            LE_CORE_WARN("FLightBaker: {0} normals had no length (vertices without a normal, or normals that cancel "
                         "out); the triangle's plane was used",
                         fallbacks);
        constexpr int kDilationPasses = 4;
        for (int pass = 0; pass < kDilationPasses; ++pass) {
            std::vector<uint8_t> nextFilled = filled;
            std::vector<glm::vec3> nextColors = filtered;
            bool bChanged = false;
            for (uint32_t y = 0; y < H; ++y) {
                for (uint32_t x = 0; x < W; ++x) {
                    const size_t idx = static_cast<size_t>(y) * W + x;
                    if (filled[idx])
                        continue;
                    glm::vec3 sum(0.0f);
                    float wsum = 0.0f;
                    for (int dy = -1; dy <= 1; ++dy) {
                        for (int dx = -1; dx <= 1; ++dx) {
                            const int nx = static_cast<int>(x) + dx;
                            const int ny = static_cast<int>(y) + dy;
                            if (nx < 0 || ny < 0 || nx >= static_cast<int>(W) || ny >= static_cast<int>(H))
                                continue;
                            const size_t nidx = static_cast<size_t>(ny) * W + static_cast<size_t>(nx);
                            if (!filled[nidx])
                                continue;
                            sum += filtered[nidx];
                            wsum += 1.0f;
                        }
                    }
                    if (wsum > 0.0f) {
                        nextColors[idx] = sum / wsum;
                        nextFilled[idx] = 1;
                        bChanged = true;
                    }
                }
            }
            filtered = std::move(nextColors);
            filled = std::move(nextFilled);
            if (!bChanged)
                break;
        }

        for (uint32_t i = 0; i < W * H; ++i) {
            OutRGBA32F[i * 4 + 0] = filtered[i].r;
            OutRGBA32F[i * 4 + 1] = filtered[i].g;
            OutRGBA32F[i * 4 + 2] = filtered[i].b;
            OutRGBA32F[i * 4 + 3] = coverage[i];
        }
    }

    // --- Light probes ---------------------------------------------------------------------------

    void FLightBaker::LayOutProbes(const FLightProbeBakeSettings& InProbeSettings, FLightProbeGrid& OutGrid) {
        const glm::vec3 low = glm::min(InProbeSettings.BoundsMin, InProbeSettings.BoundsMax);
        const glm::vec3 high = glm::max(InProbeSettings.BoundsMin, InProbeSettings.BoundsMax);
        const glm::vec3 extent = high - low;
        const uint32_t maxProbes = std::max(InProbeSettings.MaxProbes, 1u);
        glm::vec3 spacing(std::max(InProbeSettings.Spacing, 0.05f), std::max(InProbeSettings.VerticalSpacing, 0.05f),
                          std::max(InProbeSettings.Spacing, 0.05f));
        glm::uvec3 counts(1);
        for (int attempt = 0; attempt < 256; ++attempt) {
            uint64_t total = 1;
            for (int axis = 0; axis < 3; ++axis) {
                counts[axis] = static_cast<uint32_t>(std::floor(extent[axis] / spacing[axis] + 1.0e-4f)) + 1;
                total *= counts[axis];
            }
            if (total <= maxProbes)
                break;
            // Too many: the same grid a little coarser, until it fits.
            spacing *= 1.1f;
        }
        OutGrid = FLightProbeGrid{};
        OutGrid.Spacing = spacing;
        OutGrid.Origin = low + 0.5f * (extent - glm::vec3(counts - glm::uvec3(1)) * spacing);
        OutGrid.Resize(counts);
    }

    void FLightBaker::BakeProbes(const FLightBakerScene& InScene, const FLightBakerSettings& InSettings,
                                 const FLightProbeBakeSettings& InProbeSettings, FLightProbeGrid& OutGrid) {
        LayOutProbes(InProbeSettings, OutGrid);
        if (!CurrentTree(InScene))
            BuildAcceleration(InScene);
        GNormalFallbacks.store(0);

        const uint32_t samples = std::max(InProbeSettings.SamplesPerProbe, 1u);
        const size_t probeCount = OutGrid.ProbeCount();
        // Another sequence than the lightmap's, so the two do not repeat each other's pattern.
        const uint64_t seed = InSettings.Seed ^ 0x50524F4245534831ull;
        const float solidAnglePerSample = 4.0f * kPI / static_cast<float>(samples);

        // Every probe is worked out from the scene alone and written to its own slot: threads share
        // them out and the result is the same however many there are.
        const auto bakeProbe = [&](size_t InIndex) {
            const uint32_t x = static_cast<uint32_t>(InIndex % OutGrid.Counts.x);
            const uint32_t y = static_cast<uint32_t>((InIndex / OutGrid.Counts.x) % OutGrid.Counts.y);
            const uint32_t z = static_cast<uint32_t>(InIndex / (static_cast<size_t>(OutGrid.Counts.x) * OutGrid.Counts.y));
            const glm::vec3 position = OutGrid.PositionOf(x, y, z);
            const uint32_t probeId = static_cast<uint32_t>(InIndex);
            // Each probe turns the shared pattern by its own amount.
            const float turnU = RadicalInverseVdC(HashCombine(seed, probeId, 0x1F123BB5u, 0));
            const float turnV = RadicalInverseVdC(HashCombine(seed, probeId, 0x5F356495u, 1));

            FLightProbeSample light;
            uint32_t backFaces = 0;
            for (uint32_t s = 0; s < samples; ++s) {
                glm::vec2 xi = HammersleyLocal(s, samples);
                xi.x = std::fmod(xi.x + turnU, 1.0f);
                xi.y = std::fmod(xi.y + turnV, 1.0f);
                // Uniform over the sphere.
                const float height = 1.0f - 2.0f * xi.x;
                const float ring = std::sqrt(std::max(1.0f - height * height, 0.0f));
                const float angle = 2.0f * kPI * xi.y;
                const glm::vec3 dir(ring * std::cos(angle), height, ring * std::sin(angle));

                float tHit;
                uint32_t hitTri;
                glm::vec3 bary;
                if (!IntersectScene(InScene, position, dir, 1e6f, tHit, hitTri, bary)) {
                    light.AddRadiance(dir, SampleBakeEnvironment(InScene, dir) * solidAnglePerSample);
                    continue;
                }
                const auto& tri = InScene.Triangles[hitTri];
                const auto& hv0 = InScene.Vertices[tri.I0];
                const auto& hv1 = InScene.Vertices[tri.I1];
                const auto& hv2 = InScene.Vertices[tri.I2];
                const glm::vec3 surfaceNormal = SurfaceNormal(hv0, hv1, hv2, bary);
                if (glm::dot(surfaceNormal, dir) > 0.0f) {
                    // The back of a surface: nothing a place in the open would see. It sends no light.
                    ++backFaces;
                    continue;
                }
                if (InSettings.NumIndirectBounces == 0)
                    continue;

                // The light the surface sends back, and what reaches it in turn: the same walk the
                // lightmap takes from a texel.
                glm::vec3 radiance = SurfaceOutgoingRadiance(InScene, hv0, hv1, hv2, bary, dir);
                glm::vec3 curPos = FLightmapUV::Interpolate(hv0.Position, hv1.Position, hv2.Position, bary);
                glm::vec3 curN = FacingNormal(surfaceNormal, dir);
                glm::vec3 throughput = FLightmapUV::Interpolate(hv0.Albedo, hv1.Albedo, hv2.Albedo, bary) *
                                       (1.0f - (hv0.Metallic * bary.x + hv1.Metallic * bary.y + hv2.Metallic * bary.z));
                for (uint32_t bounce = 1; bounce < InSettings.NumIndirectBounces; ++bounce) {
                    const uint32_t hb = HashCombine(seed, probeId, s, bounce);
                    const glm::vec3 dir2 =
                        CosineSampleHemisphere(curN, RadicalInverseVdC(hb), RadicalInverseVdC(hb ^ 0xA5A5A5A5u));
                    float t2;
                    uint32_t tri2;
                    glm::vec3 bary2;
                    if (!IntersectScene(InScene, curPos + curN * kEpsilon * 2.0f, dir2, 1e6f, t2, tri2, bary2)) {
                        radiance += throughput * SampleBakeEnvironment(InScene, dir2);
                        break;
                    }
                    const auto& tref = InScene.Triangles[tri2];
                    const auto& w0 = InScene.Vertices[tref.I0];
                    const auto& w1 = InScene.Vertices[tref.I1];
                    const auto& w2 = InScene.Vertices[tref.I2];
                    radiance += throughput * SurfaceOutgoingRadiance(InScene, w0, w1, w2, bary2, dir2);
                    curPos = FLightmapUV::Interpolate(w0.Position, w1.Position, w2.Position, bary2);
                    curN = FacingNormal(SurfaceNormal(w0, w1, w2, bary2), dir2);
                    throughput *= FLightmapUV::Interpolate(w0.Albedo, w1.Albedo, w2.Albedo, bary2) *
                                  (1.0f - (w0.Metallic * bary2.x + w1.Metallic * bary2.y + w2.Metallic * bary2.z));
                }
                light.AddRadiance(dir, radiance * (InSettings.IndirectIntensity * solidAnglePerSample));
            }

            // The lights that are wholly baked shine on the probe itself. A light is a single direction:
            // it adds its irradiance on a surface facing it. (Stationary lights are left to the game.)
            for (const auto& dl : InScene.DirectionalLights) {
                if (!dl.bContributeDirect)
                    continue;
                const glm::vec3 L = glm::normalize(-dl.Light.Direction);
                if (!IsShadowed(InScene, position, L, 1e6f))
                    light.AddRadiance(L, dl.Light.Color * dl.Light.Intensity);
            }
            for (const auto& pl : InScene.PointLights) {
                if (!pl.bContributeDirect)
                    continue;
                const glm::vec3 toLight = pl.Light.Position - position;
                const float dist = glm::length(toLight);
                if (dist < 1e-5f || dist > pl.Light.Radius)
                    continue;
                const glm::vec3 L = toLight / dist;
                if (!IsShadowed(InScene, position, L, dist - kEpsilon))
                    light.AddRadiance(L, pl.Light.Color * pl.Light.Intensity * PointAttenuation(dist, pl.Light.Radius));
            }
            for (const auto& sl : InScene.SpotLights) {
                if (!sl.bContributeDirect)
                    continue;
                const glm::vec3 toLight = sl.Light.Position - position;
                const float dist = glm::length(toLight);
                if (dist < 1e-5f || dist > sl.Light.Radius)
                    continue;
                const float cone = SpotConeFactor(sl.Light.Direction, toLight, sl.Light.CutOff, sl.Light.OuterCutOff);
                const glm::vec3 L = toLight / dist;
                if (cone > 0.0f && !IsShadowed(InScene, position, L, dist - kEpsilon))
                    light.AddRadiance(L, sl.Light.Color * sl.Light.Intensity * PointAttenuation(dist, sl.Light.Radius) * cone);
            }

            const bool bFinite = IsFinite(glm::vec3(light.Red)) && IsFinite(glm::vec3(light.Green)) &&
                                 IsFinite(glm::vec3(light.Blue)) && std::isfinite(light.Red.w) &&
                                 std::isfinite(light.Green.w) && std::isfinite(light.Blue.w);
            const bool bInTheOpen = static_cast<float>(backFaces) <=
                                    InProbeSettings.MaxBackFaceShare * static_cast<float>(samples);
            OutGrid.Probes[InIndex] = bFinite ? light : FLightProbeSample{};
            OutGrid.Valid[InIndex] = (bFinite && bInTheOpen) ? 1 : 0;
        };

        uint32_t threadCount = InSettings.NumThreads > 0 ? InSettings.NumThreads : std::thread::hardware_concurrency();
        threadCount = static_cast<uint32_t>(std::clamp<size_t>(threadCount, 1, std::max<size_t>(probeCount, 1)));
        std::atomic<size_t> nextProbe{0};
        const auto worker = [&]() {
            for (size_t index = nextProbe.fetch_add(1); index < probeCount; index = nextProbe.fetch_add(1))
                bakeProbe(index);
        };
        std::vector<std::thread> threads;
        threads.reserve(threadCount - 1);
        for (uint32_t i = 1; i < threadCount; ++i)
            threads.emplace_back(worker);
        worker();
        for (std::thread& thread : threads)
            thread.join();

        // A probe that is not valid takes the light of the valid ones next to it, a few probes deep. It
        // stays marked: the game reads it only where no valid probe is in reach, and then finds the
        // light of the nearest room instead of darkness.
        std::vector<uint8_t> filled = OutGrid.Valid;
        constexpr int kFillPasses = 8;
        for (int pass = 0; pass < kFillPasses; ++pass) {
            std::vector<uint8_t> nextFilled = filled;
            bool bChanged = false;
            for (uint32_t z = 0; z < OutGrid.Counts.z; ++z) {
                for (uint32_t y = 0; y < OutGrid.Counts.y; ++y) {
                    for (uint32_t x = 0; x < OutGrid.Counts.x; ++x) {
                        const size_t index = OutGrid.IndexOf(x, y, z);
                        if (filled[index])
                            continue;
                        FLightProbeSample sum;
                        float weight = 0.0f;
                        const int offsets[6][3] = {{-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}};
                        for (const auto& offset : offsets) {
                            const int nx = static_cast<int>(x) + offset[0];
                            const int ny = static_cast<int>(y) + offset[1];
                            const int nz = static_cast<int>(z) + offset[2];
                            if (nx < 0 || ny < 0 || nz < 0 || nx >= static_cast<int>(OutGrid.Counts.x) ||
                                ny >= static_cast<int>(OutGrid.Counts.y) || nz >= static_cast<int>(OutGrid.Counts.z))
                                continue;
                            const size_t neighbour = OutGrid.IndexOf(static_cast<uint32_t>(nx), static_cast<uint32_t>(ny),
                                                                     static_cast<uint32_t>(nz));
                            if (!filled[neighbour])
                                continue;
                            sum.AddScaled(OutGrid.Probes[neighbour], 1.0f);
                            weight += 1.0f;
                        }
                        if (weight > 0.0f) {
                            OutGrid.Probes[index] = FLightProbeSample{};
                            OutGrid.Probes[index].AddScaled(sum, 1.0f / weight);
                            nextFilled[index] = 1;
                            bChanged = true;
                        }
                    }
                }
            }
            filled = std::move(nextFilled);
            if (!bChanged)
                break;
        }
    }

} // namespace Leon
