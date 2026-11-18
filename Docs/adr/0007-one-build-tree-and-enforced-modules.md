# 0007 — One CMake tree; modules are targets with declared dependencies

Date: 2026-11-18. Status: accepted, partly applied.

## Context

Every product (engine, editor, each game) configured and built its own copy of everything; `out/` held
6.3 GB. The eleven runtime modules compiled into one library, so nothing stopped include cycles: Core
included RHI and Renderer, RHI and Renderer included each other.

## Decision

- One build tree (`CMakePresets.json`: `dev-debug`, `dev-release`, `dev-relwithdebinfo`, `ci`, `asan`)
  builds engine, editor, tools, games and tests. Executables land in `out/build/<preset>/bin`.
- A module is a static library created with `leon_add_module(Name DEPENDS ...)`. It can include only its
  own headers and those of the modules it declares; an include pointing the wrong way does not compile.
- Layout: `Engine/Source/Runtime/<Module>/{Public/<Module>,Private}`, `Engine/Source/Developer`,
  `Engine/Source/Editor`, `Engine/Source/Programs`, `Games/`, `Samples/`, `Tests/`, `Tools/`.
- Games register themselves with `leon_register_game`; the editor gets its list of games from a table
  CMake generates, not from names in engine code.

## Status

Core, Platform, RHI, RHINull, RHID3D12 and ShaderCompiler are real modules. Assets, Renderer, Engine,
Gameplay, AI, Physics, Audio and UMG still build as one library (`LeonEngineCore`) and include each
other freely. Separating them was postponed until the renderer was ported; it is the next structural
step.
