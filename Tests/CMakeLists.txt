if(NOT DEFINED LEON_ENGINE_ROOT)
    get_filename_component(LEON_ENGINE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
endif()

set(LEON_ENGINE_TEST_SOURCES
    Main.cpp
    Math/VectorTBNTests.cpp
    Math/RendererContractTests.cpp
    Math/HammersleyTests.cpp
    Math/HemispherePDFTests.cpp
    IBL/IrradianceConvolutionTests.cpp
    IBL/ReferenceIntegratorTests.cpp
    IBL/SolarHDRRegressionTests.cpp
    IBL/IrradianceContinuityTests.cpp
    IBL/SpecularPrefilterTests.cpp
    PBR/BRDFLUTTests.cpp
    PBR/PBRBrdfTests.cpp
    PBR/EnergyConservationTests.cpp
    HDR/HDRDecodingTests.cpp
    HDR/MipChainTests.cpp
    Cache/SerializationTests.cpp
    Cache/CacheInvalidationTests.cpp
    GPU/CubemapSeamTests.cpp
    IBL/DeterminismTests.cpp
    Asset/AssetIDTests.cpp
    Asset/TextureImportTests.cpp
    Asset/HDRAssetTests.cpp
    Asset/AssetCacheUnloadTests.cpp
    Physics/TriangleMeshCollisionTests.cpp
    Physics/JoltPhysicsSceneTests.cpp
    Asset/SkeletalAnimationTests.cpp
    Asset/AssetValidationTests.cpp
    Gameplay/GameplayFrameworkTests.cpp
    Gameplay/GameplayLifecycleTests.cpp
    Gameplay/ProjectBootTests.cpp
    Gameplay/UIHUDTests.cpp
    Gameplay/GameplayTagTests.cpp
    Gameplay/InputSettingsTests.cpp
    Gameplay/CharacterComponentFrustumTests.cpp
    Gameplay/NetDriverTests.cpp
    Gameplay/EngineGameSeparationTests.cpp
    Gameplay/NamingConventionTests.cpp
    Gameplay/HealthComponentTests.cpp
    Gameplay/InventoryTests.cpp
    Gameplay/ActorLifetimeTests.cpp
    Gameplay/MapSerializerTests.cpp
    Gameplay/WorldPauseTests.cpp
    Gameplay/ControlSchemaTests.cpp
    Gameplay/OverlapAndDamageTests.cpp
    Gameplay/PhysicsMovementAITests.cpp
    Gameplay/AIPerceptionTests.cpp
    Gameplay/AudioDeviceTests.cpp
    Lightmass/StaticLightingTests.cpp
    Renderer/RenderDebugHotkeysTests.cpp
    Core/FrameProfilerTests.cpp
    Core/WindowDisplayPolicyTests.cpp
    Performance/ScalingBenchmarkTests.cpp
)

function(leon_configure_test_exe TargetName)
    target_include_directories(${TargetName} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${LEON_ENGINE_ROOT}/Engine/Plugins/Physics/Jolt/include
        ${LEON_ENGINE_ROOT}/Engine/Plugins/Networking/ENet/include
        ${LEON_ENGINE_ROOT}/Engine/ThirdParty/stb
        ${LEON_ENGINE_ROOT}/Engine/ThirdParty/doctest
        ${LEON_ENGINE_ROOT}/Engine/ThirdParty
    )
    target_link_libraries(${TargetName} PRIVATE
        LeonEngineCore
        LeonEnginePipeline
        LeonEngine_Jolt
        LeonEngine_ENet
        doctest
        glfw
        glm
        stb_image
    )
endfunction()

# Pure Engine test suite (unit / GPU / assets / lifecycle / math / PBR / IBL).
add_executable(RendererTests ${LEON_ENGINE_TEST_SOURCES})
leon_configure_test_exe(RendererTests)

enable_testing()
add_test(NAME EngineTests COMMAND RendererTests WORKING_DIRECTORY ${LEON_ENGINE_ROOT})
add_custom_target(EngineTests DEPENDS RendererTests)

leon_apply_compile_options(RendererTests)
