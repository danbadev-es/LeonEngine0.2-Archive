#include "Engine/FMapSerializer.hpp"
#include "FMapSerializerInternals.hpp"
#include "Core/FLog.hpp"
#include "Core/FStringUtils.hpp"
#include "Gameplay/AActor.hpp"
#include "Gameplay/ACameraActor.hpp"
#include "Gameplay/ACharacter.hpp"
#include "Gameplay/ADefaultPawn.hpp"
#include "Gameplay/AGameModeBase.hpp"
#include "Gameplay/AGameStateBase.hpp"
#include "Gameplay/AHUD.hpp"
#include "Gameplay/APlayerCameraManager.hpp"
#include "Gameplay/APlayerController.hpp"
#include "Gameplay/APlayerStart.hpp"
#include "Gameplay/APlayerState.hpp"
#include "Gameplay/ATriggerVolume.hpp"
#include "Gameplay/AWorldSettings.hpp"
#include "Gameplay/UClassRegistry.hpp"
#include "Assets/UAssetManager.hpp"
#include "RenderCore/FMeshPrimitives.hpp"
#include "Scene/Components.hpp"
#include "RHI/IRenderDriver.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>
#include <string>
#include <vector>

namespace Leon {

    FMapSerializer::FMapSerializer(const TRef<UWorld>& InWorld) : World(InWorld) {}

    bool FMapSerializer::Serialize(const std::string& InFilePath) {
        std::string yaml;
        if (!SerializeText(yaml)) {
            return false;
        }

        // Atomic save: write a sibling temp file, verify it, then swap it over the target. A full disk or a
        // crash mid-write can no longer truncate the map; the previous version is kept as <file>.bak.
        namespace fs = std::filesystem;
        const fs::path target(InFilePath);
        fs::path tempPath = target;
        tempPath += ".tmp";
        fs::path backupPath = target;
        backupPath += ".bak";
        std::error_code ec;

        {
            std::ofstream fout(tempPath, std::ios::out | std::ios::trunc);
            if (!fout.is_open()) {
                LE_CORE_ERROR("FMapSerializer: Failed to open file for writing: {0}", tempPath.string());
                return false;
            }
            fout << yaml;
            fout.flush();
            if (!fout.good()) {
                fout.close();
                fs::remove(tempPath, ec);
                LE_CORE_ERROR("FMapSerializer: Write failed for '{0}'; the existing map was left untouched.",
                              InFilePath);
                return false;
            }
        }

        if (fs::exists(target, ec)) {
            fs::copy_file(target, backupPath, fs::copy_options::overwrite_existing, ec);
            if (ec)
                LE_CORE_WARN("FMapSerializer: Could not write backup '{0}': {1}", backupPath.string(), ec.message());
        }

        ec.clear();
        fs::rename(tempPath, target, ec);
        if (ec) {
            LE_CORE_ERROR("FMapSerializer: Could not replace '{0}': {1}. The existing map was left untouched.",
                          InFilePath, ec.message());
            std::error_code cleanup;
            fs::remove(tempPath, cleanup);
            return false;
        }

        LE_CORE_INFO("FMapSerializer: Successfully saved Map to '{0}'", InFilePath);
        return true;
    }

    bool FMapSerializer::SerializeText(std::string& OutYamlString) {
        if (!World) {
            LE_CORE_ERROR("FMapSerializer: Attempted to serialize a null UWorld!");
            return false;
        }

        FMapTextWriter ss;
        ss << "# LeonEngine2 Map Asset File (.lmap)\n";
        ss << "# Human-Readable & Deterministic Serialization\n";
        ss << "Map:\n";
        Indent(ss, 1);
        ss << "Name: " << QuoteMapString(World->GetName()) << "\n";
        Indent(ss, 1);
        ss << "Version: " << QuoteMapString(kMapFormatVersion) << "\n";

        // Collect outliner folders (registered + implied by actors)
        std::vector<std::string> folders = World->GetEditorFolders();
        for (const auto& actorRef : World->GetAllActors()) {
            if (!actorRef)
                continue;
            const std::string& fp = actorRef->GetFolderPath();
            if (fp.empty())
                continue;
            bool bExists = false;
            for (const auto& f : folders) {
                if (f == fp) {
                    bExists = true;
                    break;
                }
            }
            if (!bExists)
                folders.push_back(fp);
        }
        std::sort(folders.begin(), folders.end());
        folders.erase(std::unique(folders.begin(), folders.end()), folders.end());
        if (!folders.empty()) {
            Indent(ss, 1);
            ss << "OutlinerFolders:\n";
            for (const auto& folder : folders) {
                Indent(ss, 2);
                ss << "- " << QuoteMapString(folder) << "\n";
            }
        }
        ss << "\n";

        // 1. Environment / Global Settings
        ss << "Environment:\n";
        const FWorldSettingsComponent* worldSettings = nullptr;
        const FSkyboxComponent* skyboxComp = nullptr;
        for (const auto& actorRef : World->GetAllActors()) {
            if (!actorRef)
                continue;
            if (!worldSettings && actorRef->HasComponent<FWorldSettingsComponent>())
                worldSettings = &actorRef->GetComponent<FWorldSettingsComponent>();
            if (!skyboxComp && actorRef->HasComponent<FSkyboxComponent>())
                skyboxComp = &actorRef->GetComponent<FSkyboxComponent>();
        }
        if (worldSettings) {
            WriteWorldSettings(ss, *worldSettings);
        }
        if (skyboxComp) {
            const auto& sky = *skyboxComp;
            Indent(ss, 1);
            ss << "Skybox:\n";
            Indent(ss, 2);
            ss << "Enabled: " << (sky.bEnabled ? "true" : "false") << "\n";
            Indent(ss, 2);
            ss << "Exposure: " << sky.Exposure << "\n";
            Indent(ss, 2);
            ss << "SunIntensity: " << sky.SunIntensity << "\n";
            Indent(ss, 2);
            ss << "EnvironmentIntensity: " << sky.EnvironmentIntensity << "\n";
            Indent(ss, 2);
            ss << "UseHDREnvironmentMap: " << (sky.bUseHDREnvironmentMap ? "true" : "false") << "\n";
            Indent(ss, 2);
            ss << "HDREnvironmentMap: " << QuoteMapString(sky.HDREnvironmentMapPath) << "\n";
            Indent(ss, 2);
            ss << "SkyZenithColor: [" << sky.SkyZenithColor.r << ", " << sky.SkyZenithColor.g << ", "
               << sky.SkyZenithColor.b << "]\n";
            Indent(ss, 2);
            ss << "HorizonColor: [" << sky.HorizonColor.r << ", " << sky.HorizonColor.g << ", " << sky.HorizonColor.b
               << "]\n";
            Indent(ss, 2);
            ss << "GroundColor: [" << sky.GroundColor.r << ", " << sky.GroundColor.g << ", " << sky.GroundColor.b
               << "]\n";
            Indent(ss, 2);
            ss << "SunColor: [" << sky.SunColor.r << ", " << sky.SunColor.g << ", " << sky.SunColor.b << "]\n";
            if (sky.bFogEnabled) {
                Indent(ss, 2);
                ss << "FogEnabled: true\n";
                Indent(ss, 2);
                ss << "FogColor: [" << sky.FogColor.r << ", " << sky.FogColor.g << ", " << sky.FogColor.b << "]\n";
                Indent(ss, 2);
                ss << "FogDensity: " << sky.FogDensity << "\n";
                Indent(ss, 2);
                ss << "FogStartDistance: " << sky.FogStartDistance << "\n";
                Indent(ss, 2);
                ss << "FogHeightFalloff: " << sky.FogHeightFalloff << "\n";
                Indent(ss, 2);
                ss << "FogBaseHeight: " << sky.FogBaseHeight << "\n";
                Indent(ss, 2);
                ss << "FogMaxOpacity: " << sky.FogMaxOpacity << "\n";
            }
        } else {
            Indent(ss, 1);
            ss << "Skybox:\n";
            Indent(ss, 2);
            ss << "Enabled: false\n";
        }

        // 2. Actors Collection
        ss << "\nActors:\n";

        for (const auto& actorRef : World->GetAllActors()) {
            if (!actorRef)
                continue;
            const AActor& entity = *actorRef;

            if (entity.HasComponent<FSkyboxComponent>() || entity.HasComponent<FWorldSettingsComponent>()) {
                continue;
            }
            if (IsRuntimeFrameworkActor(entity)) {
                continue;
            }

            Indent(ss, 1);
            ss << "- Name: "
               << QuoteMapString(entity.HasComponent<FTagComponent>() ? entity.GetComponent<FTagComponent>().Tag
                                                                      : entity.GetName())
               << "\n";
            Indent(ss, 2);
            ss << "Class: " << QuoteMapString(entity.GetClass()) << "\n";
            Indent(ss, 2);
            ss << "GUID: " << QuoteMapString(entity.GetActorGuid().ToString()) << "\n";
            if (!entity.GetFolderPath().empty()) {
                Indent(ss, 2);
                ss << "FolderPath: " << QuoteMapString(entity.GetFolderPath()) << "\n";
            }
            if (const auto* start = dynamic_cast<const APlayerStart*>(&entity)) {
                if (!start->GetPlayerStartTag().empty()) {
                    Indent(ss, 2);
                    ss << "PlayerStartTag: " << QuoteMapString(start->GetPlayerStartTag()) << "\n";
                }
                if (start->GetTeamIndex() != 0) {
                    Indent(ss, 2);
                    ss << "TeamIndex: " << start->GetTeamIndex() << "\n";
                }
                if (!start->IsEnabled()) {
                    Indent(ss, 2);
                    ss << "PlayerStartEnabled: false\n";
                }
            }
            if (const auto* trigger = dynamic_cast<const ATriggerVolume*>(&entity)) {
                if (!trigger->IsEnabled()) {
                    Indent(ss, 2);
                    ss << "TriggerEnabled: false\n";
                }
            }

            if (!entity.GetProperties().empty()) {
                Indent(ss, 2);
                ss << "Properties:\n";
                for (const auto& [key, value] : entity.GetProperties()) {
                    Indent(ss, 3);
                    ss << key << ": " << QuoteMapString(value) << "\n";
                }
            }

            // Transform
            if (entity.HasComponent<FTransformComponent>()) {
                const auto& tc = entity.GetComponent<FTransformComponent>();
                Indent(ss, 2);
                ss << "Transform:\n";
                Indent(ss, 3);
                ss << "Translation: [" << tc.Translation.x << ", " << tc.Translation.y << ", " << tc.Translation.z
                   << "]\n";
                Indent(ss, 3);
                ss << "Rotation: [" << tc.Rotation.x << ", " << tc.Rotation.y << ", " << tc.Rotation.z << "]\n";
                Indent(ss, 3);
                ss << "Scale: [" << tc.Scale.x << ", " << tc.Scale.y << ", " << tc.Scale.z << "]\n";
            }

            // Box collision (blocking / trigger bounds edited in the Details panel)
            if (entity.HasComponent<FBoxCollisionComponent>()) {
                const auto& box = entity.GetComponent<FBoxCollisionComponent>();
                Indent(ss, 2);
                ss << "BoxCollision:\n";
                Indent(ss, 3);
                ss << "Min: [" << box.LocalMin.x << ", " << box.LocalMin.y << ", " << box.LocalMin.z << "]\n";
                Indent(ss, 3);
                ss << "Max: [" << box.LocalMax.x << ", " << box.LocalMax.y << ", " << box.LocalMax.z << "]\n";
                Indent(ss, 3);
                ss << "BlockMovement: " << (box.bBlockMovement ? "true" : "false") << "\n";
                Indent(ss, 3);
                ss << "Channel: " << static_cast<int>(box.Channel) << "\n";
            }

            // Camera Component
            if (entity.HasComponent<FCameraComponent>()) {
                const auto& cam = entity.GetComponent<FCameraComponent>();
                Indent(ss, 2);
                ss << "Camera:\n";
                Indent(ss, 3);
                ss << "Primary: " << (cam.bPrimary ? "true" : "false") << "\n";
                Indent(ss, 3);
                ss << "FOV: " << cam.Camera.GetFOV() << "\n";
                Indent(ss, 3);
                ss << "NearClip: " << cam.Camera.GetNearClip() << "\n";
                Indent(ss, 3);
                ss << "FarClip: " << cam.Camera.GetFarClip() << "\n";
            }

            // Static Mesh Component (Asset Based)
            if (entity.HasComponent<FStaticMeshComponent>()) {
                const auto& smc = entity.GetComponent<FStaticMeshComponent>();
                Indent(ss, 2);
                ss << "StaticMesh:\n";
                Indent(ss, 3);
                ss << "Asset: " << QuoteMapString(smc.AssetPath) << "\n";
                Indent(ss, 3);
                ss << "CastShadows: " << (smc.bCastShadows ? "true" : "false") << "\n";
                Indent(ss, 3);
                ss << "ReceiveShadows: " << (smc.bReceiveShadows ? "true" : "false") << "\n";
                Indent(ss, 3);
                ss << "VisibleInReflection: " << (smc.bVisibleInReflection ? "true" : "false") << "\n";
                // Written only when off: maps saved before the switch existed read as colliding.
                if (!smc.bCollision) {
                    Indent(ss, 3);
                    ss << "Collision: false\n";
                }
                Indent(ss, 3);
                ss << "Mobility: " << ComponentMobilityToString(smc.Mobility) << "\n";
                Indent(ss, 3);
                ss << "LightmapResolution: " << smc.LightmapResolution << "\n";
                if (smc.LightmapIndex >= 0) {
                    Indent(ss, 3);
                    ss << "LightmapIndex: " << smc.LightmapIndex << "\n";
                    Indent(ss, 3);
                    ss << "LightmapScale: [" << smc.LightmapScale.x << ", " << smc.LightmapScale.y << "]\n";
                    Indent(ss, 3);
                    ss << "LightmapBias: [" << smc.LightmapBias.x << ", " << smc.LightmapBias.y << "]\n";
                }
                if (!smc.LightmapAssetPath.empty()) {
                    Indent(ss, 3);
                    ss << "LightmapAsset: " << QuoteMapString(smc.LightmapAssetPath) << "\n";
                }

                if (!smc.MaterialOverridePaths.empty()) {
                    Indent(ss, 3);
                    ss << "MaterialOverrides:\n";
                    for (size_t slot = 0; slot < smc.MaterialOverridePaths.size(); ++slot) {
                        if (!smc.MaterialOverridePaths[slot].empty()) {
                            Indent(ss, 4);
                            ss << "- Slot: " << slot << "\n";
                            Indent(ss, 5);
                            ss << "Asset: " << QuoteMapString(smc.MaterialOverridePaths[slot]) << "\n";
                        }
                    }
                }
            }

            // Procedural Mesh Component
            if (entity.HasComponent<FMeshComponent>()) {
                const auto& mc = entity.GetComponent<FMeshComponent>();
                Indent(ss, 2);
                ss << "StaticMesh:\n";
                Indent(ss, 3);
                ss << "Type: " << QuoteMapString(mc.MeshType) << "\n";
                Indent(ss, 3);
                ss << "Size: " << mc.MeshSize << "\n";
                Indent(ss, 3);
                ss << "Width: " << mc.MeshWidth << "\n";
                Indent(ss, 3);
                ss << "Height: " << mc.MeshHeight << "\n";
                Indent(ss, 3);
                ss << "Depth: " << mc.MeshDepth << "\n";
                Indent(ss, 3);
                ss << "Radius: " << mc.MeshRadius << "\n";
                Indent(ss, 3);
                ss << "MetersPerUv: " << mc.MeshMetersPerUv << "\n";
                Indent(ss, 3);
                ss << "SubdivisionsX: " << mc.MeshSubdivX << "\n";
                Indent(ss, 3);
                ss << "SubdivisionsZ: " << mc.MeshSubdivZ << "\n";
                Indent(ss, 3);
                ss << "CastShadows: " << (mc.bCastShadows ? "true" : "false") << "\n";
                Indent(ss, 3);
                ss << "ReceiveShadows: " << (mc.bReceiveShadows ? "true" : "false") << "\n";
                Indent(ss, 3);
                ss << "VisibleInReflection: " << (mc.bVisibleInReflection ? "true" : "false") << "\n";
                // Written only when off: maps saved before the switch existed read as colliding.
                if (!mc.bCollision) {
                    Indent(ss, 3);
                    ss << "Collision: false\n";
                }
                Indent(ss, 3);
                ss << "Mobility: " << ComponentMobilityToString(mc.Mobility) << "\n";
                Indent(ss, 3);
                ss << "LightmapResolution: " << mc.LightmapResolution << "\n";
                if (mc.LightmapIndex >= 0) {
                    Indent(ss, 3);
                    ss << "LightmapIndex: " << mc.LightmapIndex << "\n";
                    Indent(ss, 3);
                    ss << "LightmapScale: [" << mc.LightmapScale.x << ", " << mc.LightmapScale.y << "]\n";
                    Indent(ss, 3);
                    ss << "LightmapBias: [" << mc.LightmapBias.x << ", " << mc.LightmapBias.y << "]\n";
                }
                if (!mc.LightmapAssetPath.empty()) {
                    Indent(ss, 3);
                    ss << "LightmapAsset: " << QuoteMapString(mc.LightmapAssetPath) << "\n";
                }
            }

            // Material Component
            if (entity.HasComponent<FMaterialComponent>()) {
                const auto& matComp = entity.GetComponent<FMaterialComponent>();
                Indent(ss, 2);
                ss << "Material:\n";
                Indent(ss, 3);
                ss << "Asset: " << QuoteMapString(matComp.AssetPath) << "\n";
            }

            // Directional Light Component
            if (entity.HasComponent<FDirectionalLightComponent>()) {
                const auto& dlc = entity.GetComponent<FDirectionalLightComponent>();
                Indent(ss, 2);
                ss << "DirectionalLight:\n";
                Indent(ss, 3);
                ss << "Enabled: " << (dlc.bEnabled ? "true" : "false") << "\n";
                Indent(ss, 3);
                ss << "Direction: [" << dlc.Light.Direction.x << ", " << dlc.Light.Direction.y << ", "
                   << dlc.Light.Direction.z << "]\n";
                Indent(ss, 3);
                ss << "Color: [" << dlc.Light.Color.r << ", " << dlc.Light.Color.g << ", " << dlc.Light.Color.b
                   << "]\n";
                Indent(ss, 3);
                ss << "Intensity: " << dlc.Light.Intensity << "\n";
                Indent(ss, 3);
                ss << "Mobility: " << LightMobilityToString(dlc.Mobility) << "\n";
            }

            // Point Light Component
            if (entity.HasComponent<FPointLightComponent>()) {
                const auto& plc = entity.GetComponent<FPointLightComponent>();
                Indent(ss, 2);
                ss << "PointLight:\n";
                Indent(ss, 3);
                ss << "Enabled: " << (plc.bEnabled ? "true" : "false") << "\n";
                Indent(ss, 3);
                ss << "Color: [" << plc.Light.Color.r << ", " << plc.Light.Color.g << ", " << plc.Light.Color.b
                   << "]\n";
                Indent(ss, 3);
                ss << "Intensity: " << plc.Light.Intensity << "\n";
                Indent(ss, 3);
                ss << "Radius: " << plc.Light.Radius << "\n";
                Indent(ss, 3);
                ss << "Mobility: " << LightMobilityToString(plc.Mobility) << "\n";
                // Written only when it is not the default (point lights: off, spot lights: on), so maps
                // that never set it are not rewritten.
                if (plc.bCastShadows) {
                    Indent(ss, 3);
                    ss << "CastShadows: true\n";
                }
            }

            // Spot Light Component
            if (entity.HasComponent<FSpotLightComponent>()) {
                const auto& slc = entity.GetComponent<FSpotLightComponent>();
                Indent(ss, 2);
                ss << "SpotLight:\n";
                Indent(ss, 3);
                ss << "Enabled: " << (slc.bEnabled ? "true" : "false") << "\n";
                Indent(ss, 3);
                ss << "Direction: [" << slc.Light.Direction.x << ", " << slc.Light.Direction.y << ", "
                   << slc.Light.Direction.z << "]\n";
                Indent(ss, 3);
                ss << "Color: [" << slc.Light.Color.r << ", " << slc.Light.Color.g << ", " << slc.Light.Color.b
                   << "]\n";
                Indent(ss, 3);
                ss << "Intensity: " << slc.Light.Intensity << "\n";
                Indent(ss, 3);
                ss << "Radius: " << slc.Light.Radius << "\n";
                Indent(ss, 3);
                ss << "CutOff: " << slc.Light.CutOff << "\n";
                Indent(ss, 3);
                ss << "OuterCutOff: " << slc.Light.OuterCutOff << "\n";
                Indent(ss, 3);
                ss << "Mobility: " << LightMobilityToString(slc.Mobility) << "\n";
                if (!slc.bCastShadows) {
                    Indent(ss, 3);
                    ss << "CastShadows: false\n";
                }
            }

            // Text Component
            if (entity.HasComponent<FTextComponent>()) {
                const auto& txt = entity.GetComponent<FTextComponent>();
                Indent(ss, 2);
                ss << "Text:\n";
                Indent(ss, 3);
                ss << "Content: " << QuoteMapString(txt.Text) << "\n";
                Indent(ss, 3);
                ss << "Color: [" << txt.Color.r << ", " << txt.Color.g << ", " << txt.Color.b << ", " << txt.Color.a
                   << "]\n";
                Indent(ss, 3);
                ss << "Scale: " << txt.Size << "\n";
                Indent(ss, 3);
                ss << "LineSpacing: " << txt.LineSpacing << "\n";
                Indent(ss, 3);
                ss << "Alignment: " << static_cast<int>(txt.Alignment) << "\n";
                Indent(ss, 3);
                ss << "DoubleSided: " << (txt.bDoubleSided ? "true" : "false") << "\n";
            }
        }

        OutYamlString = ss.str();
        return true;
    }

} // namespace Leon
