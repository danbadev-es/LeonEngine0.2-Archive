# GPU tests waiting for the Direct3D 12 test device

These suites render with a GPU. They were written against a hidden OpenGL context
(`HeadlessGLContext.hpp`) and left the build when the OpenGL backend was deleted. They come back
in phase 6 of the migration, running on a D3D12 device over WARP: the checks stay, the setup is
rewritten.

34 files, 75 test cases.

| File | Test cases |
| :--- | ---: |
| `Tests/Asset/MeshImportTests.cpp` | 7 |
| `Tests/Asset/StaticMeshLODTests.cpp` | 8 |
| `Tests/GPU/TextureUploadTests.cpp` | 1 |
| `Tests/Renderer/GoldenSceneTests.cpp` | 1 |
| `Tests/Renderer/ViewportResizeTests.cpp` | 2 |
| `Tests/Shader/PBRShaderAlphaTests.cpp` | 1 |
| `Tests/Shader/PBRShaderColorSpaceTests.cpp` | 2 |
| `Tests/Shader/PBRShaderCookTorranceTests.cpp` | 1 |
| `Tests/Shader/PBRShaderDeterminismTests.cpp` | 1 |
| `Tests/Shader/PBRShaderDirectLightsTests.cpp` | 3 |
| `Tests/Shader/PBRShaderEmissiveTests.cpp` | 1 |
| `Tests/Shader/PBRShaderFogTests.cpp` | 4 |
| `Tests/Shader/PBRShaderFresnelTests.cpp` | 1 |
| `Tests/Shader/PBRShaderGGXSmithTests.cpp` | 2 |
| `Tests/Shader/PBRShaderIBLTests.cpp` | 1 |
| `Tests/Shader/PBRShaderMaterialTextureTests.cpp` | 2 |
| `Tests/Shader/PBRShaderNormalMappingTests.cpp` | 2 |
| `Tests/Shader/PBRShaderPlanarHDRTests.cpp` | 4 |
| `Tests/Shader/PBRShaderPlanarReflectionTests.cpp` | 3 |
| `Tests/Shader/PBRShaderShadowTests.cpp` | 2 |
| `Tests/Shader/PBRShaderTangentSpaceTests.cpp` | 2 |
| `Tests/Shader/PBRShaderUVTransformTests.cpp` | 1 |
| `Tests/Shader/PostProcessBloomTests.cpp` | 2 |
| `Tests/Shader/PostProcessFXAATests.cpp` | 3 |
| `Tests/Shader/PostProcessPipelineTests.cpp` | 2 |
| `Tests/Shader/PostProcessSSAOTests.cpp` | 2 |
| `Tests/Shader/PostProcessToneMappingTests.cpp` | 2 |
| `Tests/Shader/ShadowAtlasTests.cpp` | 1 |
| `Tests/Shader/ShadowBiasTests.cpp` | 1 |
| `Tests/Shader/ShadowCascadeTests.cpp` | 6 |
| `Tests/Shader/ShadowPCFTests.cpp` | 1 |
| `Tests/Shader/ShadowPointTests.cpp` | 1 |
| `Tests/Shader/ShadowSelectionTests.cpp` | 1 |
| `Tests/Shader/SkyboxEquirectSeamTests.cpp` | 1 |
