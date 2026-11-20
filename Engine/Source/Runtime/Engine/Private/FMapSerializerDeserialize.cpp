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
#include <cmath>
#include <algorithm>
#include "RHI/IRenderDriver.hpp"

#include <cstdlib>
#include <exception>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace Leon {

    bool FMapSerializer::Deserialize(const std::string& InFilePath) {
        std::ifstream fin(InFilePath);
        if (!fin.is_open()) {
            LE_CORE_ERROR("FMapSerializer: Failed to open map file '{0}'", InFilePath);
            return false;
        }

        std::stringstream ss;
        ss << fin.rdbuf();
        fin.close();

        bool result = DeserializeText(ss.str());
        if (result) {
            LE_CORE_INFO("FMapSerializer: Successfully loaded Map from '{0}'", InFilePath);
        }
        return result;
    }

    struct FActorDeserializationData {
        std::string Name = "Actor";
        std::string ClassName = "AActor";
        FUUID Guid;
        std::string FolderPath;
        std::string PlayerStartTag;
        int32_t TeamIndex = 0;
        glm::vec3 Translation{0.0f};
        glm::vec3 Rotation{0.0f};
        glm::vec3 Scale{1.0f};

        bool bHasCamera = false;
        bool bCameraPrimary = true;
        float CameraFOV = 45.0f;
        float CameraNear = 0.1f;
        float CameraFar = 1000.0f;

        bool bHasStaticMeshAsset = false;
        std::string StaticMeshAssetPath;
        std::vector<std::pair<size_t, std::string>> MaterialOverrides;
        size_t CurrentOverrideSlot = 0;

        bool bHasMesh = false;
        std::string MeshType = "Cube";
        float MeshSize = 1.0f;
        float MeshWidth = 1.0f;
        float MeshHeight = 1.0f;
        float MeshDepth = 1.0f;
        float MeshRadius = 0.5f;
        float MeshMetersPerUv = 1.0f;
        unsigned int MeshSubdivX = 24;
        unsigned int MeshSubdivZ = 24;
        bool bCastShadows = true;
        bool bReceiveShadows = true;
        bool bVisibleInReflection = true;
        bool bCollision = true;
        EComponentMobility Mobility = EComponentMobility::Static;
        uint32_t LightmapResolution = 64;
        int32_t LightmapIndex = -1;
        glm::vec2 LightmapScale{1.0f, 1.0f};
        glm::vec2 LightmapBias{0.0f, 0.0f};
        std::string LightmapAssetPath;

        bool bHasMaterial = false;
        std::string MaterialAssetPath;
        TRef<FMaterialInstance> MaterialInstance;

        bool bHasDirLight = false;
        bool bDirLightEnabled = true;
        ELightMobility DirLightMobility = ELightMobility::Movable;
        FDirectionalLight DirLight;

        bool bHasPointLight = false;
        bool bPointLightEnabled = true;
        ELightMobility PointLightMobility = ELightMobility::Movable;
        bool bPointLightCastShadows = false;
        FPointLight PointLight;

        bool bHasSpotLight = false;
        bool bSpotLightEnabled = true;
        ELightMobility SpotLightMobility = ELightMobility::Movable;
        bool bSpotLightCastShadows = true;
        FSpotLight SpotLight;

        bool bHasText = false;
        FTextComponent TextComp;

        bool bHasReflectionProbe = false;
        FReflectionProbeComponent ReflectionProbe;

        bool bHasBoxCollision = false;
        FBoxCollisionComponent BoxCollision;
        bool bTriggerEnabled = true;
        bool bPlayerStartEnabled = true;

        std::vector<std::pair<std::string, std::string>> Properties;
    };

    bool FMapSerializer::DeserializeText(const std::string& InYamlString) {
        if (!World) {
            LE_CORE_ERROR("FMapSerializer: Attempted to deserialize into a null UWorld!");
            return false;
        }

        // Numeric conversions in the parser throw on malformed text. Parsing completes before the world is
        // touched, so a bad file is reported with its line number and leaves the current world intact.
        FLoadProgress progress;
        try {
            return ParseAndPopulate(InYamlString, progress);
        } catch (const std::exception& e) {
            if (progress.bWorldModified) {
                LE_CORE_ERROR("FMapSerializer: Failed while instantiating actors ({0}). The world is incomplete.",
                              e.what());
            } else {
                LE_CORE_ERROR("FMapSerializer: Malformed value at line {0} ({1}). Map not loaded; world untouched.",
                              progress.Line, e.what());
            }
            return false;
        }
    }

    bool FMapSerializer::ParseAndPopulate(const std::string& InYamlString, FLoadProgress& OutProgress) {
        std::stringstream ss(InYamlString);
        std::string line;
        std::string mapName;
        bool bHasMapName = false;
        std::string mapVersion;

        // Format 2.2+ escapes quotes, backslashes and line breaks inside quoted strings. The version line
        // comes after the map name, so it is looked up before parsing starts.
        const bool bEscapedStrings = MapTextUsesEscapes(InYamlString);
        auto unquote = [bEscapedStrings](const std::string& InValue) {
            return StripQuotes(InValue, bEscapedStrings);
        };

        enum class EParserSection {
            None,
            Map,
            OutlinerFolders,
            Environment,
            WorldSettings,
            Skybox,
            PostProcess,
            VolumetricFog,
            Actors,
            InActor
        };
        enum class EActorComponentSection {
            None,
            Transform,
            Camera,
            StaticMesh,
            MaterialOverrides,
            MaterialOverrideItem,
            Mesh,
            Material,
            DirectionalLight,
            PointLight,
            SpotLight,
            Text,
            ReflectionProbe,
            BoxCollision,
            Properties
        };

        EParserSection currentSection = EParserSection::None;
        EActorComponentSection currentCompSection = EActorComponentSection::None;

        FSkyboxComponent skybox;
        bool bHasSkybox = false;
        FWorldSettingsComponent worldSettings;
        bool bHasWorldSettings = false;
        FPostProcessComponent postProcess;
        bool bHasPostProcess = false;
        FVolumetricFogComponent volumetricFog;
        bool bHasVolumetricFog = false;
        bool bLegacyBakeOnSkybox = false;

        std::vector<FActorDeserializationData> actors;
        FActorDeserializationData currentActor;
        bool bParsingActor = false;
        std::vector<std::string> outlinerFolders;

        while (std::getline(ss, line)) {
            ++OutProgress.Line;
            std::string trimmed = FStringUtils::Trim(line);
            if (trimmed.empty() || trimmed[0] == '#') {
                continue;
            }

            // Top-Level Section headers
            if (currentSection == EParserSection::None && (trimmed == "Map:" || trimmed.rfind("Map:", 0) == 0)) {
                currentSection = EParserSection::Map;
                size_t colon = trimmed.find(':');
                std::string val = FStringUtils::Trim(trimmed.substr(colon + 1));
                if (!val.empty()) {
                    mapName = unquote(val);
                    bHasMapName = true;
                }
                continue;
            }

            if (trimmed == "Environment:") {
                currentSection = EParserSection::Environment;
                continue;
            }

            if (trimmed == "Actors:" || trimmed == "Entities:") {
                if (bParsingActor) {
                    actors.push_back(currentActor);
                    bParsingActor = false;
                }
                currentSection = EParserSection::Actors;
                continue;
            }

            // Parse Map Info block
            if (currentSection == EParserSection::Map || currentSection == EParserSection::OutlinerFolders) {
                if (trimmed == "OutlinerFolders:") {
                    currentSection = EParserSection::OutlinerFolders;
                    continue;
                }
                if (currentSection == EParserSection::OutlinerFolders) {
                    if (trimmed.rfind("- ", 0) == 0) {
                        outlinerFolders.push_back(unquote(FStringUtils::Trim(trimmed.substr(2))));
                        continue;
                    }
                    // Fall through to other Map keys if list ended
                    currentSection = EParserSection::Map;
                }
                size_t colon = trimmed.find(':');
                if (colon != std::string::npos) {
                    std::string key = FStringUtils::Trim(trimmed.substr(0, colon));
                    std::string val = unquote(trimmed.substr(colon + 1));
                    if (key == "Name") {
                        mapName = val;
                        bHasMapName = true;
                    } else if (key == "Version") {
                        mapVersion = val;
                    } else if (key == "OutlinerFolders" && !val.empty()) {
                        outlinerFolders.push_back(val);
                    }
                }
                continue;
            }

            // Parse Environment block
            if (currentSection == EParserSection::Environment || currentSection == EParserSection::Skybox ||
                currentSection == EParserSection::WorldSettings || currentSection == EParserSection::PostProcess ||
                currentSection == EParserSection::VolumetricFog) {
                if (trimmed == "PostProcess:") {
                    currentSection = EParserSection::PostProcess;
                    bHasPostProcess = true;
                    continue;
                }
                if (trimmed == "VolumetricFog:") {
                    currentSection = EParserSection::VolumetricFog;
                    bHasVolumetricFog = true;
                    continue;
                }
                if (currentSection == EParserSection::VolumetricFog && trimmed != "WorldSettings:" &&
                    trimmed != "Skybox:") {
                    const size_t colon = trimmed.find(':');
                    if (colon != std::string::npos) {
                        const std::string key = FStringUtils::Trim(trimmed.substr(0, colon));
                        const std::string val = unquote(trimmed.substr(colon + 1));
                        FVolumetricFogSettings& fog = volumetricFog.Settings;
                        if (key == "Enabled")
                            fog.bEnabled = (val == "true");
                        else if (key == "Density")
                            fog.Density = std::stof(val);
                        else if (key == "Scattering")
                            fog.Scattering = std::stof(val);
                        else if (key == "Anisotropy")
                            fog.Anisotropy = std::stof(val);
                        else if (key == "AmbientColor") {
                            const auto v = ParseFloatArray(val);
                            if (v.size() >= 3)
                                fog.AmbientColor = {v[0], v[1], v[2]};
                        } else if (key == "AmbientIntensity")
                            fog.AmbientIntensity = std::stof(val);
                        else if (key == "MaxDistance")
                            fog.MaxDistance = std::stof(val);
                        else if (key == "Steps")
                            fog.StepCount = static_cast<uint32_t>(std::stoul(val));
                    }
                    continue;
                }
                if (currentSection == EParserSection::PostProcess && trimmed != "WorldSettings:" && trimmed != "Skybox:") {
                    const size_t colon = trimmed.find(':');
                    if (colon != std::string::npos) {
                        const std::string key = FStringUtils::Trim(trimmed.substr(0, colon));
                        const std::string val = unquote(trimmed.substr(colon + 1));
                        FPostProcessLook& look = postProcess.Look;
                        const auto readColor = [&val](glm::vec3& OutColor) {
                            const auto v = ParseFloatArray(val);
                            if (v.size() >= 3)
                                OutColor = {v[0], v[1], v[2]};
                        };
                        if (key == "ToneMapper")
                            postProcess.ToneMapper = ParseToneMapper(val);
                        else if (key == "Exposure") {
                            postProcess.Exposure = std::stof(val);
                            postProcess.bOverrideExposure = true;
                        } else if (key == "BloomIntensity")
                            postProcess.BloomIntensity = std::stof(val);
                        else if (key == "BloomThreshold")
                            postProcess.BloomThreshold = std::stof(val);
                        else if (key == "VignetteIntensity")
                            look.VignetteIntensity = std::stof(val);
                        else if (key == "VignetteRadius")
                            look.VignetteRadius = std::stof(val);
                        else if (key == "VignetteSoftness")
                            look.VignetteSoftness = std::stof(val);
                        else if (key == "FilmGrainIntensity")
                            look.FilmGrainIntensity = std::stof(val);
                        else if (key == "FilmGrainAnimated")
                            look.bFilmGrainAnimated = (val == "true");
                        else if (key == "FilmGrainSeed")
                            look.FilmGrainSeed = static_cast<uint32_t>(std::stoul(val));
                        else if (key == "Lift")
                            readColor(look.Lift);
                        else if (key == "Gamma")
                            readColor(look.Gamma);
                        else if (key == "Gain")
                            readColor(look.Gain);
                        else if (key == "Contrast")
                            look.Contrast = std::stof(val);
                        else if (key == "Saturation")
                            look.Saturation = std::stof(val);
                        else if (key == "ChromaticAberration")
                            look.ChromaticAberration = std::stof(val);
                    }
                    continue;
                }
                if (trimmed == "WorldSettings:") {
                    currentSection = EParserSection::WorldSettings;
                    bHasWorldSettings = true;
                    continue;
                }
                if (trimmed == "Skybox:") {
                    currentSection = EParserSection::Skybox;
                    bHasSkybox = true;
                    continue;
                }

                auto applyBakeKey = [&](FWorldSettingsComponent& ws, const std::string& key, const std::string& val) {
                    if (key == "GameModeClass")
                        ws.GameModeClass = val;
                    else if (key == "StaticLighting")
                        ws.bStaticLighting = (val == "true");
                    else if (key == "LightingBuildQuality")
                        ws.LightingBuildQuality = StringToLightingQuality(val);
                    else if (key == "LightmapResolution")
                        ws.LightmapResolution = static_cast<uint32_t>(std::stoul(val));
                    else if (key == "NumIndirectBounces")
                        ws.NumIndirectBounces = static_cast<uint32_t>(std::stoul(val));
                    else if (key == "SamplesPerTexel")
                        ws.SamplesPerTexel = static_cast<uint32_t>(std::stoul(val));
                    else if (key == "IndirectIntensity")
                        ws.IndirectIntensity = std::stof(val);
                    else if (key == "AmbientOcclusion")
                        ws.bAmbientOcclusion = (val == "true");
                    else if (key == "AOIntensity")
                        ws.AOIntensity = std::stof(val);
                    else if (key == "AORadius")
                        ws.AORadius = std::stof(val);
                    else if (key == "TexelPadding")
                        ws.TexelPadding = std::stof(val);
                    else if (key == "WorldScale")
                        ws.WorldScale = std::stof(val);
                    else if (key == "LightmapAsset")
                        ws.LightmapAssetPath = val;
                    else if (key == "LightmapBakeHash")
                        ws.LightmapBakeHash = std::strtoull(val.c_str(), nullptr, 16);
                    else if (key == "LightProbes")
                        ws.bLightProbes = (val == "true");
                    else if (key == "LightProbeSpacing")
                        ws.LightProbeSpacing = std::stof(val);
                    else if (key == "LightProbeVerticalSpacing")
                        ws.LightProbeVerticalSpacing = std::stof(val);
                    else if (key == "LightProbeAsset")
                        ws.LightProbeAssetPath = val;
                    else
                        return false;
                    return true;
                };

                if (currentSection == EParserSection::WorldSettings) {
                    size_t colon = trimmed.find(':');
                    if (colon != std::string::npos) {
                        std::string key = FStringUtils::Trim(trimmed.substr(0, colon));
                        std::string val = unquote(trimmed.substr(colon + 1));
                        applyBakeKey(worldSettings, key, val);
                    }
                    continue;
                }

                if (currentSection == EParserSection::Skybox) {
                    size_t colon = trimmed.find(':');
                    if (colon != std::string::npos) {
                        std::string key = FStringUtils::Trim(trimmed.substr(0, colon));
                        std::string val = unquote(trimmed.substr(colon + 1));

                        if (key == "Enabled")
                            skybox.bEnabled = (val == "true");
                        else if (key == "Exposure")
                            skybox.Exposure = std::stof(val);
                        else if (key == "SunIntensity")
                            skybox.SunIntensity = std::stof(val);
                        else if (key == "EnvironmentIntensity")
                            skybox.EnvironmentIntensity = std::stof(val);
                        else if (key == "UseHDREnvironmentMap")
                            skybox.bUseHDREnvironmentMap = (val == "true");
                        else if (key == "HDREnvironmentMap" || key == "HDREnvironmentMapPath" ||
                                 key == "EnvironmentMap") {
                            skybox.HDREnvironmentMapPath = val;
                            if (!val.empty()) {
                                skybox.bUseHDREnvironmentMap = true;
                                skybox.HDREnvironmentMap = UAssetManager::GetTexture2D(val);
                            }
                        } else if (key == "ZenithColor" || key == "SkyZenithColor") {
                            auto v = ParseFloatArray(val);
                            if (v.size() >= 3)
                                skybox.SkyZenithColor = {v[0], v[1], v[2]};
                        } else if (key == "HorizonColor") {
                            auto v = ParseFloatArray(val);
                            if (v.size() >= 3)
                                skybox.HorizonColor = {v[0], v[1], v[2]};
                        } else if (key == "GroundColor") {
                            auto v = ParseFloatArray(val);
                            if (v.size() >= 3)
                                skybox.GroundColor = {v[0], v[1], v[2]};
                        } else if (key == "SunColor") {
                            auto v = ParseFloatArray(val);
                            if (v.size() >= 3)
                                skybox.SunColor = {v[0], v[1], v[2]};
                        } else if (key == "FogEnabled") {
                            skybox.bFogEnabled = (val == "true");
                        } else if (key == "FogColor") {
                            auto v = ParseFloatArray(val);
                            if (v.size() >= 3)
                                skybox.FogColor = {v[0], v[1], v[2]};
                        } else if (key == "FogDensity") {
                            skybox.FogDensity = std::stof(val);
                        } else if (key == "FogStartDistance") {
                            skybox.FogStartDistance = std::stof(val);
                        } else if (key == "FogHeightFalloff") {
                            skybox.FogHeightFalloff = std::stof(val);
                        } else if (key == "FogBaseHeight") {
                            skybox.FogBaseHeight = std::stof(val);
                        } else if (key == "FogMaxOpacity") {
                            skybox.FogMaxOpacity = std::stof(val);
                        } else if (applyBakeKey(worldSettings, key, val)) {
                            bLegacyBakeOnSkybox = true;
                        }
                    }
                }
                continue;
            }

            // Parse Actors block
            if (currentSection == EParserSection::Actors || currentSection == EParserSection::InActor) {
                // New Actor entry
                if (trimmed.rfind("- Name:", 0) == 0 || trimmed.rfind("- Actor:", 0) == 0 ||
                    trimmed.rfind("- Entity:", 0) == 0 || trimmed == "-") {
                    if (bParsingActor) {
                        actors.push_back(currentActor);
                    }
                    currentActor = FActorDeserializationData{};
                    bParsingActor = true;
                    currentSection = EParserSection::InActor;
                    currentCompSection = EActorComponentSection::None;

                    size_t colon = trimmed.find(':');
                    if (colon != std::string::npos) {
                        currentActor.Name = unquote(trimmed.substr(colon + 1));
                    }
                    continue;
                }

                // Component section headers
                if (trimmed == "Transform:") {
                    currentCompSection = EActorComponentSection::Transform;
                    continue;
                } else if (trimmed == "Camera:" || trimmed == "CameraComponent:") {
                    currentCompSection = EActorComponentSection::Camera;
                    currentActor.bHasCamera = true;
                    continue;
                } else if (trimmed == "StaticMesh:" || trimmed == "StaticMeshComponent:") {
                    currentCompSection = EActorComponentSection::StaticMesh;
                    continue;
                } else if (trimmed == "MaterialOverrides:") {
                    currentCompSection = EActorComponentSection::MaterialOverrides;
                    continue;
                } else if (trimmed == "Mesh:" || trimmed == "MeshComponent:") {
                    currentCompSection = EActorComponentSection::Mesh;
                    currentActor.bHasMesh = true;
                    continue;
                } else if (trimmed == "Material:" || trimmed == "MaterialComponent:") {
                    currentCompSection = EActorComponentSection::Material;
                    currentActor.bHasMaterial = true;
                    continue;
                } else if (trimmed == "DirectionalLight:" || trimmed == "DirectionalLightComponent:") {
                    currentCompSection = EActorComponentSection::DirectionalLight;
                    currentActor.bHasDirLight = true;
                    continue;
                } else if (trimmed == "PointLight:" || trimmed == "PointLightComponent:") {
                    currentCompSection = EActorComponentSection::PointLight;
                    currentActor.bHasPointLight = true;
                    continue;
                } else if (trimmed == "SpotLight:" || trimmed == "SpotLightComponent:") {
                    currentCompSection = EActorComponentSection::SpotLight;
                    currentActor.bHasSpotLight = true;
                    continue;
                } else if (trimmed == "Text:" || trimmed == "TextComponent:") {
                    currentCompSection = EActorComponentSection::Text;
                    currentActor.bHasText = true;
                    continue;
                } else if (trimmed == "ReflectionProbe:") {
                    currentCompSection = EActorComponentSection::ReflectionProbe;
                    currentActor.bHasReflectionProbe = true;
                    continue;
                } else if (trimmed == "BoxCollision:" || trimmed == "BoxCollisionComponent:") {
                    currentCompSection = EActorComponentSection::BoxCollision;
                    currentActor.bHasBoxCollision = true;
                    continue;
                } else if (trimmed == "Properties:") {
                    currentCompSection = EActorComponentSection::Properties;
                    continue;
                }

                // Material Overrides List items (- Slot: 0)
                if (currentCompSection == EActorComponentSection::MaterialOverrides ||
                    currentCompSection == EActorComponentSection::MaterialOverrideItem) {
                    if (trimmed.rfind("- Slot:", 0) == 0) {
                        currentCompSection = EActorComponentSection::MaterialOverrideItem;
                        size_t colon = trimmed.find(':');
                        currentActor.CurrentOverrideSlot = std::stoul(FStringUtils::Trim(trimmed.substr(colon + 1)));
                        continue;
                    }
                }

                // Component Key-Value parsing
                size_t colon = trimmed.find(':');
                if (colon != std::string::npos) {
                    std::string key = FStringUtils::Trim(trimmed.substr(0, colon));
                    std::string val = unquote(trimmed.substr(colon + 1));

                    switch (currentCompSection) {
                    case EActorComponentSection::None:
                        if (key == "Class")
                            currentActor.ClassName = val;
                        else if (key == "GUID" || key == "Guid")
                            currentActor.Guid = FUUID::FromString(val);
                        else if (key == "FolderPath" || key == "Folder")
                            currentActor.FolderPath = val;
                        else if (key == "PlayerStartTag")
                            currentActor.PlayerStartTag = val;
                        else if (key == "TeamIndex")
                            currentActor.TeamIndex = std::stoi(val);
                        else if (key == "PlayerStartEnabled")
                            currentActor.bPlayerStartEnabled = (val == "true");
                        else if (key == "TriggerEnabled")
                            currentActor.bTriggerEnabled = (val == "true");
                        break;

                    case EActorComponentSection::Properties:
                        currentActor.Properties.emplace_back(key, val);
                        break;

                    case EActorComponentSection::BoxCollision:
                        if (key == "Min" || key == "LocalMin") {
                            auto v = ParseFloatArray(val);
                            if (v.size() >= 3)
                                currentActor.BoxCollision.LocalMin = {v[0], v[1], v[2]};
                        } else if (key == "Max" || key == "LocalMax") {
                            auto v = ParseFloatArray(val);
                            if (v.size() >= 3)
                                currentActor.BoxCollision.LocalMax = {v[0], v[1], v[2]};
                        } else if (key == "BlockMovement")
                            currentActor.BoxCollision.bBlockMovement = (val == "true");
                        else if (key == "Channel")
                            currentActor.BoxCollision.Channel = static_cast<ECollisionChannel>(std::stoi(val));
                        break;

                    case EActorComponentSection::Transform:
                        if (key == "Translation" || key == "Position") {
                            auto v = ParseFloatArray(val);
                            if (v.size() >= 3)
                                currentActor.Translation = {v[0], v[1], v[2]};
                        } else if (key == "Rotation") {
                            auto v = ParseFloatArray(val);
                            if (v.size() >= 3)
                                currentActor.Rotation = {v[0], v[1], v[2]};
                        } else if (key == "Scale") {
                            auto v = ParseFloatArray(val);
                            if (v.size() >= 3)
                                currentActor.Scale = {v[0], v[1], v[2]};
                        }
                        break;

                    case EActorComponentSection::Camera:
                        if (key == "Primary")
                            currentActor.bCameraPrimary = (val == "true");
                        else if (key == "FOV")
                            currentActor.CameraFOV = std::stof(val);
                        else if (key == "Near" || key == "NearClip")
                            currentActor.CameraNear = std::stof(val);
                        else if (key == "Far" || key == "FarClip")
                            currentActor.CameraFar = std::stof(val);
                        break;

                    case EActorComponentSection::StaticMesh:
                        if (key == "Asset" || key == "AssetPath") {
                            currentActor.bHasStaticMeshAsset = true;
                            currentActor.StaticMeshAssetPath = val;
                        } else if (key == "Type") {
                            currentActor.bHasMesh = true;
                            currentActor.MeshType = val;
                        } else if (key == "Size") {
                            currentActor.bHasMesh = true;
                            currentActor.MeshSize = std::stof(val);
                        } else if (key == "Width") {
                            currentActor.bHasMesh = true;
                            currentActor.MeshWidth = std::stof(val);
                        } else if (key == "Height") {
                            currentActor.bHasMesh = true;
                            currentActor.MeshHeight = std::stof(val);
                        } else if (key == "Depth") {
                            currentActor.bHasMesh = true;
                            currentActor.MeshDepth = std::stof(val);
                        } else if (key == "Radius") {
                            currentActor.bHasMesh = true;
                            currentActor.MeshRadius = std::stof(val);
                        } else if (key == "MetersPerUv") {
                            currentActor.bHasMesh = true;
                            currentActor.MeshMetersPerUv = std::stof(val);
                        } else if (key == "SubdivisionsX" || key == "SubdivX") {
                            currentActor.bHasMesh = true;
                            currentActor.MeshSubdivX = std::stoul(val);
                        } else if (key == "SubdivisionsZ" || key == "SubdivZ") {
                            currentActor.bHasMesh = true;
                            currentActor.MeshSubdivZ = std::stoul(val);
                        } else if (key == "Shader" || key == "ShaderPath") {
                            // Maps written for the OpenGL renderer name a shader file; nothing reads it now.
                            currentActor.bHasMesh = true;
                        } else if (key == "CastShadows")
                            currentActor.bCastShadows = (val == "true");
                        else if (key == "ReceiveShadows")
                            currentActor.bReceiveShadows = (val == "true");
                        else if (key == "VisibleInReflection")
                            currentActor.bVisibleInReflection = (val == "true");
                        else if (key == "Collision")
                            currentActor.bCollision = (val == "true");
                        else if (key == "Mobility")
                            currentActor.Mobility = StringToComponentMobility(val);
                        else if (key == "LightmapResolution")
                            currentActor.LightmapResolution = static_cast<uint32_t>(std::stoul(val));
                        else if (key == "LightmapIndex")
                            currentActor.LightmapIndex = std::stoi(val);
                        else if (key == "LightmapScale") {
                            auto v = ParseFloatArray(val);
                            if (v.size() >= 2)
                                currentActor.LightmapScale = {v[0], v[1]};
                        } else if (key == "LightmapBias") {
                            auto v = ParseFloatArray(val);
                            if (v.size() >= 2)
                                currentActor.LightmapBias = {v[0], v[1]};
                        } else if (key == "LightmapAsset")
                            currentActor.LightmapAssetPath = val;
                        break;

                    case EActorComponentSection::MaterialOverrides:
                    case EActorComponentSection::MaterialOverrideItem:
                        if (key == "Asset" || key == "AssetPath") {
                            currentActor.MaterialOverrides.push_back({currentActor.CurrentOverrideSlot, val});
                        } else if (key.rfind("Slot_", 0) == 0) {
                            size_t slotIdx = std::stoul(key.substr(5));
                            currentActor.MaterialOverrides.push_back({slotIdx, val});
                        }
                        break;

                    case EActorComponentSection::Mesh:
                        if (key == "Type")
                            currentActor.MeshType = val;
                        else if (key == "Size")
                            currentActor.MeshSize = std::stof(val);
                        else if (key == "Width")
                            currentActor.MeshWidth = std::stof(val);
                        else if (key == "Height")
                            currentActor.MeshHeight = std::stof(val);
                        else if (key == "Depth")
                            currentActor.MeshDepth = std::stof(val);
                        else if (key == "Radius")
                            currentActor.MeshRadius = std::stof(val);
                        else if (key == "MetersPerUv")
                            currentActor.MeshMetersPerUv = std::stof(val);
                        else if (key == "SubdivisionsX" || key == "SubdivX")
                            currentActor.MeshSubdivX = std::stoul(val);
                        else if (key == "SubdivisionsZ" || key == "SubdivZ")
                            currentActor.MeshSubdivZ = std::stoul(val);
                        else if (key == "Shader" || key == "ShaderPath") {
                            // Written by older versions; ignored.
                        }
                        else if (key == "CastShadows")
                            currentActor.bCastShadows = (val == "true");
                        else if (key == "ReceiveShadows")
                            currentActor.bReceiveShadows = (val == "true");
                        else if (key == "VisibleInReflection")
                            currentActor.bVisibleInReflection = (val == "true");
                        else if (key == "Collision")
                            currentActor.bCollision = (val == "true");
                        else if (key == "Mobility")
                            currentActor.Mobility = StringToComponentMobility(val);
                        else if (key == "LightmapResolution")
                            currentActor.LightmapResolution = static_cast<uint32_t>(std::stoul(val));
                        else if (key == "LightmapIndex")
                            currentActor.LightmapIndex = std::stoi(val);
                        else if (key == "LightmapScale") {
                            auto v = ParseFloatArray(val);
                            if (v.size() >= 2)
                                currentActor.LightmapScale = {v[0], v[1]};
                        } else if (key == "LightmapBias") {
                            auto v = ParseFloatArray(val);
                            if (v.size() >= 2)
                                currentActor.LightmapBias = {v[0], v[1]};
                        } else if (key == "LightmapAsset")
                            currentActor.LightmapAssetPath = val;
                        break;

                    case EActorComponentSection::Material:
                        if (key == "Asset" || key == "AssetPath") {
                            currentActor.MaterialAssetPath = val;
                            if (!val.empty()) {
                                currentActor.MaterialInstance = UAssetManager::GetMaterialInstance(val);
                                if (!currentActor.MaterialInstance) {
                                    auto baseMat = UAssetManager::GetMaterial(val);
                                    if (baseMat) {
                                        currentActor.MaterialInstance = baseMat->CreateInstance();
                                    }
                                }
                            }
                        }
                        break;

                    case EActorComponentSection::DirectionalLight:
                        if (key == "Enabled")
                            currentActor.bDirLightEnabled = (val == "true");
                        else if (key == "Direction") {
                            auto v = ParseFloatArray(val);
                            if (v.size() >= 3)
                                currentActor.DirLight.Direction = {v[0], v[1], v[2]};
                        } else if (key == "Color" || key == "Radiance") {
                            auto v = ParseFloatArray(val);
                            if (v.size() >= 3)
                                currentActor.DirLight.Color = {v[0], v[1], v[2]};
                        } else if (key == "Intensity")
                            currentActor.DirLight.Intensity = std::stof(val);
                        else if (key == "Mobility")
                            currentActor.DirLightMobility = StringToLightMobility(val);
                        break;

                    case EActorComponentSection::PointLight:
                        if (key == "Enabled")
                            currentActor.bPointLightEnabled = (val == "true");
                        else if (key == "Color" || key == "Radiance") {
                            auto v = ParseFloatArray(val);
                            if (v.size() >= 3)
                                currentActor.PointLight.Color = {v[0], v[1], v[2]};
                        } else if (key == "Intensity")
                            currentActor.PointLight.Intensity = std::stof(val);
                        else if (key == "Radius")
                            currentActor.PointLight.Radius = std::stof(val);
                        else if (key == "Mobility")
                            currentActor.PointLightMobility = StringToLightMobility(val);
                        else if (key == "CastShadows")
                            currentActor.bPointLightCastShadows = (val == "true");
                        break;

                    case EActorComponentSection::ReflectionProbe:
                        if (key == "Extent" || key == "CaptureOffset") {
                            const auto v = ParseFloatArray(val);
                            if (v.size() >= 3) {
                                (key == "Extent" ? currentActor.ReflectionProbe.Extent
                                                 : currentActor.ReflectionProbe.CaptureOffset) = {v[0], v[1], v[2]};
                            }
                        } else if (key == "Intensity")
                            currentActor.ReflectionProbe.Intensity = std::stof(val);
                        else if (key == "BlendDistance")
                            currentActor.ReflectionProbe.BlendDistance = std::stof(val);
                        break;

                    case EActorComponentSection::SpotLight:
                        if (key == "Enabled")
                            currentActor.bSpotLightEnabled = (val == "true");
                        else if (key == "Direction") {
                            auto v = ParseFloatArray(val);
                            if (v.size() >= 3)
                                currentActor.SpotLight.Direction = {v[0], v[1], v[2]};
                        } else if (key == "Color" || key == "Radiance") {
                            auto v = ParseFloatArray(val);
                            if (v.size() >= 3)
                                currentActor.SpotLight.Color = {v[0], v[1], v[2]};
                        } else if (key == "Intensity")
                            currentActor.SpotLight.Intensity = std::stof(val);
                        else if (key == "Radius")
                            currentActor.SpotLight.Radius = std::stof(val);
                        else if (key == "CutOff" || key == "InnerConeAngle")
                            currentActor.SpotLight.CutOff = std::stof(val);
                        else if (key == "OuterCutOff" || key == "OuterConeAngle")
                            currentActor.SpotLight.OuterCutOff = std::stof(val);
                        else if (key == "Mobility")
                            currentActor.SpotLightMobility = StringToLightMobility(val);
                        else if (key == "CastShadows")
                            currentActor.bSpotLightCastShadows = (val == "true");
                        break;

                    case EActorComponentSection::Text:
                        if (key == "Content" || key == "Text") {
                            currentActor.TextComp.Text = val;
                        } else if (key == "Color") {
                            auto v = ParseFloatArray(val);
                            if (v.size() >= 4)
                                currentActor.TextComp.Color = {v[0], v[1], v[2], v[3]};
                        } else if (key == "Scale" || key == "Size")
                            currentActor.TextComp.Size = std::stof(val);
                        else if (key == "LineSpacing")
                            currentActor.TextComp.LineSpacing = std::stof(val);
                        else if (key == "Alignment") {
                            if (val == "Left" || val == "0")
                                currentActor.TextComp.Alignment = ETextAlignment::Left;
                            else if (val == "Center" || val == "1")
                                currentActor.TextComp.Alignment = ETextAlignment::Center;
                            else if (val == "Right" || val == "2")
                                currentActor.TextComp.Alignment = ETextAlignment::Right;
                        } else if (key == "DoubleSided")
                            currentActor.TextComp.bDoubleSided = (val == "true");
                        break;

                    default:
                        break;
                    }
                }
            }
        }

        if (bParsingActor) {
            actors.push_back(currentActor);
        }

        // Files without a Version predate the field and are accepted. A newer major means a format this
        // build does not understand: refuse instead of loading it partially and saving it back truncated.
        if (!mapVersion.empty()) {
            const int major = static_cast<int>(std::strtol(mapVersion.c_str(), nullptr, 10));
            if (major > SupportedFormatMajor) {
                LE_CORE_ERROR("FMapSerializer: Map format version {0} is newer than supported major {1}. "
                              "Map not loaded; world untouched.",
                              mapVersion, SupportedFormatMajor);
                return false;
            }
        }

        // 3. Populate World (nothing above this line modified it)
        OutProgress.bWorldModified = true;
        World->Clear();
        if (bHasMapName)
            World->SetName(mapName);
        World->SetEditorFolders(outlinerFolders);

        // Environment / Skybox / WorldSettings
        if (bLegacyBakeOnSkybox && !bHasWorldSettings)
            bHasWorldSettings = true;

        if (bHasSkybox || bHasWorldSettings || bHasPostProcess || bHasVolumetricFog) {
            AActor* envEntity = World->SpawnActor("Environment Skybox");
            if (bHasSkybox)
                envEntity->AddComponent<FSkyboxComponent>(skybox);
            if (bHasWorldSettings)
                envEntity->AddComponent<FWorldSettingsComponent>(worldSettings);
            if (bHasPostProcess)
                envEntity->AddComponent<FPostProcessComponent>(postProcess);
            if (bHasVolumetricFog)
                envEntity->AddComponent<FVolumetricFogComponent>(volumetricFog);
        }

        // Spawn actors
        std::set<std::pair<uint64_t, uint64_t>> usedGuids;
        for (const auto& actorData : actors) {
            AActor* entity = nullptr;
            std::string className = actorData.ClassName.empty() ? "AActor" : actorData.ClassName;
            if (UClassRegistry::Get().HasClass(className)) {
                entity = UClassRegistry::Get().CreateActorOfClass(className, World.get(), actorData.Name);
            }
            if (!entity)
                entity = World->SpawnActor(actorData.Name);
            if (!entity)
                continue;

            entity->SetClass(className);
            // GUIDs identify actors for undo, save games and cross references, so they must be unique.
            // Copy-pasted blocks and same-named actors without a GUID would otherwise collide.
            FUUID guid = actorData.Guid.IsValid() ? actorData.Guid
                                                  : FUUID::FromPath(World->GetName() + "/" + actorData.Name);
            if (!usedGuids.insert({guid.High, guid.Low}).second) {
                LE_CORE_WARN("FMapSerializer: Actor '{0}' reuses a GUID already present in the map; "
                             "assigned a new one.",
                             actorData.Name);
                guid = FUUID::Generate();
                usedGuids.insert({guid.High, guid.Low});
            }
            entity->SetActorGuid(guid);
            if (!actorData.FolderPath.empty()) {
                entity->SetFolderPath(actorData.FolderPath);
                World->RegisterEditorFolder(actorData.FolderPath);
            }

            if (auto* start = dynamic_cast<APlayerStart*>(entity)) {
                if (!actorData.PlayerStartTag.empty())
                    start->SetPlayerStartTag(actorData.PlayerStartTag);
                if (actorData.TeamIndex != 0)
                    start->SetTeamIndex(actorData.TeamIndex);
                start->SetEnabled(actorData.bPlayerStartEnabled);
            }

            // Box collision: volume classes create the component themselves, so assign rather than add.
            if (actorData.bHasBoxCollision) {
                if (entity->HasComponent<FBoxCollisionComponent>())
                    entity->GetComponent<FBoxCollisionComponent>() = actorData.BoxCollision;
                else
                    entity->AddComponent<FBoxCollisionComponent>(actorData.BoxCollision);
            }
            // After the box: a trigger forces its bounds to be non-blocking.
            if (auto* trigger = dynamic_cast<ATriggerVolume*>(entity))
                trigger->SetEnabled(actorData.bTriggerEnabled);

            // Transform
            auto& transform = entity->GetComponent<FTransformComponent>();
            transform.Translation = actorData.Translation;
            transform.Rotation = actorData.Rotation;
            transform.Scale = actorData.Scale;

            // Camera Component
            if (actorData.bHasCamera) {
                FPerspectiveCamera camera(actorData.CameraFOV, 1280.0f / 720.0f, actorData.CameraNear,
                                          actorData.CameraFar);
                camera.SetPosition(actorData.Translation);
                camera.SetRotation(actorData.Rotation.x, actorData.Rotation.y);
                if (entity->HasComponent<FCameraComponent>()) {
                    auto& cam = entity->GetComponent<FCameraComponent>();
                    cam.Camera = camera;
                    cam.bPrimary = actorData.bCameraPrimary;
                } else {
                    auto& cam = entity->AddComponent<FCameraComponent>(camera);
                    cam.bPrimary = actorData.bCameraPrimary;
                }
            }

            // Static Mesh Component (Asset-based)
            if (actorData.bHasStaticMeshAsset) {
                auto mesh = UAssetManager::GetStaticMesh(actorData.StaticMeshAssetPath);
                if (mesh) {
                    auto& comp = entity->AddComponent<FStaticMeshComponent>(mesh, actorData.StaticMeshAssetPath);
                    comp.bCastShadows = actorData.bCastShadows;
                    comp.bReceiveShadows = actorData.bReceiveShadows;
                    comp.bVisibleInReflection = actorData.bVisibleInReflection;
                    comp.bCollision = actorData.bCollision;
                    comp.Mobility = actorData.Mobility;
                    comp.LightmapResolution = actorData.LightmapResolution;
                    comp.LightmapIndex = actorData.LightmapIndex;
                    comp.LightmapScale = actorData.LightmapScale;
                    comp.LightmapBias = actorData.LightmapBias;
                    comp.LightmapAssetPath = actorData.LightmapAssetPath;

                    // Apply Material Overrides per slot
                    for (const auto& [slotIdx, overridePath] : actorData.MaterialOverrides) {
                        auto matInst = UAssetManager::GetMaterialInstance(overridePath);
                        if (!matInst) {
                            auto baseMat = UAssetManager::GetMaterial(overridePath);
                            if (baseMat) {
                                matInst = baseMat->CreateInstance();
                            }
                        }
                        if (matInst) {
                            if (comp.MaterialOverrides.size() <= slotIdx) {
                                comp.MaterialOverrides.resize(slotIdx + 1, nullptr);
                                comp.MaterialOverridePaths.resize(slotIdx + 1, "");
                            }
                            comp.MaterialOverrides[slotIdx] = matInst;
                            comp.MaterialOverridePaths[slotIdx] = overridePath;
                        }
                    }
                } else {
                    LE_CORE_WARN("FMapSerializer: Failed to load static mesh '{0}' for actor '{1}'",
                                 actorData.StaticMeshAssetPath, actorData.Name);
                }
            }
            // Mesh Component (Procedural)
            else if (actorData.bHasMesh) {
                TRef<FVertexArray> va = nullptr;
                std::string meshType = actorData.MeshType;
                float meshWidth = actorData.MeshWidth;
                float meshHeight = actorData.MeshHeight;
                float meshDepth = actorData.MeshDepth;
                float meshMetersPerUv = actorData.MeshMetersPerUv;
                if (meshType == "Cube") {
                    // Legacy: unit cube * actor scale. It becomes a Box with world-metre UVs and the
                    // scale is cleared. Decided from the data alone, with or without a renderer: the
                    // light baker (no renderer) and the game must load the same world, or the bake
                    // never matches the map it was made for.
                    const float sx = actorData.Scale.x * actorData.MeshSize;
                    const float sy = actorData.Scale.y * actorData.MeshSize;
                    const float sz = actorData.Scale.z * actorData.MeshSize;
                    if (std::abs(sx - actorData.MeshSize) > 0.01f || std::abs(sy - actorData.MeshSize) > 0.01f ||
                        std::abs(sz - actorData.MeshSize) > 0.01f) {
                        meshType = "Box";
                        meshWidth = sx;
                        meshHeight = sy;
                        meshDepth = sz;
                        meshMetersPerUv = meshMetersPerUv > 0.05f ? meshMetersPerUv : 1.0f;
                        transform.Scale = {1.0f, 1.0f, 1.0f};
                    }
                }
                if (FRenderDriverRegistry::GetActiveDriver()) {
                    if (meshType == "Box") {
                        va = FMeshPrimitives::CreateBox(meshWidth, meshHeight, meshDepth, meshMetersPerUv);
                    } else if (meshType == "Cube") {
                        va = FMeshPrimitives::CreateCube(actorData.MeshSize);
                    } else if (meshType == "Plane") {
                        va = FMeshPrimitives::CreatePlane(meshWidth, meshDepth, actorData.MeshSubdivX,
                                                          actorData.MeshSubdivZ);
                    } else if (meshType == "Sphere") {
                        va = FMeshPrimitives::CreateSphere(actorData.MeshRadius, actorData.MeshSubdivX,
                                                           actorData.MeshSubdivZ);
                    } else if (meshType == "Cylinder") {
                        va = FMeshPrimitives::CreateCylinder(actorData.MeshRadius, actorData.MeshRadius, meshHeight,
                                                             actorData.MeshSubdivX, true);
                    } else if (meshType == "Cone") {
                        va = FMeshPrimitives::CreateCylinder(actorData.MeshRadius, 0.0f, meshHeight,
                                                             actorData.MeshSubdivX, true);
                    } else if (meshType == "Ramp") {
                        va = FMeshPrimitives::CreateRamp(meshWidth, meshHeight, meshDepth);
                    } else if (meshType == "Pyramid") {
                        va = FMeshPrimitives::CreatePyramid(meshWidth, meshHeight, meshDepth);
                    } else if (meshType == "Quad") {
                        va = FMeshPrimitives::CreateQuad(meshWidth, meshHeight);
                    }
                }

                auto& comp = entity->AddComponent<FMeshComponent>(va);
                comp.MeshType = meshType;
                comp.MeshSize = actorData.MeshSize;
                comp.MeshWidth = meshWidth;
                comp.MeshHeight = meshHeight;
                comp.MeshDepth = meshDepth;
                comp.MeshRadius = actorData.MeshRadius;
                comp.MeshMetersPerUv = meshMetersPerUv;
                comp.MeshSubdivX = actorData.MeshSubdivX;
                comp.MeshSubdivZ = actorData.MeshSubdivZ;
                comp.bCastShadows = actorData.bCastShadows;
                comp.bReceiveShadows = actorData.bReceiveShadows;
                comp.bVisibleInReflection = actorData.bVisibleInReflection;
                comp.bCollision = actorData.bCollision;
                comp.Mobility = actorData.Mobility;
                comp.LightmapResolution = actorData.LightmapResolution;
                comp.LightmapIndex = actorData.LightmapIndex;
                comp.LightmapScale = actorData.LightmapScale;
                comp.LightmapBias = actorData.LightmapBias;
                comp.LightmapAssetPath = actorData.LightmapAssetPath;
            }

            // Material Component
            if (actorData.bHasMaterial && actorData.MaterialInstance) {
                entity->AddComponent<FMaterialComponent>(actorData.MaterialInstance, actorData.MaterialAssetPath);
            }

            // Directional Light
            if (actorData.bHasDirLight) {
                auto& comp = entity->AddComponent<FDirectionalLightComponent>(actorData.DirLight);
                comp.bEnabled = actorData.bDirLightEnabled;
                comp.Mobility = actorData.DirLightMobility;
            }

            // Point Light
            if (actorData.bHasPointLight) {
                auto& comp = entity->AddComponent<FPointLightComponent>(actorData.PointLight);
                comp.bEnabled = actorData.bPointLightEnabled;
                comp.Mobility = actorData.PointLightMobility;
                comp.bCastShadows = actorData.bPointLightCastShadows;
                comp.Light.Position = actorData.Translation;
            }

            // Spot Light
            if (actorData.bHasSpotLight) {
                auto& comp = entity->AddComponent<FSpotLightComponent>(actorData.SpotLight);
                comp.bEnabled = actorData.bSpotLightEnabled;
                comp.Mobility = actorData.SpotLightMobility;
                comp.bCastShadows = actorData.bSpotLightCastShadows;
                comp.Light.Position = actorData.Translation;
                // The actor's rotation is what aims the light (the rotate gizmo drives it); the stored
                // direction is what the file says it aims at. When they disagree the direction wins and
                // the rotation is rebuilt from it. When they agree nothing is rewritten: rebuilding one
                // from the other every time moved both by a rounding error on each load, so a map never
                // loaded twice the same and its lightmap bake always read as stale.
                if (glm::length(comp.Light.Direction) > 1e-5f) {
                    auto& xf = entity->GetComponent<FTransformComponent>();
                    const glm::vec3 authored = glm::normalize(comp.Light.Direction);
                    if (glm::dot(SpotLightWorldDirectionFromEuler(xf.Rotation), authored) < 0.999999f)
                        xf.Rotation = SpotLightEulerFromWorldDirection(authored);
                    SyncSpotLightFromTransform(comp.Light, xf.Translation, xf.Rotation);
                }
            }

            // Text Component
            if (actorData.bHasText) {
                entity->AddComponent<FTextComponent>(actorData.TextComp);
            }

            // Reflection probe: AReflectionProbe creates the component itself, so assign rather than add.
            if (actorData.bHasReflectionProbe)
                entity->AddOrReplaceComponent<FReflectionProbeComponent>(actorData.ReflectionProbe);

            // Authored properties last, then the hook: the actor is complete when it runs.
            for (const auto& [key, value] : actorData.Properties) {
                if (!entity->SetProperty(key, value))
                    LE_CORE_WARN("FMapSerializer: Ignored invalid property key '{0}' on actor '{1}'", key,
                                 actorData.Name);
            }
            entity->OnPropertiesLoaded();
        }

        LE_CORE_INFO("FMapSerializer: Instantiated {0} actors from map file", actors.size());
        return true;
    }

} // namespace Leon
