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
    Source/Runtime/RHI/Private/FNullRenderDriver.cpp
    Source/Runtime/RHI/Private/FRHI.cpp
    Source/Runtime/RHI/Private/FFrameProfiler.cpp
    Source/Runtime/RHI/Private/IGraphicsContext.cpp
    Source/Runtime/RHI/Private/IRenderAPI.cpp
    Source/Runtime/RHI/Private/IRenderDriver.cpp
    Source/Runtime/RHI/Private/FRenderCommand.cpp
    Source/Runtime/RHI/Private/FBuffer.cpp
    Source/Runtime/RHI/Private/FFramebuffer.cpp
    Source/Runtime/RHI/Private/FShader.cpp
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

# --- Everything above them still builds as one library. These modules include each other in
# --- cycles (see Docs/adr); they are split one by one as the cycles are removed.
set(LEON_ENGINE_SOURCES
    Source/Runtime/Application/Private/FApplication.cpp
    Source/Runtime/Application/Private/FLayerStack.cpp
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
    Source/Runtime/Assets/Private/FTextureImporter.cpp
    Source/Runtime/Assets/Private/FAssetExporter.cpp
    Source/Runtime/Assets/Private/FLightmapAsset.cpp
    Source/Runtime/Renderer/Private/FWorldRenderer.cpp
    Source/Runtime/Renderer/Private/FWorldRendererLighting.cpp
    Source/Runtime/Renderer/Private/FWorldRendererGeometry.cpp
    Source/Runtime/Renderer/Private/FWorldRendererPostProcess.cpp
    Source/Runtime/Renderer/Private/FMaterial.cpp
    Source/Runtime/Renderer/Private/FMaterialInstance.cpp
    Source/Runtime/Renderer/Private/FMeshPrimitives.cpp
    Source/Runtime/Renderer/Private/FPerspectiveCamera.cpp
    Source/Runtime/Renderer/Private/FPostProcessPipeline.cpp
    Source/Runtime/Renderer/Private/FIBLGenerator.cpp
    Source/Runtime/Renderer/Private/FRenderDebugHotkeys.cpp
    Source/Runtime/Renderer/Private/FDebugOverlay.cpp
    Source/Runtime/Renderer/Private/FDebugRenderer.cpp
    Source/Runtime/Renderer/Private/FParticleRenderer.cpp
    Source/Runtime/Renderer/Private/FTextRenderer.cpp
    Source/Runtime/Renderer/Private/FShadowMath.cpp
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
    Source/Runtime/Audio/Private/FAudioDevice.cpp
    Source/Runtime/Audio/Private/USoundWave.cpp
    Source/Runtime/Audio/Private/UAudioComponent.cpp
    Source/Runtime/Engine/Private/UEngine.cpp
    Source/Runtime/Engine/Private/UEngineTravel.cpp
    Source/Runtime/Engine/Private/FGameplaySession.cpp
    Source/Runtime/Engine/Private/UWorld.cpp
    Source/Runtime/Engine/Private/FParticleSimulation.cpp
    Source/Runtime/Engine/Private/UGameInstance.cpp
    Source/Runtime/Engine/Private/FTimerManager.cpp
    Source/Runtime/Engine/Private/FMapSerializer.cpp
    Source/Runtime/Engine/Private/FMapSerializerDeserialize.cpp
    Source/Runtime/Engine/Private/FMaterialSerializer.cpp
    Source/Runtime/Engine/Private/UNetDriver.cpp
    Source/Runtime/Engine/Private/ULoopbackNetDriver.cpp
    Source/Runtime/Engine/Private/UIpNetDriver.cpp
    Source/Runtime/Engine/Private/FGraphicsQuality.cpp
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
    Source/Runtime/Physics/Private/FPhysicsModule.cpp
    Source/Runtime/Physics/Private/FSimplePhysicsScene.cpp
    Source/Runtime/Physics/Private/FCollisionQuery.cpp
    Source/Runtime/Physics/Private/UPhysicalMaterial.cpp
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
    Source/Runtime/Assets/Private/UPhysicsAsset.cpp
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
        "${LEON_RUNTIME_ROOT}/Application/Public"
        "${LEON_RUNTIME_ROOT}/Assets/Public"
        "${LEON_RUNTIME_ROOT}/Renderer/Public"
        "${LEON_RUNTIME_ROOT}/UMG/Public"
        "${LEON_RUNTIME_ROOT}/Engine/Public"
        "${LEON_RUNTIME_ROOT}/Gameplay/Public"
        "${LEON_RUNTIME_ROOT}/Physics/Public"
        "${LEON_RUNTIME_ROOT}/AI/Public"
        "${LEON_RUNTIME_ROOT}/Audio/Public"
        "${LEON_ENGINE_ROOT}/Engine/ThirdParty/stb"
    PRIVATE
        "${LEON_RUNTIME_ROOT}/Lightmass/Public"
)

target_compile_features(LeonEngineCore PUBLIC cxx_std_20)

target_link_libraries(LeonEngineCore
    PUBLIC
        LeonModule_Core
        LeonModule_Platform
        LeonModule_RHI
        glfw
        glm::glm
        EnTT::EnTT
        stb_image
        miniaudio
)

if(WIN32)
    target_link_libraries(LeonEngineCore PRIVATE psapi dxgi ws2_32)
endif()

set(LEON_PIPELINE_SOURCES
    Source/Runtime/Assets/Private/FMaterialImporter.cpp
    Source/Runtime/Assets/Private/FMeshImporter.cpp
    Source/Runtime/Assets/Private/FSkeletalImporter.cpp
    Source/Runtime/Assets/Private/FLightmapUV.cpp
    Source/Runtime/Lightmass/Private/FLightmapBuilder.cpp
    Source/Runtime/Lightmass/Private/FLightBaker.cpp
    Source/Runtime/Lightmass/Private/FLightmass.cpp
)

add_library(LeonEnginePipeline STATIC ${LEON_PIPELINE_SOURCES})
add_library(Leon::Pipeline ALIAS LeonEnginePipeline)

target_include_directories(LeonEnginePipeline
    PUBLIC
        "${LEON_RUNTIME_ROOT}/Lightmass/Public"
        "${LEON_RUNTIME_ROOT}/Assets/Public"
        "${LEON_ENGINE_ROOT}/Engine/ThirdParty/ufbx"
)

target_compile_features(LeonEnginePipeline PUBLIC cxx_std_20)

target_link_libraries(LeonEnginePipeline
    PUBLIC
        LeonEngineCore
        ufbx
)

leon_apply_compile_options(LeonEngineCore LeonEnginePipeline)
