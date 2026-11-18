# LeonEngine2 — Testing Architecture

## Binaries

| CMake target | Role | Location | Links Projects? |
| :--- | :--- | :--- | :---: |
| `RendererTests` | Engine unit/integration/GPU suite | `Tests/` (`out/Engine/`) | No |
| `LeonTournamentTests` | Product gameplay, combat, AI, UI, match | `Projects/LeonTournament/Tests/` (`out/Projects/LeonTournament/`) | Project only |

`python Scripts/run_tests.py` builds and runs the Engine test suite (`RendererTests`).
Product gameplay tests are built and executed via project scripts: `python Projects/LeonTournament/Scripts/test.py`.

## Layout

```text
Tests/
  Main.cpp                 Shared doctest entry
  Math/ IBL/ PBR/ HDR/ Cache/ GPU/ Shader/ Asset/ Lightmass/ Renderer/
  Gameplay/                Engine gameplay framework (not the product)

Projects/LeonTournament/Tests/
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

Every GPU test also requires that the debug layer reported nothing. For the world renderer that covers
each shader, pipeline, render pass and resource state a frame uses.

A test that draws must register `FLegacyRenderDriver` so meshes and textures become device resources,
and unregister it (`FRenderDriverRegistry::RegisterDriver(nullptr)`) when done: the rest of the suite
runs without a driver, where those factories return null. See the fixture in `WorldRendererTests.cpp`.

CPU math / IBL / cache suites live under `Tests/Math`, `Tests/IBL`, `Tests/PBR`, `Tests/Cache`, `Tests/HDR`.

Known gaps:

- 34 test files written against the OpenGL renderer (shader-level checks of PBR terms, shadows,
  post-processing, golden constraints) are listed in `Tests/GPU/PENDING.md` and are not built. Their
  assertions are still the right ones; their set-up has to be rewritten for the RHI.
- No test draws a real game level. A golden image of the LeonHorror start room would have caught the
  bugs that started the Direct3D 12 migration.
- Headless tests cannot judge whether a frame looks right. See "Checking a change" in [RENDERER.md](RENDERER.md).

## Behavioral bar

Tests must exercise behavior (movement, interpolation, spawn of the configured pawn, INI class resolution), not only “pointer is non-null”. Existence-only asserts are not added as new coverage.
