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
ShaderCompiler, RenderCore, Assets, Audio, Physics, Scene, Renderer, Application, UMG. Importers and the
light baker are a development-only library (AssetPipeline, Lightmass) that games do not link.

How the upper ones were separated:

- **Assets**: materials moved in (they are assets, and the asset manager caches them); the asset
  manager stopped knowing about worlds.
- **Renderer**: it used a world for four things (its registry, whether its lightmaps are trusted, the
  world matrix of an actor's entity, gameplay debug drawing). Those are `IRenderScene`, in `Scene`,
  which the world implements.
- **UMG**: a user widget asked its owning player's HUD to show it. That is `IWidgetViewport`, declared
  by UMG and implemented by the HUD.
- **Application**: needed nothing once the renderer was below it.

Still one library (`LeonEngineCore`): Engine, Gameplay and AI. This is not a leftover of the same kind.
`Engine` includes `Gameplay` in a hundred places and `Gameplay` includes `Engine/UWorld.hpp` in
thirty: the world spawns and ticks actors, the map serializer knows every actor class, and actors
reach their world for everything. The world also keeps the list of AI controllers, ticks and
replicates them, and owns the navigation system, which in turn walks the world's actors. World,
actor and controller are one design, as they are in the engine this one is modelled on.

Splitting them further would mean redesigning the world's loop (controllers and navigation behind
interfaces, actor classes created only through the registry), not moving dependencies. That is a
change of behaviour-carrying code in the three games and should be decided on its own merits, not as
part of a build clean-up.
