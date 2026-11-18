# 0003 — An explicit RHI with a fixed binding model

Date: 2026-11-18. Status: accepted.

## Context

The old render interface had the shape of OpenGL: global state (`SetDepthMask`, `SetCulling`), uniforms
set by name, numbered texture units, framebuffers by integer id. Direct3D 12 does not work that way, so
writing "another backend" behind that interface was not an option.

## Decision

The RHI (`IRHIDevice`, `IRHICommandList`) is explicit:

- State lives in immutable pipeline objects. Nothing is bound globally.
- Drawing happens inside render passes that name their targets and how they are cleared. A texture drawn
  in one pass can be sampled in the next; the backend tracks resource states.
- The binding model is fixed for every shader: five constant slots ordered by how often they change
  (frame, pass, material, draw, extra), sixteen texture slots, five static samplers. There is one root
  signature and no per-shader layout to describe or reflect.
- Constants are copied into per-frame memory when set, so a draw always sees what was set before it.
  This removes the class of bug where a draw reads the previous draw's data.
- Resources are reference counted and released only after the GPU has finished with them.

## Consequences

- A shader cannot use more than sixteen textures or a sixth constant buffer. The surface shader uses
  fourteen texture slots today.
- Resource states are tracked per texture, not per mip or slice. The one operation that needs levels of
  a texture in different states, mip generation, is done inside the backend (`GenerateMips`).
- There are no bindless resources, compute shaders or multiple render queues. They were not needed to
  reach parity with the previous renderer.
