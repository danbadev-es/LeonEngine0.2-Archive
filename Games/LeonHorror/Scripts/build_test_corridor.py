#!/usr/bin/env python3
"""
LeonHorror - a stretch of corridor that takes the art pipeline from end to end.

    python Games/LeonHorror/Scripts/build_test_corridor.py           write Content/Maps/TestCorridor.lmap
    python Games/LeonHorror/Scripts/build_test_corridor.py --bake    and bake its lightmaps

It is not part of the game: it exists to look at materials, meshes and lighting together in the
engine before the level is built from them. Run Tools/build_art.py first.

    out/build/dev-debug/bin/LeonHorror.exe --map=/Game/Maps/TestCorridor
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
PROJECT_DIR = HERE.parent
ROOT = PROJECT_DIR.parent.parent
sys.path.insert(0, str(HERE))

from leonmap import Level, from_blender  # noqa: E402

PROJECT = "Games/LeonHorror/LeonHorror.lproject"
LENGTH, WIDTH, HEIGHT = 8.0, 2.4, 2.8  # as built in Art/leonart/kit.py


def build(quality: str) -> Level:
    lv = Level("TestCorridor", PROJECT_DIR, quality)
    lv.exposure = 1.0

    lv.player_start(from_blender((1.2, WIDTH * 0.5, 1.0)), yaw=0.0)
    lv.nav_bounds(from_blender((LENGTH * 0.5, WIDTH * 0.5, 1.4)), (LENGTH + 2.0, 3.0, WIDTH + 2.0))

    # The shell is modelled around the corridor's own corner, so it goes at the origin.
    lv.static_mesh("Corridor_Floor", "SM_TestCorridor_Floor")
    lv.static_mesh("Corridor_Walls", "SM_TestCorridor_Walls")
    lv.static_mesh("Corridor_Ceiling", "SM_TestCorridor_Ceiling")
    lv.static_mesh("Corridor_Skirting", "SM_TestCorridor_Skirting", collision=False)

    # The doorway in the far wall. The frame sits in the middle of the wall's thickness; the leaf
    # hangs from the jamb on the left as you face the door and stands a little open.
    door_centre = (LENGTH + 0.075, WIDTH * 0.5, 0.0)
    lv.static_mesh("Door_Frame", "SM_DoorFrame_Single", from_blender(door_centre), yaw=90.0, collision=False)
    hinge = (LENGTH + 0.075, WIDTH * 0.5 + 0.45, 0.0)
    lv.static_mesh("Door_Leaf", "SM_Door_Single", from_blender(hinge), yaw=90.0 + 180.0 - 28.0)

    # A bench against the wall on the left.
    lv.static_mesh("Bench", "SM_Prop_Bench", from_blender((3.2, WIDTH - 0.27, 0.0)), yaw=180.0)

    # Two ceiling lights: the fitting, and the light it gives. The far one is dimmer and greenish,
    # a tube on its way out.
    for index, (x, color, intensity) in enumerate(((2.0, (1.0, 0.95, 0.84), 5.0), (6.0, (0.82, 1.0, 0.86), 2.8))):
        # Not part of the bake: its glowing face is what the eye sees, the point light below is what
        # lights the room. A small bright surface in the bake only adds noise.
        lv.movable_mesh(f"Light_{index}_Fitting", "SM_Light_Panel", from_blender((x, WIDTH * 0.5, HEIGHT)))
        lv.point_light(f"Light_{index}", from_blender((x, WIDTH * 0.5, HEIGHT - 0.25)), color, intensity, 6.5)
    return lv


def main() -> int:
    parser = argparse.ArgumentParser(description="Build the art pipeline test corridor")
    parser.add_argument("--bake", action="store_true", help="bake lightmaps after writing the map")
    parser.add_argument("--quality", default="Production", choices=["Preview", "Draft", "Production"])
    args = parser.parse_args()

    path = build(args.quality).write("Games/LeonHorror/Scripts/build_test_corridor.py")
    print("[OK] wrote", path)
    if not args.bake:
        return 0
    env = dict(os.environ, MSYS_NO_PATHCONV="1")
    command = [sys.executable, str(ROOT / "Tools/bake_lightmaps.py"), "--project", PROJECT, "--map", "/Game/Maps/TestCorridor",
               "--force", "--quality", args.quality]
    return subprocess.run(command, cwd=ROOT, env=env).returncode


if __name__ == "__main__":
    sys.exit(main())
