#include "Engine/FLightmapBakeInputs.hpp"

#include "Engine/FLightProbeAsset.hpp"

#include "Assets/FMaterialInstance.hpp"
#include "Assets/UAssetManager.hpp"
#include "Assets/UStaticMesh.hpp"
#include "Core/FAssetPath.hpp"
#include "Core/FLog.hpp"
#include "Scene/EMobility.hpp"
#include "Engine/UWorld.hpp"
#include "Gameplay/AActor.hpp"
#include "RenderCore/FIBLMath.hpp"

#include <filesystem>

namespace Leon {

    namespace fs = std::filesystem;

    namespace {

        uint64_t HashBytes(uint64_t Hash, const void* Data, size_t Size) {
            const auto* b = static_cast<const uint8_t*>(Data);
            for (size_t i = 0; i < Size; ++i) {
                Hash ^= b[i];
                Hash *= 1099511628211ull;
            }
            return Hash;
        }

        uint64_t HashString(uint64_t Hash, const std::string& InStr) {
            return HashBytes(Hash, InStr.data(), InStr.size());
        }

        uint64_t HashVec3(uint64_t Hash, const glm::vec3& InV) {
            return HashBytes(Hash, &InV, sizeof(InV));
        }

        uint64_t HashAssetFileFingerprint(uint64_t Hash, const std::string& InPath) {
            if (InPath.empty())
                return Hash;
            std::string resolved = InPath;
            if (!fs::path(InPath).is_absolute()) {
                fs::path candidate = fs::path(UAssetManager::GetContentRoot()) / InPath;
                if (!fs::exists(candidate))
                    candidate = InPath;
                resolved = candidate.string();
            }
            // The path as the map names it, not where it is on disk: moving or renaming the project
            // folder must not make a bake look stale.
            Hash = HashString(Hash, FAssetPath::Normalize(InPath));
            std::error_code ec;
            if (fs::exists(resolved, ec)) {
                uint64_t content = ComputeFileHash64(resolved);
                Hash = HashBytes(Hash, &content, sizeof(content));
            }
            return Hash;
        }

        FWorldSettingsComponent* FindWorldSettings(UWorld& InWorld) {
            for (auto& actorRef : InWorld.GetAllActors()) {
                if (actorRef && actorRef->HasComponent<FWorldSettingsComponent>())
                    return &actorRef->GetComponent<FWorldSettingsComponent>();
            }
            return nullptr;
        }

    } // namespace

    void FLightmapBakeInputs::ApplyWorldSettings(const FWorldSettingsComponent& Ws, FLightmassSettings& OutSettings,
                                                 std::string& OutLightmapVirtual) {
        if (Ws.LightmapResolution > 0)
            OutSettings.LightmapResolution = Ws.LightmapResolution;
        OutSettings.NumIndirectBounces = Ws.NumIndirectBounces;
        OutSettings.SamplesPerTexel = Ws.SamplesPerTexel;
        OutSettings.IndirectIntensity = Ws.IndirectIntensity;
        OutSettings.bAmbientOcclusion = Ws.bAmbientOcclusion;
        OutSettings.AOIntensity = Ws.AOIntensity;
        OutSettings.AORadius = Ws.AORadius;
        OutSettings.TexelPadding = Ws.TexelPadding;
        OutSettings.WorldScale = Ws.WorldScale;
        OutSettings.LightingBuildQuality = Ws.LightingBuildQuality;
        OutSettings.bLightProbes = Ws.bLightProbes;
        OutSettings.LightProbeSpacing = Ws.LightProbeSpacing;
        OutSettings.LightProbeVerticalSpacing = Ws.LightProbeVerticalSpacing;
        if (!Ws.LightmapAssetPath.empty())
            OutLightmapVirtual = Ws.LightmapAssetPath;
    }

    bool FLightmapBakeInputs::IsDecal(const AActor& InActor) {
        const auto isDecal = [](const TRef<FMaterialInstance>& InMaterial) {
            return InMaterial && InMaterial->GetAlphaMode() == EAlphaMode::Decal;
        };
        if (InActor.HasComponent<FStaticMeshComponent>()) {
            const auto& component = InActor.GetComponent<FStaticMeshComponent>();
            if (!component.StaticMesh || component.StaticMesh->GetSubmeshes().empty())
                return false;
            // The materials the renderer draws the mesh with, part by part. A mesh that is a decal
            // only in part is baked whole: its other parts are surfaces like any other.
            for (const auto& submesh : component.StaticMesh->GetSubmeshes()) {
                if (!isDecal(ResolveStaticSubmeshMaterial(*component.StaticMesh, submesh, component.MaterialOverrides)))
                    return false;
            }
            return true;
        }
        if (InActor.HasComponent<FMeshComponent>() && InActor.HasComponent<FMaterialComponent>())
            return isDecal(InActor.GetComponent<FMaterialComponent>().MaterialInstance);
        return false;
    }

    uint64_t FLightmapBakeInputs::ComputeHash(const UWorld& InWorld, const FLightmassSettings& InSettings) {
        uint64_t hash = 14695981039346656037ull;
        constexpr uint32_t kBakerAlgorithmVersion = 5;
        hash = HashBytes(hash, &kBakerAlgorithmVersion, sizeof(kBakerAlgorithmVersion));

        hash = HashBytes(hash, &InSettings.LightmapResolution, sizeof(InSettings.LightmapResolution));
        hash = HashBytes(hash, &InSettings.NumIndirectBounces, sizeof(InSettings.NumIndirectBounces));
        hash = HashBytes(hash, &InSettings.SamplesPerTexel, sizeof(InSettings.SamplesPerTexel));
        hash = HashBytes(hash, &InSettings.IndirectIntensity, sizeof(InSettings.IndirectIntensity));
        uint8_t ao = InSettings.bAmbientOcclusion ? 1 : 0;
        hash = HashBytes(hash, &ao, sizeof(ao));
        hash = HashBytes(hash, &InSettings.AOIntensity, sizeof(InSettings.AOIntensity));
        hash = HashBytes(hash, &InSettings.AORadius, sizeof(InSettings.AORadius));
        hash = HashBytes(hash, &InSettings.TexelPadding, sizeof(InSettings.TexelPadding));
        hash = HashBytes(hash, &InSettings.WorldScale, sizeof(InSettings.WorldScale));
        hash = HashBytes(hash, &InSettings.DeterministicSeed, sizeof(InSettings.DeterministicSeed));
        uint8_t quality = static_cast<uint8_t>(InSettings.LightingBuildQuality);
        hash = HashBytes(hash, &quality, sizeof(quality));
        // The light probe settings count only where a map changed them: with the defaults the hash is
        // what it was before probes existed, so the bakes of older maps stay valid.
        const FLightmassSettings probeDefaults;
        if (InSettings.bLightProbes != probeDefaults.bLightProbes ||
            InSettings.LightProbeSpacing != probeDefaults.LightProbeSpacing ||
            InSettings.LightProbeVerticalSpacing != probeDefaults.LightProbeVerticalSpacing) {
            const uint8_t probes = InSettings.bLightProbes ? 1 : 0;
            hash = HashBytes(hash, &probes, sizeof(probes));
            hash = HashBytes(hash, &InSettings.LightProbeSpacing, sizeof(InSettings.LightProbeSpacing));
            hash = HashBytes(hash, &InSettings.LightProbeVerticalSpacing, sizeof(InSettings.LightProbeVerticalSpacing));
        }

        for (const auto& actorRef : InWorld.GetAllActors()) {
            if (!actorRef)
                continue;
            AActor& actor = *actorRef;
            // A reflection probe is captured from the baked scene and puts no light into it: placing
            // or moving one must not make the bake stale.
            if (actor.HasComponent<FReflectionProbeComponent>())
                continue;
            // Nor does a decal: the baker leaves it out altogether.
            if (IsDecal(actor))
                continue;

            if (actor.HasComponent<FTransformComponent>()) {
                const auto& t = actor.GetComponent<FTransformComponent>();
                hash = HashVec3(hash, t.Translation);
                hash = HashVec3(hash, t.Rotation);
                hash = HashVec3(hash, t.Scale);
            }

            if (actor.HasComponent<FStaticMeshComponent>()) {
                const auto& smc = actor.GetComponent<FStaticMeshComponent>();
                uint8_t mob = static_cast<uint8_t>(smc.Mobility);
                hash = HashBytes(hash, &mob, sizeof(mob));
                hash = HashBytes(hash, &smc.LightmapResolution, sizeof(smc.LightmapResolution));
                hash = HashBytes(hash, &smc.bCastShadows, sizeof(smc.bCastShadows));
                hash = HashString(hash, actor.GetName());
                if (smc.Mobility == EComponentMobility::Static && smc.StaticMesh) {
                    // The path the map names. The asset remembers the path it was first loaded by,
                    // which depends on what was loaded before this map.
                    const std::string& meshPath =
                        smc.AssetPath.empty() ? smc.StaticMesh->GetAssetPath() : smc.AssetPath;
                    hash = HashString(hash, meshPath);
                    hash = HashAssetFileFingerprint(hash, meshPath);
                    for (const auto& slot : smc.StaticMesh->GetMaterialSlots()) {
                        hash = HashString(hash, slot.DefaultMaterialPath);
                        hash = HashAssetFileFingerprint(hash, slot.DefaultMaterialPath);
                    }
                }
                for (const auto& path : smc.MaterialOverridePaths) {
                    hash = HashString(hash, path);
                    hash = HashAssetFileFingerprint(hash, path);
                }
                if (actor.HasComponent<FMaterialComponent>()) {
                    hash = HashString(hash, actor.GetComponent<FMaterialComponent>().AssetPath);
                    hash = HashAssetFileFingerprint(hash, actor.GetComponent<FMaterialComponent>().AssetPath);
                }
            } else if (actor.HasComponent<FMeshComponent>()) {
                const auto& mc = actor.GetComponent<FMeshComponent>();
                uint8_t mob = static_cast<uint8_t>(mc.Mobility);
                hash = HashBytes(hash, &mob, sizeof(mob));
                hash = HashBytes(hash, &mc.LightmapResolution, sizeof(mc.LightmapResolution));
                hash = HashBytes(hash, &mc.bCastShadows, sizeof(mc.bCastShadows));
                hash = HashString(hash, mc.MeshType);
                hash = HashBytes(hash, &mc.MeshSize, sizeof(mc.MeshSize));
                hash = HashBytes(hash, &mc.MeshWidth, sizeof(mc.MeshWidth));
                hash = HashBytes(hash, &mc.MeshHeight, sizeof(mc.MeshHeight));
                hash = HashBytes(hash, &mc.MeshDepth, sizeof(mc.MeshDepth));
                hash = HashBytes(hash, &mc.MeshRadius, sizeof(mc.MeshRadius));
                hash = HashString(hash, actor.GetName());
                if (actor.HasComponent<FMaterialComponent>()) {
                    hash = HashString(hash, actor.GetComponent<FMaterialComponent>().AssetPath);
                    hash = HashAssetFileFingerprint(hash, actor.GetComponent<FMaterialComponent>().AssetPath);
                }
            }

            auto hashLight = [&](ELightMobility Mobility, bool bEnabled, const auto& LightPayload) {
                if (!bEnabled || !IsLightmassBakeLight(Mobility))
                    return;
                uint8_t mob = static_cast<uint8_t>(Mobility);
                hash = HashBytes(hash, &mob, sizeof(mob));
                hash = HashBytes(hash, &LightPayload, sizeof(LightPayload));
            };

            if (actor.HasComponent<FDirectionalLightComponent>()) {
                const auto& c = actor.GetComponent<FDirectionalLightComponent>();
                hashLight(c.Mobility, c.bEnabled, c.Light);
            }
            if (actor.HasComponent<FPointLightComponent>()) {
                const auto& c = actor.GetComponent<FPointLightComponent>();
                hashLight(c.Mobility, c.bEnabled, c.Light);
            }
            if (actor.HasComponent<FSpotLightComponent>()) {
                const auto& c = actor.GetComponent<FSpotLightComponent>();
                hashLight(c.Mobility, c.bEnabled, c.Light);
            }

            if (actor.HasComponent<FSkyboxComponent>()) {
                const auto& sky = actor.GetComponent<FSkyboxComponent>();
                uint8_t en = sky.bEnabled ? 1 : 0;
                uint8_t useHdr = sky.bUseHDREnvironmentMap ? 1 : 0;
                hash = HashBytes(hash, &en, sizeof(en));
                hash = HashBytes(hash, &useHdr, sizeof(useHdr));
                hash = HashVec3(hash, sky.SkyZenithColor);
                hash = HashVec3(hash, sky.HorizonColor);
                hash = HashVec3(hash, sky.GroundColor);
                hash = HashBytes(hash, &sky.EnvironmentIntensity, sizeof(sky.EnvironmentIntensity));
                hash = HashString(hash, sky.HDREnvironmentMapPath);
                hash = HashAssetFileFingerprint(hash, sky.HDREnvironmentMapPath);
            }
        }

        return hash;
    }

    void FLightmapBakeInputs::RefreshRuntimeTrust(UWorld& InWorld) {
        InWorld.SetLightmapsTrusted(true);
        InWorld.SetLightProbes(FLightProbeGrid{});
        FWorldSettingsComponent* ws = FindWorldSettings(InWorld);
        if (!ws || !ws->bStaticLighting)
            return;

        FLightmassSettings settings;
        ApplyLightingBuildQuality(ws->LightingBuildQuality, settings);
        std::string unusedPath;
        ApplyWorldSettings(*ws, settings, unusedPath);
        const uint64_t current = ComputeHash(InWorld, settings);
        if (ws->LightmapBakeHash == 0 || current != ws->LightmapBakeHash) {
            LE_CORE_ERROR("Lightmaps: the bake stored in the map is stale (stored={0:x} current={1:x}): not using them. "
                          "Bake again with Tools/bake_lightmaps.py",
                          ws->LightmapBakeHash, current);
            InWorld.SetLightmapsTrusted(false);
            return;
        }

        // The light probes belong to the same bake: a file with another hash is from another one.
        if (ws->LightProbeAssetPath.empty())
            return;
        FLightProbeGrid probes;
        uint64_t probeHash = 0;
        const std::string probePath = UAssetManager::ResolveVirtualPath(ws->LightProbeAssetPath);
        if (!FLightProbeAsset::Load(probePath, probes, probeHash)) {
            LE_CORE_WARN("Light probes: '{0}' is missing or damaged: characters take their ambient light from the sky",
                         probePath);
            return;
        }
        if (probeHash != ws->LightmapBakeHash) {
            LE_CORE_ERROR("Light probes: '{0}' is from another bake (file={1:x} map={2:x}): not using it. Bake again",
                          probePath, probeHash, ws->LightmapBakeHash);
            return;
        }
        InWorld.SetLightProbes(std::move(probes));
    }

} // namespace Leon
