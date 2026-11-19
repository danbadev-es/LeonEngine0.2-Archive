#pragma once

// EnTT render/physics mirror PODs attached to AActor entities. These are not UActorComponent
// subclasses. UObject components live under Gameplay/U*Component.hpp. Aggregation is intentional:
// AActor.hpp includes this registry as the canonical ECS component set.

#include "Core/Base.hpp"
#include "RHI/FBuffer.hpp"
#include "RenderCore/FLight.hpp"
#include "Assets/FMaterial.hpp"
#include "Assets/FMaterialInstance.hpp"
#include "RenderCore/FPerspectiveCamera.hpp"
#include "RenderCore/FPostProcessSettings.hpp"
#include "Assets/UStaticMesh.hpp"
#include "Assets/FLODSettings.hpp"
#include "Assets/USkeletalMesh.hpp"
#include "Scene/EMobility.hpp"
#include "Physics/ECollisionChannel.hpp"
#include "RHI/FTexture.hpp"
#include "RHI/FVertexArray.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>
#include <string>
#include <vector>

namespace Leon {

    /**
     * @brief Local-space AABB used by UWorld Sweep/Overlap queries (no physics engine).
     */
    struct FBoxCollisionComponent {
        glm::vec3 LocalMin{-0.5f};
        glm::vec3 LocalMax{0.5f};
        bool bBlockMovement = true;
        ECollisionChannel Channel = ECollisionChannel::WorldStatic;

        FBoxCollisionComponent() = default;
        FBoxCollisionComponent(const FBoxCollisionComponent&) = default;
        FBoxCollisionComponent(const glm::vec3& InLocalMin, const glm::vec3& InLocalMax, bool bInBlock = true,
                               ECollisionChannel InChannel = ECollisionChannel::WorldStatic)
            : LocalMin(InLocalMin), LocalMax(InLocalMax), bBlockMovement(bInBlock), Channel(InChannel) {}
    };

    /**
     * @brief World bake / Lightmass settings. Lives on the Environment actor (not the skybox).
     */
    enum class ELightingBuildQuality : uint8_t { Preview = 0, Draft = 1, Production = 2 };

    struct FWorldSettingsComponent {
        /** Optional map override; empty = use project/INI DefaultGameMode. */
        std::string GameModeClass;
        bool bStaticLighting = false;
        ELightingBuildQuality LightingBuildQuality = ELightingBuildQuality::Draft;
        uint32_t LightmapResolution = 64;
        uint32_t NumIndirectBounces = 2;
        uint32_t SamplesPerTexel = 16;
        float IndirectIntensity = 1.0f;
        bool bAmbientOcclusion = true;
        float AOIntensity = 1.0f;
        float AORadius = 1.0f;
        float TexelPadding = 2.0f;
        float WorldScale = 1.0f;
        std::string LightmapAssetPath;
        uint64_t LightmapBakeHash = 0;
        /** The bake also fills a grid of light probes: ambient light for characters and whatever moves. */
        bool bLightProbes = true;
        /** Metres between probes, along the ground and upwards. */
        float LightProbeSpacing = 1.5f;
        float LightProbeVerticalSpacing = 1.0f;
        /** Written by the bake: the probes on disk (.lprobes). Empty = the map has none. */
        std::string LightProbeAssetPath;

        FWorldSettingsComponent() = default;
        FWorldSettingsComponent(const FWorldSettingsComponent&) = default;
    };

    /**
     * Post-processing authored for one map (its PostProcess: block). Lives on the Environment actor,
     * next to the sky and the world settings. A map without it is drawn with the renderer's own
     * settings and the exposure of the sky.
     */
    struct FPostProcessComponent {
        /** 0 = ACES, 1 = Reinhard, 2 = clamp, 3 = Uncharted 2 (see ToneMapperName). */
        int ToneMapper = 0;
        /** False: the exposure of the sky is used. */
        bool bOverrideExposure = false;
        float Exposure = 1.0f;
        float BloomIntensity = 0.05f;
        float BloomThreshold = 1.0f;
        FPostProcessLook Look;

        FPostProcessComponent() = default;
        FPostProcessComponent(const FPostProcessComponent&) = default;
    };

    struct FTagComponent {
        std::string Tag;

        FTagComponent() = default;
        FTagComponent(const FTagComponent&) = default;
        FTagComponent(const std::string& InTag) : Tag(InTag) {}
    };

    struct FTransformComponent {
        glm::vec3 Translation{0.0f, 0.0f, 0.0f};
        glm::vec3 Rotation{0.0f, 0.0f, 0.0f}; // Euler angles in degrees (Pitch, Yaw, Roll)
        glm::vec3 Scale{1.0f, 1.0f, 1.0f};

        FTransformComponent() = default;
        FTransformComponent(const FTransformComponent&) = default;
        FTransformComponent(const glm::vec3& InTranslation) : Translation(InTranslation) {}
        FTransformComponent(const glm::vec3& InTranslation, const glm::vec3& InRotation, const glm::vec3& InScale)
            : Translation(InTranslation), Rotation(InRotation), Scale(InScale) {}

        glm::mat4 GetTransform() const {
            glm::mat4 rotation = glm::toMat4(glm::quat(glm::radians(Rotation)));
            return glm::translate(glm::mat4(1.0f), Translation) * rotation * glm::scale(glm::mat4(1.0f), Scale);
        }
    };

    struct FMeshComponent {
        TRef<FVertexArray> VertexArray;
        bool bCastShadows = true;
        bool bReceiveShadows = true;
        bool bVisibleInReflection = true;
        bool bVisible = true;
        /**
         * False: the mesh is only drawn. Nothing collides with it and no trace hits it; collision for
         * that part of the level comes from blocking volumes (ABlockingVolume).
         */
        bool bCollision = true;
        EComponentMobility Mobility = EComponentMobility::Static;
        uint32_t LightmapResolution = 64;
        int32_t LightmapIndex = -1;
        glm::vec2 LightmapScale{1.0f, 1.0f};
        glm::vec2 LightmapBias{0.0f, 0.0f};
        std::string LightmapAssetPath;

        // Mesh geometry metadata for lossless map serialization
        std::string MeshType = "Cube";
        float MeshSize = 1.0f;
        float MeshWidth = 1.0f;
        float MeshHeight = 1.0f;
        float MeshDepth = 1.0f;
        float MeshRadius = 0.5f;
        /** World meters per UV tile for Box primitives (CreateBox). */
        float MeshMetersPerUv = 1.0f;
        unsigned int MeshSubdivX = 24;
        unsigned int MeshSubdivZ = 24;

        FMeshComponent() = default;
        FMeshComponent(const FMeshComponent&) = default;
        explicit FMeshComponent(const TRef<FVertexArray>& InVertexArray) : VertexArray(InVertexArray) {}
    };

    /**
     * @brief Unreal Engine aligned StaticMesh component.
     * Holds an UStaticMesh asset reference with material slot overrides.
     */
    struct FStaticMeshComponent {
        TRef<UStaticMesh> StaticMesh = nullptr;
        std::vector<TRef<FMaterialInstance>> MaterialOverrides;
        std::vector<std::string> MaterialOverridePaths;
        std::string AssetPath; // Virtual path to .lmesh
        bool bCastShadows = true;
        bool bReceiveShadows = true;
        bool bVisibleInReflection = true;
        bool bVisible = true;
        /**
         * False: the mesh is only drawn. Nothing collides with it and no trace hits it; collision for
         * that part of the level comes from blocking volumes (ABlockingVolume).
         */
        bool bCollision = true;
        EComponentMobility Mobility = EComponentMobility::Static;
        uint32_t LightmapResolution = 64;
        int32_t LightmapIndex = -1; ///< Index into world lightmap atlas entries (-1 = none)
        glm::vec2 LightmapScale{1.0f, 1.0f};
        glm::vec2 LightmapBias{0.0f, 0.0f};
        std::string LightmapAssetPath; ///< Virtual path to .llightmap (atlas for this world/instance)
        uint32_t CurrentLOD = 0;
        uint32_t ForcedLOD = kForcedLODAuto;
        bool bUseCoarserShadowLOD = true;

        FStaticMeshComponent() = default;
        FStaticMeshComponent(const FStaticMeshComponent&) = default;
        explicit FStaticMeshComponent(const TRef<UStaticMesh>& InMesh, const std::string& InAssetPath = "")
            : StaticMesh(InMesh), AssetPath(InAssetPath) {}
    };

    /**
     * Render-side skinned mesh (EnTT POD). Gameplay ticks USkeletalMeshComponent which writes BonePalette here.
     * Named F* (not U*) — render state POD, not a UObject component.
     */
    struct FSkinnedMeshRenderState {
        TRef<USkeletalMesh> SkeletalMesh = nullptr;
        std::vector<TRef<FMaterialInstance>> MaterialOverrides;
        std::string AssetPath;
        std::vector<glm::mat4> BonePalette;
        glm::vec3 RelativeLocation{0.0f};
        glm::vec3 RelativeRotation{0.0f};
        glm::vec3 RelativeScale{1.0f};
        bool bCastShadows = true;
        bool bReceiveShadows = true;
        bool bVisibleInReflection = true;
        bool bVisible = true;
        /** Friend/foe silhouette (inverted-hull outline pass). */
        bool bDrawOutline = false;
        glm::vec3 OutlineColor{1.0f, 0.14f, 0.1f};
        float OutlineWidth = 0.016f;

        FSkinnedMeshRenderState() = default;
        FSkinnedMeshRenderState(const FSkinnedMeshRenderState&) = default;
    };

    struct FMaterialComponent {
        TRef<FMaterialInstance> MaterialInstance;
        std::string AssetPath;

        FMaterialComponent() = default;
        FMaterialComponent(const FMaterialComponent&) = default;
        explicit FMaterialComponent(const TRef<FMaterialInstance>& InInstance, const std::string& InAssetPath = "")
            : MaterialInstance(InInstance), AssetPath(InAssetPath) {}
    };

    /**
     * @brief Unreal Engine aligned Light components.
     */
    struct FDirectionalLightComponent {
        FDirectionalLight Light;
        bool bEnabled = true;
        ELightMobility Mobility = ELightMobility::Movable;

        FDirectionalLightComponent() = default;
        FDirectionalLightComponent(const FDirectionalLightComponent&) = default;
        FDirectionalLightComponent(const FDirectionalLight& InLight) : Light(InLight) {}
    };

    struct FPointLightComponent {
        FPointLight Light;
        bool bEnabled = true;
        ELightMobility Mobility = ELightMobility::Movable;
        /**
         * True: the light may take one of the shadow cubes (the renderer gives them to the most important
         * lights for the camera). Off unless asked for: a cube costs six views of the scene, and most
         * lights of a level have their shadows in the lightmap.
         */
        bool bCastShadows = false;

        FPointLightComponent() = default;
        FPointLightComponent(const FPointLightComponent&) = default;
        FPointLightComponent(const FPointLight& InLight) : Light(InLight) {}
    };

    struct FSpotLightComponent {
        FSpotLight Light;
        bool bEnabled = true;
        ELightMobility Mobility = ELightMobility::Movable;
        /** False: the light never takes a layer of the spot shadow map, it lights without shadows. */
        bool bCastShadows = true;

        FSpotLightComponent() = default;
        FSpotLightComponent(const FSpotLightComponent&) = default;
        FSpotLightComponent(const FSpotLight& InLight) : Light(InLight) {}
    };

    /**
     * A local reflection probe (AReflectionProbe, a ReflectionProbe: block in a map): a cube map of
     * the static scene, captured from inside a room and read by the surfaces inside the box. The box
     * is centred on the actor and axis-aligned; the actor's rotation and scale are not used.
     */
    struct FReflectionProbeComponent {
        /** Half the size of the box, in metres: the room. */
        glm::vec3 Extent{2.0f, 1.5f, 2.0f};
        /** Where the cube is captured from, relative to the centre of the box. Keep it clear of furniture. */
        glm::vec3 CaptureOffset{0.0f};
        /** Scales what the probe reflects. */
        float Intensity = 1.0f;
        /** Metres outside the box over which the probe fades into its neighbour or the sky. */
        float BlendDistance = 0.5f;

        FReflectionProbeComponent() = default;
        FReflectionProbeComponent(const FReflectionProbeComponent&) = default;
    };

    /**
     * @brief Unreal Engine aligned Camera component.
     */
    struct FCameraComponent {
        FPerspectiveCamera Camera{45.0f, 1280.0f / 720.0f, 0.1f, 1000.0f};
        bool bPrimary = true;

        FCameraComponent() = default;
        FCameraComponent(const FCameraComponent&) = default;
        FCameraComponent(const FPerspectiveCamera& InCamera) : Camera(InCamera) {}
    };

    struct FSkyboxComponent {
        bool bEnabled = true;
        float Exposure = 1.0f;
        float SunIntensity = 3.5f;
        float EnvironmentIntensity = 1.2f;

        TRef<FTexture2D> HDREnvironmentMap;
        std::string HDREnvironmentMapPath;
        bool bUseHDREnvironmentMap = false;

        glm::vec3 SkyZenithColor{0.18f, 0.44f, 0.88f};
        glm::vec3 HorizonColor{0.78f, 0.84f, 0.95f};
        glm::vec3 GroundColor{0.22f, 0.24f, 0.28f};
        glm::vec3 SunColor{1.0f, 0.98f, 0.92f};

        // Distance + height fog applied to lit geometry (not to the sky, particles or world text).
        // Lives with the environment because it is authored per map, like the sky colours.
        bool bFogEnabled = false;
        glm::vec3 FogColor{0.045f, 0.05f, 0.055f};
        /** Extinction per metre: 0.05 is thin haze, 0.3 hides everything past ~10 m. */
        float FogDensity = 0.08f;
        /** Metres from the camera before fog starts to build up. */
        float FogStartDistance = 0.0f;
        /** 0 = uniform. Higher values thin the fog above FogBaseHeight (ground fog). */
        float FogHeightFalloff = 0.0f;
        float FogBaseHeight = 0.0f;
        /** Upper bound of the fog amount (0..1), so distant lights never vanish completely. */
        float FogMaxOpacity = 1.0f;

        FSkyboxComponent() = default;
        FSkyboxComponent(const FSkyboxComponent&) = default;
    };

    enum class ETextAlignment : uint8_t { Left = 0, Center = 1, Right = 2 };

    struct FTextComponent {
        std::string Text = "Text";
        glm::vec4 Color{0.72f, 0.72f, 0.72f, 1.0f};
        float Size = 1.0f;
        float LineSpacing = 1.2f;
        ETextAlignment Alignment = ETextAlignment::Center;
        bool bDoubleSided = true;

        FTextComponent() = default;
        FTextComponent(const FTextComponent&) = default;
        FTextComponent(const std::string& InText, const glm::vec4& InColor = glm::vec4(0.72f, 0.72f, 0.72f, 1.0f),
                       float InSize = 1.0f, ETextAlignment InAlignment = ETextAlignment::Center)
            : Text(InText), Color(InColor), Size(InSize), Alignment(InAlignment) {}
    };

} // namespace Leon
