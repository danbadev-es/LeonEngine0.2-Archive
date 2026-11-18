# 0008 — Mesh and texture classes of the asset code are kept, backed by the RHI

Date: 2026-11-18. Status: accepted for now.

## Context

Meshes, materials, importers, serialisation and many tests are written against `FVertexBuffer`,
`FIndexBuffer`, `FVertexArray`, `FTexture2D`, `FTextureCube` and `FShader`, created through a driver so
that tools and tests can run with none (the factories then return null).

## Decision

Those classes stay. `FLegacyRenderDriver` implements them on top of the RHI: buffers and textures keep
their data in memory and create the device object the first time the renderer asks for it
(`GetRHIVertexBuffer`, `GetRHITexture`, ...). Image files are decoded by the asset code
(`FImageFileLoader`) and handed over as pixels with their mip chain.

The interfaces that only made sense for OpenGL were deleted: `FRenderCommand`, `IRenderAPI`,
`FFramebuffer`, `FUniformBuffer`, `IGraphicsContext`.

## Consequences

- The asset code did not have to change for the port.
- `FShader`, which had become only a name, has since been removed with the `.glsl` files it named. Maps
  written before still carry a `Shader:` line; it is read and ignored.
- Pixels stay in memory until a texture is first drawn; a texture that is never drawn keeps its copy.
- "Legacy" in the names is honest about the intent: new code should use the RHI directly.
