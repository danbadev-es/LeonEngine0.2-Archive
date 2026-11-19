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
    Renderer/LightSelectionTests.cpp
    Core/FrameProfilerTests.cpp
    RHI/RHINullTests.cpp
    RHI/RHID3D12Tests.cpp
    GPU/WorldRendererTests.cpp
    Lightmass/BakerAccelerationTests.cpp
    Asset/TextureCompressionTests.cpp
    Core/MulticastDelegateTests.cpp
    Lightmass/BakeHashTests.cpp
    RHI/ShaderConstantLayoutTests.cpp
    Shader/PostProcessPipelineTests.cpp
    Asset/MeshImportTests.cpp
    Asset/StaticMeshLODTests.cpp
    GPU/TextureUploadTests.cpp
    Renderer/GoldenSceneTests.cpp
    Renderer/ViewportResizeTests.cpp
    Shader/PBRShaderTangentSpaceTests.cpp
    Shader/PostProcessSSAOTests.cpp
    Shader/SkyboxEquirectSeamTests.cpp
    Shader/PostProcessFXAATests.cpp
    Shader/PostProcessBloomTests.cpp
    Shader/PostProcessToneMappingTests.cpp
    Shader/PostProcessLookTests.cpp
    Shader/PBRShaderIBLTests.cpp
    Shader/PBRShaderReflectionProbeTests.cpp
    Shader/PBRShaderPlanarReflectionTests.cpp
    Shader/PBRShaderPlanarHDRTests.cpp
    Shader/ShadowBiasTests.cpp
    Shader/ShadowPCFTests.cpp
    Shader/PBRShaderEmissiveTests.cpp
    Shader/ShadowSelectionTests.cpp
    Shader/PBRShaderAlphaTests.cpp
    Shader/ShadowPointTests.cpp
    Shader/PBRShaderNormalMappingTests.cpp
    Shader/PBRShaderColorSpaceTests.cpp
    Shader/PBRShaderMaterialTextureTests.cpp
    Shader/PBRShaderFresnelTests.cpp
    Shader/PBRShaderShadowTests.cpp
    Shader/PBRShaderFogTests.cpp
    Shader/ShadowCascadeTests.cpp
    Shader/PBRShaderGGXSmithTests.cpp
    Shader/PBRShaderDirectLightsTests.cpp
    Shader/ShadowAtlasTests.cpp
    Shader/PBRShaderDeterminismTests.cpp
    Shader/PBRShaderUVTransformTests.cpp
    Shader/PBRShaderCookTorranceTests.cpp
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
        LeonModule_RHINull
        LeonModule_RHID3D12
        LeonModule_ShaderCompiler
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
# One executable, registered three times so CTest can run a kind on its own (ctest -L unit|gpu|golden).
# The suites that create a Direct3D 12 device (on WARP, so they need no graphics card):
set(LEON_GPU_TEST_SUITES "Shader GPU*,World renderer*,RHI Direct3D 12*,GPU - *,Renderer pipeline*,Lightmap bake hash")
set(LEON_GOLDEN_TEST_SUITES "Renderer golden*")
add_test(NAME EngineTests.unit
    COMMAND RendererTests "--test-suite-exclude=${LEON_GPU_TEST_SUITES},${LEON_GOLDEN_TEST_SUITES}"
    WORKING_DIRECTORY ${LEON_ENGINE_ROOT})
add_test(NAME EngineTests.gpu
    COMMAND RendererTests "--test-suite=${LEON_GPU_TEST_SUITES}"
    WORKING_DIRECTORY ${LEON_ENGINE_ROOT})
add_test(NAME EngineTests.golden
    COMMAND RendererTests "--test-suite=${LEON_GOLDEN_TEST_SUITES}"
    WORKING_DIRECTORY ${LEON_ENGINE_ROOT})
set_tests_properties(EngineTests.unit PROPERTIES LABELS "unit")
set_tests_properties(EngineTests.gpu PROPERTIES LABELS "gpu")
set_tests_properties(EngineTests.golden PROPERTIES LABELS "golden")
add_custom_target(EngineTests DEPENDS RendererTests)

leon_apply_compile_options(RendererTests)
