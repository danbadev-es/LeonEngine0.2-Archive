# Architecture decisions

One file per decision: what was decided, why, and what it costs. A decision is changed by adding a new
file that supersedes the old one, not by editing history.

| # | Decision |
| :--- | :--- |
| [0001](0001-direct3d-12-only.md) | Direct3D 12 is the only graphics API; OpenGL was removed |
| [0002](0002-hlsl-compiled-with-dxc.md) | Shaders are HLSL, compiled with DXC |
| [0003](0003-explicit-rhi-with-a-fixed-binding-model.md) | An explicit RHI with a fixed binding model |
| [0004](0004-clip-space-converted-at-the-rhi-boundary.md) | Engine matrices keep −1..1 depth; conversion happens where a shader receives them |
| [0005](0005-per-frame-geometry-through-the-command-list.md) | Geometry rebuilt every frame is uploaded through the command list |
| [0006](0006-imgui-drawn-through-the-rhi.md) | The editor draws Dear ImGui through the RHI |
| [0007](0007-one-build-tree-and-enforced-modules.md) | One CMake tree; modules are targets with declared dependencies |
| [0008](0008-asset-resource-classes-kept-as-a-bridge.md) | Mesh and texture classes of the asset code are kept, backed by the RHI |
