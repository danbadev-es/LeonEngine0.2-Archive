# LeonEngine2 — Architecture & System Design

This document details the architectural principles, dependency hierarchy, subsystem responsibilities, and extension guidelines for **LeonEngine2**.

---

## 1. Architectural Principles & Dependency Direction

Dependencies point one way: games and the editor use the engine; the engine never names a game.
Inside the engine, lower modules never include higher ones. Naming follows the Unreal Engine C++
coding standard ([NAMING.md](NAMING.md)). The decisions behind the current shape are in [adr/](adr/).

```text
 Games (LeonHorror, LeonTournament), Samples (Sandbox), Editor
                      │ use
                      ▼
 Launch ──► LeonEngineCore (Renderer, Engine, Gameplay, AI, UMG, Application)
                      │
                      ▼
        Assets · Audio · Physics ──► RenderCore ──► RHI ──► Core
                                                     ▲
                                  RHID3D12, RHINull ─┘        Platform ──► Core
```

### Modules

A module is a static library made with `leon_add_module(Name DEPENDS ...)` (`cmake/LeonModule.cmake`).
It sees its own headers and those of the modules it declares; an include pointing the wrong way does
not compile.

| Module | Depends on | Holds |
| :--- | :--- | :--- |
| `Core` | — | Log, config files, project paths, `UObject` base, events |
| `Platform` | Core | Window, input (GLFW stays inside this module) |
| `RHI` | Core | GPU interface (`IRHIDevice`, `IRHICommandList`), render statistics, the resource classes assets still use |
| `RHINull` | RHI | A backend with no GPU, for tests and tools |
| `RHID3D12` | RHI | The Direct3D 12 backend |
| `ShaderCompiler` (Developer) | RHI | HLSL to DXIL with DXC |
| `RenderCore` | Core, RHI, ShaderCompiler | Vertex formats, camera, colour / lighting / shadow maths, shader library, shader constant structures |
| `Assets` | Core, RHI, RenderCore | Asset types and `UAssetManager`: meshes, skeletons, animation, textures, materials |
| `Audio` | Core | Audio device and sound data |
| `Physics` | Core, Assets | Physics interface for backends, collision vocabulary, physics assets |
| `LeonEngineCore` (one library) | all of the above | `Renderer` (the world renderer), `Engine` (world, maps, net), `Gameplay`, `AI`, `UMG`, `Application` |
| `Launch` | LeonEngineCore, RHID3D12, Jolt | The `main()` every game shares |

The modules inside `LeonEngineCore` keep their own folders but still compile together, because they
include each other in cycles around `UWorld` and `AActor`. What separating them needs is recorded in
[ADR 0007](adr/0007-one-build-tree-and-enforced-modules.md).

### Rules

1. **Engine → games: forbidden.** Engine sources never include game code nor contain a game's name or
   path. Enforced by `Tools/verify_ue_naming.py` and `Tests/Gameplay/EngineGameSeparationTests.cpp`.
2. **Runtime → Developer / Editor: forbidden.** Importers, the light baker and the editor depend on
   the runtime, never the other way round.
3. **Graphics API only below the RHI.** Direct3D is used in `RHID3D12` and nowhere else; the renderer,
   the editor and the tests draw through `IRHIDevice` / `IRHICommandList`.
4. **Plugins implement engine interfaces** (`IPhysicsScene` by Jolt, `INetTransport` by ENet) and are
   registered at start-up; the engine does not include plugin headers.

### Engine and projects

Build and run tools live with the engine and are pointed at a `.lproject`:

```text
python Tools/build_engine.py
python Tools/build_editor.py
python Tools/build_project.py --project Samples/Sandbox/Sandbox.lproject
python Tools/run_project.py    --project Samples/Sandbox/Sandbox.lproject
python Tools/validate_project.py --project Samples/Sandbox/Sandbox.lproject
python Tools/bake_lightmaps.py --project Samples/Sandbox/Sandbox.lproject
python Tools/create_project.py --name MyGame --output D:/Games/MyGame
```

Environment: `LEON_ENGINE_ROOT` (optional), `LEON_PROJECT` (or `--project`). Full inventory:
[SCRIPTS.md](SCRIPTS.md). Building: [BUILD.md](BUILD.md).

- One CMake tree builds engine, editor, tools, games and tests (`CMakePresets.json`).
- A game registers itself with `leon_register_game`; the editor gets its list of games from a table
  CMake generates. Games outside the repository are added with `LEON_EXTERNAL_GAME_DIRS`.
- `UEngine::Run` requires a `.lproject`: there is no default game inside the engine.

---

## 2. Directory Hierarchy

```text
LeonEngine2/
├── CMakeLists.txt, CMakePresets.json      # one build tree: out/build/<preset>
├── cmake/                                 # compile options, dependencies, leon_add_module()
├── Engine/
│   ├── Source/
│   │   ├── Runtime/<Module>/{Public/<Module>,Private}   # what ships with a game (table above)
│   │   ├── Developer/ShaderCompiler/      # development only
│   │   ├── Editor/                        # LeonEditor
│   │   └── Programs/                      # AssetTool, LightmassTool, ProjectTool
│   ├── Shaders/                           # HLSL
│   ├── Resources/                         # engine content (fonts, LUTs) and the old shader names materials still use
│   ├── Plugins/Physics/Jolt, Plugins/Networking/ENet
│   └── ThirdParty/
├── Games/LeonHorror, Games/LeonTournament
├── Samples/Sandbox
├── Tests/                                 # engine suite; each game has its own under <Game>/Tests
├── Tools/                                 # Python scripts — see SCRIPTS.md
└── Docs/                                  # this folder; decisions in Docs/adr
```

> **Include form:** `#include "Engine/UWorld.hpp"`, `#include "RenderCore/FVertexLayout.hpp"`.
> Namespace is `Leon::`. Module folders are short (`Core`, not `LeonCore`).

> **Naming contract:** UE-style `U`/`A`/`F` prefixes are used without Unreal GC or reflection.
> `AActor` wraps an EnTT entity; render components are POD structs on the registry.
> There is no separate `Scene` / `Entity` type — use `UWorld` + `AActor`.

---

## 3. Project Architecture & Virtual Paths (`.lproject`, `/Game/...`, `/Engine/...`)

### 3.1 Project Descriptor (`.lproject`)
```json
{
  "FileVersion": 1,
  "EngineVersion": "0.15.0",
  "ProjectName": "Sandbox",
  "DefaultMap": "/Game/Maps/ShowcaseLevel",
  "DefaultGameMode": "ASandboxGameMode"
}
```

| Field | Required | Role |
| :--- | :--- | :--- |
| `FileVersion` | yes | Descriptor schema version |
| `EngineVersion` | yes | Informational engine version string |
| `ProjectName` | yes | Used for INI section `/Script/<ProjectName>.GameMode` |
| `DefaultMap` | yes | Fallback if `DefaultEngine.ini` omits `GameDefaultMap` |
| `DefaultGameMode` | yes | Fallback if INI omits `GlobalDefaultGameMode` / `GameModeClass` |

### 3.2 Project Boot Contract

Boot order (`UEngine::InternalRun`):

1. Locate `.lproject` (path as given, directory scan, or walk parents from cwd).
2. Load `FProjectDescriptor` and `FProjectPaths::SetProjectRoot`.
3. Load flat Multi-INI from `<Project>/Config/` (no Base.ini / user Saved hierarchy yet).
4. Create `FApplication` / window from DisplaySettings.
5. Create `UGameInstance` + `UWorld`, load default map (fatal if missing on boot).
6. Spawn GameMode from `UClassRegistry`, apply class config, `InitWorld` / `BeginPlay`.
7. Push viewport layer and run the loop.

**Config priority (highest wins):**

| Setting | Priority |
| :--- | :--- |
| Default map | `DefaultEngine.ini` `GameDefaultMap` → `.lproject` `DefaultMap` |
| GameMode class | `DefaultGame.ini` `/Script/<Project>.GameMode` `GameModeClass` → `GlobalDefaultGameMode` → `.lproject` `DefaultGameMode` |
| Pawn/PC/HUD/… | `/Script/<Project>.GameMode` → `/Script/Engine.GameModeBase` |
| Input | `DefaultInput.ini` only |

This is **not** full Unreal config stacking (`Base.ini` + project + `Saved/Config`). It is a flat per-project Multi-INI.

**Supported INI keys**

| Section | Keys | Applied to |
| :--- | :--- | :--- |
| `/Script/Engine.DisplaySettings` | `WindowTitle`, `WindowWidth`, `WindowHeight`, `VSync`, `Fullscreen` | `FApplication` / `FWindow` |
| `/Script/EngineSettings.GameMapsSettings` | `GameDefaultMap`, `GlobalDefaultGameMode` | Boot map + GameMode class |
| `/Script/Engine.RendererSettings` | `ShadowMapResolution`, `EnablePlanarReflection`, `PlanarReflectionQuality` (`Low`/`Medium`/`High`/`Epic`, default **Epic** = full viewport), optional `PlanarReflectionResolutionScale` (0.25–1.0) | `FWorldRenderer` project defaults (before first create) |
| `/Script/Engine.RendererSettings` | `Exposure`, `SunIntensity` | Documented only — **map skybox / lights own these** (not overwritten) |
| `/Script/Engine.InputSettings` | mouse look + Move* / Sprint keys | `FInputSettings` |
| `/Script/<Project>.GameMode` | `GameModeClass`, `DefaultPawnClass`, … | `FGameModeConfig` |

**Project executable duties:** register RHI driver + project `UClassRegistry` classes **before** `UEngine::Run`. Missing default map on initial boot returns non-zero.

### 3.3 Virtual Path Resolution (`FProjectPaths`)
* `/Game/Maps/ShowcaseLevel` → `<ProjectRoot>/Content/Maps/ShowcaseLevel.lmap`
* `/Game/Materials/M_StudioFloor` → `<ProjectRoot>/Content/Materials/M_StudioFloor.lmat`
* `/Engine/Shaders/PBR_Lit.glsl` → `Engine/Content/Shaders/PBR_Lit.glsl`

### 3.4 Multi-INI Configuration
1. **`DefaultEngine.ini`**: window, maps, GameMode class, renderer project defaults.
2. **`DefaultGame.ini`**: pawn / PC / HUD / GameState / PlayerState classes.
3. **`DefaultInput.ini`**: `FInputSettings` consumed by pawns.

### 3.5 Entry point
Every game's `main()` fills a `FGameLaunchDesc` (project name, the function that registers its classes,
an optional hook for its own command line arguments) and calls `LaunchGame` from the `Launch` module.
`LaunchGame` registers the Direct3D 12 backend and Jolt, resolves the `.lproject` (`--project=` →
bare `*.lproject` argument → `LEON_PROJECT` → beside the executable → working directory → the game's
default path) and runs the engine.

---

## 4. Core Engine Subsystems

### 3.1 FApplication & Lifecycle (`FApplication.hpp`)
* Orchestrates the main loop, frame delta timing (`FTimestep`), and top-level window events.
* Manages the `FLayerStack`, calling `OnUpdate()` and routing input `OnEvent()` through active layers.

### 3.2 Modular FLayer System (`FLayer.hpp`, `FLayerStack.hpp`)
* Allows game logic, debug tools, and UI systems to be isolated into distinct `FLayer` instances.
* Updates flow forward through the stack (`Layer 0 -> Layer N`), while events flow backwards from overlays down to base layers until marked handled (`bHandled = true`).

### 3.3 FWindow (`Platform/FWindow.hpp`)
* `FWindow` creates the GLFW window with no graphics API attached (`GLFW_NO_API`) and exposes its
  native handle. The application creates the swapchain on it through the RHI.
* No public header exposes a GLFW type.

---

## 4. The Render Hardware Interface (RHI)

The RHI is shaped like Direct3D 12, and small ([ADR 0003](adr/0003-explicit-rhi-with-a-fixed-binding-model.md)):

* `IRHIDevice` creates buffers, textures, shaders (bytecode), graphics pipelines and the swapchain;
  uploads and reads back textures; opens and closes a frame.
* `IRHICommandList` records a frame: render passes, pipeline, vertex and index data, constants,
  textures, draws.
* State is not global: culling, depth and blending live in the pipeline description.
* Binding is fixed: constant buffers `b0` frame, `b1` pass, `b2` material, `b3` draw, `b4` bones;
  textures `t0..t15`; five fixed samplers. Constants are copied per draw, so a draw can never see the
  values of the previous one.
* Backends register themselves (`FD3D12RHI::Register()`, the null backend for tests) and
  `FRHI::Init` picks one.

`FVertexBuffer`, `FIndexBuffer`, `FTexture2D` and `FTextureCube` (with `FLegacyRenderDriver`) remain
as the resource classes meshes and materials hold; they are thin wrappers over RHI objects
([ADR 0008](adr/0008-asset-resource-classes-kept-as-a-bridge.md)). Details of the renderer on top:
[RENDERER.md](RENDERER.md).

---

## 5. First-Class Material System (`FMaterial` & `FMaterialInstance`)

The Material System decouples shader logic and asset properties into a shared, reusable resource hierarchy:

```text
┌─────────────────────────────────────────────────────────────┐
│                          FMaterial                          │
│               (Master Shader & Parameter Layout)            │
└──────────────────────────────┬──────────────────────────────┘
                               │ creates
                               ▼
┌─────────────────────────────────────────────────────────────┐
│                      FMaterialInstance                      │
│        (Resolved Scalars, Textures & FPipelineState)        │
└──────────────────────────────┬──────────────────────────────┘
                               │ attached to
                               ▼
┌─────────────────────────────────────────────────────────────┐
│                     FMaterialComponent                      │
│                  (ECS Entity Mesh Binding)                  │
└─────────────────────────────────────────────────────────────┘
```

* **`FPipelineState`**: Encapsulates `CullMode`, `bDepthTest`, `bDepthWrite`, `DepthFunc`, `bBlend`, `SrcBlend`, and `DstBlend`.
* **Serialization**: Material assets are stored as human-readable `.lmat` YAML files and loaded via `UAssetManager`.

---

## 6. Image-Based Lighting & Binary Cache Subsystem (`FIBLGenerator.cpp`)

Split-sum IBL. Canonical math: [RENDERER_CONTRACT.md](RENDERER_CONTRACT.md). Cache format: [IBL_CACHE_DESIGN.md](IBL_CACHE_DESIGN.md).

1. **2D BRDF LUT**: $256 \times 256$ `RG16F`, texel centers `(x+0.5)/size`, IBL Smith $k = \alpha/2$, 512-sample `IntegrateBRDF`. Disk header `LEONBRDF` **v2**.
2. **Diffuse irradiance cubemap**: Cosine-weighted Hammersley ($N=512$), $E = \pi \cdot \mathrm{mean}(L_i)$. Karis `saTexel` $= 4\pi / (6 \cdot \mathrm{size}^2)$ against the **environment cube**, not the equirect.
3. **Specular prefilter**: 5 mips ($128 \to 8$), GGX importance sampling + Karis solid-angle LOD.
4. **Binary cache (`.libl` v6)**: `FIBLCacheHeader` + FNV-1a **content** hash of the HDR. Exposure and `EnvironmentIntensity` are **not** baked. Atmosphere-only (no HDR file) does not hit this cache path.

Runtime: `diffuseIBL = E * albedo / π`, `specularIBL = Li_prefilter * (F_rough * A + B)`. Planar Li, if weighted, replaces `Li_prefilter` then uses the same BRDF factor.

---

## 7. Multi-Pass World Rendering Pipeline (`FWorldRenderer`)

Runtime path: `UEngine` → viewport layer → `UWorld::OnRender` → `FWorldRenderer::Render`.

Implementation: `FWorldRenderer.cpp` (view state: lights, LOD, IBL, planar planes), `FWorldRendererRHI.cpp` (the passes), `FPostProcessRHI.cpp` (post-processing), `FImmediateDrawRHI.cpp` (HUD, text, debug lines, particles).

Static lighting (offline): `LightmassTool bake` → `FLightmass` / `FLightBaker` → `.llightmap`. Runtime sampling: [STATIC_LIGHTING.md](STATIC_LIGHTING.md). Bake AO: when enabled, `FLightBaker` multiplies AO into stored `E` (aligned with [RENDERER_CONTRACT.md](RENDERER_CONTRACT.md)). Renderer docs: [RENDERER.md](RENDERER.md).

```text
Gather lights (skip ELightMobility::Static) → pass constants
UpdateIBL
PASS 1: Cascaded Shadow (4-slice D32_Float array)
PASS 2: Spot shadow (one 2D D32_Float map — `ShadowedSpotIndex` in the runtime spot list)
PASS 3: Planar reflection (mirrored camera, RGBA16F, shadows disabled, clip plane)
PASS 4: Opaque HDR geometry RGBA16F — CPU AABB frustum cull, one draw per mesh section
         opaque → skinned inverted-hull outline
PASS 5: Skybox (z = w, depth LessEqual)
PASS 6: Transparent Blend (back-to-front, depth write off)
PASS 7: 3D world text → particles → optional gameplay debug (depth test on, write off)
PASS 8: SSAO → Bloom → exposure → tone map (ACES default) → IEC sRGB → FXAA
UI / F1 overlay on the output → Present
```

**Coordinates (implemented):** right-handed, +Y up, camera forward −Z (default yaw −90°), clip $z \in [-1,1]$ in engine matrices (converted to Direct3D's $[0,1]$ where they reach the shaders), front face CCW, `M = T * R * S`, GPU column-major no-transpose. Not reversed-Z. Near 0.1 / far 1000. Actor world pose is `AActor::GetActorWorldMatrix()` (root `USceneComponent` chain, else `FTransformComponent`).

**Planar pass:** not cubemap probes. The floor capture fills when a plane is registered; an optional wall capture when the camera faces a non-horizontal plane. Games currently register the floor (`n = +Y`, `d = 0`). Contract: [RENDERER_CONTRACT.md](RENDERER_CONTRACT.md#planar-reflections).

**Lighting equation (lit):** `hdr = ambient + Lo_direct + baked + emissive`. With a static lightmap, diffuse IBL is replaced by `kD * albedo/π * E`; specular IBL remains. Dynamic lights with `Mobility != Static` still add `Lo_direct`. Material AO multiplies indirect specular (and full ambient when no lightmap), not baked `E` and not direct `Lo`.

---

## 8. Diagnostic and Interactive Debugging Subsystem

LeonEngine2 provides direct single-key forensic debugging controls available across all viewport layers:

| Hotkey | Mode / Subsystem | Description |
| :--- | :--- | :--- |
| **`F1`** | **Diagnostics HUD** | Real-time panel rendering FPS, frametimes (CPU/GPU), VRAM allocation, RAM usage, triangle counts, and draw call metrics. |
| **`F2`** | **3D Light Gizmos** | 3D wireframe cones for Spot Lights, bounding attenuation spheres for Point Lights, and directional sunlight vectors. |
| **`F3`** | **Wireframe** | Toggle solid fill vs wireframe (pipeline raster state). Geometry only; restored before post/UI. |
| **`F4`** | **Material cycle** | Albedo → Metallic → Roughness → AO → Emissive → UV0 → Tangent → Bitangent → N·L. |
| **`F5`** | **Geometry cycle** | World normals → reflection vector. |
| **`F6`** | **Lighting cycle** | Lo → direct diffuse → direct specular → IBL specular → IBL diffuse → baked → lightmap irradiance → lightmap UV → Lo+baked (no IBL) → HDR before tone map. |
| **`F7`** | **IBL maps cycle** | Prefilter mips 0–4 → irradiance cubemap → BRDF LUT. |
| **`F8`** | **Shadows cycle** | Occlusion mask → CSM false-color → spot shadow → cascade 0–3 depth. |
| **`F9`** | **Planar buffer** | Offscreen planar reflection texture (debug view 13). |
| **`F10`** | **Post-process cycle** | Composite → raw HDR → bloom → bright pass → tone map (no FXAA) → SSAO. |
| **`F11`** | **Post-process master** | Toggle `FPostProcessSettings::bEnabled` (tone map / bloom / SSAO / FXAA). |
| **`F12`** | **Reset lit** | Shader mode 0, post debug 0, post enabled, solid fill. |

---

## 3. LeonEngine2 Gameplay Framework (Unreal Engine Architecture)

LeonEngine2 implements a strict, faithful Unreal Engine gameplay framework architecture:

```text
UEngine
  │
  ├── UGameInstance (High-level persistent game state)
  │
  └── UWorld (Active loaded map)
        │
        ├── AGameModeBase (Match rules, player login flow, spawn rules)
        │     │
        │     ├── AGameStateBase (Global match state, PlayerArray)
        │     │
        │     ├── APlayerController (Player input, possession, camera manager, InputMode)
        │     │     │
        │     │     ├── APlayerState (Persistent player data: Name, ID, Score)
        │     │     ├── APawn / ADefaultPawn (Possessed 6-DOF actor)
        │     │     ├── APlayerCameraManager (Resolves ViewTarget, Pawn camera & Fallback)
        │     │     └── AHUD (Viewport widgets + DrawHUD + PrintString paint)
        │     │           │
        │     │           └── UUserWidget → UWidget tree (UCanvasPanel, UButton, UTextBlock, …)
        │     │
        │     └── AActor* (All world actors with attached UActorComponents)
        │
        └── FWorldRenderer (Consumes UWorld data and renders PBR, CSM, IBL, Post-Processing)
```

### HUD / UI Framework

LeonEngine ships a minimal Unreal-aligned UMG-style UI layer (not full Slate/UMG):

```text
UObject
  ├── UWidget
  │     ├── UUserWidget          (AddToViewport / RemoveFromParent)
  │     ├── UButton              (OnClicked, hover/pressed/disabled)
  │     ├── UTextBlock           (SetText / color / scale / alignment)
  │     ├── UImage
  │     ├── UProgressBar         (percent fill)
  │     └── UPanelWidget
  │           ├── UCanvasPanel   (anchors + offsets; FUILayout / FUITypeScale)
  │           ├── UHorizontalBox
  │           └── UVerticalBox
  └── AActor
        └── AHUD                 (owned by APlayerController)
```

**Lifecycle**

```text
UWorld → GameMode → PlayerController → HUD → UserWidget → Viewport → FUIRenderer
```

**Critical BeginPlay ordering:** During `UWorld::BeginPlay`, `StartPlay`/`Login` runs with deferred actor `BeginPlay`. PC ↔ HUD wiring (`SetPlayerController`) completes before `AHUD::BeginPlay`, so `CreateWidget` / `AddToViewport` can succeed. Spawning HUD without this deferral called `BeginPlay` inside `SpawnActor` with a null PlayerController (widgets never registered).

**Frame render order**

```text
World 3D → Light gizmos (F2) → AHUD widgets + PrintString → F1 Diagnostics Overlay → Present
```

**Engine vs Sandbox responsibilities**

| Engine | Sandbox project |
|--------|-----------------|
| `AHUD`, widgets, `FUIRenderer`, `PrintString`, `OpenLevel`, InputMode, F1–F12 | `ASandboxGameMode`, `ASandboxHUD`, INI / `.lproject` config |

**PrintString** (`UGameplayStatics::PrintString` / `Leon::PrintString`) queues on-screen debug messages via `FOnScreenDebugMessageManager`. Messages are distinct from `FLog` and are painted inside the viewport by `AHUD::DrawHUD`.

**OpenLevel** (`UGameplayStatics::OpenLevel("/Game/Maps/MyMap")`) requests a safe-frame travel on `UEngine`: EndPlay → Clear old World → Create World → Load `.lmap` (virtual path) → GameMode → Login (PC / Pawn / HUD) → BeginPlay. Product HUD widgets use this to change maps.

**FInput modes** (`APlayerController`): `SetInputModeGameOnly`, `SetInputModeUIOnly`, `SetInputModeGameAndUI`. GameAndUI allows pawn movement and UI mouse interaction simultaneously.

**Debug hotkeys (coexist with gameplay HUD)**

| Key | Behavior |
|-----|----------|
| F1 | Diagnostics performance overlay (`FDebugOverlay`) |
| Shift+F1 | Gameplay debug: line traces, hitboxes, and colliders (`FDebugRenderer`) |
| F2 | 3D light gizmos (`FDebugRenderer`) |
| F3–F12 | Render debug cycles (`FRenderDebugHotkeys`: material, lighting, IBL maps, shadows, planar, post) |

### Unreal Naming & Prefix Standards:
- **`U`** = Engine objects, worlds, components, assets, widgets (`UObject`, `UWorld`, `UGameInstance`, `UEngine`, `UWidget`, `UUserWidget`, `UButton`, `UTextBlock`, `UPanelWidget`, `UCanvasPanel`, `UGameplayStatics`, `UClassRegistry`).
- **`A`** = Spawnable world actors (`AActor`, `APawn`, `ADefaultPawn`, `APlayerController`, `APlayerState`, `AGameModeBase`, `AGameStateBase`, `ACameraActor`, `APlayerCameraManager`, `AHUD`).
- **`F`** = Structs and value types (`FTransformComponent`, `FUIRenderer`, `FOnScreenDebugMessage`, `FGameModeConfig`, `FTimestep`).
- **`E`** = Enumerations (`ETextAlignment`, `ESlateVisibility`, `EInputMode`, `EButtonState`, `EShadowFilterMode`).
- **`T`** = Templates and container wrappers (`TRef`, `TScope`).

---

## 11. Editor Architecture (Context-Driven Pattern)

The Leon Engine Editor (`Editor/`) follows an **Unreal Engine-inspired Context-Driven architecture**. Instead of fragmented or over-engineered subsystem patterns, all editing state, selection sets, and undo/redo stacks are managed through a centralized `FEditorContext` that serves as the single source of truth for all panels.

### 11.1 File Structure & Modular Organization

```text
Editor/
├── Source/
│   ├── Public/Editor/
│   │   ├── FEditorApp.hpp               # Out-of-process editor application host
│   │   ├── Context/                     # Single source of truth for editing state
│   │   │   ├── FEditorContext.hpp       # Central coordinator (World, Map, Selection, History)
│   │   │   ├── FEditorSelection.hpp     # Multi-actor and multi-asset selection state
│   │   │   └── FEditorHistory.hpp       # Undo / Redo command stack
│   │   ├── Commands/                    # Reversible action command interface
│   │   │   └── IEditorCommand.hpp       # Base command contract (Execute, Undo)
│   │   ├── Panels/                      # Independent ImGui panels
│   │   │   ├── FViewportPanel.hpp       # 3D interactive viewport + camera + gizmos + drop target
│   │   │   ├── FOutlinerPanel.hpp       # World outliner actor hierarchy tree
│   │   │   ├── FDetailsPanel.hpp        # Property inspector & component editor
│   │   │   ├── FContentBrowserPanel.hpp # Asset browser (Grid/List, search, import, breadcrumbs)
│   │   │   ├── FToolbarPanel.hpp        # Top toolbar actions (Save, Bake, Hub, Run, Layout)
│   │   │   ├── FPlaceActorsPanel.hpp    # Fast actor spawn palette + drag source
│   │   │   ├── FProjectHubPanel.hpp     # Project launcher & recent projects
│   │   │   ├── FOutputLogPanel.hpp      # Console logs & diagnostics
│   │   │   ├── FWorldSettingsPanel.hpp  # Level gravity, gamemode, audio settings
│   │   │   └── FProjectSettingsPanel.hpp# Project descriptor settings (.lproject)
│   │   ├── Gizmos/                      # 3D interactive manipulation tools
│   │   │   └── FTransformGizmo.hpp      # Translate, Rotate, Scale gizmos with snapping
│   │   ├── UI/                          # Reusable UI styling, widgets and vector icons
│   │   │   ├── FEditorTheme.hpp         # Unreal Dark theme & typography loader
│   │   │   ├── FLucideIcons.hpp         # Vector-based Lucide icon rasterizer
│   │   │   └── FEditorWidgets.hpp       # Shared ImGui controls (XYZ colored vectors, search)
│   │   ├── Utils/                       # Platform utilities
│   │   │   └── FEditorFileDialog.hpp    # Native file open/save dialogs
│   │   └── Window/                      # GLFW and docking window orchestration
│   │       └── FEditorWindow.hpp        # Window wrapper and configuration state
│   └── Private/                         # Matching .cpp implementations
```

### 11.2 Architectural Principles

1. **Centralized Context (`FEditorContext`)**:
   - `FEditorContext` owns `FEditorSelection` and `FEditorHistory`.
   - Panels receive `FEditorContext*` and subscribe to selection/transaction changes without direct inter-panel coupling.
2. **Reversible Commands (`IEditorCommand`)**:
   - Operations that mutate actor state, delete actors, or change transforms execute via `IEditorCommand` through `Context.GetHistory().ExecuteCommand(...)` for full `Ctrl+Z` / `Ctrl+Y` support.
3. **Consistent UI Primitives (`FEditorWidgets`)**:
   - All panels share standard Unreal-style property rows, labeled XYZ vectors (with Red/Green/Blue color badges and reset buttons), and search filter inputs.
4. **Decoupled 3D Interaction (`FTransformGizmo`)**:
   - Viewport handles camera raycasts, screen-to-world projections, and marquee box selection; the transform gizmo manages axis handles, plane snapping, and coordinate spaces (World vs Local).

