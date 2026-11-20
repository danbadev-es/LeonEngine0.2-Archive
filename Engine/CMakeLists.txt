# LeonEngine2 — Unreal-style Runtime layout (Public/Private folders).
# Single link unit for now: cross-module includes (Core↔RHI) match the previous monolith.
# Folder modules (Core, RHI, Assets, …) remain the include/organization contract.

# When Engine is add_subdirectory'd from Editor, CMAKE_SOURCE_DIR is Editor/.
# Prefer LEON_ENGINE_ROOT (repo root) for ThirdParty and shared paths.
if(NOT DEFINED LEON_ENGINE_ROOT)
    get_filename_component(LEON_ENGINE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
endif()

set(LEON_RUNTIME_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/Source/Runtime")

include("${LEON_ENGINE_ROOT}/cmake/LeonModule.cmake")

# --- Lower layers: real modules. Each one only sees the headers of what it depends on. --------
leon_add_module(Core
    SOURCES
    Source/Runtime/Core/Private/FConfigFile.cpp
    Source/Runtime/Core/Private/FLog.cpp
    Source/Runtime/Core/Private/FProjectDescriptor.cpp
    Source/Runtime/Core/Private/FProjectPaths.cpp
    Source/Runtime/Core/Private/FAssetPath.cpp
    LIBRARIES glm::glm
)

leon_add_module(RHI
    SOURCES
    Source/Runtime/RHI/Private/FPlatformMemory.cpp
    Source/Runtime/RHI/Private/FLegacyRenderDriver.cpp
    Source/Runtime/RHI/Private/FRHI.cpp
    Source/Runtime/RHI/Private/FFrameProfiler.cpp
    Source/Runtime/RHI/Private/IRenderDriver.cpp
    Source/Runtime/RHI/Private/FBuffer.cpp
    Source/Runtime/RHI/Private/FTexture.cpp
    Source/Runtime/RHI/Private/FVertexArray.cpp
    Source/Runtime/RHI/Private/FRenderer.cpp
    DEPENDS Core
)
if(WIN32)
    target_link_libraries(LeonModule_RHI PRIVATE psapi dxgi)
endif()

# A backend with no GPU behind it, for tests and tools.
leon_add_module(RHINull
    SOURCES
    Source/Runtime/RHINull/Private/FNullRHI.cpp
    DEPENDS RHI
)

# The Direct3D 12 backend.
leon_add_module(RHID3D12
    SOURCES
    Source/Runtime/RHID3D12/Private/FD3D12Device.cpp
    Source/Runtime/RHID3D12/Private/FD3D12CommandList.cpp
    DEPENDS RHI
    PRIVATE_LIBRARIES d3d12 dxgi dxguid
)

# HLSL -> DXIL with the DirectX Shader Compiler from the Windows SDK. Developer module: a packaged
# game ships compiled shaders, not the compiler.
set(LEON_DEVELOPER_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/Source/Developer")
leon_add_module(ShaderCompiler
    ROOT "${LEON_DEVELOPER_ROOT}/ShaderCompiler"
    SOURCES
    Source/Developer/ShaderCompiler/Private/FShaderCompiler.cpp
    DEPENDS RHI
    PRIVATE_LIBRARIES dxcompiler
)
# dxcompiler.dll and dxil.dll (which signs the output so Direct3D accepts it) must sit beside
# every executable that compiles shaders.
find_file(LEON_DXCOMPILER_DLL dxcompiler.dll
    HINTS "$ENV{WindowsSdkDir}/Redist/D3D/x64" "C:/Program Files (x86)/Windows Kits/10/Redist/D3D/x64"
    NO_DEFAULT_PATH)
find_file(LEON_DXIL_DLL dxil.dll
    HINTS "$ENV{WindowsSdkDir}/Redist/D3D/x64" "C:/Program Files (x86)/Windows Kits/10/Redist/D3D/x64"
    NO_DEFAULT_PATH)
if(NOT LEON_DXCOMPILER_DLL OR NOT LEON_DXIL_DLL)
    message(FATAL_ERROR "dxcompiler.dll / dxil.dll not found in the Windows SDK (Redist/D3D/x64)")
endif()
add_custom_target(LeonShaderCompilerRuntime ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LEON_DXCOMPILER_DLL}" "${LEON_DXIL_DLL}" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
    COMMENT "Copying the DirectX Shader Compiler next to the executables"
)
add_dependencies(LeonModule_ShaderCompiler LeonShaderCompilerRuntime)

leon_add_module(Platform
    SOURCES
    Source/Runtime/Platform/Private/FInput.cpp
    Source/Runtime/Platform/Private/FWindow.cpp
    DEPENDS Core
    PRIVATE_LIBRARIES glfw stb_image
)

# What drawing needs that does not know about worlds or assets: vertex formats, cameras, colour and
# lighting maths, the shader library and the C++ side of the shaders' constant buffers.
leon_add_module(RenderCore
    SOURCES
    Source/Runtime/RenderCore/Private/FShadowMath.cpp
    Source/Runtime/RenderCore/Private/FPerspectiveCamera.cpp
    Source/Runtime/RenderCore/Private/FShaderLibrary.cpp
    Source/Runtime/RenderCore/Private/FMeshPrimitives.cpp
    Source/Runtime/RenderCore/Private/FZoneVisibility.cpp
    DEPENDS Core RHI ShaderCompiler
)

# Asset types and their manager: meshes, skeletons, animation, textures, materials. No importers
# (those are in the pipeline library) and nothing about worlds.
leon_add_module(Assets
    SOURCES
    Source/Runtime/Assets/Private/UAssetManager.cpp
    Source/Runtime/Assets/Private/UStaticMesh.cpp
    Source/Runtime/Assets/Private/FMeshSimplifier.cpp
    Source/Runtime/Assets/Private/USkeleton.cpp
    Source/Runtime/Assets/Private/USkeletalMesh.cpp
    Source/Runtime/Assets/Private/UAnimSequence.cpp
    Source/Runtime/Assets/Private/UBlendSpace.cpp
    Source/Runtime/Assets/Private/FAnimRuntime.cpp
    Source/Runtime/Assets/Private/FAssetManifest.cpp
    Source/Runtime/Assets/Private/FEngineBuiltins.cpp
    Source/Runtime/Assets/Private/FHDRImporter.cpp
    Source/Runtime/Assets/Private/FTextureCompression.cpp
    Source/Runtime/Assets/Private/FTextureImporter.cpp
    Source/Runtime/Assets/Private/FImageFileLoader.cpp
    Source/Runtime/Assets/Private/FAssetExporter.cpp
    Source/Runtime/Assets/Private/FLightmapAsset.cpp
    Source/Runtime/Assets/Private/FMaterial.cpp
    Source/Runtime/Assets/Private/FMaterialInstance.cpp
    Source/Runtime/Assets/Private/FMaterialSerializer.cpp
    DEPENDS Core RHI RenderCore
    PRIVATE_LIBRARIES stb_image
)
target_include_directories(LeonModule_Assets PRIVATE "${LEON_ENGINE_ROOT}/Engine/ThirdParty/stb")

# The audio device and sound data. The component that plays sounds on an actor is in Gameplay.
leon_add_module(Audio
    SOURCES
    Source/Runtime/Audio/Private/FAudioDevice.cpp
    Source/Runtime/Audio/Private/USoundWave.cpp
    DEPENDS Core
    PRIVATE_LIBRARIES miniaudio
)

# The physics interface a backend implements (Jolt is a plugin), collision vocabulary and physics
# assets. The engine's own simple scene and the world queries live with the world.
leon_add_module(Physics
    SOURCES
    Source/Runtime/Physics/Private/FPhysicsModule.cpp
    Source/Runtime/Physics/Private/UPhysicalMaterial.cpp
    Source/Runtime/Physics/Private/UPhysicsAsset.cpp
    DEPENDS Core Assets
)

# What a world and its renderer share: the components entities are drawn from, and the small view of
# a world the renderer draws through (IRenderScene). Headers only.
add_library(LeonModule_Scene INTERFACE)
add_library(Leon::Module::Scene ALIAS LeonModule_Scene)
target_include_directories(LeonModule_Scene INTERFACE "${LEON_RUNTIME_ROOT}/Scene/Public")
target_link_libraries(LeonModule_Scene INTERFACE
    LeonModule_Core LeonModule_RHI LeonModule_RenderCore LeonModule_Assets LeonModule_Physics EnTT::EnTT)

# The world renderer, post-processing, text, particles and debug drawing. It draws an IRenderScene:
# it does not know about worlds, actors or gameplay.
leon_add_module(Renderer
    SOURCES
    Source/Runtime/Renderer/Private/FWorldRenderer.cpp
    Source/Runtime/Renderer/Private/FWorldRendererRHI.cpp
    Source/Runtime/Renderer/Private/FPostProcessRHI.cpp
    Source/Runtime/Renderer/Private/FImmediateDrawRHI.cpp
    Source/Runtime/Renderer/Private/FIBLGenerator.cpp
    Source/Runtime/Renderer/Private/FRenderDebugHotkeys.cpp
    Source/Runtime/Renderer/Private/FDebugOverlay.cpp
    Source/Runtime/Renderer/Private/FDebugRenderer.cpp
    Source/Runtime/Renderer/Private/FParticleRenderer.cpp
    Source/Runtime/Renderer/Private/FSpriteParticleBatch.cpp
    Source/Runtime/Renderer/Private/FTextRenderer.cpp
    DEPENDS Core Platform RHI RenderCore Assets Scene
    PRIVATE_LIBRARIES stb_image
)
target_include_directories(LeonModule_Renderer PRIVATE "${LEON_ENGINE_ROOT}/Engine/ThirdParty/stb")

# The application: window, main loop, layers, and the start and stop of the engine's subsystems.
leon_add_module(Application
    SOURCES
    Source/Runtime/Application/Private/FApplication.cpp
    Source/Runtime/Application/Private/FLayerStack.cpp
    DEPENDS Core Platform RHI RenderCore Assets Physics Renderer
)

# The UI library: widgets, layout and their drawing. It reaches the screen through IWidgetViewport,
# which the HUD implements.
leon_add_module(UMG
    SOURCES
    Source/Runtime/UMG/Private/UWidget.cpp
    Source/Runtime/UMG/Private/UPanelWidget.cpp
    Source/Runtime/UMG/Private/UCanvasPanel.cpp
    Source/Runtime/UMG/Private/UTextBlock.cpp
    Source/Runtime/UMG/Private/UEditableText.cpp
    Source/Runtime/UMG/Private/UButton.cpp
    Source/Runtime/UMG/Private/UUserWidget.cpp
    Source/Runtime/UMG/Private/UImage.cpp
    Source/Runtime/UMG/Private/FUIRenderer.cpp
    Source/Runtime/UMG/Private/FUILayout.cpp
    Source/Runtime/UMG/Private/UProgressBar.cpp
    Source/Runtime/UMG/Private/UHorizontalBox.cpp
    Source/Runtime/UMG/Private/UVerticalBox.cpp
    Source/Runtime/UMG/Private/USlider.cpp
    Source/Runtime/UMG/Private/UCheckBox.cpp
    Source/Runtime/UMG/Private/UWidgetSwitcher.cpp
    Source/Runtime/UMG/Private/UScrollBox.cpp
    Source/Runtime/UMG/Private/UTableView.cpp
    Source/Runtime/UMG/Private/ULoadingSpinner.cpp
    DEPENDS Core Platform RHI RenderCore Assets Scene Renderer Application
    PRIVATE_LIBRARIES stb_image
)
target_include_directories(LeonModule_UMG PRIVATE "${LEON_ENGINE_ROOT}/Engine/ThirdParty/stb")

# --- Everything above them still builds as one library. These modules include each other in
# --- cycles (see Docs/adr); they are split one by one as the cycles are removed.
set(LEON_ENGINE_SOURCES
    Source/Runtime/Gameplay/Private/UAudioComponent.cpp
    Source/Runtime/Engine/Private/UEngine.cpp
    Source/Runtime/Engine/Private/UEngineTravel.cpp
    Source/Runtime/Engine/Private/FGameplaySession.cpp
    Source/Runtime/Engine/Private/UWorld.cpp
    Source/Runtime/Engine/Private/FParticleSimulation.cpp
    Source/Runtime/Engine/Private/UGameInstance.cpp
    Source/Runtime/Engine/Private/FTimerManager.cpp
    Source/Runtime/Engine/Private/FMapSerializer.cpp
    Source/Runtime/Engine/Private/FMapSerializerDeserialize.cpp
    Source/Runtime/Engine/Private/UNetDriver.cpp
    Source/Runtime/Engine/Private/ULoopbackNetDriver.cpp
    Source/Runtime/Engine/Private/UIpNetDriver.cpp
    Source/Runtime/Engine/Private/FGraphicsQuality.cpp
    Source/Runtime/Engine/Private/FLightmapBakeInputs.cpp
    Source/Runtime/Engine/Private/FLightProbeAsset.cpp
    Source/Runtime/Gameplay/Private/AActor.cpp
    Source/Runtime/Gameplay/Private/ACameraActor.cpp
    Source/Runtime/Gameplay/Private/ACharacter.cpp
    Source/Runtime/Gameplay/Private/ADefaultPawn.cpp
    Source/Runtime/Gameplay/Private/AGameModeBase.cpp
    Source/Runtime/Gameplay/Private/AGameMode.cpp
    Source/Runtime/Gameplay/Private/AGameStateBase.cpp
    Source/Runtime/Gameplay/Private/AGameState.cpp
    Source/Runtime/Gameplay/Private/AHUD.cpp
    Source/Runtime/Gameplay/Private/APawn.cpp
    Source/Runtime/Gameplay/Private/APlayerCameraManager.cpp
    Source/Runtime/Gameplay/Private/APlayerController.cpp
    Source/Runtime/Gameplay/Private/APlayerState.cpp
    Source/Runtime/Gameplay/Private/APlayerStart.cpp
    Source/Runtime/Gameplay/Private/AWorldSettings.cpp
    Source/Runtime/Gameplay/Private/UGameplayStatics.cpp
    Source/Runtime/Gameplay/Private/FOnScreenDebugMessage.cpp
    Source/Runtime/Gameplay/Private/FGameplayTag.cpp
    Source/Runtime/Gameplay/Private/UActorComponent.cpp
    Source/Runtime/Gameplay/Private/USceneComponent.cpp
    Source/Runtime/Gameplay/Private/UHealthComponent.cpp
    Source/Runtime/Gameplay/Private/UCombatComponent.cpp
    Source/Runtime/Gameplay/Private/UInventoryComponent.cpp
    Source/Runtime/Gameplay/Private/FControlInput.cpp
    Source/Runtime/Gameplay/Private/FProceduralPrimitiveSpawner.cpp
    Source/Runtime/Gameplay/Private/UFootstepComponent.cpp
    Source/Runtime/Gameplay/Private/UClassRegistry.cpp
    Source/Runtime/Gameplay/Private/UAnimInstance.cpp
    Source/Runtime/Gameplay/Private/USkeletalMeshComponent.cpp
    Source/Runtime/Gameplay/Private/FAnimStateMachine.cpp
    Source/Runtime/Engine/Private/FSimplePhysicsScene.cpp
    Source/Runtime/Engine/Private/FCollisionQuery.cpp
    Source/Runtime/Gameplay/Private/UPrimitiveComponent.cpp
    Source/Runtime/Gameplay/Private/UNavMovementComponent.cpp
    Source/Runtime/Gameplay/Private/UCharacterMovementComponent.cpp
    Source/Runtime/Gameplay/Private/UProjectileMovementComponent.cpp
    Source/Runtime/Gameplay/Private/AProjectile.cpp
    Source/Runtime/Gameplay/Private/APickup.cpp
    Source/Runtime/Gameplay/Private/ALaunchPad.cpp
    Source/Runtime/Gameplay/Private/AWeaponBase.cpp
    Source/Runtime/Gameplay/Private/USpringArmComponent.cpp
    Source/Runtime/Gameplay/Private/UParticleComponent.cpp
    Source/Runtime/Gameplay/Private/AController.cpp
    Source/Runtime/Gameplay/Private/AAIController.cpp
    Source/Runtime/Gameplay/Private/ABlockingVolume.cpp
    Source/Runtime/Gameplay/Private/ASkyLight.cpp
    Source/Runtime/Gameplay/Private/AReflectionProbe.cpp
    Source/Runtime/Gameplay/Private/AVisibilityZone.cpp
    Source/Runtime/Gameplay/Private/APortal.cpp
    Source/Runtime/Gameplay/Private/ATriggerVolume.cpp
    Source/Runtime/Gameplay/Private/APhysicsVolume.cpp
    Source/Runtime/Gameplay/Private/ANavMeshBoundsVolume.cpp
    Source/Runtime/Gameplay/Private/FGameplayDebugger.cpp
    Source/Runtime/AI/Private/UBlackboardData.cpp
    Source/Runtime/AI/Private/UBlackboardComponent.cpp
    Source/Runtime/AI/Private/UBehaviorTree.cpp
    Source/Runtime/AI/Private/UBehaviorTreeComponent.cpp
    Source/Runtime/AI/Private/UNavigationSystem.cpp
    Source/Runtime/AI/Private/UPathFollowingComponent.cpp
    Source/Runtime/AI/Private/UAIPerceptionComponent.cpp
)

add_library(LeonEngineCore STATIC ${LEON_ENGINE_SOURCES})
add_library(Leon::Core ALIAS LeonEngineCore)
add_library(Leon::Engine ALIAS LeonEngineCore)
add_library(Leon::RHI ALIAS LeonEngineCore)
add_library(Leon::Assets ALIAS LeonEngineCore)
add_library(Leon::Renderer ALIAS LeonEngineCore)
add_library(Leon::UMG ALIAS LeonEngineCore)
add_library(Leon::Gameplay ALIAS LeonEngineCore)

target_include_directories(LeonEngineCore
    PUBLIC
        "${LEON_RUNTIME_ROOT}/Engine/Public"
        "${LEON_RUNTIME_ROOT}/Gameplay/Public"
        "${LEON_RUNTIME_ROOT}/AI/Public"
        "${LEON_ENGINE_ROOT}/Engine/ThirdParty/stb"
)

target_compile_features(LeonEngineCore PUBLIC cxx_std_20)

target_link_libraries(LeonEngineCore
    PUBLIC
        LeonModule_Core
        LeonModule_Platform
        LeonModule_RHI
        LeonModule_ShaderCompiler
        LeonModule_RenderCore
        LeonModule_Assets
        LeonModule_Audio
        LeonModule_Physics
        LeonModule_Scene
        LeonModule_Renderer
        LeonModule_Application
        LeonModule_UMG
        glfw
        glm::glm
        EnTT::EnTT
        stb_image
        miniaudio
)

if(WIN32)
    target_link_libraries(LeonEngineCore PRIVATE psapi dxgi ws2_32)
endif()

# Development only: importers and the light baker. Tools, the editor and tests link it; games do not.
set(LEON_PIPELINE_SOURCES
    Source/Developer/AssetPipeline/Private/FMaterialImporter.cpp
    Source/Developer/AssetPipeline/Private/FMeshImporter.cpp
    Source/Developer/AssetPipeline/Private/FSkeletalImporter.cpp
    Source/Developer/AssetPipeline/Private/FLightmapUV.cpp
    Source/Developer/AssetPipeline/Private/FEnginePrimitiveImport.cpp
    Source/Developer/Lightmass/Private/FLightmapBuilder.cpp
    Source/Developer/Lightmass/Private/FLightBaker.cpp
    Source/Developer/Lightmass/Private/FLightmass.cpp
)

add_library(LeonEnginePipeline STATIC ${LEON_PIPELINE_SOURCES})
add_library(Leon::Pipeline ALIAS LeonEnginePipeline)

target_include_directories(LeonEnginePipeline
    PUBLIC
        "${LEON_DEVELOPER_ROOT}/AssetPipeline/Public"
        "${LEON_DEVELOPER_ROOT}/Lightmass/Public"
        "${LEON_ENGINE_ROOT}/Engine/ThirdParty/ufbx"
)

target_include_directories(LeonEnginePipeline PRIVATE "${LEON_RUNTIME_ROOT}/Assets/Private")
target_compile_features(LeonEnginePipeline PUBLIC cxx_std_20)

target_link_libraries(LeonEnginePipeline
    PUBLIC
        LeonEngineCore
        ufbx
)

leon_apply_compile_options(LeonEngineCore LeonEnginePipeline)
