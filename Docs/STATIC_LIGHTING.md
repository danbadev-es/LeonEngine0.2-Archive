# Static Lighting & Lightmap Baking (v0.15+)

Offline static lighting for indie-scale scenes. Inspired by Unreal Lightmass / early UE4, not Lumen.

## Concepts

| Enum | Values | Runtime / Bake |
| :--- | :--- | :--- |
| `ELightMobility` | Static, Stationary, Movable | **Static**: full bake (direct+indirect), excluded from dynamic UBO. **Stationary**: indirect bake only; direct + shadows remain dynamic (`!= Static` in UBO). **Movable**: fully dynamic, ignored by Lightmass. |
| `EComponentMobility` | Static, Stationary, Movable | Only **Static** geometry receives / samples lightmaps. |

Helpers: `IsLightmassBakeLight`, `DoesLightmassBakeDirect`.

## Assets

- **`.llightmap`**: native HDR atlas (`FLightmapAsset`), magic `LLLM`, version **2**, RGBA32F payload (`rgb` = irradiance `E`, `a` = coverage) + bake-input `ContentHash`.
- **`.lmesh` v4**: packed tangent `vec4` + `LightmapUV` + unique-UV1 flag. v1–v3 migrate on load. Missing UV1 is generated at bake (split per triangle) and **persisted** via `UStaticMesh::SaveToFile`.
- **`.lmap` Environment**: `StaticLighting`, `LightmapResolution`, `NumIndirectBounces`, `SamplesPerTexel`, `IndirectIntensity`, `AmbientOcclusion`, `AOIntensity`, `AORadius`, `TexelPadding`, `WorldScale`, `LightmapAsset`, `LightmapBakeHash`.

## Offline bake (`LightmassTool` / `AssetTool`)

```
python Tools/bake_lightmaps.py --project <path.lproject> [--map /Game/Maps/Name] [--force]
python Tools/bake_lightmaps.py --project <path.lproject> --validate-only

LightmassTool bake --map <path.lmap> [--quality=Preview|Draft|Production] [--force]
LightmassTool validate --map <path.lmap>
AssetTool inspect <path.llightmap>
```

Pipeline:

1. Load map
2. Collect Static meshes + Static/Stationary lights
3. Resolve material albedo (base color × albedo map sample at UV0)
4. UV1 (generate + persist if missing)
5. Atlas (`FLightmapBuilder`, `TexelPadding`)
6. CPU bake (`FLightBaker`): stores **diffuse irradiance** `E = ∫ Li max(N·ω,0) dω`. Static lights contribute direct `E`; Static+Stationary contribute to GI. Cosine misses add sky `E += π L_env` (HDR or atmosphere, scaled by `EnvironmentIntensity`, not `IndirectIntensity`). `NumIndirectBounces == 0` skips GI but still includes environment miss. Receptor emissive is not stored. Bake AO can scale stored `E`. Bake-input hash algorithm version **5** (content hash of mesh/material/HDR files).
   A vertex normal of no length (a mesh exported without normals, or corner normals that cancel out) is
   replaced by the triangle's plane, and a texel that still comes out not-a-number or infinite is
   filled from its neighbours; the baker reports how many of each there were. Such a texel on screen
   used to be spread into black rectangles by the bloom.
7. Write `.llightmap` (`ContentHash` = bake input hash)
8. Stamp `.lmap` chart metadata + `LightmapBakeHash`

### Cache

`FLightmass::ComputeBakeInputHash(UWorld, settings)` fingerprints actors/transforms/mobility/resolutions, mesh/material **file content** (FNV-1a), and bake knobs — **not** the stamped Scale/Bias/Index/hash fields. Second bake without `--force` skips when hash matches. At play, a stale `LightmapBakeHash` disables lightmap sampling (`UWorld::AreLightmapsTrusted`).

Reflection probes (`AReflectionProbe`) are left out of the hash: they take light from the level and
give none to the bake, so placing or moving one keeps a bake valid. Their captures are cached under
the bake hash (see `Docs/RENDERER.md`), so a new bake also captures them again.

Decals (actors whose materials are all `AlphaMode: "Decal"`, `FLightmapBakeInputs::IsDecal`) are left out
of the bake and of the hash, whatever their mobility: they get no chart, cast no baked shadow and
bounce no light, and adding, moving or removing one leaves the bake valid.

### Validate

`ValidateMap` requires Static meshes; when `StaticLighting` is on, also checks atlas exists, header valid, hash not stale, and chart metadata finiteness.

## Runtime

`FWorldRenderer` binds atlas slot **12**. `PBR_Lit.hlsl`: `Lo_diffuse = kD * albedo / PI * E` (baked irradiance) + dynamic Movable/Stationary lights + specular IBL (diffuse IBL off when a lightmap is bound).

See `Docs/RENDERER_CONTRACT.md` for equations, mobility, and cache versions.

### Debug (F6 lighting cycle)

| Mode | `u_DebugMode` | Shows |
| :--- | :--- | :--- |
| Dynamic only | 10 | Runtime lights (`Lo`) |
| Baked only | 31 | `albedo * lightmap` |
| Lightmap irradiance | 32 | Raw atlas HDR |
| Lightmap UV | 33 | Atlas UV (R/G); blue=1 if no lightmap |
| Dyn + Baked | 34 | `Lo + baked` without IBL |
| Lit | 0 (F12) | Full composite |

## Light probes: baked ambient light for what has no lightmap

Only Static geometry reads the lightmap. Characters and everything that moves used to take their
ambient light from the sky alone: black in a dark level, and without any of the light the walls bounce.
The bake now also measures the light in the air, at the points of a grid, and the game gives each such
object the light of the grid where it stands.

- **What a probe holds.** The diffuse light arriving from every direction, as first-order spherical
  harmonics folded with the cosine lobe: four numbers per colour channel, `irradiance(n) =
  dot(coefficients, (n.x, n.y, n.z, 1))`, never below zero (`RenderCore/FLightProbeGrid.hpp`). That is
  a level and a direction: enough for "redder from the side of the red wall", not for two lights on
  opposite sides at once (they cancel into a level). Second order would hold that, at nine numbers per
  channel and more than twice the constants of every draw, for one sample per object: not taken.
- **What goes into it** (`FLightBaker::BakeProbes`): what a lightmap texel there would get. Static
  lights light the probe directly (each adds its irradiance along its direction, if nothing is in the
  way); every ray that reaches a surface brings what that surface sends back, lit by Static **and**
  Stationary lights, with the same bounces as the lightmap (`NumIndirectBounces`, `IndirectIntensity`);
  rays that reach nothing bring the sky; emissive surfaces are included. The direct light of Stationary
  lights is not: the game adds it per pixel, as for lightmapped surfaces. Baked ambient occlusion is
  not applied.
- **The grid.** Regular, axis-aligned, centred in a volume: the bounds of the Static geometry, cut to
  the map's `ANavMeshBoundsVolume` actors when it has any. `LightProbeSpacing` (1.5 m) along the
  ground, `LightProbeVerticalSpacing` (1 m) upwards; at most 65 536 probes (the spacing is widened to
  fit). 64 / 128 / 256 directions per probe for Preview / Draft / Production.
- **Probes in walls.** A probe that sees the back of surfaces in more than a quarter of its directions
  is inside something or outside the level. It is marked not valid, takes the average of the valid
  probes next to it (several probes deep) and is used only where no valid probe is in reach.
- **File.** `Content/Lightmaps/<Map>.lprobes` (`Engine/FLightProbeAsset.hpp`), little endian: magic
  `LLPV`, version 1, the bake hash (uint64), origin and spacing (3 float32 each), counts (3 uint32), a
  reserved uint32; then one validity byte per probe; then twelve float16 per probe (red xyzw, green
  xyzw, blue xyzw). Probe (x, y, z) is entry `x + countX * (y + countY * z)`. 25 bytes a probe.
- **Trust.** The file carries the hash of the bake, like the lightmap. `RefreshRuntimeTrust` loads it
  only when the lightmaps are trusted and the file's hash is the map's; otherwise the world has no
  probes and says why in the log. `ValidateMap` checks the same. A bake is not "cache valid" while the
  probes it should have are missing.
- **Runtime.** `IRenderScene::SampleLightProbes(position)` blends the eight probes around a point
  (valid ones only; outside the grid, the nearest point of it). The renderer asks once per draw
  without a lightmap, at the middle of its bounds (movable and stationary meshes, skinned meshes,
  static ones the bake did not cover), and passes the twelve numbers in the draw constants
  (`u_AmbientSH`, `u_DrawFlags2.x`); `PBR_Lit.hlsl` uses them in place of the sky's irradiance. Specular
  is not touched (sky or reflection probes). A map without probes is drawn exactly as before.

Map keys (`WorldSettings:`), written only when they differ from the default:

| Key | Default | Meaning |
|-----|---------|---------|
| `LightProbes: true\|false` | `true` | The bake fills a grid of probes |
| `LightProbeSpacing` | 1.5 | Metres between probes along X and Z |
| `LightProbeVerticalSpacing` | 1 | Metres between probes along Y |
| `LightProbeAsset` | (written by the bake) | Virtual path of the `.lprobes` file |

The three settings enter the bake hash only when a map changes them, so bakes made before probes
existed stay valid; such maps get their probes the next time they are baked.

Limits: one sample per object, so a long object is lit as its middle is; probes are blended across
walls (a probe on the other side of a thin wall counts as much as one on this side: keep rooms at
least a probe spacing apart from what they must not share light with, or lower the spacing); no light
from Movable lights; nothing moves the grid at run time (a door that opens changes nothing).

## Modules

- `FLightmass` — orchestration (tool entry)
- `FLightmapBuilder` — atlas pack
- `FLightBaker` — shading (same attenuation model as `FLight` / PBR); `BakeProbes` for the light probes
- `FLightProbeGrid` (RenderCore) / `FLightProbeAsset` (Engine) — the probes in memory and on disk
- `FLightmapAsset` / `FLightmapUV` — asset + UV helpers

## Limits (vs Unreal)

No GPU Lightmass, no photon mapping, no true Stationary shadow-map baking. Procedural primitives write non-overlapping UV1 (cube 2×3 atlas, cylinder side+caps). Runtime samples UV1 (`u_LightmapUseTexCoord` stays 0). Imported meshes store generated UV1 in `.lmesh`.

## Sandbox maps

| Map | Geometry | HDRI | Lightmap |
| :--- | :--- | :--- | :--- |
| `/Game/Maps/ShowcaseLevel` | Procedural primitives (studio floor 40×28) | `DaySky1k` | `ShowcaseLevel.llightmap` |

`DaySky1k.lhdr` is the IBL/HDR test fixture under Sandbox `Content/HDR/`. Rebake after content changes: `python Samples/Sandbox/Scripts/bake.py --force`.
