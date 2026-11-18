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

Libraries of their own, each seeing only what it declares: Core, Platform, RHI, RHINull, RHID3D12,
ShaderCompiler, RenderCore, Assets, Audio, Physics.

Still one library (`LeonEngineCore`): Renderer, Engine, Gameplay, AI, UMG and Application. They include
each other in cycles, all of which pass through `UWorld` and `AActor`:

- `Engine` includes `Gameplay` in about a hundred places (the world spawns and ticks actors, the map
  serializer knows every actor class) and `Gameplay` includes `Engine/UWorld.hpp` in forty-five.
- The world renderer reads the world's registry and its actors directly, and the world owns its renderer.
- `UMG` widgets reach the world, the HUD and the player controller; the HUD (Gameplay) owns widgets.
- `AAIController` (Gameplay) uses the behaviour tree and navigation (AI), which use actors.

Separating them is not a matter of moving files, as the first four were. It needs dependencies turned
round, in roughly this order:

1. A `Scene` module with the data both sides share (the EnTT components in `Engine/Components.hpp`,
   mobility, particle and net types), below the renderer.
2. The world renderer drawing from that data (a registry and a small view of the scene) instead of from
   `UWorld`, which lets `Renderer` sit below `Engine`.
3. `UWorld` and `AActor` together in one module (they are one design), with the engine loop, travel and
   map loading above it.
4. `AI` and `UMG` reaching the world through interfaces declared below them, or moving above `Gameplay`
   with `AAIController` and `AHUD`.

Each step changes behaviour-carrying code in the three games and the editor, so each needs its own
round of tests and captures.
