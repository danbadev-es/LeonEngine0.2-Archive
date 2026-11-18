# LeonEngine2

Unreal Engine–inspired C++20 game engine for solo / small-team development. Forward PBR (OpenGL 4.5), native asset pipeline, and a thin gameplay framework (`UWorld`, GameMode, HUD/UI).

## Quick start

One build tree (`out/build/<preset>`) holds the engine, editor, tools, tests and games. See
[Docs/BUILD.md](Docs/BUILD.md).

```bash
# Engine tests and tools
python Tools/build_engine.py --config Debug

# Editor
python Tools/build_editor.py --run

# A game: build, or build and run
python Tools/build_project.py --project Games/LeonHorror/LeonHorror.lproject
python Tools/run_project.py --project Games/LeonHorror/LeonHorror.lproject

# Engine test suite
python Tools/run_tests.py

# Validate project assets
python Tools/validate_project.py --project Samples/Sandbox/Sandbox.lproject
```

## Layout

| Path | Role |
|------|------|
| `Engine/Source/Runtime/` | Engine modules that ship with a game |
| `Engine/Source/Editor/` | The editor (`LeonEditor`) |
| `Engine/Source/Programs/` | Stand-alone tools (`AssetTool`, `LightmassTool`, `ProjectTool`) |
| `Engine/Plugins/` | Backends and optional features (RHI, physics, networking) |
| `Engine/Resources/` | Engine shaders, fonts and built-in assets |
| `Engine/ThirdParty/` | Vendored dependencies |
| `Games/` | Game projects (`LeonHorror`, `LeonTournament`) |
| `Samples/` | Reference project (`Sandbox`) |
| `Tests/` | Engine test suite (`RendererTests`) |
| `Tools/` | Python scripts: build, run, assets, checks |
| `cmake/` | Shared CMake helpers |
| `Docs/` | Architecture, renderer, assets, decisions |
| `out/` | Build output (ignored) |

## Docs

- [Architecture](Docs/ARCHITECTURE.md)
- [Architecture Decisions & Best Practices](Docs/ARCHITECTURE_DECISIONS.md)
- [Product builds](Docs/BUILD.md)
- [Editor architecture](Docs/EDITOR_ARCHITECTURE.md)
- [Editor roadmap](Docs/EDITOR_ROADMAP.md)
- [Engine architecture](Docs/ENGINE_ARCHITECTURE.md)
- [Gameplay framework](Docs/GAMEPLAY_FRAMEWORK.md)
- [Engine / game boundary](Docs/ENGINE_GAME_BOUNDARY.md)
- [Testing architecture](Docs/TESTING_ARCHITECTURE.md)
- [Naming](Docs/NAMING.md)
- [Asset pipeline](Docs/ASSET_PIPELINE.md)
- [Renderer](Docs/RENDERER.md)

## Naming note

UE-style prefixes (`U`/`A`/`F`) are intentional. Runtime objects use `std::shared_ptr` + EnTT — there is **no** Unreal GC, reflection, or `NewObject`.
