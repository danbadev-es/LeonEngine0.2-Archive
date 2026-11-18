#include "AssetPipeline/FEnginePrimitiveImport.hpp"

#include "AssetPipeline/FLightmapUV.hpp"
#include "AssetPipeline/FMeshImporter.hpp"
#include "Assets/FEngineBuiltins.hpp"
#include "Assets/UAssetManager.hpp"
#include "Assets/UStaticMesh.hpp"

#include <filesystem>
#include <system_error>

namespace Leon {

    namespace fs = std::filesystem;

    FEnginePrimitiveImport::FResult FEnginePrimitiveImport::Run(bool bInForce) {
        FResult out;
        const std::pair<const char*, const char*> primitives[] = {{FEngineBuiltins::kMeshCube, "Cube"},
                                                                  {FEngineBuiltins::kMeshSphere, "Sphere"},
                                                                  {FEngineBuiltins::kMeshCylinder, "Cylinder"},
                                                                  {FEngineBuiltins::kMeshPlane, "Plane"}};
        for (const auto& [virtualPath, name] : primitives) {
            const std::string lmesh = UAssetManager::ResolveVirtualPath(virtualPath);
            if (lmesh.empty()) {
                out.Errors.push_back(std::string(name) + ": cannot resolve " + virtualPath);
                continue;
            }
            const fs::path fbx = fs::path(lmesh).replace_extension(".fbx");
            std::error_code ec;
            if (!fs::exists(fbx, ec)) {
                out.Errors.push_back(std::string(name) + ": no " + fbx.string());
                continue;
            }
            if (!bInForce && fs::exists(lmesh, ec) && fs::last_write_time(lmesh, ec) >= fs::last_write_time(fbx, ec) && !ec) {
                out.UpToDate.push_back(lmesh);
                continue;
            }

            FMeshImportSettings settings;
            settings.bSplitStaticMeshes = false;
            settings.bExtractMaterials = false;
            settings.bGenerateTangents = true;
            settings.bGenerateNormalsIfMissing = true;
            FMeshImportResult result;
            if (!FMeshImporter::ImportFBX(fbx.string(), settings, result) || !result.StaticMesh) {
                std::string message = std::string(name) + ": import of " + fbx.string() + " failed";
                for (const auto& error : result.Errors)
                    message += "; " + error;
                out.Errors.push_back(message);
                continue;
            }
            auto mesh = result.StaticMesh;
            mesh->GetMaterialSlots().clear();
            mesh->GetMaterialSlots().push_back({"Default", FEngineBuiltins::kWorldGridMaterial, nullptr});
            if (mesh->GetSubmeshes().empty()) {
                FStaticSubmesh section;
                section.Name = name;
                section.IndexCount = static_cast<uint32_t>(mesh->GetIndices().size());
                section.VertexCount = static_cast<uint32_t>(mesh->GetVertices().size());
                section.MaterialSlotIndex = 0;
                mesh->GetSubmeshes().push_back(section);
            }
            // An FBX import leaves the lightmap UVs at (0,0); a bake needs a unique second set.
            FLightmapUV::GenerateBoxPackedLightmapUVs(*mesh);
            mesh->CalculateBounds();
            mesh->SetAssetPath(virtualPath);
            fs::create_directories(fs::path(lmesh).parent_path(), ec);
            if (mesh->SaveToFile(lmesh))
                out.Imported.push_back(lmesh);
            else
                out.Errors.push_back(std::string(name) + ": could not write " + lmesh);
        }
        return out;
    }

} // namespace Leon
