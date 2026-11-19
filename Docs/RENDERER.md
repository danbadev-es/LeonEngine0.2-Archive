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
shadow maps        sun: up to 4 cascades · up to 4 spot lights · up to 4 point lights (cube maps)
reflection captures the scene mirrored in the floor and, if faced, a wall mirror; then their mip chains
scene pass (HDR)   opaque → skinned outlines → sky → blended (back to front) → queued world-space batches
post-processing    ambient occlusion → bloom → tone mapping → FXAA, into the output
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
| `b3` | Draw: object transform, lightmap placement, per-draw flags |
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
shadow (24), cascades (25), spot shadow (26), point shadow (40), planar capture (13).

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
