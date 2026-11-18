# 0002 — Shaders are HLSL, compiled with DXC

Date: 2026-11-18. Status: accepted.

## Decision

Shaders are written in HLSL (`Engine/Shaders`) and compiled to DXIL with the DirectX Shader Compiler
that ships with the Windows SDK (`dxcompiler.dll`, `dxil.dll`, copied next to every executable that
draws). The alternative, keeping GLSL and cross-compiling it, was rejected: two shading languages in
the pipeline for a single-API engine.

Compilation uses `-HV 2021`, column-major matrices (`-Zpc`) and warnings as errors.

## Consequences

- Matrices are written `mul(Matrix, Vector)` and uploaded as GLM stores them, with no transposition.
- Variants are made with defines (`SKINNED`), not by copying files; the OpenGL renderer had separate
  skinned copies of the surface and shadow shaders.
- Shaders are compiled at start-up, every run. A disk cache is not implemented yet.
- The compiler is a development dependency of the runtime today; a shipped game should carry compiled
  shaders instead.
