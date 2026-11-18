# LeonEngine2 — Building

One CMake tree builds everything: engine, plugins, tools, tests, editor and games. Dependencies
are fetched and compiled once per tree.

## Presets

Defined in [`CMakePresets.json`](../CMakePresets.json). Each preset has its own tree under
`out/build/<preset>/`, and every executable lands in `out/build/<preset>/bin/` (the editor in
`bin/Editor/`, because it stages a copy of the engine resources next to itself).

| Preset | Configuration | Use |
| :--- | :--- | :--- |
| `dev-debug` | Debug | Day-to-day work |
| `dev-release` | Release | Performance checks, packaging |
| `dev-relwithdebinfo` | RelWithDebInfo | Profiling |
| `ci` | Release, warnings as errors | Continuous integration |
| `asan` | Debug with the address sanitizer | Memory bugs |

## Commands

From a shell with the MSVC environment (a "Developer" prompt), in the repository root:

```
cmake --preset dev-debug
cmake --build --preset dev-debug                      # everything
cmake --build --preset dev-debug --target LeonHorror  # one target
ctest --preset dev-debug
```

The Python scripts do the same and set up the MSVC environment themselves, so they work from any
shell:

| Script | Builds |
| :--- | :--- |
| `python Scripts/build_engine.py` | Engine tests and tools (`RendererTests`, `AssetTool`, `LightmassTool`, `ProjectTool`) |
| `python Scripts/build_editor.py [--run]` | `LeonEditor` |
| `python Scripts/build_project.py --project <path.lproject> [--run] [--target <name>]` | A game |
| `python Scripts/run_tests.py` | Builds and runs the engine test suite |

All of them take `--config Debug|Release|RelWithDebInfo`, `--clean` (delete the tree first) and
`--rebuild`.

## CMake options

| Option | Default | Meaning |
| :--- | :--- | :--- |
| `LEON_BUILD_EDITOR` | `ON` | Build the editor |
| `LEON_BUILD_TOOLS` | `ON` | Build the asset, lightmass and project tools |
| `LEON_BUILD_TESTS` | `ON` | Build test executables and register them with CTest |
| `LEON_GAMES` | `Sandbox;LeonTournament;LeonHorror` | Game folders under `Projects/` to build |
| `LEON_EXTERNAL_GAME_DIRS` | empty | Absolute paths of game projects outside this repository |
| `LEON_WARNINGS_AS_ERRORS` | `OFF` | Used by the `ci` preset |
| `LEON_ENABLE_ASAN` | `OFF` | Used by the `asan` preset |

## Running

Games and tests resolve engine resources relative to the working directory, so run them from the
repository root:

```
out/build/dev-debug/bin/LeonHorror.exe
out/build/dev-debug/bin/RendererTests.exe
```

The editor is run from its own folder (`out/build/dev-debug/bin/Editor`), which
`build_editor.py --run` does for you.
