#pragma once

#include "Core/Base.hpp"
#include "Renderer/FIBLGenerator.hpp"
#include "Renderer/FPerspectiveCamera.hpp"
#include "Renderer/FPlanarReflectionTypes.hpp"
#include "Renderer/FPostProcessSettings.hpp"
#include "Renderer/FShadowMath.hpp"
#include "Renderer/FShadowTypes.hpp"

#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

namespace Leon {

    class UWorld;
    class IRHICommandList;
    class IRHITexture;
    struct FSkyboxComponent;
    struct FWorldRendererRHIState;

    /**
     * Draws a UWorld: shadow maps, reflection captures, the lit scene and post-processing.
     *
     * The drawing itself lives in FWorldRendererRHI.cpp; this class holds the settings and the state
     * that outlives a frame.
     */
    class FWorldRenderer {
    public:
        explicit FWorldRenderer(UWorld* InWorld);
        ~FWorldRenderer() = default;

        FWorldRenderer(const FWorldRenderer&) = delete;
        FWorldRenderer& operator=(const FWorldRenderer&) = delete;

        /**
         * Draws the world as InCamera sees it into the current frame's output (see FRHIFrame).
         * Outside a frame (tests, tools) nothing is drawn, but everything that depends on the view is
         * still brought up to date: level-of-detail choices, the environment, reflection planes.
         */
        void Render(const FPerspectiveCamera& InCamera);

        /** As Render, into a target of the caller's choice. Call between render passes. */
        void RenderRHI(const FPerspectiveCamera& InCamera, IRHICommandList& InCommands, IRHITexture& InTarget);
        void RenderScene(const FPerspectiveCamera& InCamera) { Render(InCamera); }

        void SetWorld(UWorld* InWorld) { World = InWorld; }
        UWorld* GetWorld() const { return World; }

        /** The size of the view; reflection captures are sized as a fraction of it. */
        void OnViewportResize(uint32_t InWidth, uint32_t InHeight);

        /** A view mode of the surface shader (see the debug views in PBR_Lit.hlsl); 0 = normal. */
        void SetDebugMode(int InMode) { DebugMode = InMode; }
        int GetDebugMode() const { return DebugMode; }

        /** Draws the scene's surfaces as wireframe. Post-processing and UI are not affected. */
        void SetWireframeEnabled(bool bEnabled) { bWireframeEnabled = bEnabled; }
        bool IsWireframeEnabled() const { return bWireframeEnabled; }

        FPostProcessSettings& GetPostProcessSettings() { return PostProcessSettings; }
        const FPostProcessSettings& GetPostProcessSettings() const { return PostProcessSettings; }

        /** Changes take effect on the next frame; shadow maps are recreated when a resolution changes. */
        FShadowSettings& GetShadowSettings() { return ShadowSettings; }
        const FShadowSettings& GetShadowSettings() const { return ShadowSettings; }

        void SetPlanarReflectionEnabled(bool bEnabled) { bEnablePlanarReflection = bEnabled; }
        bool IsPlanarReflectionEnabled() const { return bEnablePlanarReflection; }
        void SetPlanarReflectionQuality(EPlanarReflectionQuality InQuality);
        EPlanarReflectionQuality GetPlanarReflectionQuality() const { return PlanarQuality; }
        void SetPlanarReflectionResolutionScale(float InScale);
        float GetPlanarReflectionResolutionScale() const { return PlanarResolutionScale; }

        /** A mirror plane in world space: points x with dot(Normal, x) + Distance = 0. */
        struct FPlanarReflectionPlane {
            glm::vec3 Normal{0.0f, 1.0f, 0.0f};
            float Distance = 0.0f;
        };

        /**
         * The planes that can be mirrored. The first horizontal one is the floor and is always
         * captured; of the others, the one the camera faces most directly is captured as a wall mirror.
         */
        void ClearPlanarReflectionPlanes();
        void AddPlanarReflectionPlane(const glm::vec3& InNormal, float InDistance);

        /** Applies the project's renderer defaults (DefaultEngine.ini). */
        void ApplyProjectRendererDefaults(uint32_t InShadowMapResolution, bool bInEnablePlanarReflection,
                                          EPlanarReflectionQuality InPlanarQuality = EPlanarReflectionQuality::Epic,
                                          float InPlanarResolutionScale = 0.0f);

        /** Rebuilds the image-based lighting environment on the next frame (e.g. after a quality change). */
        void InvalidateEnvironment();

        /**
         * Off: surfaces are lit by the sky's three colours instead of an environment map, and the
         * environment is not generated at all (it takes seconds on the CPU).
         */
        void SetImageBasedLightingEnabled(bool bEnabled) { bUseIBL = bEnabled; }
        bool IsImageBasedLightingEnabled() const { return bUseIBL; }

    private:
        /** The part of a frame that needs no device: everything Render promises to keep up to date. */
        void UpdateViewState(const FPerspectiveCamera& InCamera);

        /** Regenerates the environment when the sky it was built from changes. */
        void UpdateIBL(const FSkyboxComponent& InSkybox);

        /** Chooses which skinned meshes may cast shadows this frame (the nearest few). */
        void RefreshSkinnedShadowCasterSelection(const glm::vec3& InCameraPos);
        bool IsSkinnedShadowCasterSelected(uint32_t InEntityId) const;

        FPlanarReflectionPlane SelectFloorPlane() const;
        bool SelectWallMirrorPlane(const FPerspectiveCamera& InCamera, FPlanarReflectionPlane& OutPlane) const;
        uint32_t PlanarCaptureWidth() const;
        uint32_t PlanarCaptureHeight() const;

        UWorld* World = nullptr;
        /** Device objects: targets, shadow maps, pipelines. Created on the first frame drawn. */
        std::shared_ptr<FWorldRendererRHIState> RHIState;

        uint32_t ViewportWidth = 1280;
        uint32_t ViewportHeight = 720;
        int DebugMode = 0;
        bool bWireframeEnabled = false;

        // Planar reflections. The mirrored cameras are chosen in UpdateViewState.
        bool bEnablePlanarReflection = true;
        EPlanarReflectionQuality PlanarQuality = EPlanarReflectionQuality::Epic;
        float PlanarResolutionScale = 1.0f;
        std::vector<FPlanarReflectionPlane> PlanarReflectionPlanes;
        glm::mat4 PlanarViewProjection{1.0f};
        glm::vec3 PlanarPlaneNormal{0.0f, 1.0f, 0.0f};
        float PlanarPlaneDistance = 0.0f;
        glm::mat4 WallPlanarViewProjection{1.0f};
        glm::vec3 WallPlanarPlaneNormal{0.0f, 0.0f, 1.0f};
        float WallPlanarPlaneDistance = 0.0f;
        bool bWallPlanarActive = false;

        // Image-based lighting.
        FIBLEnvironment IBLEnvironment;
        bool bUseIBL = true;
        bool bEnvironmentGenerated = false;
        std::string LoadedHDRPath;

        FPostProcessSettings PostProcessSettings;

        FShadowSettings ShadowSettings;
        std::unordered_set<uint32_t> SelectedSkinnedShadowCasters;
        bool bSkinnedShadowSelectionUnlimited = true;
    };

} // namespace Leon
