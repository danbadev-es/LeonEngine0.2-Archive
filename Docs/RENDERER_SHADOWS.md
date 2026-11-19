# Shadow System

Cascaded shadow maps (CSM), up to four shadowed spotlights, and up to four point-light cubemaps for LeonEngine2. Contracts: [RENDERER_CONTRACT.md](RENDERER_CONTRACT.md). Remediation: [RENDERER_REMEDIATION.md](RENDERER_REMEDIATION.md).

## Overview

- **Master flag**: `FShadowSettings.bEnableShadows`. When false, CSM / spot / point passes are skipped and `u_ShadowFlags` (x sun, y spot, z point) stays 0.
- **CSM**: 4-slice 2D texture array, `D32_Float` (resolution from `FShadowSettings`, often 2048²). Unused layers are cleared and padded with the last valid light-space matrix. `u_ShadowSettings.z` is `CascadeCount` (shader clamps index and fade to that count).
- **Stabilization**: frustum-slice bounding sphere + texel snapping to reduce swimming.
- **Splits**: Practical (hybrid log/linear) scheme, λ ≈ 0.85.
- **Bias**: constant + slope-scale + normal offset.
- **Filters**: Hard, PCF 3×3, PCF 5×5, Poisson disk (16 fixed offsets, rotated per pixel by interleaved gradient noise). PCF / Poisson taps with UV outside `[0,1]` are skipped and the kernel is renormalized (nothing outside the map is counted as lit).
- **Cascade blend**: mix over `max(cascadeLength × CascadeBlendWidth, 3 m)` (capped); do not blend `index+1` when it is past `CascadeCount`.
- **Far fade**: linear fade ~15 m past the **last valid** split (not always cascade 3).
- **Spot**: 2D texture array, max **4** layers (`kMaxShadowedSpotLights`), one per shadowed light (`params.z`, `-1` = none), `SpotResolution` each. Quality: Low 1×512², Medium 2×1024², High 4×1024². The lights that get a layer are the most important for the camera among those with `bCastShadows` ([RENDERER.md](RENDERER.md), "Lights").
- **Point**: cube-map array, max **4** cubes (`kMaxShadowedPointLights`). Linear depth `length(world-light)/radius`. Texture register `t14`. Quality: Low 1×256², Medium 2×512², High 4×512². The most important point lights with `bCastShadows` (off by default) receive cube slots (`params.y`); others stay `-1`.
- Contact shadows: removed.

CSM uses front-face cull + polygon offset `(2, 4)`. Spot and point faces use back-face cull. Double-sided materials disable culling in the shadow pass. Pass entry/exit calls `ResetDefaultMeshRasterState()`.

Casters are AABB-culled against each cascade / spot / point-face frustum. Skinned casters inflate bind-pose AABB by `kSkinnedShadowBoundsPadding` (1.35). Masked materials (`EAlphaMode::Mask`) discard in the depth shader.

## Practical split scheme

For \(N\) partitions between \(z_{\text{near}}\) and shadow far \(z_{\text{far}}\):

$$z_{\text{log}, i} = z_{\text{near}} \left( \frac{z_{\text{far}}}{z_{\text{near}}} \right)^{i / N}$$
$$z_{\text{lin}, i} = z_{\text{near}} + (z_{\text{far}} - z_{\text{near}}) \frac{i}{N}$$
$$z_i = \lambda z_{\text{log}, i} + (1 - \lambda) z_{\text{lin}, i}$$

## Bounding sphere & texel snap

1. Eight world corners of each slice from inverse view-projection.
2. Centroid \(\mathbf{C}\) and radius \(R\).
3. Snap light-space \(\mathbf{C}\) to texel size \(2R / \text{Resolution}\).

## Bias

Shadow tests use the **rasterized face normal** (fallback: vertex normal). Detail normals would not match the depth map.

$$\text{slopeFactor} = 1.0 - \max(\mathbf{N}_{\mathrm{face}} \cdot \mathbf{L}, 0.0)$$
$$\tan\theta = \min\left(\frac{\sin\theta}{\max(\mathbf{N}_{\mathrm{face}} \cdot \mathbf{L},\, 0.08)},\, 8\right)$$
$$\mathbf{p}' = \mathbf{p} + \mathbf{N}_{\mathrm{face}} \cdot (\text{normalBias} \cdot \text{slopeFactor})$$
$$\text{bias} = \text{constBias} + \text{slopeBias} \cdot \tan\theta$$

PCF taps add a receiver-plane depth prediction. Each tap is a hardware comparison filtered between the four nearest texels (`SampleCmpLevelZero`); the 3×3, 5×5 and Poisson kernels are built from those taps in the shader. Grazing receivers fade with \(\mathrm{smoothstep}(0, 0.22, \mathbf{N}_{\mathrm{face}}\cdot\mathbf{L})\). Defaults: `ConstantBias=0.001`, `SlopeBias=0.0035`, `NormalBias=0.04`.

**Known limit:** for the sun, the bias is scaled by the depth range of each cascade (`SampleCascade` in
`Shadows.hlsli`), which is eight times the cascade's radius. With the default values, objects about a
metre tall cast no visible sun shadow. Lower `ConstantBias` / `SlopeBias` for scenes that need them.

Point cubes store **linear** depth (not clip-Z) so hardware `samplerCubeShadow` is not used.

## Debug modes

| Mode | Name |
| :--- | :--- |
| 24 | Direct shadow factor |
| 25 | Cascade false-color |
| 26 | Spot shadow factor |
| 27–30 | Cascade depth visualizations |
| 40 | Point shadow factor |

Runtime: `F8` cycles shadow debug views (mask, CSM color, spot, point, cascade depths). `F12` resets to lit. `FGameplayDebugger` physics overlay draws cascade and spot wire frustums. Shadow filter is a graphics setting, not an F-key.
