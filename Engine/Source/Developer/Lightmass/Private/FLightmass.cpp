#include "Lightmass/FLightmass.hpp"
#include "Core/FAssetPath.hpp"
#include "Assets/FHDRImporter.hpp"
#include "Assets/FLightmapAsset.hpp"
#include "AssetPipeline/FLightmapUV.hpp"
#include "Assets/FTextureImporter.hpp"
#include "Assets/UAssetManager.hpp"
#include "Assets/UStaticMesh.hpp"
#include "Core/FLog.hpp"
#include "Scene/Components.hpp"
#include "Engine/FLightProbeAsset.hpp"
#include "Engine/FMapSerializer.hpp"
#include "Engine/UWorld.hpp"
#include "Gameplay/AActor.hpp"
#include "Gameplay/ANavMeshBoundsVolume.hpp"
#include "Lightmass/FLightBaker.hpp"
#include "Lightmass/FLightmapBuilder.hpp"
#include "RenderCore/FVertexLayout.hpp"
#include "RenderCore/FColorSpace.hpp"
#include "RenderCore/FIBLMath.hpp"
#include "Assets/FMaterial.hpp"
#include "Assets/FMaterialInstance.hpp"

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <iostream>
#include <sstream>

namespace fs = std::filesystem;

namespace Leon {
    namespace {

        void LogLM(const std::string& Msg) {
            std::cout << "[Lightmass] " << Msg << "\n";
        }

        struct FBakeAlbedoTexture {
            std::vector<uint8_t> Pixels;
            uint32_t Width = 0;
            uint32_t Height = 0;
            uint32_t Channels = 4;
            bool bSRGB = true;
            /** Mean colour of the whole texture, linear. */
            glm::vec3 Average{1.0f};
        };

        struct FBakeMaterialSample {
            glm::vec3 Albedo{0.7f};
            float Metallic = 0.0f;
            float Roughness = 0.5f;
            glm::vec3 Emissive{0.0f};
            FBakeAlbedoTexture AlbedoMap;
        };

        bool LoadBakeAlbedoTexture(const std::string& InVirtualOrAbsolutePath, FBakeAlbedoTexture& OutTex) {
            if (InVirtualOrAbsolutePath.empty())
                return false;

            // A material names its textures as "/Game/Textures/...": that is not a place on disk until
            // the asset manager says which. Left as it was, the texture was never found and every
            // textured surface bounced plain white.
            fs::path resolved = UAssetManager::ResolveVirtualPath(InVirtualOrAbsolutePath);
            if (!fs::exists(resolved))
                resolved = InVirtualOrAbsolutePath;
            if (!resolved.is_absolute() && !fs::exists(resolved)) {
                fs::path underContent = fs::path(UAssetManager::GetContentRoot()) / InVirtualOrAbsolutePath;
                if (fs::exists(underContent))
                    resolved = underContent;
            }
            if (!fs::exists(resolved))
                return false;

            FNativeTextureData tex;
            if (!tex.LoadFromFile(resolved.string()) || !tex.DecodeToRGBA8() || tex.Mips.empty() ||
                tex.Mips[0].Pixels.empty())
                return false;

            const auto& mip = tex.Mips[0];
            OutTex.Pixels = mip.Pixels;
            OutTex.Width = mip.Width;
            OutTex.Height = mip.Height;
            OutTex.Channels = std::max(1u, tex.Header.Channels);
            OutTex.bSRGB = tex.Header.ColorSpace == 1 && !IsLinearDataTexturePath(resolved.string());
            // The smallest level of the chain is the texture's mean, already filtered in linear space.
            const FTextureMipData& smallest = tex.Mips.back();
            if (smallest.Pixels.size() >= 3) {
                const size_t texels = smallest.Pixels.size() / 4;
                glm::vec3 sum(0.0f);
                for (size_t i = 0; i < std::max<size_t>(texels, 1); ++i) {
                    const glm::vec3 encoded(smallest.Pixels[i * 4 + 0] / 255.0f, smallest.Pixels[i * 4 + 1] / 255.0f,
                                            smallest.Pixels[i * 4 + 2] / 255.0f);
                    sum += OutTex.bSRGB ? SRGBToLinear(encoded) : encoded;
                }
                OutTex.Average = sum / static_cast<float>(std::max<size_t>(texels, 1));
            }
            return OutTex.Width > 0 && OutTex.Height > 0;
        }

        /**
         * The colour a surface bounces. With a texture it is the texture's mean: one texel read at a
         * vertex and stretched over the triangle says nothing about a tiled wall.
         */
        glm::vec3 BounceAlbedo(const FBakeMaterialSample& InMat) {
            const FBakeAlbedoTexture& tex = InMat.AlbedoMap;
            if (tex.Pixels.empty() || tex.Width == 0 || tex.Height == 0)
                return InMat.Albedo;
            return tex.Average * InMat.Albedo;
        }

        FBakeMaterialSample SampleMaterialInstance(const TRef<FMaterialInstance>& InMatInst) {
            FBakeMaterialSample sample;
            if (!InMatInst)
                return sample;

            sample.Albedo = InMatInst->GetAlbedoColor();
            sample.Metallic = InMatInst->GetMetallic();
            sample.Roughness = InMatInst->GetRoughness();
            sample.Emissive = InMatInst->GetEmissiveColor() * InMatInst->GetEmissiveIntensity();

            if (InMatInst->GetParent()) {
                const std::string& texPath = InMatInst->GetParent()->GetTexturePath(0);
                LoadBakeAlbedoTexture(texPath, sample.AlbedoMap);

                // With a metalness map the number in the material only scales it: a material written
                // as "Metallic: 1" with a map that is mostly black is not a metal, and treating it as
                // one made it bounce no light at all.
                const std::string& metallicPath = InMatInst->GetParent()->GetTexturePath(2);
                FBakeAlbedoTexture masks;
                if (!metallicPath.empty() && LoadBakeAlbedoTexture(metallicPath, masks))
                    sample.Metallic *= InMatInst->GetParent()->IsPackedORM() ? masks.Average.b : masks.Average.r;
                const std::string& emissivePath = InMatInst->GetParent()->GetTexturePath(5);
                FBakeAlbedoTexture glow;
                if (!emissivePath.empty() && LoadBakeAlbedoTexture(emissivePath, glow))
                    sample.Emissive *= glow.Average;
            }
            return sample;
        }

        FBakeMaterialSample ResolveBakeMaterial(AActor& Actor) {
            if (Actor.HasComponent<FMaterialComponent>()) {
                auto& mat = Actor.GetComponent<FMaterialComponent>();
                if (mat.MaterialInstance)
                    return SampleMaterialInstance(mat.MaterialInstance);
            }

            if (Actor.HasComponent<FStaticMeshComponent>()) {
                auto& smc = Actor.GetComponent<FStaticMeshComponent>();
                if (!smc.MaterialOverrides.empty()) {
                    for (const auto& overrideMat : smc.MaterialOverrides) {
                        if (overrideMat)
                            return SampleMaterialInstance(overrideMat);
                    }
                }
                if (smc.StaticMesh && !smc.StaticMesh->GetMaterialSlots().empty()) {
                    const auto& slot = smc.StaticMesh->GetMaterialSlots()[0];
                    if (slot.MaterialInstance)
                        return SampleMaterialInstance(slot.MaterialInstance);
                    if (!slot.DefaultMaterialPath.empty()) {
                        auto base = UAssetManager::GetMaterial(slot.DefaultMaterialPath);
                        if (base)
                            return SampleMaterialInstance(base->CreateInstance());
                    }
                }
            }
            return {};
        }

        void AppendBox(FLightBakerScene& Scene, uint32_t ChartIndex, const glm::mat4& M, float SizeX, float SizeY,
                       float SizeZ, const FBakeMaterialSample& Mat, bool bCastShadow) {
            const float hx = std::max(SizeX, 0.01f) * 0.5f;
            const float hy = std::max(SizeY, 0.01f) * 0.5f;
            const float hz = std::max(SizeZ, 0.01f) * 0.5f;

            // Face order + lightmap packing MUST match FMeshPrimitives::CreateBox / CreateCube
            // (PackLightmapCell with 3x2 atlas cells, face index 0..5).
            struct FFace {
                glm::vec3 P[4];
                glm::vec3 N;
                int LightmapFace;
            };
            const FFace faces[6] = {
                {{{-hx, -hy, hz}, {hx, -hy, hz}, {hx, hy, hz}, {-hx, hy, hz}}, {0, 0, 1}, 0},
                {{{hx, -hy, -hz}, {-hx, -hy, -hz}, {-hx, hy, -hz}, {hx, hy, -hz}}, {0, 0, -1}, 1},
                {{{-hx, hy, hz}, {hx, hy, hz}, {hx, hy, -hz}, {-hx, hy, -hz}}, {0, 1, 0}, 2},
                {{{-hx, -hy, -hz}, {hx, -hy, -hz}, {hx, -hy, hz}, {-hx, -hy, hz}}, {0, -1, 0}, 3},
                {{{-hx, -hy, -hz}, {-hx, -hy, hz}, {-hx, hy, hz}, {-hx, hy, -hz}}, {-1, 0, 0}, 4},
                {{{hx, -hy, hz}, {hx, -hy, -hz}, {hx, hy, -hz}, {hx, hy, hz}}, {1, 0, 0}, 5},
            };
            const glm::vec2 faceUV[4] = {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};

            glm::mat3 normalMat = glm::transpose(glm::inverse(glm::mat3(M)));
            for (const FFace& face : faces) {
                uint32_t base = static_cast<uint32_t>(Scene.Vertices.size());
                for (int i = 0; i < 4; ++i) {
                    FBakeVertex v;
                    v.Position = glm::vec3(M * glm::vec4(face.P[i], 1.0f));
                    v.Normal = glm::normalize(normalMat * face.N);
                    v.LightmapUV = PackLightmapCell(faceUV[i], face.LightmapFace % 3, face.LightmapFace / 3, 3, 2);
                    v.Albedo = Mat.Albedo;
                    v.Metallic = Mat.Metallic;
                    v.Roughness = Mat.Roughness;
                    v.Emissive = Mat.Emissive;
                    Scene.Vertices.push_back(v);
                }
                Scene.Triangles.push_back({base + 0, base + 1, base + 2, ChartIndex, bCastShadow});
                Scene.Triangles.push_back({base + 0, base + 2, base + 3, ChartIndex, bCastShadow});
            }
        }

        void AppendPlane(FLightBakerScene& Scene, uint32_t ChartIndex, const glm::mat4& M, float Width, float Depth,
                         const FBakeMaterialSample& Mat, bool bCastShadow) {
            float hx = Width * 0.5f;
            float hz = Depth * 0.5f;
            glm::mat3 normalMat = glm::transpose(glm::inverse(glm::mat3(M)));
            glm::vec3 n = glm::normalize(normalMat * glm::vec3(0, 1, 0));
            glm::vec3 p0 = glm::vec3(M * glm::vec4(-hx, 0, -hz, 1));
            glm::vec3 p1 = glm::vec3(M * glm::vec4(hx, 0, -hz, 1));
            glm::vec3 p2 = glm::vec3(M * glm::vec4(hx, 0, hz, 1));
            glm::vec3 p3 = glm::vec3(M * glm::vec4(-hx, 0, hz, 1));
            uint32_t base = static_cast<uint32_t>(Scene.Vertices.size());
            auto push = [&](const glm::vec3& p, const glm::vec2& uv) {
                FBakeVertex v;
                v.Position = p;
                v.Normal = n;
                v.LightmapUV = uv;
                v.Albedo = Mat.Albedo;
                v.Metallic = Mat.Metallic;
                v.Roughness = Mat.Roughness;
                v.Emissive = Mat.Emissive;
                Scene.Vertices.push_back(v);
            };
            push(p0, {0.02f, 0.02f});
            push(p1, {0.98f, 0.02f});
            push(p2, {0.98f, 0.98f});
            push(p3, {0.02f, 0.98f});
            Scene.Triangles.push_back({base + 0, base + 1, base + 2, ChartIndex, bCastShadow});
            Scene.Triangles.push_back({base + 0, base + 2, base + 3, ChartIndex, bCastShadow});
        }

        void AppendSphere(FLightBakerScene& Scene, uint32_t ChartIndex, const glm::mat4& M, float Radius,
                          const FBakeMaterialSample& Mat, bool bCastShadow, uint32_t Segments = 16,
                          uint32_t Rings = 12) {
            glm::mat3 normalMat = glm::transpose(glm::inverse(glm::mat3(M)));
            uint32_t base = static_cast<uint32_t>(Scene.Vertices.size());
            for (uint32_t y = 0; y <= Rings; ++y) {
                float v = static_cast<float>(y) / static_cast<float>(Rings);
                float phi = v * 3.14159265f;
                float sy = std::cos(phi);
                float sr = std::sin(phi);
                for (uint32_t x = 0; x <= Segments; ++x) {
                    float u = static_cast<float>(x) / static_cast<float>(Segments);
                    float theta = u * 6.2831853f;
                    glm::vec3 local(sr * std::cos(theta) * Radius, sy * Radius, sr * std::sin(theta) * Radius);
                    glm::vec3 nLocal = glm::normalize(local);
                    FBakeVertex vert;
                    vert.Position = glm::vec3(M * glm::vec4(local, 1.0f));
                    vert.Normal = glm::normalize(normalMat * nLocal);
                    vert.LightmapUV = {u * 0.96f + 0.02f, v * 0.96f + 0.02f};
                    vert.Albedo = Mat.Albedo;
                    vert.Metallic = Mat.Metallic;
                    vert.Roughness = Mat.Roughness;
                    vert.Emissive = Mat.Emissive;
                    Scene.Vertices.push_back(vert);
                }
            }
            for (uint32_t y = 0; y < Rings; ++y) {
                for (uint32_t x = 0; x < Segments; ++x) {
                    uint32_t i0 = base + y * (Segments + 1) + x;
                    uint32_t i1 = i0 + 1;
                    uint32_t i2 = i0 + (Segments + 1);
                    uint32_t i3 = i2 + 1;
                    Scene.Triangles.push_back({i0, i2, i1, ChartIndex, bCastShadow});
                    Scene.Triangles.push_back({i1, i2, i3, ChartIndex, bCastShadow});
                }
            }
        }

        void AppendCylinder(FLightBakerScene& Scene, uint32_t ChartIndex, const glm::mat4& M, float BottomRadius,
                            float TopRadius, float Height, const FBakeMaterialSample& Mat, bool bCastShadow,
                            uint32_t Segments = 16) {
            glm::mat3 normalMat = glm::transpose(glm::inverse(glm::mat3(M)));
            float hy = Height * 0.5f;
            uint32_t base = static_cast<uint32_t>(Scene.Vertices.size());
            for (uint32_t i = 0; i <= Segments; ++i) {
                float u = static_cast<float>(i) / static_cast<float>(Segments);
                float a = u * 6.2831853f;
                float c = std::cos(a);
                float s = std::sin(a);
                for (int row = 0; row < 2; ++row) {
                    float y = (row == 0) ? -hy : hy;
                    float r = (row == 0) ? BottomRadius : TopRadius;
                    glm::vec3 local(c * r, y, s * r);
                    glm::vec3 nLocal = glm::normalize(glm::vec3(c, 0.0f, s));
                    FBakeVertex vert;
                    vert.Position = glm::vec3(M * glm::vec4(local, 1.0f));
                    vert.Normal = glm::normalize(normalMat * nLocal);
                    vert.LightmapUV = {u * 0.96f + 0.02f, (row == 0 ? 0.02f : 0.98f)};
                    vert.Albedo = Mat.Albedo;
                    vert.Metallic = Mat.Metallic;
                    vert.Roughness = Mat.Roughness;
                    vert.Emissive = Mat.Emissive;
                    Scene.Vertices.push_back(vert);
                }
            }
            for (uint32_t i = 0; i < Segments; ++i) {
                uint32_t i0 = base + i * 2;
                uint32_t i1 = i0 + 1;
                uint32_t i2 = i0 + 2;
                uint32_t i3 = i0 + 3;
                Scene.Triangles.push_back({i0, i2, i1, ChartIndex, bCastShadow});
                Scene.Triangles.push_back({i1, i2, i3, ChartIndex, bCastShadow});
            }
        }

        void AppendRamp(FLightBakerScene& Scene, uint32_t ChartIndex, const glm::mat4& M, float Width, float Height,
                        float Depth, const FBakeMaterialSample& Mat, bool bCastShadow) {
            float hx = Width * 0.5f;
            float hz = Depth * 0.5f;
            // Wedge: bottom rectangle + slope + sides approximated as box-like tris
            glm::vec3 corners[6] = {{-hx, 0, -hz}, {hx, 0, -hz},       {hx, 0, hz},
                                    {-hx, 0, hz},  {-hx, Height, -hz}, {hx, Height, -hz}};
            glm::mat3 normalMat = glm::transpose(glm::inverse(glm::mat3(M)));
            auto push = [&](const glm::vec3& p, const glm::vec3& n, const glm::vec2& uv) {
                FBakeVertex v;
                v.Position = glm::vec3(M * glm::vec4(p, 1.0f));
                v.Normal = glm::normalize(normalMat * n);
                v.LightmapUV = uv;
                v.Albedo = Mat.Albedo;
                v.Metallic = Mat.Metallic;
                v.Roughness = Mat.Roughness;
                v.Emissive = Mat.Emissive;
                Scene.Vertices.push_back(v);
            };
            uint32_t base = static_cast<uint32_t>(Scene.Vertices.size());
            // Bottom
            push(corners[0], {0, -1, 0}, {0.02f, 0.02f});
            push(corners[1], {0, -1, 0}, {0.98f, 0.02f});
            push(corners[2], {0, -1, 0}, {0.98f, 0.48f});
            push(corners[3], {0, -1, 0}, {0.02f, 0.48f});
            Scene.Triangles.push_back({base + 0, base + 1, base + 2, ChartIndex, bCastShadow});
            Scene.Triangles.push_back({base + 0, base + 2, base + 3, ChartIndex, bCastShadow});
            // Slope
            glm::vec3 slopeN = glm::normalize(glm::cross(corners[1] - corners[0], corners[4] - corners[0]));
            uint32_t s = static_cast<uint32_t>(Scene.Vertices.size());
            push(corners[0], slopeN, {0.02f, 0.52f});
            push(corners[1], slopeN, {0.98f, 0.52f});
            push(corners[5], slopeN, {0.98f, 0.98f});
            push(corners[4], slopeN, {0.02f, 0.98f});
            Scene.Triangles.push_back({s + 0, s + 1, s + 2, ChartIndex, bCastShadow});
            Scene.Triangles.push_back({s + 0, s + 2, s + 3, ChartIndex, bCastShadow});
        }

        void AppendPyramid(FLightBakerScene& Scene, uint32_t ChartIndex, const glm::mat4& M, float Width, float Height,
                           float Depth, const FBakeMaterialSample& Mat, bool bCastShadow) {
            float hx = Width * 0.5f;
            float hz = Depth * 0.5f;
            glm::vec3 apex(0, Height, 0);
            glm::vec3 basePts[4] = {{-hx, 0, -hz}, {hx, 0, -hz}, {hx, 0, hz}, {-hx, 0, hz}};
            glm::mat3 normalMat = glm::transpose(glm::inverse(glm::mat3(M)));
            auto pushTri = [&](const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, float u0) {
                glm::vec3 n = glm::normalize(glm::cross(b - a, c - a));
                uint32_t base = static_cast<uint32_t>(Scene.Vertices.size());
                glm::vec3 pts[3] = {a, b, c};
                glm::vec2 uvs[3] = {{u0, 0.02f}, {u0 + 0.2f, 0.02f}, {u0 + 0.1f, 0.98f}};
                for (int i = 0; i < 3; ++i) {
                    FBakeVertex v;
                    v.Position = glm::vec3(M * glm::vec4(pts[i], 1.0f));
                    v.Normal = glm::normalize(normalMat * n);
                    v.LightmapUV = uvs[i];
                    v.Albedo = Mat.Albedo;
                    v.Metallic = Mat.Metallic;
                    v.Roughness = Mat.Roughness;
                    v.Emissive = Mat.Emissive;
                    Scene.Vertices.push_back(v);
                }
                Scene.Triangles.push_back({base + 0, base + 1, base + 2, ChartIndex, bCastShadow});
            };
            pushTri(basePts[0], basePts[1], basePts[2], 0.02f);
            pushTri(basePts[0], basePts[2], basePts[3], 0.28f);
            pushTri(basePts[0], basePts[1], apex, 0.52f);
            pushTri(basePts[1], basePts[2], apex, 0.68f);
            pushTri(basePts[2], basePts[3], apex, 0.84f);
            pushTri(basePts[3], basePts[0], apex, 0.02f);
        }

        void AppendProceduralMesh(FLightBakerScene& Scene, uint32_t ChartIndex, const FMeshComponent& Mesh,
                                  const glm::mat4& M, const FBakeMaterialSample& Mat) {
            const std::string& type = Mesh.MeshType;
            if (type == "Plane" || type == "Quad") {
                AppendPlane(Scene, ChartIndex, M, Mesh.MeshWidth, Mesh.MeshDepth, Mat, Mesh.bCastShadows);
            } else if (type == "Sphere") {
                AppendSphere(Scene, ChartIndex, M, Mesh.MeshRadius > 0 ? Mesh.MeshRadius : 0.5f, Mat,
                             Mesh.bCastShadows);
            } else if (type == "Cylinder") {
                AppendCylinder(Scene, ChartIndex, M, Mesh.MeshRadius, Mesh.MeshRadius,
                               Mesh.MeshHeight > 0 ? Mesh.MeshHeight : 1.0f, Mat, Mesh.bCastShadows);
            } else if (type == "Cone") {
                AppendCylinder(Scene, ChartIndex, M, Mesh.MeshRadius, 0.0f,
                               Mesh.MeshHeight > 0 ? Mesh.MeshHeight : 1.0f, Mat, Mesh.bCastShadows);
            } else if (type == "Ramp") {
                AppendRamp(Scene, ChartIndex, M, Mesh.MeshWidth, Mesh.MeshHeight, Mesh.MeshDepth, Mat,
                           Mesh.bCastShadows);
            } else if (type == "Pyramid") {
                AppendPyramid(Scene, ChartIndex, M, Mesh.MeshWidth, Mesh.MeshHeight, Mesh.MeshDepth, Mat,
                              Mesh.bCastShadows);
            } else if (type == "Cube") {
                const float s = Mesh.MeshSize > 0.0f ? Mesh.MeshSize : 1.0f;
                AppendBox(Scene, ChartIndex, M, s, s, s, Mat, Mesh.bCastShadows);
            } else {
                // Box (and default): use Width/Height/Depth like FMeshPrimitives::CreateBox.
                const float sx = Mesh.MeshWidth > 0.0f ? Mesh.MeshWidth : (Mesh.MeshSize > 0.0f ? Mesh.MeshSize : 1.0f);
                const float sy =
                    Mesh.MeshHeight > 0.0f ? Mesh.MeshHeight : (Mesh.MeshSize > 0.0f ? Mesh.MeshSize : 1.0f);
                const float sz = Mesh.MeshDepth > 0.0f ? Mesh.MeshDepth : (Mesh.MeshSize > 0.0f ? Mesh.MeshSize : 1.0f);
                AppendBox(Scene, ChartIndex, M, sx, sy, sz, Mat, Mesh.bCastShadows);
            }
        }

        void PersistMissingLightmapUVs(UStaticMesh& Mesh) {
            if (FLightmapUV::HasLightmapUV(Mesh)) {
                if (!Mesh.HasUniqueLightmapUV())
                    Mesh.SetHasUniqueLightmapUV(true);
                return;
            }
            FLightmapUV::GenerateBoxPackedLightmapUVs(Mesh);
            if (Mesh.GetAssetPath().empty()) {
                Mesh.CreateGPUResources();
                return;
            }
            const std::string disk = UAssetManager::ResolveVirtualPath(Mesh.GetAssetPath());
            const std::string savePath = !disk.empty() ? disk : Mesh.GetAssetPath();
            if (Mesh.SaveToFile(savePath))
                LogLM("Wrote lightmap UVs to " + savePath);
            else
                LogLM("Warning: failed to persist lightmap UVs to " + savePath);
            // Baker mutates CPU verts; re-upload so the viewport samples the same UV1.
            Mesh.CreateGPUResources();
        }

        void PersistWorldLightmapUVs(UWorld& InWorld) {
            for (auto& actorRef : InWorld.GetAllActors()) {
                if (!actorRef || !actorRef->HasComponent<FStaticMeshComponent>())
                    continue;
                auto& smc = actorRef->GetComponent<FStaticMeshComponent>();
                if (smc.Mobility != EComponentMobility::Static || !smc.StaticMesh)
                    continue;
                PersistMissingLightmapUVs(*smc.StaticMesh);
            }
        }

        /**
         * InMat is the material of the whole mesh. When InSubmeshMaterials is given, each part of the
         * mesh bounces the colour of its own material instead.
         */
        void AppendStaticMesh(FLightBakerScene& Scene, uint32_t ChartIndex, UStaticMesh& Mesh, const glm::mat4& M,
                              const FBakeMaterialSample& Mat, bool bCastShadow,
                              const std::vector<TRef<FMaterialInstance>>* InSubmeshMaterials = nullptr) {
            auto& verts = Mesh.GetVertices();
            const auto& indices = Mesh.GetIndices();
            if (verts.empty() || indices.size() < 3)
                return;

            PersistMissingLightmapUVs(Mesh);

            glm::mat3 normalMat = glm::transpose(glm::inverse(glm::mat3(M)));
            uint32_t base = static_cast<uint32_t>(Scene.Vertices.size());
            for (const auto& sv : verts) {
                FBakeVertex v;
                v.Position = glm::vec3(M * glm::vec4(sv.Position, 1.0f));
                v.Normal = glm::normalize(normalMat * sv.Normal);
                v.LightmapUV = sv.LightmapUV;
                v.Albedo = BounceAlbedo(Mat);
                v.Metallic = Mat.Metallic;
                v.Roughness = Mat.Roughness;
                v.Emissive = Mat.Emissive;
                Scene.Vertices.push_back(v);
            }
            if (InSubmeshMaterials) {
                std::map<const FMaterialInstance*, FBakeMaterialSample> sampled;
                for (const FStaticSubmesh& submesh : Mesh.GetSubmeshes()) {
                    const TRef<FMaterialInstance> material =
                        ResolveStaticSubmeshMaterial(Mesh, submesh, *InSubmeshMaterials);
                    if (!material)
                        continue;
                    auto found = sampled.find(material.get());
                    if (found == sampled.end())
                        found = sampled.emplace(material.get(), SampleMaterialInstance(material)).first;
                    const FBakeMaterialSample& part = found->second;
                    const glm::vec3 albedo = BounceAlbedo(part);
                    const size_t end = std::min<size_t>(submesh.IndexOffset + submesh.IndexCount, indices.size());
                    for (size_t i = submesh.IndexOffset; i < end; ++i) {
                        if (indices[i] >= verts.size())
                            continue;
                        FBakeVertex& v = Scene.Vertices[base + indices[i]];
                        v.Albedo = albedo;
                        v.Metallic = part.Metallic;
                        v.Roughness = part.Roughness;
                        v.Emissive = part.Emissive;
                    }
                }
            }
            for (size_t i = 0; i + 2 < indices.size(); i += 3) {
                Scene.Triangles.push_back(
                    {base + indices[i], base + indices[i + 1], base + indices[i + 2], ChartIndex, bCastShadow});
            }
        }

        fs::path ResolveLightmapAbsolutePath(const std::string& InMapPath, const std::string& InLightmapVirtual) {
            fs::path mapDir = fs::path(InMapPath).parent_path();
            fs::path contentRoot = mapDir;
            if (mapDir.filename() == "Maps")
                contentRoot = mapDir.parent_path();
            // Support both "Lightmaps/X.llightmap" and "/Game/Lightmaps/X.llightmap"
            std::string rel = InLightmapVirtual;
            if (rel.rfind("/Game/", 0) == 0)
                rel = rel.substr(6);
            else if (rel.rfind("Game/", 0) == 0)
                rel = rel.substr(5);
            return contentRoot / rel;
        }

        FWorldSettingsComponent* FindWorldSettings(UWorld& InWorld) {
            for (auto& actorRef : InWorld.GetAllActors()) {
                if (actorRef && actorRef->HasComponent<FWorldSettingsComponent>())
                    return &actorRef->GetComponent<FWorldSettingsComponent>();
            }
            return nullptr;
        }

        const FSkyboxComponent* FindSkybox(UWorld& InWorld) {
            const FSkyboxComponent* found = nullptr;
            for (auto& actorRef : InWorld.GetAllActors()) {
                if (actorRef && actorRef->HasComponent<FSkyboxComponent>())
                    found = &actorRef->GetComponent<FSkyboxComponent>();
            }
            return found;
        }

        void FillBakeEnvironment(FLightBakerScene& Scene, const FSkyboxComponent& InSkybox) {
            Scene.Environment.bEnabled = InSkybox.bEnabled;
            Scene.Environment.Zenith = InSkybox.SkyZenithColor;
            Scene.Environment.Horizon = InSkybox.HorizonColor;
            Scene.Environment.Ground = InSkybox.GroundColor;
            Scene.Environment.Intensity = InSkybox.EnvironmentIntensity;

            if (!InSkybox.bEnabled || !InSkybox.bUseHDREnvironmentMap)
                return;

            std::string hdrPath = InSkybox.HDREnvironmentMapPath;
            if (hdrPath.empty())
                return;
            hdrPath = UAssetManager::ResolveVirtualPath(hdrPath);
            if (hdrPath.size() < 5 || hdrPath.substr(hdrPath.size() - 5) != ".lhdr")
                return;

            FNativeHDRData hdr;
            if (!hdr.LoadFromFile(hdrPath) || hdr.Pixels.empty())
                return;
            Scene.Environment.HDRRGBA = std::move(hdr.Pixels);
            Scene.Environment.HDRWidth = static_cast<int>(hdr.Header.Width);
            Scene.Environment.HDRHeight = static_cast<int>(hdr.Header.Height);
        }

    } // namespace

    uint64_t FLightmass::ComputeBakeInputHash(const UWorld& InWorld, const FLightmassSettings& InSettings) {
        return FLightmapBakeInputs::ComputeHash(InWorld, InSettings);
    }

    void FLightmass::RefreshRuntimeLightmapTrust(UWorld& InWorld) {
        FLightmapBakeInputs::RefreshRuntimeTrust(InWorld);
    }

    bool FLightmass::ValidateMap(const std::string& InMapPath, std::string& OutMessage) {
        if (!fs::exists(InMapPath)) {
            OutMessage = "Map file not found";
            return false;
        }

        fs::path mapPath = InMapPath;
        fs::path contentRoot = mapPath.parent_path();
        if (contentRoot.filename() == "Maps")
            contentRoot = contentRoot.parent_path();
        UAssetManager::SetContentRoot(contentRoot.string());

        auto world = UWorld::Create();
        FMapSerializer serializer(world);
        if (!serializer.Deserialize(InMapPath)) {
            OutMessage = "Failed to deserialize map";
            return false;
        }

        FLightmassSettings settings;
        std::string lightmapVirtual;
        bool bStaticLighting = false;
        uint64_t storedHash = 0;
        if (FWorldSettingsComponent* ws = FindWorldSettings(*world)) {
            bStaticLighting = ws->bStaticLighting;
            storedHash = ws->LightmapBakeHash;
            lightmapVirtual = ws->LightmapAssetPath;
            FLightmapBakeInputs::ApplyWorldSettings(*ws, settings, lightmapVirtual);
        }

        uint32_t staticMeshes = 0;
        uint32_t bakeLights = 0;
        uint32_t badCharts = 0;
        for (auto& actorRef : world->GetAllActors()) {
            if (!actorRef)
                continue;
            if (actorRef->HasComponent<FStaticMeshComponent>()) {
                auto& smc = actorRef->GetComponent<FStaticMeshComponent>();
                if (smc.Mobility == EComponentMobility::Static) {
                    ++staticMeshes;
                    if (smc.LightmapIndex < 0 || !std::isfinite(smc.LightmapScale.x) ||
                        !std::isfinite(smc.LightmapBias.x))
                        ++badCharts;
                }
            }
            if (actorRef->HasComponent<FMeshComponent>()) {
                auto& mc = actorRef->GetComponent<FMeshComponent>();
                if (mc.Mobility == EComponentMobility::Static) {
                    ++staticMeshes;
                    if (mc.LightmapIndex < 0 || !std::isfinite(mc.LightmapScale.x) || !std::isfinite(mc.LightmapBias.x))
                        ++badCharts;
                }
            }
            if (actorRef->HasComponent<FDirectionalLightComponent>()) {
                auto& c = actorRef->GetComponent<FDirectionalLightComponent>();
                if (c.bEnabled && IsLightmassBakeLight(c.Mobility))
                    ++bakeLights;
            }
            if (actorRef->HasComponent<FPointLightComponent>()) {
                auto& c = actorRef->GetComponent<FPointLightComponent>();
                if (c.bEnabled && IsLightmassBakeLight(c.Mobility))
                    ++bakeLights;
            }
            if (actorRef->HasComponent<FSpotLightComponent>()) {
                auto& c = actorRef->GetComponent<FSpotLightComponent>();
                if (c.bEnabled && IsLightmassBakeLight(c.Mobility))
                    ++bakeLights;
            }
        }

        std::ostringstream ss;
        ss << "Static meshes: " << staticMeshes << ", Bake lights (Static+Stationary): " << bakeLights;

        if (staticMeshes == 0) {
            OutMessage = ss.str() + " — FAIL: no static geometry";
            return false;
        }

        if (bStaticLighting) {
            if (lightmapVirtual.empty()) {
                OutMessage = ss.str() + " — FAIL: StaticLighting enabled but LightmapAsset empty";
                return false;
            }
            fs::path lightmapAbs = ResolveLightmapAbsolutePath(InMapPath, lightmapVirtual);
            if (!fs::exists(lightmapAbs)) {
                OutMessage = ss.str() + " — FAIL: missing lightmap " + lightmapAbs.string();
                return false;
            }
            FLightmapAsset atlas;
            if (!atlas.LoadFromFile(lightmapAbs.string()) || atlas.GetWidth() == 0 || atlas.GetHeight() == 0) {
                OutMessage = ss.str() + " — FAIL: invalid lightmap asset";
                return false;
            }
            uint64_t expected = ComputeBakeInputHash(*world, settings);
            if (storedHash == 0) {
                OutMessage = ss.str() + " — FAIL: LightmapBakeHash is 0";
                return false;
            }
            if (storedHash != expected) {
                ss << " — FAIL: LightmapBakeHash stale (stored=" << std::hex << storedHash << " expected=" << expected
                   << std::dec << ")";
                OutMessage = ss.str();
                return false;
            }
            if (atlas.GetContentHash() == 0) {
                OutMessage = ss.str() + " — FAIL: .llightmap ContentHash is 0";
                return false;
            }
            if (atlas.GetContentHash() != expected) {
                ss << " — FAIL: .llightmap ContentHash stale";
                OutMessage = ss.str();
                return false;
            }
            ss << ", Atlas " << atlas.GetWidth() << "x" << atlas.GetHeight();
            if (FWorldSettingsComponent* ws = FindWorldSettings(*world); ws && !ws->LightProbeAssetPath.empty()) {
                FLightProbeGrid probes;
                uint64_t probeHash = 0;
                const fs::path probesAbs = ResolveLightmapAbsolutePath(InMapPath, ws->LightProbeAssetPath);
                if (!FLightProbeAsset::Load(probesAbs.string(), probes, probeHash)) {
                    OutMessage = ss.str() + " — FAIL: missing or invalid light probes " + probesAbs.string();
                    return false;
                }
                if (probeHash != expected) {
                    OutMessage = ss.str() + " — FAIL: light probes are from another bake";
                    return false;
                }
                ss << ", Light probes " << probes.Counts.x << "x" << probes.Counts.y << "x" << probes.Counts.z;
            }
            if (badCharts > 0) {
                OutMessage = ss.str() + " — FAIL: " + std::to_string(badCharts) + " meshes missing chart metadata";
                return false;
            }
            ss << " — OK";
        }

        OutMessage = ss.str();
        return true;
    }

    FLightmassBakeResult FLightmass::BakeMap(const std::string& InMapPath, const FLightmassSettings& InSettings,
                                             bool bForce) {
        FLightmassBakeResult result;
        LogLM("Loading map...");

        if (!fs::exists(InMapPath)) {
            result.Message = "Map not found: " + InMapPath;
            return result;
        }

        auto world = UWorld::Create();
        FMapSerializer serializer(world);
        if (!serializer.Deserialize(InMapPath)) {
            result.Message = "Failed to load map";
            return result;
        }

        FLightmassSettings settings = InSettings;
        std::string lightmapVirtual = "/Game/Lightmaps/" + fs::path(InMapPath).stem().string() + ".llightmap";

        if (FWorldSettingsComponent* ws = FindWorldSettings(*world)) {
            FLightmapBakeInputs::ApplyWorldSettings(*ws, settings, lightmapVirtual);
        }

        PersistWorldLightmapUVs(*world);

        uint64_t bakeHash = ComputeBakeInputHash(*world, settings);
        result.BakeHash = bakeHash;

        fs::path lightmapAbs = ResolveLightmapAbsolutePath(InMapPath, lightmapVirtual);

        if (!bForce && fs::exists(lightmapAbs)) {
            FLightmapAsset existing;
            bool bAtlasMatch = existing.LoadFromFile(lightmapAbs.string()) && existing.GetContentHash() != 0 &&
                               existing.GetContentHash() == bakeHash;
            bool bWorldMatch = false;
            // A bake made before light probes existed, or without them, is not what was asked for now.
            bool bProbesMatch = !settings.bLightProbes;
            if (FWorldSettingsComponent* ws = FindWorldSettings(*world)) {
                bWorldMatch = ws->LightmapBakeHash != 0 && ws->LightmapBakeHash == bakeHash;
                if (settings.bLightProbes && !ws->LightProbeAssetPath.empty()) {
                    FLightProbeGrid existingProbes;
                    uint64_t probeHash = 0;
                    bProbesMatch = FLightProbeAsset::Load(
                                       ResolveLightmapAbsolutePath(InMapPath, ws->LightProbeAssetPath).string(),
                                       existingProbes, probeHash) &&
                                   probeHash == bakeHash;
                }
            }
            if (bAtlasMatch && bWorldMatch && bProbesMatch) {
                result.bSuccess = true;
                result.Message = "Lightmap cache valid";
                result.LightmapPath = lightmapAbs.string();
                LogLM("Build skipped (cache valid).");
                return result;
            }
        }

        FLightBakerScene scene;
        std::vector<FLightmapChart> charts;
        struct FBakeInstanceRef {
            AActor* Actor = nullptr;
            bool bIsStaticMeshAsset = false;
        };
        std::vector<FBakeInstanceRef> instances;

        for (auto& actorRef : world->GetAllActors()) {
            if (!actorRef)
                continue;

            if (actorRef->HasComponent<FStaticMeshComponent>()) {
                auto& smc = actorRef->GetComponent<FStaticMeshComponent>();
                if (smc.Mobility != EComponentMobility::Static || !smc.StaticMesh)
                    continue;
                FLightmapChart chart;
                chart.InstanceIndex = static_cast<uint32_t>(charts.size());
                chart.Resolution = smc.LightmapResolution > 0 ? smc.LightmapResolution : settings.LightmapResolution;
                charts.push_back(chart);
                instances.push_back({actorRef.get(), true});
            } else if (actorRef->HasComponent<FMeshComponent>()) {
                auto& mc = actorRef->GetComponent<FMeshComponent>();
                if (mc.Mobility != EComponentMobility::Static)
                    continue;
                FLightmapChart chart;
                chart.InstanceIndex = static_cast<uint32_t>(charts.size());
                chart.Resolution = mc.LightmapResolution > 0 ? mc.LightmapResolution : settings.LightmapResolution;
                charts.push_back(chart);
                instances.push_back({actorRef.get(), false});
            }

            if (actorRef->HasComponent<FDirectionalLightComponent>()) {
                auto& c = actorRef->GetComponent<FDirectionalLightComponent>();
                if (c.bEnabled && IsLightmassBakeLight(c.Mobility))
                    scene.DirectionalLights.push_back({c.Light, DoesLightmassBakeDirect(c.Mobility)});
            }
            if (actorRef->HasComponent<FPointLightComponent>()) {
                auto& c = actorRef->GetComponent<FPointLightComponent>();
                if (c.bEnabled && IsLightmassBakeLight(c.Mobility)) {
                    FPointLight l = c.Light;
                    l.Position = actorRef->GetComponent<FTransformComponent>().Translation;
                    scene.PointLights.push_back({l, DoesLightmassBakeDirect(c.Mobility)});
                }
            }
            if (actorRef->HasComponent<FSpotLightComponent>()) {
                auto& c = actorRef->GetComponent<FSpotLightComponent>();
                if (c.bEnabled && IsLightmassBakeLight(c.Mobility)) {
                    FSpotLight l = c.Light;
                    const auto& xf = actorRef->GetComponent<FTransformComponent>();
                    SyncSpotLightFromTransform(l, xf.Translation, xf.Rotation);
                    scene.SpotLights.push_back({l, DoesLightmassBakeDirect(c.Mobility)});
                }
            }
        }

        result.StaticMeshCount = static_cast<uint32_t>(instances.size());
        result.StaticLightCount =
            static_cast<uint32_t>(scene.DirectionalLights.size() + scene.PointLights.size() + scene.SpotLights.size());
        LogLM("Static meshes: " + std::to_string(result.StaticMeshCount));
        LogLM("Bake lights (Static+Stationary): " + std::to_string(result.StaticLightCount));

        if (instances.empty()) {
            // Empty maps (e.g. PlayerStart + lights only) are a successful no-op bake.
            result.bSuccess = true;
            result.Message = "Nothing to bake (no static geometry)";
            LogLM(result.Message);
            return result;
        }

        LogLM("Building lightmap UVs...");
        LogLM("Building atlas...");
        FLightmapBuilder::PackCharts(charts, scene.AtlasWidth, scene.AtlasHeight,
                                     static_cast<uint32_t>(std::max(0.0f, settings.TexelPadding)));
        scene.Charts = charts;
        result.AtlasWidth = scene.AtlasWidth;
        result.AtlasHeight = scene.AtlasHeight;

        for (size_t i = 0; i < instances.size(); ++i) {
            AActor* actor = instances[i].Actor;
            glm::mat4 M = actor->GetActorWorldMatrix();
            FBakeMaterialSample mat = ResolveBakeMaterial(*actor);
            uint32_t chartIndex = static_cast<uint32_t>(i);

            if (instances[i].bIsStaticMeshAsset) {
                auto& smc = actor->GetComponent<FStaticMeshComponent>();
                // A material set on the actor covers the whole mesh; otherwise each part has its own.
                const bool bWholeMeshMaterial = actor->HasComponent<FMaterialComponent>() &&
                                                actor->GetComponent<FMaterialComponent>().MaterialInstance;
                AppendStaticMesh(scene, chartIndex, *smc.StaticMesh, M, mat, smc.bCastShadows,
                                 bWholeMeshMaterial ? nullptr : &smc.MaterialOverrides);
                smc.LightmapIndex = static_cast<int32_t>(i);
                smc.LightmapScale = charts[i].Scale;
                smc.LightmapBias = charts[i].Bias;
                smc.LightmapAssetPath = lightmapVirtual;
            } else {
                auto& mc = actor->GetComponent<FMeshComponent>();
                AppendProceduralMesh(scene, chartIndex, mc, M, mat);
                mc.LightmapIndex = static_cast<int32_t>(i);
                mc.LightmapScale = charts[i].Scale;
                mc.LightmapBias = charts[i].Bias;
                mc.LightmapAssetPath = lightmapVirtual;
            }
        }

        LogLM("Baking direct lighting...");
        LogLM("Baking indirect lighting...");

        FLightBakerSettings bakerSettings;
        bakerSettings.NumIndirectBounces = settings.NumIndirectBounces;
        bakerSettings.SamplesPerTexel = settings.SamplesPerTexel;
        bakerSettings.IndirectIntensity = settings.IndirectIntensity;
        bakerSettings.bAmbientOcclusion = settings.bAmbientOcclusion;
        bakerSettings.AOIntensity = settings.AOIntensity;
        bakerSettings.AORadius = settings.AORadius * std::max(settings.WorldScale, 1e-4f);
        bakerSettings.Seed = settings.DeterministicSeed;

        if (const FSkyboxComponent* sky = FindSkybox(*world)) {
            FillBakeEnvironment(scene, *sky);
            if (scene.Environment.bEnabled) {
                if (scene.Environment.HDRWidth > 0) {
                    LogLM("Bake environment: HDR " + std::to_string(scene.Environment.HDRWidth) + "x" +
                          std::to_string(scene.Environment.HDRHeight) +
                          " intensity=" + std::to_string(scene.Environment.Intensity));
                } else {
                    LogLM("Bake environment: atmosphere intensity=" + std::to_string(scene.Environment.Intensity));
                }
            }
        }

        std::vector<float> pixels;
        FLightBaker::Bake(scene, bakerSettings, pixels);

        // Light probes: the same scene, measured at points in the air instead of on the surfaces.
        FLightProbeGrid probes;
        const std::string probesVirtual = fs::path(lightmapVirtual).replace_extension(".lprobes").generic_string();
        const fs::path probesAbs = ResolveLightmapAbsolutePath(InMapPath, probesVirtual);
        if (settings.bLightProbes) {
            FLightProbeBakeSettings probeSettings;
            probeSettings.Spacing = settings.LightProbeSpacing;
            probeSettings.VerticalSpacing = settings.LightProbeVerticalSpacing;
            probeSettings.SamplesPerProbe = settings.LightingBuildQuality == ELightingBuildQuality::Production
                                                ? 256u
                                                : (settings.LightingBuildQuality == ELightingBuildQuality::Preview ? 64u : 128u);
            // Where the level has geometry...
            glm::vec3 low(1.0e30f);
            glm::vec3 high(-1.0e30f);
            for (const FBakeVertex& vertex : scene.Vertices) {
                low = glm::min(low, vertex.Position);
                high = glm::max(high, vertex.Position);
            }
            // ...and, when the map says where the game is played, only there.
            glm::vec3 playLow(1.0e30f);
            glm::vec3 playHigh(-1.0e30f);
            bool bHasPlayVolume = false;
            for (auto& actorRef : world->GetAllActors()) {
                if (const auto* volume = dynamic_cast<const ANavMeshBoundsVolume*>(actorRef.get())) {
                    glm::vec3 volumeLow;
                    glm::vec3 volumeHigh;
                    volume->GetBounds(volumeLow, volumeHigh);
                    playLow = glm::min(playLow, glm::min(volumeLow, volumeHigh));
                    playHigh = glm::max(playHigh, glm::max(volumeLow, volumeHigh));
                    bHasPlayVolume = true;
                }
            }
            if (bHasPlayVolume) {
                const glm::vec3 clippedLow = glm::max(low, playLow);
                const glm::vec3 clippedHigh = glm::min(high, playHigh);
                // A play volume that misses the geometry altogether says nothing useful.
                if (clippedLow.x <= clippedHigh.x && clippedLow.y <= clippedHigh.y && clippedLow.z <= clippedHigh.z) {
                    low = clippedLow;
                    high = clippedHigh;
                }
            }
            probeSettings.BoundsMin = low;
            probeSettings.BoundsMax = high;
            LogLM("Baking light probes...");
            const auto probeStart = std::chrono::steady_clock::now();
            FLightBaker::BakeProbes(scene, bakerSettings, probeSettings, probes);
            const float probeSeconds =
                std::chrono::duration<float>(std::chrono::steady_clock::now() - probeStart).count();
            size_t validProbes = 0;
            for (const uint8_t valid : probes.Valid)
                validProbes += valid ? 1 : 0;
            LogLM("Light probes: " + std::to_string(probes.Counts.x) + " x " + std::to_string(probes.Counts.y) + " x " +
                  std::to_string(probes.Counts.z) + " every " + std::to_string(probes.Spacing.x) + " m (" +
                  std::to_string(probes.Spacing.y) + " m up), " + std::to_string(validProbes) + " of " +
                  std::to_string(probes.ProbeCount()) + " in the open, " + std::to_string(probeSeconds) + " s");
            if (!FLightProbeAsset::Save(probesAbs.string(), probes, bakeHash)) {
                result.Message = "Failed to write light probes";
                return result;
            }
        }

        LogLM("Writing lightmaps...");
        fs::create_directories(lightmapAbs.parent_path());
        FLightmapAsset asset;
        asset.Allocate(scene.AtlasWidth, scene.AtlasHeight);
        asset.GetPixelsRGBA32F() = std::move(pixels);
        asset.SetContentHash(bakeHash);
        if (!asset.SaveToFile(lightmapAbs.string())) {
            result.Message = "Failed to write lightmap";
            return result;
        }

        FWorldSettingsComponent* ws = FindWorldSettings(*world);
        if (!ws) {
            AActor* env = world->FindActorByName("Environment Skybox");
            if (!env)
                env = world->SpawnActor("Environment Skybox");
            ws = &env->AddComponent<FWorldSettingsComponent>();
        }
        ws->bStaticLighting = true;
        ws->LightmapAssetPath = lightmapVirtual;
        ws->LightmapBakeHash = bakeHash;
        ws->LightmapResolution = settings.LightmapResolution;
        ws->NumIndirectBounces = settings.NumIndirectBounces;
        ws->SamplesPerTexel = settings.SamplesPerTexel;
        ws->IndirectIntensity = settings.IndirectIntensity;
        ws->bAmbientOcclusion = settings.bAmbientOcclusion;
        ws->AOIntensity = settings.AOIntensity;
        ws->AORadius = settings.AORadius;
        ws->TexelPadding = settings.TexelPadding;
        ws->WorldScale = settings.WorldScale;
        ws->LightingBuildQuality = settings.LightingBuildQuality;
        ws->bLightProbes = settings.bLightProbes;
        ws->LightProbeSpacing = settings.LightProbeSpacing;
        ws->LightProbeVerticalSpacing = settings.LightProbeVerticalSpacing;
        // A map baked without probes names none: an older file next to it is not this bake's.
        ws->LightProbeAssetPath = settings.bLightProbes ? probesVirtual : std::string();

        if (!serializer.Serialize(InMapPath)) {
            LogLM("Warning: failed to write lightmap metadata back to map");
        }

        // The game hashes the map as it loads it, which is not always the world this bake started
        // from: saving normalises light directions and writes derived rotations. So the hash that
        // counts is that of the file just written. Read it back and stamp that, until it settles.
        for (int round = 0; round < 3; ++round) {
            auto written = UWorld::Create();
            FMapSerializer reader(written);
            if (!reader.Deserialize(InMapPath))
                break;
            FWorldSettingsComponent* writtenSettings = FindWorldSettings(*written);
            if (!writtenSettings)
                break;
            FLightmassSettings asLoadedSettings;
            ApplyLightingBuildQuality(writtenSettings->LightingBuildQuality, asLoadedSettings);
            std::string unusedPath;
            FLightmapBakeInputs::ApplyWorldSettings(*writtenSettings, asLoadedSettings, unusedPath);
            const uint64_t asLoaded = ComputeBakeInputHash(*written, asLoadedSettings);
            if (asLoaded == writtenSettings->LightmapBakeHash)
                break;
            if (round == 2) {
                LogLM("Warning: the map does not load back the same twice; its bake will read as stale");
                break;
            }
            writtenSettings->LightmapBakeHash = asLoaded;
            asset.SetContentHash(asLoaded);
            const bool bProbesRestamped =
                !settings.bLightProbes || FLightProbeAsset::Save(probesAbs.string(), probes, asLoaded);
            if (!bProbesRestamped || !asset.SaveToFile(lightmapAbs.string()) || !reader.Serialize(InMapPath)) {
                LogLM("Warning: failed to restamp the bake hash");
                break;
            }
            result.BakeHash = asLoaded;
        }

        result.bSuccess = true;
        result.LightmapPath = lightmapAbs.string();
        result.Message = "Build completed successfully.";
        LogLM("Build completed successfully.");
        return result;
    }

} // namespace Leon
