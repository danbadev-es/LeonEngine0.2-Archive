# LeonEngine2 — Testing Architecture

## Binaries

| CMake target | Role | Location | Links Projects? |
| :--- | :--- | :--- | :---: |
| `RendererTests` | Engine unit/integration/GPU suite | `Tests/` (`out/Engine/`) | No |
| `LeonTournamentTests` | Product gameplay, combat, AI, UI, match | `Games/LeonTournament/Tests/` (`out/Games/LeonTournament/`) | Project only |

`python Tools/run_tests.py` builds and runs the Engine test suite (`RendererTests`).
Product gameplay tests are built and executed via project scripts: `python Games/LeonTournament/Scripts/test.py`.

## Layout

```text
Tests/
  Main.cpp                 Shared doctest entry
  Math/ IBL/ PBR/ HDR/ Cache/ GPU/ Shader/ Asset/ Lightmass/ Renderer/
  Gameplay/                Engine gameplay framework (not the product)

Games/LeonTournament/Tests/
  Main.cpp                 Project test entry with class and driver bindings
  *Tests.cpp               Product gameplay, combat, anim, bot, UI tests
```

Engine tests must not include project headers (`Projects/...`) or compile game `.cpp` files.

## Skip policy

- **Zero skipped tests** in the default suite.
- This vendored doctest has no runtime `SKIP()` macro. A test that needs a device `REQUIRE`s it and fails
  when there is none; that is the only honest result without a skip API. Do not reintroduce `MESSAGE` + `return`.
- Missing fixtures `REQUIRE` the file. Obsolete fixtures are deleted or retargeted (`DaySky1k.lhdr` replaced the missing `AutumnField1k.lhdr`).

`Tools/run_tests.py` treats `skipped > 0` as failure.

## GPU / renderer tests

GPU tests run on WARP, the software Direct3D 12 adapter, with the debug layer on. They need no graphics
card, so they run anywhere, including CI. `LEON_TEST_GPU=1` runs them on the machine's adapter instead.

| Suite | File | What it covers |
| :--- | :--- | :--- |
| RHI contract | `Tests/RHI/RHIContractTests.hpp`, `RHINullTests.cpp` | Buffers, textures and swapchain behave the same on every backend |
| Direct3D 12 backend | `Tests/RHI/RHID3D12Tests.cpp` | Triangles, winding, constants per draw, sampling, depth, pass-to-pass sampling, lifetime, shader errors |
| World renderer | `Tests/GPU/WorldRendererTests.cpp` | Whole frames of a small scene: lighting, shadows, each post-process effect, planar reflections, 2D and world-space immediate drawing |
| Shaders | `Tests/Shader/*.cpp`, `Tests/Renderer/GoldenSceneTests.cpp` | One quad or one full-screen pass per check: each term of the surface shader (Fresnel, GGX, fog, normal mapping, IBL, planar reflections), shadow sampling, and each post-process shader |
| Post-process chain | `Tests/Shader/PostProcessPipelineTests.cpp` | `FPostProcessRHI` from an HDR texture to the 8-bit image, and its targets through resizes |
| Device resources | `Tests/GPU/TextureUploadTests.cpp`, `Tests/Renderer/ViewportResizeTests.cpp`, `Tests/Asset/StaticMeshLODTests.cpp` | Half-float cube upload and readback, render targets at several sizes, video memory accounting, mesh buffers per LOD |
| A real level | `Games/LeonHorror/Tests/RenderTests.cpp` | The game's start room from the player's camera (character, ceiling light, flashlight on and off) against stored pictures in `Games/LeonHorror/Tests/Reference` |

Every GPU test also requires that the debug layer reported nothing. For the world renderer that covers
each shader, pipeline, render pass and resource state a frame uses.

The shader tests share `Tests/GPU/HeadlessDevice.hpp`. A test declares one `FHeadlessDevice` first; it
creates the device and destroys it, so cases do not depend on each other. The tests were written
against the OpenGL renderer and still name shader inputs as the GLSL shaders did (`u_Roughness`,
`u_UseShadows`); the harness translates those names to the constant buffers of the HLSL shaders, so
the assertions are the original ones. `ReadPixel` counts rows from where texture coordinate V = 0 is
drawn, as the tests always did.

A test that draws engine meshes or textures must register `FLegacyRenderDriver` so they become device
resources, and unregister it (`FRenderDriverRegistry::RegisterDriver(nullptr)`) when done: the rest of
the suite runs without a driver, where those factories return null. `FHeadlessDevice::UseEngineResources`
does both; the fixture in `WorldRendererTests.cpp` does it by hand.

Reference pictures are compared with a tolerance small enough to notice the character in a wrong pose
(checked by breaking skinning on purpose). When a change is meant to alter the picture, run the test
with `LEON_UPDATE_REFERENCE=1`, look at the new pictures, and commit them with the change. A failed
comparison leaves the frame it drew in `build/LeonHorrorTests/`.

When a shader test is added or ported, break the shader on purpose once and see the test fail. The
panorama seam test passed with the fix removed until its camera was turned by one pixel.

CPU math / IBL / cache suites live under `Tests/Math`, `Tests/IBL`, `Tests/PBR`, `Tests/Cache`, `Tests/HDR`.

Known gaps:

- Headless tests cannot judge whether a frame looks right. See "Checking a change" in [RENDERER.md](RENDERER.md).

## Behavioral bar

Tests must exercise behavior (movement, interpolation, spawn of the configured pawn, INI class resolution), not only “pointer is non-null”. Existence-only asserts are not added as new coverage.
