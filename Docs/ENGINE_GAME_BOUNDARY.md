# LeonEngine2 — Engine / Game Boundary

Dependency direction is **Game → Engine** only.

```text
Games/LeonTournament  →  Engine
Samples/Sandbox         →  Engine
Engine                   ↛  any project
```

## What the Engine must not know

LeonTournament, Shooter, Rifle, TDM, teams, kills, deathmatch, game HUD, game weapons, game characters, game AI behavior, game maps, Mixamo Y Bot paths, `/Game/Animations/` product clips.

## Configuration, not compilation

Product class names appear in **project** `Config/*.ini` and `.lproject`, never as `#include` of game code from `Engine/Source`.

The root CMake builds the games listed in `LEON_GAMES` (folders under `Games/` or `Samples/`) and `LEON_EXTERNAL_GAME_DIRS`; each registers itself with `leon_register_game`, so no engine source names a product.

## Enforcement

| Check | Location |
| :--- | :--- |
| No game name or game path in Engine sources | `Tools/verify_ue_naming.py` |
| No game tokens in `Engine/Source` | `Tests/Gameplay/EngineGameSeparationTests.cpp` |
| Engine test binary does not link game `.cpp` | `Tests/CMakeLists.txt` (`RendererTests`) |

## Asset ownership

| Owner | Examples |
| :--- | :--- |
| Engine | Shaders, fonts, BRDF LUT, generic importers, `/Engine/...` |
| Project | Maps, materials `M_*`, blend spaces `BS_*`, skeletons, BT/Blackboard assets, HDR used by that game |

Sample-project HDR (`Samples/Sandbox/Content/HDR/DaySky1k.lhdr`) may be used as an **engine test fixture** because Sandbox is the blank sample game, analogous to a UE template — not because the engine ships that map. Engine **source** still must not embed that path; tests may.

## Plugin boundary

Plugins implement RHI / physics / net. They depend on Engine public interfaces. They must not include project headers.
