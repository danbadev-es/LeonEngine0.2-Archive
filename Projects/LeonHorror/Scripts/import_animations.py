#!/usr/bin/env python3
"""
LeonHorror - build the Blender-authored animation clips and import them into Content/Animations.

    python Projects/LeonHorror/Scripts/import_animations.py              # author + import every clip
    python Projects/LeonHorror/Scripts/import_animations.py --only MeleeSwing
    python Projects/LeonHorror/Scripts/import_animations.py --roundtrip  # pipeline check only

Steps:
  1. Blender (headless) runs make_animations.py and writes one armature-only FBX per clip to Raw/Anim.
  2. AssetTool imports those FBX files against Content/Skeletons/YBot.lskeleton into .lanim.

--roundtrip re-exports the existing Idle clip untouched into build/anim_roundtrip, for the
"Blender round trip" test in LeonHorrorTests: it proves scale and bone axes survive before any
authored clip is trusted.
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT = os.path.dirname(HERE)
REPO = os.path.dirname(os.path.dirname(PROJECT))
MAKE = os.path.join(HERE, "make_animations.py")

BLENDER_CANDIDATES = [
    os.environ.get("LEON_BLENDER", ""),
    shutil.which("blender") or "",
    r"C:\Program Files\Blender Foundation\Blender 5.2\blender.exe",
    r"C:\Program Files\Blender Foundation\Blender 5.0\blender.exe",
    r"C:\Program Files\Blender Foundation\Blender 4.5\blender.exe",
]

# Clips that existed before authoring; never deleted by the cleanup below.
AUTHORED = ["IdleRelaxed", "EnemyIdle", "WalkForward", "MeleeSwing", "PistolAimIdle", "PistolFire", "HitReact",
            "Interact", "EnemyShamble", "EnemyAttack"]


def find_blender() -> str:
    for candidate in BLENDER_CANDIDATES:
        if candidate and os.path.isfile(candidate):
            return candidate
    raise SystemExit("Blender not found. Set LEON_BLENDER to the blender executable.")


def find_asset_tool() -> str:
    for build in ("out/Engine", "out/Projects/LeonHorror"):
        for sub in ("_tools/AssetTool/AssetTool.exe", "_tools/AssetTool/AssetTool"):
            path = os.path.join(REPO, build, sub)
            if os.path.isfile(path):
                return path
    raise SystemExit("AssetTool not built. Run: python Scripts/build_engine.py --target AssetTool")


def run(cmd: list[str]) -> None:
    print("[RUN]", " ".join(cmd))
    result = subprocess.run(cmd, cwd=REPO)
    if result.returncode != 0:
        raise SystemExit(f"command failed with exit code {result.returncode}")


def blender(args: list[str]) -> None:
    run([find_blender(), "-b", "--factory-startup", "--python", MAKE, "--"] + args)


def import_folder(raw_dir: str, content_dir: str) -> None:
    # The importer does not create the output folder and does not count a failed save as an error.
    os.makedirs(os.path.join(content_dir, "Animations"), exist_ok=True)
    if not os.path.isfile(os.path.join(content_dir, "Skeletons", "YBot.lskeleton")):
        raise SystemExit("Content/Skeletons/YBot.lskeleton is required: clips are matched to it by bone name.")
    run([find_asset_tool(), "import", "--raw", raw_dir, "--content", content_dir, "--force"])


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--roundtrip", action="store_true")
    parser.add_argument("--only", default="", help="author a single clip by name")
    args = parser.parse_args()

    if args.roundtrip:
        work = os.path.join(REPO, "build", "anim_roundtrip")
        shutil.rmtree(work, ignore_errors=True)
        os.makedirs(os.path.join(work, "Raw"))
        os.makedirs(os.path.join(work, "Content", "Skeletons"))
        shutil.copy2(os.path.join(PROJECT, "Content", "Skeletons", "YBot.lskeleton"),
                     os.path.join(work, "Content", "Skeletons"))
        blender(["--mode", "roundtrip", "--source", os.path.join(PROJECT, "Raw", "Anim", "Idle.fbx"),
                 "--out", os.path.join(work, "Raw")])
        import_folder(os.path.join(work, "Raw"), os.path.join(work, "Content"))
        print("[OK] round trip written to", work)
        return 0

    # Author into a staging folder so only the authored clips are imported (Raw/Anim also holds Idle.fbx).
    staging = os.path.join(REPO, "build", "anim_authoring")
    shutil.rmtree(staging, ignore_errors=True)
    os.makedirs(staging)
    author = ["--mode", "author", "--rig", os.path.join(PROJECT, "Raw", "YBot.fbx"), "--out", staging]
    if args.only:
        author += ["--only", args.only]
    blender(author)

    produced = sorted(f for f in os.listdir(staging) if f.lower().endswith(".fbx"))
    if not produced:
        raise SystemExit("Blender produced no clips.")
    import_folder(staging, os.path.join(PROJECT, "Content"))

    raw_anim = os.path.join(PROJECT, "Raw", "Anim")
    os.makedirs(raw_anim, exist_ok=True)
    for name in produced:
        shutil.copy2(os.path.join(staging, name), os.path.join(raw_anim, name))
        stem = os.path.splitext(name)[0]
        if not os.path.isfile(os.path.join(PROJECT, "Content", "Animations", stem + ".lanim")):
            raise SystemExit(f"import did not produce Content/Animations/{stem}.lanim")
    print("[OK] imported:", ", ".join(os.path.splitext(n)[0] for n in produced))
    return 0


if __name__ == "__main__":
    sys.exit(main())
