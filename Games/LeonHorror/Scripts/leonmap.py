"""
Writing .lmap files from Python.

A level script describes the level with the calls below and gets the map text back. Positions are
in engine space: X right, Y up, -Z forward, metres. Meshes come from the art build
(Tools/build_art.py); what each one needs to be placed (its lightmap resolution) is read from the
.json the art build leaves beside the FBX.

Coming from Blender: a point (x, y, z) there is (x, z, -y) here, and a turn of `a` degrees about
Blender's Z is a yaw of `a` degrees.
"""

from __future__ import annotations

import hashlib
import json
from pathlib import Path


def num(x: float) -> str:
    if abs(x - round(x)) < 1e-6:
        return str(int(round(x)))
    return f"{x:.6g}"


def vec(v) -> str:
    return "[" + ", ".join(num(c) for c in v) + "]"


def quote(text: str) -> str:
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


def from_blender(point) -> tuple[float, float, float]:
    return (point[0], point[2], -point[1])


class Level:
    def __init__(self, name: str, project: Path, lightmap_quality: str = "Production") -> None:
        self.name = name
        self.project = Path(project)
        self.lightmap = f"/Game/Lightmaps/{name}.llightmap"
        self.lightmap_quality = lightmap_quality
        self.blocks: list[str] = []
        self._names: set[str] = set()
        self._mesh_info: dict[str, dict] = {}
        # Environment, as the map header stores it. A level script changes what it needs.
        self.exposure = 1.0
        self.environment_intensity = 0.5
        self.sky = ((0.01, 0.011, 0.013), (0.012, 0.013, 0.015), (0.006, 0.006, 0.007))
        self.fog = {"color": (0.02, 0.022, 0.025), "density": 0.03, "start": 3.0, "max_opacity": 0.85}
        # The look of the final image (Docs/RENDERER.md, "The final image"). Empty: the engine's neutral one.
        self.post_process: dict[str, str] = {}

    # ------------------------------------------------------------------ plumbing
    def guid(self, name: str) -> str:
        assert name not in self._names, f"duplicate actor name: {name}"
        self._names.add(name)
        return hashlib.md5(f"LeonHorror|{self.name}|{name}".encode()).hexdigest()

    def head(self, name: str, cls: str, loc, rot=(0, 0, 0), scale=(1, 1, 1), props: dict[str, str] | None = None) -> list[str]:
        lines = [f"  - Name: {quote(name)}", f"    Class: {quote(cls)}", f'    GUID: "{self.guid(name)}"']
        if props:
            lines.append("    Properties:")
            for key in sorted(props):
                lines.append(f"      {key}: {quote(str(props[key]))}")
        lines += ["    Transform:", f"      Translation: {vec(loc)}", f"      Rotation: {vec(rot)}", f"      Scale: {vec(scale)}"]
        return lines

    def add(self, lines: list[str]) -> None:
        self.blocks.append("\n".join(lines) + "\n")

    def mesh_info(self, mesh: str) -> dict:
        if mesh not in self._mesh_info:
            path = self.project / "Intermediate" / "Art" / "Raw" / "Meshes" / f"{mesh}.json"
            if not path.is_file():
                raise SystemExit(f"[ERROR] {path} not found: run Tools/build_art.py first")
            self._mesh_info[mesh] = json.loads(path.read_text(encoding="utf-8"))
        return self._mesh_info[mesh]

    def material_slot(self, mesh: str, material: str) -> int:
        """Which slot of the mesh uses the material M_<material>."""
        return self.mesh_info(mesh)["materials"].index("M_" + material)

    def mesh_block(self, mesh: str, *, static: bool, collision: bool = True, cast_shadows: bool = True,
                   overrides: dict[int, str] | None = None) -> list[str]:
        """overrides: {slot: material name}; gives the actor its own copy of that material."""
        info = self.mesh_info(mesh)
        lines = [
            "    StaticMesh:",
            f'      Asset: "/Game/Meshes/{mesh}.lmesh"',
            f"      CastShadows: {'true' if cast_shadows else 'false'}",
            "      ReceiveShadows: true",
            "      VisibleInReflection: true",
        ]
        if not collision:
            lines.append("      Collision: false")
        lines.append(f"      Mobility: {'Static' if static else 'Movable'}")
        if static:
            lines += [f"      LightmapResolution: {info['lightmap_resolution']}", f'      LightmapAsset: "{self.lightmap}"']
        if overrides:
            lines.append("      MaterialOverrides:")
            for slot in sorted(overrides):
                lines += [f"        - Slot: {slot}", f'          Asset: "/Game/Materials/M_{overrides[slot]}.lmat"']
        return lines

    # ------------------------------------------------------------------ what a level is made of
    def static_mesh(self, name: str, mesh: str, loc=(0, 0, 0), yaw: float = 0.0, *, collision: bool = True,
                    cast_shadows: bool = True) -> None:
        """Part of the building or a fixed prop: lit by the bake, solid unless told otherwise."""
        self.add(self.head(name, "AActor", loc, rot=(0, yaw, 0)) + self.mesh_block(mesh, static=True, collision=collision,
                                                                                    cast_shadows=cast_shadows))

    def movable_mesh(self, name: str, mesh: str, loc=(0, 0, 0), yaw: float = 0.0, cls: str = "AActor",
                     props: dict[str, str] | None = None, overrides: dict[int, str] | None = None, collision: bool = True) -> None:
        """Something that moves or is used: not in the bake, lit by the lights and the probes."""
        self.add(self.head(name, cls, loc, rot=(0, yaw, 0), props=props)
                 + self.mesh_block(mesh, static=False, overrides=overrides, collision=collision))

    def actor(self, name: str, cls: str, loc=(0, 0, 0), yaw: float = 0.0, props: dict[str, str] | None = None) -> None:
        """An actor with no mesh: a spawner, a sound, a volume."""
        self.add(self.head(name, cls, loc, rot=(0, yaw, 0), props=props))

    def point_light(self, name: str, loc, color=(1.0, 0.95, 0.85), intensity: float = 8.0, radius: float = 7.0,
                    mobility: str = "Stationary", cast_shadows: bool = False, props: dict[str, str] | None = None) -> None:
        """A point light casts no shadows unless asked: only four of them can at a time."""
        lines = ["    PointLight:", "      Enabled: true", f"      Color: {vec(color)}", f"      Intensity: {num(intensity)}",
                 f"      Radius: {num(radius)}", f"      Mobility: {mobility}"]
        if cast_shadows:
            lines.append("      CastShadows: true")
        self.add(self.head(name, "AActor", loc, props=props) + lines)

    def spot_light(self, name: str, loc, direction=(0, -1, 0), color=(1.0, 0.95, 0.85), intensity: float = 20.0,
                   radius: float = 9.0, inner: float = 28.0, outer: float = 42.0, mobility: str = "Stationary") -> None:
        self.add(self.head(name, "AActor", loc) + [
            "    SpotLight:", "      Enabled: true", f"      Direction: {vec(direction)}", f"      Color: {vec(color)}",
            f"      Intensity: {num(intensity)}", f"      Radius: {num(radius)}", f"      CutOff: {num(inner)}",
            f"      OuterCutOff: {num(outer)}", f"      Mobility: {mobility}"])

    def player_start(self, loc, yaw: float = 0.0) -> None:
        """The player starts looking along -Z (north on a plan drawn in Blender)."""
        self.add(self.head("PlayerStart", "APlayerStart", loc, rot=(0, yaw, 0)))

    def nav_bounds(self, centre, size) -> None:
        self.add(self.head("NavBounds", "ANavMeshBoundsVolume", centre, scale=size))

    # ------------------------------------------------------------------ the file
    def text(self, generator: str) -> str:
        zenith, horizon, ground = self.sky
        header = f"""# LeonEngine2 Map Asset File (.lmap)
# Generated by {generator} - edit the script, not this file.
Map:
  Name: "{self.name}"
  Version: "2.2"

Environment:
  WorldSettings:
    GameModeClass: "ALeonHorrorGameMode"
    StaticLighting: true
    LightingBuildQuality: {self.lightmap_quality}
    LightmapResolution: 64
    NumIndirectBounces: 3
    SamplesPerTexel: 32
    IndirectIntensity: 1
    AmbientOcclusion: true
    AOIntensity: 0.8
    AORadius: 0.8
    TexelPadding: 4
    WorldScale: 1
    LightmapAsset: "{self.lightmap}"
  Skybox:
    Enabled: true
    Exposure: {num(self.exposure)}
    SunIntensity: 0
    EnvironmentIntensity: {num(self.environment_intensity)}
    UseHDREnvironmentMap: false
    HDREnvironmentMap: ""
    SkyZenithColor: {vec(zenith)}
    HorizonColor: {vec(horizon)}
    GroundColor: {vec(ground)}
    SunColor: [0.3, 0.32, 0.4]
    FogEnabled: true
    FogColor: {vec(self.fog["color"])}
    FogDensity: {num(self.fog["density"])}
    FogStartDistance: {num(self.fog["start"])}
    FogHeightFalloff: 0
    FogBaseHeight: 0
    FogMaxOpacity: {num(self.fog["max_opacity"])}
"""
        if self.post_process:
            header += "  PostProcess:" + chr(10) + "".join(f"    {key}: {value}" + chr(10) for key, value in self.post_process.items())
        header += chr(10) + "Actors:" + chr(10)
        return header + "".join(self.blocks)

    def write(self, generator: str) -> Path:
        path = self.project / "Content" / "Maps" / f"{self.name}.lmap"
        path.write_text(self.text(generator), encoding="utf-8", newline="\n")
        return path
