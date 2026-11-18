#!/usr/bin/env python3
"""
Generate the LeonHorror vertical slice map (Content/Maps/Slice.lmap).

Layout, seen from above (world is Y-up, metres; the player starts looking down -Z):

                 z = -24   [ EXIT ]            needs Key.Exit
                           |      |
      +--------------------+      +--------------------+  z = -20
      |                    |      D  Room B (locked)   |  needs Key.Storage
      |                    |      |  first aid, Key.Exit
      +---------+----------+      +--------------------+  z = -14
      | Room C             D      |
      | Key.Storage,       |      |      the enemy wanders the corridor
      | handgun, ammo      |      |
      +--------------------+      |                       z = -8
                           |      |
                    +------+--DD--+------+                z = 0
                    | Start room         |
                    | save point, note,  |
                    | steel pipe         |
                    +--------------------+                z = 8

Everything is boxes. Level geometry is Static (baked, blocks movement); the things the
player uses are game actors carrying a Movable box as their visual and a Properties block.

Usage (from the repository root):
  python Projects/LeonHorror/Scripts/build_slice_map.py
  python Projects/LeonHorror/Scripts/build_slice_map.py --bake     # also bake Draft lightmaps
"""

from __future__ import annotations

import argparse
import hashlib
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
PROJECT = "Projects/LeonHorror/LeonHorror.lproject"
MAP_PATH = ROOT / "Projects/LeonHorror/Content/Maps/Slice.lmap"
MAP_VIRTUAL = "/Game/Maps/Slice"
LIGHTMAP = "/Game/Lightmaps/Slice.llightmap"

WALL_H = 3.0       # floor to ceiling
WALL_T = 0.3       # wall thickness
DOOR_W = 1.8       # doorway width (wide enough for the navigation grid)
DOOR_H = 2.2       # doorway height
SLAB = 0.5         # floor and ceiling thickness

MAT = {
    "Floor": ("/Game/Materials/M_HorrorFloor.lmat", 2.0),
    "Wall": ("/Game/Materials/M_HorrorWall.lmat", 1.5),
    "Ceiling": ("/Game/Materials/M_HorrorCeiling.lmat", 3.0),
    "Door": ("/Game/Materials/M_HorrorDoor.lmat", 1.0),
    "Metal": ("/Game/Materials/M_HorrorMetal.lmat", 1.0),
    "Prop": ("/Game/Materials/M_HorrorProp.lmat", 1.0),
}

KEY_STORAGE = "Key.Storage"
KEY_EXIT = "Key.Exit"

_names: set[str] = set()


def guid(name: str) -> str:
    assert name not in _names, f"duplicate actor name: {name}"
    _names.add(name)
    return hashlib.md5(f"LeonHorrorSlice|{name}".encode()).hexdigest()


def num(x: float) -> str:
    if abs(x - round(x)) < 1e-6:
        return str(int(round(x)))
    return f"{x:.6g}"


def vec(v) -> str:
    return "[" + ", ".join(num(c) for c in v) + "]"


def quote(text: str) -> str:
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


def head(name: str, cls: str, loc, props: dict[str, str] | None = None, scale=(1, 1, 1), rot=(0, 0, 0)) -> list[str]:
    lines = [f"  - Name: {quote(name)}", f"    Class: {quote(cls)}", f'    GUID: "{guid(name)}"']
    if props:
        lines.append("    Properties:")
        for key in sorted(props):
            lines.append(f"      {key}: {quote(str(props[key]))}")
    lines += [
        "    Transform:",
        f"      Translation: {vec(loc)}",
        f"      Rotation: {vec(rot)}",
        f"      Scale: {vec(scale)}",
    ]
    return lines


def box_mesh(size, surface: str, *, static: bool) -> list[str]:
    material, metres_per_uv = MAT[surface]
    lines = [
        "    StaticMesh:",
        '      Type: "Box"',
        "      Size: 1",
        f"      Width: {num(size[0])}",
        f"      Height: {num(size[1])}",
        f"      Depth: {num(size[2])}",
        "      Radius: 0.5",
        f"      MetersPerUv: {num(metres_per_uv)}",
        "      SubdivisionsX: 24",
        "      SubdivisionsZ: 24",
        '      Shader: "Engine/Resources/Shaders/PBR_Lit.glsl"',
        "      CastShadows: true",
        "      ReceiveShadows: true",
        "      VisibleInReflection: false",
        f"      Mobility: {'Static' if static else 'Movable'}",
        "      LightmapResolution: 64",
    ]
    if static:
        lines.append(f'      LightmapAsset: "{LIGHTMAP}"')
    lines += ["    Material:", f"      Asset: {quote(material)}"]
    return lines


class Level:
    def __init__(self) -> None:
        self.blocks: list[str] = []

    def add(self, lines: list[str]) -> None:
        self.blocks.append("\n".join(lines) + "\n")

    # --- Static geometry ---
    def box(self, name: str, lo, hi, surface: str) -> None:
        """Static box given by two opposite corners."""
        size = [abs(hi[i] - lo[i]) for i in range(3)]
        centre = [(hi[i] + lo[i]) * 0.5 for i in range(3)]
        assert min(size) > 0.0, name
        self.add(head(name, "AActor", centre) + box_mesh(size, surface, static=True))

    def floor_and_ceiling(self, name: str, x0: float, x1: float, z0: float, z1: float) -> None:
        self.box(f"{name}_Floor", (x0, -SLAB, z0), (x1, 0.0, z1), "Floor")
        self.box(f"{name}_Ceiling", (x0, WALL_H, z0), (x1, WALL_H + SLAB, z1), "Ceiling")

    def wall_x(self, name: str, z: float, x0: float, x1: float, doors=()) -> None:
        """Wall in the plane z = const, from x0 to x1, with doorways centred at the given x."""
        self._wall(name, axis=0, plane=z, a0=x0, a1=x1, doors=doors)

    def wall_z(self, name: str, x: float, z0: float, z1: float, doors=()) -> None:
        """Wall in the plane x = const, from z0 to z1, with doorways centred at the given z."""
        self._wall(name, axis=2, plane=x, a0=z0, a1=z1, doors=doors)

    def _wall(self, name: str, axis: int, plane: float, a0: float, a1: float, doors) -> None:
        a0, a1 = min(a0, a1), max(a0, a1)
        half = WALL_T * 0.5

        def corners(start: float, end: float, y0: float, y1: float):
            if axis == 0:
                return (start, y0, plane - half), (end, y1, plane + half)
            return (plane - half, y0, start), (plane + half, y1, end)

        # Walls run past both ends by half a thickness so corners close.
        cursor = a0 - half
        index = 0
        for door in sorted(doors):
            left, right = door - DOOR_W * 0.5, door + DOOR_W * 0.5
            assert a0 < left and right < a1, f"{name}: doorway outside the wall"
            self.box(f"{name}_{index}", *corners(cursor, left, 0.0, WALL_H), "Wall")
            self.box(f"{name}_Lintel{index}", *corners(left, right, DOOR_H, WALL_H), "Wall")
            cursor = right
            index += 1
        self.box(f"{name}_{index}", *corners(cursor, a1 + half, 0.0, WALL_H), "Wall")

    def table(self, name: str, x: float, z: float, width=1.2, depth=0.7, height=0.8) -> float:
        """A plain block of furniture. Returns the height of its top."""
        self.box(name, (x - width * 0.5, 0.0, z - depth * 0.5), (x + width * 0.5, height, z + depth * 0.5), "Prop")
        return height

    def light(self, name: str, pos, color, intensity: float, radius: float) -> None:
        # Stationary: baked into the level and still lighting characters and moving things.
        self.add(
            head(name, "AActor", pos)
            + [
                "    PointLight:",
                "      Enabled: true",
                f"      Color: {vec(color)}",
                f"      Intensity: {num(intensity)}",
                f"      Radius: {num(radius)}",
                "      Mobility: Stationary",
            ]
        )

    # --- Game actors ---
    def game_actor(self, name: str, cls: str, loc, size, surface: str, props: dict[str, str] | None = None) -> None:
        self.add(head(name, cls, loc, props) + box_mesh(size, surface, static=False))

    def pickup(self, name: str, loc, item: str, count: int = 1, size=(0.22, 0.12, 0.16), surface="Metal") -> None:
        props = {"Item": item}
        if count != 1:
            props["Count"] = str(count)
        self.game_actor(name, "ALeonHorrorPickup", loc, size, surface, props)

    def door(self, name: str, loc, *, along: str, key: str = "") -> None:
        """A sliding door filling a doorway. along='x': the wall runs along X; 'z': along Z."""
        thickness = 0.12
        size = (DOOR_W, DOOR_H, thickness) if along == "x" else (thickness, DOOR_H, DOOR_W)
        props = {"SlideX": num(DOOR_W - 0.05), "SlideY": "0", "SlideZ": "0"} if along == "x" else {
            "SlideX": "0", "SlideY": "0", "SlideZ": num(DOOR_W - 0.05)}
        if key:
            props["RequiredKey"] = key
        self.game_actor(name, "ALeonHorrorDoor", (loc[0], DOOR_H * 0.5, loc[2]), size, "Door", props)


HEADER = f"""# LeonEngine2 Map Asset File (.lmap)
# Generated by Projects/LeonHorror/Scripts/build_slice_map.py - edit the script, not this file.
Map:
  Name: "Slice"
  Version: "2.2"

Environment:
  WorldSettings:
    GameModeClass: "ALeonHorrorGameMode"
    StaticLighting: true
    LightingBuildQuality: Draft
    LightmapResolution: 64
    NumIndirectBounces: 2
    SamplesPerTexel: 8
    IndirectIntensity: 1
    AmbientOcclusion: true
    AOIntensity: 1
    AORadius: 1
    TexelPadding: 2
    WorldScale: 1
    LightmapAsset: "{LIGHTMAP}"
  Skybox:
    Enabled: true
    Exposure: 0.9
    SunIntensity: 0
    EnvironmentIntensity: 0.04
    UseHDREnvironmentMap: false
    HDREnvironmentMap: ""
    SkyZenithColor: [0.008, 0.01, 0.014]
    HorizonColor: [0.014, 0.016, 0.02]
    GroundColor: [0.004, 0.004, 0.006]
    SunColor: [0.3, 0.32, 0.4]
    FogEnabled: true
    FogColor: [0.03, 0.034, 0.038]
    FogDensity: 0.085
    FogStartDistance: 1.5
    FogHeightFalloff: 0
    FogBaseHeight: 0
    FogMaxOpacity: 0.96

Actors:
"""


def build() -> str:
    lv = Level()
    cx = 1.5  # corridor half width

    lv.add(head("PlayerStart", "APlayerStart", (0.0, 1.2, 5.5), rot=(0, -90, 0)))
    # Covers every walkable area; the navigation grid is built inside it. Its underside must sit on
    # the floor: walkability is probed just above the bottom of the volume.
    lv.add(head("NavBounds", "ANavMeshBoundsVolume", (0.0, 2.0, -8.0), scale=(22, 4, 36)))

    # --- Floors and ceilings ---
    lv.floor_and_ceiling("Start", -4, 4, 0, 8)
    lv.floor_and_ceiling("Corridor", -cx, cx, -24, 0)
    lv.floor_and_ceiling("RoomC", -9.5, -cx, -14, -8)
    lv.floor_and_ceiling("RoomB", cx, 9.5, -20, -14)

    # --- Walls ---
    lv.wall_x("Start_South", 8, -4, 4)
    lv.wall_z("Start_West", -4, 0, 8)
    lv.wall_z("Start_East", 4, 0, 8)
    lv.wall_x("Start_North", 0, -4, 4, doors=[0.0])
    lv.wall_z("Corridor_West", -cx, -24, 0, doors=[-11.0])
    lv.wall_z("Corridor_East", cx, -24, 0, doors=[-17.0])
    lv.wall_x("Corridor_End", -24, -cx, cx)
    lv.wall_x("RoomC_North", -14, -9.5, -cx)
    lv.wall_x("RoomC_South", -8, -9.5, -cx)
    lv.wall_z("RoomC_West", -9.5, -14, -8)
    lv.wall_x("RoomB_North", -20, cx, 9.5)
    lv.wall_x("RoomB_South", -14, cx, 9.5)
    lv.wall_z("RoomB_East", 9.5, -20, -14)

    # --- Doors ---
    lv.door("Door_Start", (0.0, 0.0, 0.0), along="x")
    lv.door("Door_RoomC", (-cx, 0.0, -11.0), along="z")
    lv.door("Door_RoomB", (cx, 0.0, -17.0), along="z", key=KEY_STORAGE)

    # --- Start room: save point, note, pipe ---
    top = lv.table("Start_Desk", -3.0, 4.0, width=0.8, depth=1.6)
    lv.game_actor("SavePoint", "ALeonHorrorSavePoint", (-3.0, top + 0.16, 3.6), (0.42, 0.32, 0.34), "Metal")
    lv.game_actor(
        "Note_Start", "ALeonHorrorNote", (-3.0, top + 0.012, 4.5), (0.21, 0.02, 0.3), "Ceiling",
        {
            "Title": "Torn page",
            "Text": "They locked the storage room after what happened.\\n\\n"
                    "The key is still in the office down the corridor, on the left. "
                    "I am not going back for it. Something walks out there when the lights go.\\n\\n"
                    "If you go, take the pipe by the door. And do not run. It hears you.",
        },
    )
    lv.box("Start_Shelf", (2.6, 0.0, 5.0), (3.85, 1.9, 7.5), "Prop")
    lv.box("Start_Crate", (1.6, 0.0, 6.6), (2.5, 0.7, 7.6), "Prop")
    lv.pickup("Pickup_Pipe", (1.3, 0.05, 0.9), "Weapon.Pipe", size=(0.07, 0.07, 0.95))

    # --- Corridor: clutter to break the sight line, and the enemy ---
    lv.box("Corridor_Cabinet", (0.85, 0.0, -6.4), (1.35, 1.8, -5.2), "Metal")
    lv.box("Corridor_Crate", (-1.35, 0.0, -20.9), (-0.6, 0.6, -20.1), "Prop")
    lv.add(head("EnemySpawn_Corridor", "ALeonHorrorEnemySpawn", (0.0, 0.0, -19.0), {"WanderRadius": "5"}))

    # --- Room C (office): storage key, handgun, a little ammunition ---
    top = lv.table("RoomC_Desk", -7.0, -11.0, width=1.0, depth=2.0)
    lv.pickup("Pickup_KeyStorage", (-7.0, top + 0.03, -11.6), KEY_STORAGE, size=(0.14, 0.03, 0.06))
    lv.pickup("Pickup_Pistol", (-7.0, top + 0.05, -10.6), "Weapon.Pistol", size=(0.24, 0.1, 0.06))
    lv.box("RoomC_Cabinet", (-9.3, 0.0, -13.7), (-8.2, 1.9, -13.1), "Metal")
    top = lv.table("RoomC_SideTable", -4.5, -13.3, width=1.0, depth=0.6, height=0.7)
    lv.pickup("Pickup_AmmoC", (-4.5, top + 0.05, -13.3), "Ammo.Pistol", count=6, size=(0.14, 0.1, 0.1))
    lv.game_actor(
        "Note_Office", "ALeonHorrorNote", (-4.9, top + 0.012, -13.2), (0.21, 0.02, 0.3), "Ceiling",
        {
            "Title": "Inventory slip",
            "Text": "Storage room: one first aid kit, spare rounds, and the key to the outer door.\\n\\n"
                    "Six rounds in the desk. Make them count.",
        },
    )

    # --- Room B (storage, locked): first aid, exit key, ammunition ---
    lv.box("RoomB_ShelfA", (4.0, 0.0, -19.8), (8.0, 1.9, -19.2), "Prop")
    lv.box("RoomB_ShelfB", (9.0, 0.0, -18.5), (9.35, 1.9, -15.5), "Prop")
    top = lv.table("RoomB_Bench", 6.5, -14.6, width=2.4, depth=0.7, height=0.85)
    lv.pickup("Pickup_Medkit", (5.8, top + 0.08, -14.6), "Medkit", size=(0.28, 0.16, 0.2), surface="Ceiling")
    lv.pickup("Pickup_KeyExit", (7.2, top + 0.03, -14.6), KEY_EXIT, size=(0.14, 0.03, 0.06))
    lv.pickup("Pickup_AmmoB", (6.5, top + 0.05, -14.6), "Ammo.Pistol", count=4, size=(0.14, 0.1, 0.1))

    # --- Exit: a locked outer door at the far end of the corridor ---
    lv.game_actor("Exit", "ALeonHorrorExit", (0.0, DOOR_H * 0.5, -24.0 + WALL_T * 0.5 + 0.07), (DOOR_W, DOOR_H, 0.1),
                  "Metal", {"RequiredKey": KEY_EXIT})

    # --- Lights: few and weak, so the flashlight matters ---
    warm = (1.0, 0.78, 0.55)
    sick = (0.62, 0.78, 0.66)
    red = (1.0, 0.32, 0.22)
    lv.light("Light_Start", (0.0, 2.6, 4.0), warm, 3.2, 8.0)
    lv.light("Light_CorridorNear", (0.0, 2.7, -4.5), sick, 1.1, 6.5)
    lv.light("Light_CorridorFar", (0.0, 2.7, -22.0), red, 1.2, 6.0)
    lv.light("Light_RoomC", (-5.5, 2.6, -11.0), sick, 1.8, 7.0)
    lv.light("Light_RoomB", (5.5, 2.6, -17.0), warm, 1.2, 6.5)

    return HEADER + "".join(lv.blocks)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--bake", action="store_true", help="bake Draft lightmaps after writing the map")
    args = parser.parse_args()

    text = build()
    MAP_PATH.parent.mkdir(parents=True, exist_ok=True)
    MAP_PATH.write_text(text, encoding="utf-8", newline="\n")
    print(f"[OK] wrote {MAP_PATH.relative_to(ROOT)} ({len(_names)} actors)")

    if args.bake:
        # MSYS (Git Bash) would rewrite /Game/... into a Windows path before Python sees it.
        env = dict(os.environ, MSYS_NO_PATHCONV="1")
        cmd = [sys.executable, str(ROOT / "Scripts/bake_lightmaps.py"), "--project", PROJECT, "--map", MAP_VIRTUAL,
               "--force", "--quality", "Draft"]
        print("[RUN] " + " ".join(cmd))
        return subprocess.run(cmd, cwd=ROOT, env=env).returncode
    return 0


if __name__ == "__main__":
    sys.exit(main())
