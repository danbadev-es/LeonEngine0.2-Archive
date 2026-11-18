# 0001 — Direct3D 12 is the only graphics API

Date: 2026-11-18. Status: accepted.

## Context

The first play session of LeonHorror hit three bugs that only existed with OpenGL on the laptop's Intel
graphics: integer vertex attributes arriving as garbage, a uniform buffer read one update late, and a
spot shadow map that came out empty. All tests were green, because none of them looked at a frame.

## Decision

The engine renders with Direct3D 12 and nothing else. The OpenGL backend, glad and the OpenGL ImGui
backend were deleted rather than kept as a second backend or as a reference. The engine is Windows-only.

## Consequences

- One code path to test and debug, with the Direct3D debug layer available everywhere, including a
  software adapter (WARP) that needs no GPU.
- Between deleting OpenGL and finishing the port there was no image; the old renderer could not be run
  side by side to compare. Captures taken before the deletion are the only visual reference.
- Other platforms would need a new RHI backend. The RHI was shaped so that one is possible
  ([0003](0003-explicit-rhi-with-a-fixed-binding-model.md)), but none is planned.
