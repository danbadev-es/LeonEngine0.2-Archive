# 0006 — The editor draws Dear ImGui through the RHI

Date: 2026-11-18. Status: accepted. Differs from the migration plan, which named `imgui_impl_dx12`.

## Decision

`FImGuiRHI` (in the editor) renders ImGui's draw data with the engine's own RHI: one pipeline, one
shader (`ImGui.hlsl`), ImGui's vertex and index buffers uploaded per frame. A texture is given to ImGui
as its `IRHITexture` pointer. Only ImGui's platform backend for GLFW is built.

`imgui_impl_dx12` would have needed the Direct3D device, command list and a descriptor heap of its own
exposed from the backend, and a second way of turning engine textures into something ImGui can show.

## Consequences

- The viewport and thumbnails are ordinary RHI textures; showing one is a cast.
- The editor contains no Direct3D code and would follow the engine to another backend.
- ImGui features that rely on a stock renderer backend (multiple OS windows, user draw callbacks) are
  not supported. The editor uses neither.
