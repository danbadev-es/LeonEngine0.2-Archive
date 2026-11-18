# 0005 — Geometry rebuilt every frame is uploaded through the command list

Date: 2026-11-18. Status: accepted.

## Context

The HUD, debug overlays, world-space text, debug lines, particles and the editor's interface rebuild
their vertices every frame and submit several batches per frame. With a dynamic vertex buffer rewritten
per batch, draws recorded earlier in the frame would read the data of the last batch, because drawing
is deferred until the frame is submitted.

## Decision

`IRHICommandList::SetVertexData` and `SetIndexData` copy the data into the frame's upload memory, the
same memory constants use, and bind it. There is no buffer object to create, size or keep alive.

Code that draws without being handed a command list (the HUD is called from gameplay code) finds the
current one through `FRHIFrame`, which the owner of the frame fills in.

World-space batches (`FSceneBatchesRHI`) are collected during the frame and drawn by the world renderer
at the end of its scene pass, so they share the scene's depth buffer and post-processing. 2D batches
(`FCanvasRHI`) are drawn directly over the frame's output.

## Consequences

- One block cannot exceed a page of per-frame memory (4 MB). The editor's largest list is about 1.7 MB.
- World-space batches submitted after the world has been drawn appear one frame later.
