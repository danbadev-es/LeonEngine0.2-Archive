#include "AssetPipeline/FLightmapUV.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <vector>

namespace Leon {

    bool FLightmapUV::HasLightmapUV(const UStaticMesh& InMesh) {
        if (InMesh.HasUniqueLightmapUV())
            return true;

        // Older .lmesh may already store PackLightmapCell UV1 without the flag set.
        const auto& verts = InMesh.GetVertices();
        if (verts.size() < 3)
            return false;
        float minU = verts[0].LightmapUV.x;
        float maxU = minU;
        float minV = verts[0].LightmapUV.y;
        float maxV = minV;
        for (const auto& v : verts) {
            minU = std::min(minU, v.LightmapUV.x);
            maxU = std::max(maxU, v.LightmapUV.x);
            minV = std::min(minV, v.LightmapUV.y);
            maxV = std::max(maxV, v.LightmapUV.y);
        }
        return (maxU - minU) > 0.01f && (maxV - minV) > 0.01f;
    }

    bool FLightmapUV::ComputeBarycentric(const glm::vec2& InP, const glm::vec2& InA, const glm::vec2& InB,
                                         const glm::vec2& InC, glm::vec3& OutBary) {
        glm::vec2 v0 = InB - InA;
        glm::vec2 v1 = InC - InA;
        glm::vec2 v2 = InP - InA;
        float den = v0.x * v1.y - v1.x * v0.y;
        if (std::abs(den) < 1e-12f)
            return false;
        float inv = 1.0f / den;
        float v = (v2.x * v1.y - v1.x * v2.y) * inv;
        float w = (v0.x * v2.y - v2.x * v0.y) * inv;
        float u = 1.0f - v - w;
        OutBary = {u, v, w};
        return true;
    }

    glm::vec3 FLightmapUV::Interpolate(const glm::vec3& InA, const glm::vec3& InB, const glm::vec3& InC,
                                       const glm::vec3& InBary) {
        return InA * InBary.x + InB * InBary.y + InC * InBary.z;
    }

    void FLightmapUV::GenerateBoxPackedLightmapUVs(UStaticMesh& InMesh, float InPadding) {
        auto& verts = InMesh.GetVertices();
        auto& indices = InMesh.GetIndices();
        if (verts.empty() || indices.size() < 3)
            return;

        const uint32_t triCount = static_cast<uint32_t>(indices.size() / 3);
        if (triCount == 0)
            return;

        const uint32_t grid = static_cast<uint32_t>(std::ceil(std::sqrt(static_cast<float>(triCount))));
        const float cell = 1.0f / static_cast<float>(grid);
        const float pad = std::clamp(InPadding, 0.0f, cell * 0.45f);
        const float usable = cell - 2.0f * pad;

        std::vector<FStaticMeshVertex> newVerts;
        std::vector<uint32_t> newIndices;
        newVerts.reserve(static_cast<size_t>(triCount) * 3);
        newIndices.reserve(static_cast<size_t>(triCount) * 3);

        auto& submeshes = InMesh.GetSubmeshes();
        if (submeshes.empty()) {
            FStaticSubmesh whole;
            whole.IndexOffset = 0;
            whole.IndexCount = static_cast<uint32_t>(indices.size());
            whole.VertexOffset = 0;
            whole.VertexCount = static_cast<uint32_t>(verts.size());
            submeshes.push_back(whole);
        }

        std::vector<FStaticSubmesh> rebuiltSubmeshes;
        rebuiltSubmeshes.reserve(submeshes.size());

        for (const auto& sub : submeshes) {
            FStaticSubmesh out = sub;
            out.VertexOffset = static_cast<uint32_t>(newVerts.size());
            out.IndexOffset = static_cast<uint32_t>(newIndices.size());

            uint32_t firstTri = sub.IndexOffset / 3;
            uint32_t nTri = sub.IndexCount / 3;
            for (uint32_t t = 0; t < nTri; ++t) {
                uint32_t srcTri = firstTri + t;
                uint32_t i0 = indices[srcTri * 3 + 0];
                uint32_t i1 = indices[srcTri * 3 + 1];
                uint32_t i2 = indices[srcTri * 3 + 2];
                if (i0 >= verts.size() || i1 >= verts.size() || i2 >= verts.size())
                    continue;

                uint32_t gx = srcTri % grid;
                uint32_t gy = srcTri / grid;
                float ox = static_cast<float>(gx) * cell + pad;
                float oy = static_cast<float>(gy) * cell + pad;

                FStaticMeshVertex v0 = verts[i0];
                FStaticMeshVertex v1 = verts[i1];
                FStaticMeshVertex v2 = verts[i2];
                v0.LightmapUV = {ox, oy};
                v1.LightmapUV = {ox + usable, oy};
                v2.LightmapUV = {ox + usable * 0.5f, oy + usable};

                uint32_t base = static_cast<uint32_t>(newVerts.size());
                newVerts.push_back(v0);
                newVerts.push_back(v1);
                newVerts.push_back(v2);
                newIndices.push_back(base);
                newIndices.push_back(base + 1);
                newIndices.push_back(base + 2);
            }

            out.VertexCount = static_cast<uint32_t>(newVerts.size()) - out.VertexOffset;
            out.IndexCount = static_cast<uint32_t>(newIndices.size()) - out.IndexOffset;
            rebuiltSubmeshes.push_back(out);
        }

        verts = std::move(newVerts);
        indices = std::move(newIndices);
        submeshes = std::move(rebuiltSubmeshes);
        InMesh.CalculateBounds();
        InMesh.SetHasUniqueLightmapUV(true);
    }

    FLightmapUVValidationResult FLightmapUV::Validate(const UStaticMesh& InMesh, float InMinIslandPadding) {
        FLightmapUVValidationResult result;
        result.bHasUV1 = HasLightmapUV(InMesh);
        const auto& verts = InMesh.GetVertices();
        const auto& indices = InMesh.GetIndices();
        const uint32_t triCount = static_cast<uint32_t>(indices.size() / 3);

        for (const auto& v : verts) {
            if (v.LightmapUV.x < -1e-4f || v.LightmapUV.y < -1e-4f || v.LightmapUV.x > 1.0f + 1e-4f ||
                v.LightmapUV.y > 1.0f + 1e-4f) {
                result.bInUnitSquare = false;
            }
        }

        // Two triangles overlap when their interiors intersect; sharing an edge or a corner, as the
        // triangles of one chart do, is not an overlap. Separating axes, with the padding as slack.
        struct FTri {
            glm::vec2 P[3];
            glm::vec2 Min;
            glm::vec2 Max;
        };
        auto overlaps = [InMinIslandPadding](const FTri& InA, const FTri& InB) {
            const FTri* pair[2] = {&InA, &InB};
            for (int which = 0; which < 2; ++which) {
                const FTri& tri = *pair[which];
                for (int e = 0; e < 3; ++e) {
                    const glm::vec2 edge = tri.P[(e + 1) % 3] - tri.P[e];
                    const float length = glm::length(edge);
                    if (length < 1.0e-12f)
                        continue;
                    const glm::vec2 axis = glm::vec2(-edge.y, edge.x) / length;
                    float minA = 1.0e9f, maxA = -1.0e9f, minB = 1.0e9f, maxB = -1.0e9f;
                    for (int k = 0; k < 3; ++k) {
                        const float da = glm::dot(InA.P[k], axis);
                        const float db = glm::dot(InB.P[k], axis);
                        minA = std::min(minA, da);
                        maxA = std::max(maxA, da);
                        minB = std::min(minB, db);
                        maxB = std::max(maxB, db);
                    }
                    if (maxA <= minB + InMinIslandPadding || maxB <= minA + InMinIslandPadding)
                        return false; // separated along this axis
                }
            }
            return true;
        };

        std::vector<FTri> tris;
        tris.reserve(triCount);
        for (uint32_t t = 0; t < triCount; ++t) {
            const uint32_t i0 = indices[t * 3 + 0];
            const uint32_t i1 = indices[t * 3 + 1];
            const uint32_t i2 = indices[t * 3 + 2];
            if (i0 >= verts.size() || i1 >= verts.size() || i2 >= verts.size())
                continue;
            FTri tri;
            tri.P[0] = verts[i0].LightmapUV;
            tri.P[1] = verts[i1].LightmapUV;
            tri.P[2] = verts[i2].LightmapUV;
            tri.Min = glm::min(glm::min(tri.P[0], tri.P[1]), tri.P[2]);
            tri.Max = glm::max(glm::max(tri.P[0], tri.P[1]), tri.P[2]);
            const glm::vec2 e0 = tri.P[1] - tri.P[0];
            const glm::vec2 e1 = tri.P[2] - tri.P[0];
            if (std::abs(e0.x * e1.y - e0.y * e1.x) < 1.0e-12f)
                continue; // no area in the lightmap: nothing to overlap
            tris.push_back(tri);
        }

        // A grid over the unit square keeps this from comparing every pair of a large mesh.
        constexpr int kGrid = 64;
        std::vector<std::vector<uint32_t>> cells(static_cast<size_t>(kGrid) * kGrid);
        auto cellOf = [](float InValue) {
            return std::clamp(static_cast<int>(InValue * kGrid), 0, kGrid - 1);
        };
        for (uint32_t t = 0; t < tris.size() && !result.bHasOverlaps; ++t) {
            const FTri& tri = tris[t];
            for (int y = cellOf(tri.Min.y); y <= cellOf(tri.Max.y) && !result.bHasOverlaps; ++y) {
                for (int x = cellOf(tri.Min.x); x <= cellOf(tri.Max.x) && !result.bHasOverlaps; ++x) {
                    std::vector<uint32_t>& cell = cells[static_cast<size_t>(y) * kGrid + x];
                    for (uint32_t other : cell) {
                        if (overlaps(tri, tris[other])) {
                            result.bHasOverlaps = true;
                            result.bHasEnoughPadding = false;
                            break;
                        }
                    }
                    cell.push_back(t);
                }
            }
        }

        result.bValid = result.bHasUV1 && result.bInUnitSquare && !result.bHasOverlaps;
        if (!result.bValid) {
            std::ostringstream ss;
            if (!result.bHasUV1)
                ss << "missing UV1; ";
            if (!result.bInUnitSquare)
                ss << "UV1 outside unit square; ";
            if (result.bHasOverlaps)
                ss << "overlapping charts; ";
            result.Message = ss.str();
        }
        return result;
    }

} // namespace Leon
