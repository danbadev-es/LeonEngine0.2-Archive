# Renderer

How LeonEngine draws a frame. The renderer is a forward PBR renderer on Direct3D 12, reached only
through the engine's render hardware interface (RHI). The decisions behind it are in [adr/](adr/README.md).

The other `RENDERER_*.md` files were written for the OpenGL renderer this one replaced. Their maths
(PBR, shadows, tone mapping, image-based lighting) still describes what the shaders compute, because
the shaders were ported line by line; their API names, file names and frame mechanics do not.

## Where things are

| Part | Location |
| :--- | :--- |
| RHI: interfaces and plain data | `Engine/Source/Runtime/RHI/Public/RHI/IRHIDevice.hpp`, `RHITypes.hpp`, `FRHIFrame.hpp` |
| Direct3D 12 backend | `Engine/Source/Runtime/RHID3D12/` |
| Null backend (no GPU; tools and tests) | `Engine/Source/Runtime/RHINull/` |
| HLSL compiler (DXC) | `Engine/Source/Developer/ShaderCompiler/` |
| Shaders | `Engine/Shaders/*.hlsl`, `*.hlsli` |
| Shader loading and caching | `Renderer/FShaderLibrary` |
| World renderer: settings and view state | `Renderer/FWorldRenderer.{hpp,cpp}` |
| World renderer: the frame | `Renderer/Private/FWorldRendererRHI.cpp` |
| Post-processing | `Renderer/Private/FPostProcessRHI.{hpp,cpp}` |
| HUD, overlays, world text, debug lines, particles | `Renderer/FImmediateDrawRHI.{hpp,cpp}` |
| Editor interface (Dear ImGui) | `Engine/Source/Editor/Source/Private/UI/FImGuiRHI.{hpp,cpp}` |
| Mesh buffers and textures behind asset classes | `RHI/FLegacyRenderDriver.{hpp,cpp}` |

## A frame

`FApplication::Run` owns the frame: it begins it on the device, clears the back buffer, publishes the
command list and the output through `FRHIFrame`, runs the layers, ends the frame and presents.

`FWorldRenderer::Render(camera)` draws a world into the frame's output:

```text
UpdateViewState    level of detail, IBL environment, spot light aim, skinned shadow casters, mirror planes
queue              world text, particles, gameplay debug shapes (drawn at the end of the scene pass)
zones              the rooms the camera sees through open doorways, when the map has zones
collect            visible mesh sections, resolved to pipelines, buffers and textures; shadow casters
lights             the point and spot lights that get the shader's slots, by importance for the camera
probe captures     only while a reflection probe has no cube yet: two views of the static level a frame
shadow maps        sun: up to 4 cascades · up to 4 spot lights · up to 4 point lights (cube maps)
reflection captures the scene mirrored in the floor and, if faced, a wall mirror; then their mip chains
scene pass (HDR)   opaque → decals → skinned outlines → sky → blended (back to front) → queued world-space batches
post-processing    ambient occlusion → volumetric fog → bloom → tone mapping and the look of the map → FXAA → film grain, into the output
```

2D drawing (`FUIRenderer`, `FDebugOverlay`, the editor's ImGui) goes on top of the output afterwards,
each batch in its own small pass.

Everything a pass needs is created or uploaded before the pass begins: creating a buffer or a texture
uploads data, which must not happen inside a render pass.

What `collect` resolves for a draw is looked up once a frame and remembered for the rest of it: the
pipeline of each mesh buffer, material and render state (the lasting cache is keyed by a string of the
vertex layout, too slow to build for every draw of every shadow map), and each lightmap
(`UAssetManager::GetLightmap` resolves the path on disk before it looks in its cache). Done per mesh,
those two took more than half of the CPU time of a frame of LeonHorror's clinic.

## Conventions

- World space is right-handed, +Y up, metres. A camera with yaw −90° looks down −Z.
- Triangles are wound counter-clockwise.
- The engine's matrices use clip depth −1..1 (GLM's default). `ClipDepthZeroToOne()` in
  `FWorldRendererRHI.cpp` converts to Direct3D's 0..1 where a matrix is handed to a shader.
- Shadow and reflection lookups use matrices that go straight from world space to texture space;
  `ClipToShadowMap()` folds in the half-scale, the offset and the flipped V.
- Screen and render-target coordinates have their origin at the top left.
- Texture pixels are uploaded in the order the asset code already produced them, so UVs, lightmap
  coordinates and cube directions sample exactly as before.
- Point-light cube faces are drawn mirrored vertically (`ClipFlipY()`): the face view matrices assume a
  bottom-left image origin.

## Binding model

Fixed for every shader and every backend (`RHITypes.hpp`, `Engine/Shaders/Common.hlsli`):

| Register | Use |
| :--- | :--- |
| `b0` | Frame: camera. `FrameConstants` in `Common.hlsli` |
| `b1` | Pass: lights, fog, shadows, reflections — or a post-process pass's parameters |
| `b2` | Material |
| `b3` | Draw: object transform, lightmap placement, per-draw flags, the reflection probes the object reads, its baked ambient light |
| `b4` | Extra: bone palette |
| `t0`..`t15` | Textures. The surface shader's slots are listed in `PBR_Lit.hlsl` |
| `s0`..`s4` | Static samplers: linear wrap, linear clamp, point clamp, point wrap, shadow comparison |

Constants are copied when they are set, so every draw sees exactly what was set before it. The C++
structure that mirrors a constant buffer sits next to the code that fills it and names the shader file;
keep field order and types identical, and every field a multiple of 16 bytes.

Geometry that is rebuilt every frame uses `SetVertexData` / `SetIndexData`, which copy into the same
per-frame memory.

## Shaders

| File | Draws |
| :--- | :--- |
| `PBR_Lit.hlsl` (+ `PBR_Common.hlsli`, `Shadows.hlsli`, `Skinning.hlsli`) | Surfaces. Compiled again with `SKINNED` for skeletal meshes |
| `ShadowDepth.hlsl` | Shadow casters; `PSMainLinear` for point-light cubes |
| `ReflectionProbeFilter.hlsl` (+ `ReflectionProbes.hlsli`, used by the surface shader) | The levels of a reflection probe's cube, from its capture |
| `Outline_Skinned.hlsl` | Silhouette around a skinned mesh |
| `Skybox.hlsl` | Sky: HDR panorama or procedural gradient |
| `SSAO.hlsl`, `VolumetricFog.hlsl`, `Bloom.hlsl`, `ToneMapping.hlsl`, `FXAA.hlsl` | Post-processing, one entry point per step |
| `Downsample.hlsl` | Mip chains of render targets (`GenerateMips`) |
| `Canvas.hlsl`, `DebugLine.hlsl`, `WorldText.hlsl` | HUD and overlays, debug lines and particles, text in the world |
| `ImGui.hlsl` | The editor's interface |

Shaders are compiled from source when first used (`-HV 2021`, warnings as errors) and kept for the run.

- **Cache on disk.** The compiled code is also written to `Saved/ShaderCache` under the working
  directory, named by a hash of every file in the shader folder and of what was asked for. A second
  run compiles nothing; an edited shader never matches an old entry. `LEON_NO_SHADER_CACHE=1` turns it off.
- **Reload while running.** Builds with assertions look at the shader folder twice a second. Save a
  shader and the next frame is drawn with it: `FShaderLibrary` drops what it holds and whoever keeps
  pipelines (the world renderer, post-processing, 2D drawing, the editor UI) rebuilds them on seeing a
  new `GetGeneration()`. The editor watches the copy staged next to it, not the sources.
- **C++ and HLSL kept in step.** `Tests/RHI/ShaderConstantLayoutTests.cpp` compiles every shader and
  compares the size the compiler reports for each constant buffer with the C++ structure in
  `FShaderConstants.hpp` that fills it. A buffer the test does not know fails it too.
- **GPU time.** `IRHICommandList::BeginGpuTimer` / `EndGpuTimer` bracket work under a slot; the frame
  profiler shows shadows, planar captures, the scene pass and post-processing, and the volumetric fog
  on its own inside post-processing (`EGPUTimerSlot::VolumetricFog`, `fog` on the debug overlay).
Editing a `.hlsl` file and restarting is enough; nothing is rebuilt.

`u_PassFlags.y` selects a debug view of the surface shader (`FWorldRenderer::SetDebugMode`). The numbers
are listed where they are handled in `PBR_Lit.hlsl`: material channels, direct and indirect light, sun
shadow (24), cascades (25), spot shadow (26), point shadow (40), planar capture (13), reflection
probes (41), the reflection as used (42).

## Lights: who gets a slot, who gets a shadow

The surface shader lights a pixel with at most **16 point** and **8 spot** lights
(`MAX_POINT_LIGHTS` / `MAX_SPOT_LIGHTS`), and a frame draws shadow maps for at most **4 spot** lights
(one layer each of a `Texture2DArray`, `t11`) and **4 point** lights (one cube each, `t14`). A world may
hold any number of lights; each frame `FWorldRendererRHI.cpp` gives the slots to the ones that matter
most to the camera. The ranking is `SelectLightsByImportance` (`RenderCore/FLightSelection.hpp`), most
important first:

1. lights whose sphere of influence (position, radius) reaches into the view frustum;
2. **Movable** lights before Stationary and Static ones — so a light gameplay carries around, like the
   player's flashlight, never loses its slot to the lights of the level;
3. more light arriving at the camera (intensity × the shader's distance falloff);
4. nearer to the camera, for lights that do not reach it;
5. the lower entity id, so equal lights always come out in the same order.

A light that held a slot in the previous frame is favoured by 25 % in steps 3 and 4, so two lights of
almost equal importance do not swap every frame while the camera sways.

- Lit slots are filled in registry order whatever the ranking was: a world that fits in the slots is
  drawn the same as if nothing were ranked.
- Shadow maps go to the best ranked of the lit lights that have `bCastShadows`, and only to lights
  that reach the view. `FSpotLight.Params.z` is the light's layer of the spot shadow map and
  `FPointLight.Params.y` its cube (`-1` = no shadows); `u_SpotLightSpaceMatrices[layer]` is the
  matrix of each layer.
- Static lights are skipped altogether while the lightmaps are trusted.
- How many are shadowed at once is a quality setting (`FShadowSettings::MaxShadowedSpotLights`,
  `MaxShadowedPointLights`): High 4 / 4, Medium 2 / 2, Low 1 / 1. The spot shadow map has that many
  layers of `SpotResolution`.

Map keys, in a `SpotLight:` or `PointLight:` block:

| Key | Default | Meaning |
|-----|---------|---------|
| `CastShadows: true\|false` | spot `true`, point `false` | The light may take a shadow map. Written only when it is not the default. |

Point lights do not cast shadows unless the map says so: a cube costs six views of the scene, and the
lights of a baked level have their shadows in the lightmap.

A spot or point light's shadow map draws only the casters the light reaches and whose shadow can fall
in the camera's view ([RENDERER_SHADOWS.md](RENDERER_SHADOWS.md), "Which casters a spot or point
light's map draws").

Limits: the ranking looks at the camera, not at what is on screen, so a strong light at the end of a
long corridor can lose to weaker ones around the camera; a light dropped from the lit slots disappears
at once rather than fading out; a spot light's shadow is lost where the occluder is close to the surface
it shadows and both are several metres from the light (the map stores perspective depth from a near
plane of 0.1 m, which leaves little precision out there, and the bias is larger than what is left).

## Rooms and doorways: what is drawn, what is lit

A level built of rooms can say so: a **zone** is a room (an axis-aligned box), a **portal** is a
doorway between two rooms (a rectangle). The renderer then draws the camera's zone and the zones it sees
through open portals, leaves out the shadow-casting lights that light none of those, and draws into
each shadow map only what its light can shine on. A map without zones is drawn as before, bit for bit;
`FWorldRenderer::SetZoneVisibilityEnabled(false)` makes a map with zones draw as one without. The math
is `RenderCore/FZoneVisibility.hpp` (no device; tested on the CPU), the use of it
`FWorldRendererRHI.cpp`.

Why zones and portals and not something else: the levels are rooms joined by doors, and a door is the
one place a room can be seen or lit from another. Boxes and rectangles written by the map script cost
nothing to author or to bake, hold for moving things and opening doors, and answer both questions
(what the camera sees, where a light reaches) the same way. A precomputed potentially-visible set
would need a bake and could not follow doors; occlusion queries would cost a frame of latency and GPU
time for walls that are always the same.

- **Seen.** The zones holding the camera are seen whole. From there each open portal in view is
  clipped to what of it is in view, and the camera's frustum is narrowed to it: planes through the
  camera and each edge, and the portal's plane. The zone beyond is seen through that narrowed frustum,
  and its own portals are clipped to it in turn. A portal within twice the near clip of the camera
  shows both its zones whole (the near plane can reach through it). A path never crosses one portal
  twice; at most 16 narrowed frustums are kept per zone (more: the zone is taken as seen whole) and
  4096 portal crossings per frame (more: the frame is drawn without zones).
- **Drawn.** An actor of the camera's view (procedural mesh, static mesh, skinned mesh, with its
  outline) is left out when every zone whose box holds its bounds whole (5 cm of slack) is unseen, or
  seen only through narrowed frustums that miss its bounds. An actor no single zone holds (a door
  frame across two rooms, anything outside every zone) is always drawn. Moving actors are placed
  every frame from their bounds; skinned meshes with their bounds grown as for shadows. Particles,
  text and debug drawing are not culled; reflection captures (probes, planar) see everything.
- **Lit.** A light reaches zones the way the camera sees them, from the light's position, every way,
  through open portals within its radius. A shadow-casting spot or point light that reaches no zone
  that is seen (nor, through a portal to the outside, an outside that is seen) takes no lit slot and no
  shadow map that frame (`FFrameTiming::ZoneSkippedLights`). Not while a planar capture is drawn: a
  mirror can show a room the camera does not.
- **Shadowed.** A spot or point light inside a zone that does not shine out of every zone draws into
  its map only the casters that touch a zone it lights, inside what it sees of that zone; casters no
  zone touches are always drawn. Reflection-probe captures use the same rule.
- **Doors.** A portal names the actors that close it (`Doors`). The game reports a door with
  `UWorld::SetPortalDoorClosed(name, bClosed)`: closed when it is fully shut, open as soon as it starts
  to open. When every door a portal names is reported closed, nothing is seen or lit through it. A
  portal without doors (an open archway) is always open.

What this assumes of the level, and why it is then safe. **Zones are sealed**: sight and light pass
from one zone to another only through portals, and every surface seen or lit inside a zone lies in its
box (walls, floor and ceiling included). Then every pixel the camera sees lies in a zone it sees, so an
actor that no seen zone holds covers no pixel. A shadow-casting light that reaches no seen zone adds
nothing to any seen pixel: its shadow map holds the wall between them. And a ray from a light leaves
the zones it lights only through a wall whose surface faces into a lit zone and lies in its box, so the
first surface every ray meets is still drawn into the map and the map is the same along every ray that
reaches anything lit. A room shell made of its own floor, walls and ceiling facing inwards (the
clinic's) is sealed. What is not: an opening without a portal (a hatch, a window between rooms), a
zone box smaller than its room, a wall that only the room behind it models.

What changes in a sealed map with zones: nothing seen, except that a shadow-casting light left out no
longer leaks light through the walls of the room it is shut in (where a wall meets the floor nearer
than the shadow bias, and where the shadow fades on surfaces it grazes); and, as lit slots go to the
lights that count, a light that had no slot may get one.

Map keys (on an actor of class `AVisibilityZone` or `APortal`; the `Transform` places it):

| Block | Key | Default | Meaning |
|-----|-----|---------|---------|
| `VisibilityZone:` | `Extent: [x, y, z]` | `[2, 1.5, 2]` | Half-sizes of the box in metres, around the actor's location. Rotation and scale are not used |
| `Portal:` | `Size: [w, h]` | `[1, 2]` | Width and height of the opening in metres. The rectangle is centred on the actor in its local XY plane: width along local X, height along local Y. Scale is not used |
| `Portal:` | `Doors: ["A", "B"]` | none | Actors (by name) whose doors close the opening. Written only when there are some |

The zones on either side of a portal are found from the boxes: the zones holding the points 0.3 m in
front of and behind its centre. Up to 64 zones (`kMaxVisibilityZones`); a map with more is drawn as
one without.

```text
  - Name: "Zone_Corridor"
    Class: "AVisibilityZone"
    Transform:
      Translation: [7.2, 1.4, -15]
      Rotation: [0, 0, 0]
      Scale: [1, 1, 1]
    VisibilityZone:
      Extent: [1.2, 1.65, 9]
  - Name: "Portal_Ward"
    Class: "APortal"
    Transform:
      Translation: [8.4, 1.025, -9.5]
      Rotation: [0, 90, 0]
      Scale: [1, 1, 1]
    Portal:
      Size: [1.6, 2.05]
      Doors: ["Door_Ward_A", "Door_Ward_B"]
```

Measured on a grid of 16 rooms with a shadowed lamp each (`Tests/GPU/ZoneVisibilityTests.cpp`), see
"Cost of a frame". Limits: zones are boxes, so a room that is not a box needs several zones joined by
portals of the room's full cross-section; the actor is the unit of culling, so a mesh spanning rooms (one
floor under the whole level) is always drawn; a light's reach ignores a spot light's cone; nothing in
the editor draws zones or portals.

## Reflection probes: what a room reflects

Without them every surface reflects the sky (`t_Prefilter`), indoors too: metal and wet floors in a
closed room show a sky that is not there, or nothing when the sky is black. A reflection probe is a
cube map of one room, captured from inside it, that the surfaces in the room reflect instead.

- **Placing one.** An actor of class `AReflectionProbe` (`FReflectionProbeComponent`). The actor's
  location is the centre of an axis-aligned box, `Extent` its half-sizes: make the box the room, wall
  to wall and floor to ceiling. The actor's rotation and scale are not used: boxes cannot be rotated.
  The cube is captured from the centre plus `CaptureOffset` (clamped into the box); move it when the
  centre is inside furniture. Up to 32 probes per map (`kMaxReflectionProbes`); more are ignored with
  a warning.
- **What a capture holds.** The level as it lasts: Static geometry (procedural and imported meshes
  with Static mobility, most detailed level) with its lightmaps, lit by Static and Stationary lights
  with their shadows (static casters only; the sun gets one shadow map around the box), the sky as the
  map has it, fog. Not in it: Movable lights (the flashlight), Movable and Stationary meshes,
  characters, particles, text. Surfaces in a capture reflect the sky, never a probe, so captures do not
  depend on each other and one bounce of specular is all there is.
- **When.** From the first frame that draws the world, two cube faces a frame
  (`SetReflectionProbeCaptureFacesPerFrame`) until every probe has its cube: a face draws the static
  level once more, and a level's worth of them does not fit what the device has for one frame (texture
  tables, constants). Sixteen probes take 48 frames. A probe is read by nothing until its cube is
  complete; one being captured again keeps showing its old cube meanwhile. A probe is captured again
  when its key changes: the hash of the map's bake (`IRenderScene::GetStaticLightingHash`), whether the
  lightmaps are trusted, the capture position, the resolution. So a map without a trusted bake is
  captured once per run. `FWorldRenderer::InvalidateReflectionProbes` captures everything again on
  demand. Moving the box or changing `Intensity` needs no capture.
- **Filtering.** Each face is drawn at the probe resolution into a scratch cube and then filtered into
  the probe's cube of a `TextureCubeArray` (`t15`, RGBA16F, 5 levels): level *n* is the GGX lobe of
  roughness *n*/4 around the mirror direction (`ReflectionProbeFilter.hlsl`), the same convention as
  the sky's prefiltered map, so a surface reads `roughness * 4` in both. The two widest lobes are
  taken from a quarter-size copy of the capture, where a lamp is not a single bright pixel.
- **Cache.** Cubes found on disk are loaded on the first frame, all of them, before anything is
  drawn. With a trusted bake, a captured cube is written to
  `<project>/Saved/ReflectionProbes/<key>.lprobecube` after the frame (`FRHIFrame::RunAfterFrame`:
  a texture cannot be read back while a frame is recorded) and loaded instead of captured from then
  on. The key is in the file name and the header; a new bake, a moved capture point or another
  resolution is another file. Old files are never deleted: clear the folder when it grows. A map
  without a bake, or with a stale one, is captured on every run and nothing is written.
- **Which probe a surface reads.** Chosen per draw, on the CPU (`SelectReflectionProbes`): of the
  probes whose box (plus its fade) touches the object's bounds, the two that count most at the centre
  of the bounds, then by how much of the bounds they cover. The choice depends on the object alone,
  so nothing changes as the camera moves. Their boxes travel in the draw constants
  (`u_ProbeBoxMin/Max`, `u_ProbeCapture`).
- **Per pixel** (`ReflectionProbes.hlsli`, twin in `RenderCore/FReflectionProbeMath.hpp`): a probe
  counts 1 inside its box and fades to 0 over `BlendDistance` outside it. The two probes share the
  pixel in proportion to their weights; what they leave (weights adding up to less than 1) is the
  sky's. The cube is read with parallax correction: the reflection ray is followed to the face of the
  box it hits, and the cube is read towards that point as seen from the capture position.
- **What does not change.** The diffuse ambient (lightmap, or the sky's irradiance), planar
  reflections (they go on top where a material asks for them), and a map without probes: it is drawn
  as before, bit for bit.

Map keys (`ReflectionProbe:` block of an actor; the `Transform` gives the centre):

| Key | Default | Meaning |
|-----|---------|---------|
| `Extent: [x, y, z]` | `[2, 1.5, 2]` | Half-sizes of the box in metres |
| `CaptureOffset: [x, y, z]` | `[0, 0, 0]` | Capture position relative to the centre |
| `Intensity` | 1 | Scales what the probe reflects |
| `BlendDistance` | 0.5 | Metres outside the box over which the probe fades out |

```text
  - Name: "Ward_Probe"
    Class: "AReflectionProbe"
    Transform:
      Translation: [4.5, 1.4, -7.25]
    ReflectionProbe:
      Extent: [3.5, 1.4, 2.25]
      CaptureOffset: [0, 0.2, 0]
```

Quality: the edge of a cube face is 128 / 64 / 32 pixels for High / Medium / Low
(`FGraphicsPreset::ReflectionProbeResolution`); 32 probes at 128 take about 33 MB.
Debug views: 41 shows what the probes give each pixel (black where none reaches), 42 what the pixel
ends up reflecting (probes, sky and planar captures).

Limits: boxes are axis-aligned, so a room at an angle is approximated; a mesh larger than a room (one
floor under several rooms) reads only two probes and reflects the sky beyond them; a door between two
rooms is a blend of two cubes, not a view through the door; the reflection is of the level as
captured, so an open door, a moved crate or a character is not in it; with a black sky the fade
outside the last box goes to black. Placing or moving a probe does not make the lightmap bake stale.
There is nothing in the editor to edit the block; probes are captured again when their actor moves.

## Decals: marks laid over the level's surfaces

A stain, a damp patch, blood, scratches, a marking on a floor: one texture set that can lie on any
wall without the wall needing a second set of maps. A decal is ordinary geometry (a quad, or a strip
fitted to the surface it lies on, on that surface or a millimetre proud of it) with a material whose
`AlphaMode` is `Decal`. It is drawn with the surface shader like everything else: lit by the same
lights, shadowed by the same shadow maps, reflecting the same probes. Nothing is projected in screen
space.

- **Material** (`.lmat`): `AlphaMode: "Decal"`. The alpha of a pixel is the alpha of the albedo map
  times `Opacity`; where it is below half a step of an 8-bit texture the pixel is dropped before any
  lighting is done, so the empty part of a decal's quad costs next to nothing. A normal map and an
  ORM map are the decal's own: the decal is lit with its own normal, roughness and metalness, and
  that lit colour is mixed over the lit surface by the alpha. That is as far as a forward renderer
  goes: the surface under a decal keeps its own normal and roughness, they are not blended into.
- **`SortPriority`** (`.lmat`, a whole number, default 0, may be negative): among decals the lower is
  drawn first, so the higher lies on top where two overlap. Decals of equal priority are drawn in the
  order their actors came into the world, which for a map is the order of the file: the later one is
  on top. Other materials ignore the key; it is written only when it is not 0.
- **Depth.** A decal is tested against the depth buffer and does not write to it. It is drawn pulled
  towards the camera (`kDecalDepthBias`, `kDecalSlopeScaledDepthBias` in `FWorldRendererRHI.cpp`), so
  a decal in the very plane of its surface never fights it, at any angle. The pull is about a
  millimetre at 5 m and grows with the square of the distance (two centimetres at 20 m): a decal
  does not show through something standing in front of it unless that thing is closer to it than
  that.
- **Order in the frame.** After the opaque surfaces, before skinned outlines, the sky and every other
  blended surface (glass, particles, world text): those are drawn over the decals. Blended surfaces
  are not sorted against decals: a decal in front of a pane of glass is still drawn before it.
- **What a decal is left out of.** Shadow maps (it casts no shadow), ambient occlusion and the
  volumetric fog (both read the depth buffer, which it does not touch), and the lightmap bake (below).
  Reflection captures draw the decals they collect, after their opaque surfaces: a planar reflection
  shows decals that are not Movable, a reflection probe those that are Static. Placed as Movable, as
  below, a decal is in neither.
- **Placing one.** A static mesh actor, Movable, without collision and without a lightmap, whose
  material is the decal:

```text
  - Name: "Decal_BloodPool_Ward"
    Class: "AActor"
    Transform:
      Translation: [4.2, 0.001, -7.6]
      Rotation: [0, 35, 0]
    StaticMesh:
      Asset: "/Game/Meshes/SM_Decal_Floor.lmesh"
      CastShadows: false
      Collision: false
      Mobility: Movable
      MaterialOverrides:
        - Slot: 0
          Asset: "/Game/Materials/M_DecalBloodPool.lmat"
```

The override is only needed where the mesh does not name the decal material itself.

```text
Material:
  AlphaMode: "Decal"
  Opacity: 0.9
  SortPriority: 2
  AlbedoMap: "/Game/Textures/T_DecalBloodPool_Color.ltex"     # its alpha is the shape of the decal
  NormalMap: "/Game/Textures/T_DecalBloodPool_Normal.ltex"
  ORMMap: "/Game/Textures/T_DecalBloodPool_ORM.ltex"
```

- **Ambient light.** A decal has no lightmap, so its ambient light is that of the map's light probes,
  one sample for the whole decal at the middle of its bounds (next section). Its direct light is what
  the lights give it at run time: under a Static light, which is only in the lightmaps, a decal
  takes the light of the probes and nothing else.
- **Bakes.** The baker leaves out every actor whose materials are all decals
  (`FLightmapBakeInputs::IsDecal`), whatever its mobility: no chart in the lightmap, no shadow, no
  bounce. It is left out of the bake hash too, so decals can be added to a baked map, moved and
  removed without a new bake.

Limits: the ambient light of a decal is one sample, not the lightmap of the wall under it, so a decal
in a baked shadow (under a bed) is lit like the open floor next to it and can look lighter than the
wall around it where its alpha is high; a decal is flat geometry and does not wrap around a corner
unless its mesh does; draw calls are not instanced, one per decal; in the debug views of the surface
shader a decal covers its whole quad where its alpha is not zero.

## Ambient light for characters and moving things

What has no lightmap takes its diffuse ambient light from the map's light probes when it has them:
one sample per draw at the middle of its bounds (`IRenderScene::SampleLightProbes`), twelve numbers in
the draw constants, evaluated with the pixel's normal in `PBR_Lit.hlsl` in place of the sky's
irradiance. Draws that would otherwise be instanced together are only batched when their samples are
equal. Debug view 35 shows the term. How the probes are baked, stored and trusted is in
[STATIC_LIGHTING.md](STATIC_LIGHTING.md).

## Light in the air: volumetric fog

The beam of the flashlight and the cones under the ceiling lamps seen in the air of a room, with the
dark shafts that whatever stands in them cuts out. A map asks for it with a `VolumetricFog:` block
(`FVolumetricFogComponent` on the Environment actor, settings in `RenderCore/FVolumetricFogSettings.hpp`);
a map without the block, or with `Enabled: false`, is drawn as before, bit for bit.

Three steps of `VolumetricFog.hlsl` in `FPostProcessRHI`, after ambient occlusion and before bloom
(so a bright beam glows like any bright thing, and occlusion does not darken the air in front of a
surface). They are part of the scene, not of the look: they stay when post-processing is off, and
they are skipped in the surface shader's debug views and post-process debug view 5.

1. **`PSScatter`**, at half the output's size (RGBA16F). Each pixel follows its view ray from the
   camera to the surface the depth buffer has there (or `MaxDistance`, whichever is nearer; the sky is
   at `MaxDistance`). For each light it cuts the ray to the part the light reaches (its sphere, and
   for a spot light its cone) and takes a fixed number of steps along that part, started at a
   different fraction of a step in each pixel (interleaved gradient noise). Each step adds
   phase (Henyey-Greenstein, `Anisotropy` = g) × the light arriving (colour × intensity, the surface
   shader's distance falloff, the spot's cone) × the light's shadow map at that point ×
   `exp(-Density × distance to the camera)`. The sum, times the step length, times
   `Density × Scattering`, is the light the air sends towards the camera (rgb). Alpha is how much of
   the scene behind is left: `exp(-Density × length)`, and the air's own ambient glow
   `AmbientColor × AmbientIntensity × Scattering × (1 - that)` is added to rgb.
2. **`PSBlur`**, twice (horizontal, vertical): seven taps, each weighed by how close its depth is to
   the centre's, so the noise of the few steps becomes a smooth beam without bleeding across edges.
3. **`PSComposite`** at full size, blended over the HDR scene as `scene × a + rgb`. Each output pixel
   takes the four half-size texels around it, weighed by distance and by whether they were marched to
   the surface this pixel shows (depth); where none was (a thin thing in front of a far wall) the one
   closest in depth.

- **Which lights.** Only those that have a shadow map this frame: up to 4 spot lights (`t_SpotShadow`
  layers, the flashlight included) and up to 4 point lights (`t_PointShadow` cubes), the same ones
  the surfaces are shadowed by (previous section). A light without a shadow map would shine through
  the walls into the next room's air, so it lights surfaces but not the air. The sun is not followed.
- **With the map's distance fog** (`FogEnabled` in `Skybox:`): the distance fog has already dimmed the
  surfaces and tinted them with `FogColor` in the surface shader. The volumetric pass then hides
  nothing and adds no ambient glow of its own (alpha 1, `AmbientColor` unused): it adds only the
  light of the lamps, dimmed by its own `Density` on the way to the camera. The surfaces are fogged
  once. Without the distance fog, the volumetric pass does both itself: `Density` hides what is far
  and `AmbientColor × AmbientIntensity` is the colour the air turns. Use one or the other for the
  haze of a map; the volumetric block's `Density` then decides how much the beams show.
- **Decals and glass.** The fog of a pixel is that of the depth buffer's surface: a decal (which does
  not write depth) is fogged like the wall it lies on; a blended surface (glass, particles) like what
  is behind it.
- **Cost.** Two half-size RGBA16F targets (about 8 MB at 1080p), created by the first frame that draws
  fog. Measured with the GPU timers on the RTX 5060 laptop GPU at 1920 × 1080 with 12 steps and every
  shadow map of the High preset in view (4 spot + 4 point lights): 0.6 to 1.0 ms over six runs
  (`Tests/Shader/VolumetricFogTests.cpp`, run with `LEON_TEST_GPU=1`). The march costs per light
  per pixel; a frame with only the flashlight costs less.

Map keys, all optional (`VolumetricFog:` under `Environment:`):

| Key | Default | Meaning |
|-----|---------|---------|
| `Enabled` | `false` | The map has fog to show light in. Off draws nothing |
| `Density` | 0.03 | Extinction per metre. 0.02 is clean indoor air with a beam just visible, 0.1 a dusty room. 0 draws nothing |
| `Scattering` | 1 | How much of what the air takes from a beam it sends on. Above 1 brightens beams without thickening the air |
| `Anisotropy` | 0.3 | Henyey-Greenstein g, held to -0.95..0.95. Towards 1 a beam is bright looked into and faint from behind |
| `AmbientColor`, `AmbientIntensity` | `[1, 1, 1]`, 0 | The air's own glow outside the beams; unused under the distance fog |
| `MaxDistance` | 30 | Metres from the camera beyond which the air is not looked at |
| `Steps` | not set | Steps per light along a view ray, 4..64. Not set: the quality preset's |

```text
Environment:
  Skybox:
    ...
  VolumetricFog:
    Enabled: true
    Density: 0.04
    Scattering: 1
    Anisotropy: 0.5
    AmbientColor: [1, 1, 1]
    AmbientIntensity: 0
    MaxDistance: 20
```

Quality (`FGraphicsPreset::bEnableVolumetricFog`, `VolumetricFogSteps`): Low off, Medium 8 steps, High
12. `FWorldRenderer::SetVolumetricFogEnabled` / `SetVolumetricFogSteps` set them directly.

Limits: one density everywhere (no height falloff, no noise, nothing local to a room); the sun and
lights without a shadow map are not in the air; the air between the camera and a glass pane is
marched to the wall behind it; the march is fixed-step per light, so a beam that crosses a long
stretch of a view ray far from its lamp is sampled sparsely and its edges can shimmer as the camera
moves (there is no temporal filter); the composite does not reach the reflection captures, so a
mirror shows the room without fog. Nothing in the editor edits the block.

## The final image: the look of a map

A map can say how its image is finished. The settings are an `FPostProcessComponent` on the Environment
actor (`PostProcess:` in the map, next to `Skybox:`); the world renderer lays them over its own
`FPostProcessSettings` for the frame, without changing those. A map without the block is drawn as
before it existed: every part of the look is neutral by default and a neutral part is skipped by the
shader, so the image is the same bit for bit. The look is not applied to the post-process debug views
nor when post-processing is off.

Where each part happens, and why there:

- **Chromatic aberration, colour grading, vignette**: in the tone mapping pass (`ToneMapping.hlsl`).
  Aberration reads red and blue of the HDR scene a little further from / nearer to the centre; grading
  and the vignette work on the encoded colour, before the luma that anti-aliasing reads is written.
- **Bloom** drops what it cannot work with in its bright pass (`Bloom.hlsl`, `UsableSceneColor`): a
  pixel that is not a number or infinite glows with nothing, and one brighter than 512 is cut down to
  that. The levels below average their neighbours, so one such pixel would otherwise blank a
  rectangle at every level.
- **Film grain**: at the end of the FXAA pass (`FXAA.hlsl`), after the anti-aliased colour is resolved.
  Before it, FXAA would take grain for edges and smear it. The pass runs (as a copy) with FXAA off too.

Map keys, all optional (`PostProcess:` under `Environment:`):

| Key | Default | Meaning |
|-----|---------|---------|
| `ToneMapper` | `ACES` | `ACES`, `Reinhard`, `Clamp` or `Uncharted2` |
| `Exposure` | not set | Replaces `Skybox.Exposure` when present |
| `BloomIntensity`, `BloomThreshold` | 0.05, 1 | As in `FPostProcessSettings` |
| `VignetteIntensity` | 0 (off) | How much the corners darken, 0..1 |
| `VignetteRadius`, `VignetteSoftness` | 0.5, 0.5 | Where it starts and how far it takes to reach full strength; 0 = centre, 1 = corners |
| `FilmGrainIntensity` | 0 (off) | Amplitude in display values; the noise is uniform, the same on the three channels, mean zero |
| `FilmGrainAnimated`, `FilmGrainSeed` | `true`, 0 | Animated: the pattern is seeded with seed + frame number. Not animated: with the seed alone |
| `Lift`, `Gamma`, `Gain` | `[0,0,0]`, `[1,1,1]`, `[1,1,1]` | `c = (Gain * (c + Lift * (1 - c))) ^ (1 / Gamma)`: lift moves black, gain moves white, gamma the middle |
| `Contrast` | 1 | `c = (c - 0.5) * Contrast + 0.5`, after lift/gamma/gain |
| `Saturation` | 1 | Mix between the pixel's grey (Rec. 709 weights) and its colour, after contrast; 0 = black and white |
| `ChromaticAberration` | 0 (off) | Red is read at `uv + (uv - 0.5) * value`, blue at `uv - (uv - 0.5) * value`: zero in the centre |

Limits: the project switches (`[/Script/Engine.RendererSettings]`: SSAO, bloom, FXAA) and the gamma stay
with the project, not the map; grading is three numbers per control, not a lookup table; the grain is
per pixel of the output, so it is finer at higher resolutions; aberration shifts the scene, not the
bloom added to it. Nothing in the editor edits the block yet.

## Cost of a frame

`Tests/GPU/ClinicMeasureTests.cpp` loads LeonHorror's clinic with the engine alone (the game's classes
are not registered: no characters, no enemies; a Movable spot light stands in for the flashlight),
applies the settings the game runs with (High, no planar reflections, one cascade, PCF 5x5) and draws
200 frames at 1920 x 1080 at each of four places, printing draw calls, shadow-map draw calls, the CPU
time of `FWorldRenderer::Render` and its stages, and GPU time. It runs only when asked, on the
graphics card, best from a Release build:

```text
LEON_TEST_GPU=1 LEON_MEASURE_CLINIC=1 out/build/dev-release/bin/RendererTests.exe -tc="*clinic*"
```

It alternates blocks of frames with `bCullShadowCasters` off and on, so both see the same clock drift
of a laptop. On the RTX 5060 laptop GPU, medians of three runs. CPU render: before the per-frame
lookups, then with them and the caster culling. GPU: culling off, then on, in the same runs (GPU time
differs more between runs than that):

| Place (plan coordinates) | Draw calls | Shadow draws | CPU render | GPU |
| :--- | ---: | ---: | ---: | ---: |
| corridor (7.2, 8.6), north | 2304 → 1739 | 1803 → 1238 | 9.1 → 3.9 ms | 7.0 → 6.8 ms |
| ward (11.4, 9.0), north | 2273 → 1764 | 1829 → 1320 | 8.8 → 4.8 ms | 6.9 → 6.8 ms |
| reception (2.2, 2.3), north | 2435 → 2012 | 1841 → 1418 | 8.9 → 6.1 ms | 6.4 → 6.3 ms |
| reception (2.2, 2.3), east | 1102 → 662 | 1003 → 563 | 4.6 → 3.0 ms | 4.7 → 4.6 ms |

The CPU time fell with the per-frame lookups ("A frame"); with them, drawing a shadow caster costs
under a microsecond, and the caster culling changes the CPU time by less than the runs differ. Its
gain is the draw calls and a little GPU time.

Zones ("Rooms and doorways") cannot be measured on the clinic until its map has them. On the grid of
`Tests/GPU/ZoneVisibilityTests.cpp` (16 rooms of 6 m joined by 24 doorways, a dozen props and a lamp
casting shadows in each, the flashlight; looking from a corner room down a row), zones off then on:
509 → 372 draw calls (its props are drawn instanced: 1716 → 972 triangles), 500 → 364 shadow draws,
4 rooms seen of 16, 62 actors and 8 shadowed lamps left out; CPU render 1.9–2.2 → 1.7–2.0 ms (Release,
RTX 5060, three runs of 100 frames, `LEON_TEST_GPU=1`).

## Checking a change

- `RendererTests` draws whole frames of a small world on WARP with the Direct3D debug layer on
  (`Tests/GPU/WorldRendererTests.cpp`) and fails on any validation message. `LEON_TEST_GPU=1` runs the
  same on the graphics card.
- Headless tests cannot tell whether a frame looks right. Launch the game and look:
  `LeonHorror.exe --validate-seconds=N` exits by itself; Sandbox has a dynamic sun, a reflective floor
  and world text; LeonTournament's menu exercises the UI.

## Not done yet

- The sun's shadow bias is a fraction of the cascade's depth range, which is eight times its radius;
  with the default settings objects about a metre tall cast no visible sun shadow.
