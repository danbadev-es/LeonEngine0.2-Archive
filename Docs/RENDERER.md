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
collect            visible mesh sections, resolved to pipelines, buffers and textures; shadow casters
lights             the point and spot lights that get the shader's slots, by importance for the camera
probe captures     only when a reflection probe has no cube yet: six views of the static level each
shadow maps        sun: up to 4 cascades · up to 4 spot lights · up to 4 point lights (cube maps)
reflection captures the scene mirrored in the floor and, if faced, a wall mirror; then their mip chains
scene pass (HDR)   opaque → skinned outlines → sky → blended (back to front) → queued world-space batches
post-processing    ambient occlusion → bloom → tone mapping and the look of the map → FXAA → film grain, into the output
```

2D drawing (`FUIRenderer`, `FDebugOverlay`, the editor's ImGui) goes on top of the output afterwards,
each batch in its own small pass.

Everything a pass needs is created or uploaded before the pass begins: creating a buffer or a texture
uploads data, which must not happen inside a render pass.

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
| `b3` | Draw: object transform, lightmap placement, per-draw flags, the reflection probes the object reads |
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
| `SSAO.hlsl`, `Bloom.hlsl`, `ToneMapping.hlsl`, `FXAA.hlsl` | Post-processing, one entry point per step |
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
  profiler shows shadows, planar captures, the scene pass and post-processing.
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

Limits: the ranking looks at the camera, not at what is on screen, so a strong light at the end of a
long corridor can lose to weaker ones around the camera; a light dropped from the lit slots disappears
at once rather than fading out; a spot light's shadow is lost where the occluder is close to the surface
it shadows and both are several metres from the light (the map stores perspective depth from a near
plane of 0.1 m, which leaves little precision out there, and the bias is larger than what is left).

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
- **When.** On the first frame that draws the world, all probes at once, and again for a probe whose
  key changes: the hash of the map's bake (`IRenderScene::GetStaticLightingHash`), whether the
  lightmaps are trusted, the capture position, the resolution. `FWorldRenderer::InvalidateReflectionProbes`
  captures everything again on demand. Moving the box or changing `Intensity` needs no capture.
- **Filtering.** Each face is drawn at the probe resolution into a scratch cube and then filtered into
  the probe's cube of a `TextureCubeArray` (`t15`, RGBA16F, 5 levels): level *n* is the GGX lobe of
  roughness *n*/4 around the mirror direction (`ReflectionProbeFilter.hlsl`), the same convention as
  the sky's prefiltered map, so a surface reads `roughness * 4` in both. The two widest lobes are
  taken from a quarter-size copy of the capture, where a lamp is not a single bright pixel.
- **Cache.** With a trusted bake, a captured cube is written to
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
