# 0004 — Engine matrices keep −1..1 depth; conversion happens where a shader receives them

Date: 2026-11-18. Status: accepted. Differs from the migration plan, which called for
`GLM_FORCE_DEPTH_ZERO_TO_ONE` across the engine.

## Context

Direct3D clip space has depth 0..1 and render targets with the origin at the top left; the engine's
projection matrices (GLM defaults) produce depth −1..1, and a lot of CPU code depends on that: frustum
plane extraction, cascade fitting, picking, the lightmap baker, and tests with hand-built matrices.

## Decision

Projection matrices stay as they are. Three small functions in `FWorldRendererRHI.cpp` convert at the
point where a matrix is given to a shader:

- `ClipDepthZeroToOne()` — for anything rasterised.
- `ClipToShadowMap()` — world to texture space for shadow and reflection lookups, including the flipped V.
- `ClipFlipY()` — for cube faces drawn with view matrices that assume a bottom-left image origin.

## Consequences

- No CPU-side maths changed, so none of it had to be re-verified.
- The conversion is one matrix multiply per view and is easy to find.
- Reversed depth, which would improve precision at distance, would be simpler with 0..1 projections
  throughout. If it is ever wanted, this decision should be revisited rather than worked around.
