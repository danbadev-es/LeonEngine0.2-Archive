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
shadow maps        sun: up to 4 cascades · one spot light · up to 4 point lights (cube maps)
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
Editing a `.hlsl` file and restarting is enough; nothing is rebuilt.

`u_PassFlags.y` selects a debug view of the surface shader (`FWorldRenderer::SetDebugMode`). The numbers
are listed where they are handled in `PBR_Lit.hlsl`: material channels, direct and indirect light, sun
shadow (24), cascades (25), spot shadow (26), point shadow (40), planar capture (13).

## Checking a change

- `RendererTests` draws whole frames of a small world on WARP with the Direct3D debug layer on
  (`Tests/GPU/WorldRendererTests.cpp`) and fails on any validation message. `LEON_TEST_GPU=1` runs the
  same on the graphics card.
- Headless tests cannot tell whether a frame looks right. Launch the game and look:
  `LeonHorror.exe --validate-seconds=N` exits by itself; Sandbox has a dynamic sun, a reflective floor
  and world text; LeonTournament's menu exercises the UI.

## Not done yet

- GPU timings: the profiler's GPU columns read zero.
- Shader cache on disk, and a check that each C++ constant structure matches its HLSL block.
- The sun's shadow bias is a fraction of the cascade's depth range, which is eight times its radius;
  with the default settings objects about a metre tall cast no visible sun shadow.
