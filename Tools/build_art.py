#!/usr/bin/env python3
"""
Builds a project's art from its scripts and imports it.

    python Tools/build_art.py                         everything of Games/LeonHorror
    python Tools/build_art.py --materials             only the textures and materials
    python Tools/build_art.py --meshes --only SM_Door_Single,SM_Prop_Bench

Three steps: Blender (headless) runs <project>/Art/build.py, which writes source images and FBX
files into <project>/Intermediate/Art/Raw and the .lmat files into Content/Materials; AssetTool
imports the Raw folder into Content; and the contact sheets are left in Intermediate/Art/Sheets to
be looked at. Only what ends up in Content is versioned: the scripts are the source.

Blender is found through the LEON_BLENDER environment variable, or in its default install folder.
"""

from __future__ import annotations

import argparse
import glob
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def find_blender() -> str:
    candidate = os.environ.get("LEON_BLENDER", "")
    if candidate and os.path.isfile(candidate):
        return candidate
    found = sorted(glob.glob(r"C:\Program Files\Blender Foundation\Blender *\blender.exe"))
    if found:
        return found[-1]
    raise SystemExit("[ERROR] Blender not found. Set LEON_BLENDER to blender.exe.")


def find_asset_tool() -> str:
    for preset in ("dev-debug", "dev-release", "dev-relwithdebinfo", "ci"):
        path = os.path.join(ROOT, "out", "build", preset, "bin", "AssetTool.exe")
        if os.path.isfile(path):
            return path
    raise SystemExit("[ERROR] AssetTool.exe not found. Build it first (target AssetTool).")


def run(command: list[str], keep: tuple[str, ...]) -> int:
    """Runs a tool and prints only the lines that matter; the rest of Blender's chatter is dropped."""
    print("[RUN]", " ".join(command[:6]), "...")
    process = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, errors="replace")
    for line in (process.stdout + process.stderr).splitlines():
        if any(token in line for token in keep):
            print("   ", line.strip()[:200])
    return process.returncode


def main() -> int:
    parser = argparse.ArgumentParser(description="Build and import a project's scripted art")
    parser.add_argument("--project", default="Games/LeonHorror", help="Project folder (holds Art/, Content/)")
    parser.add_argument("--materials", action="store_true", help="Only textures and materials")
    parser.add_argument("--meshes", action="store_true", help="Only meshes")
    parser.add_argument("--only", default="", help="Comma separated names, passed to the chosen step")
    parser.add_argument("--no-import", action="store_true", help="Stop after Blender; do not run AssetTool")
    parser.add_argument("--force", action="store_true", help="Re-import everything, changed or not")
    args = parser.parse_args()

    project = os.path.join(ROOT, args.project)
    script = os.path.join(project, "Art", "build.py")
    if not os.path.isfile(script):
        print("[ERROR] no Art/build.py in", project)
        return 1
    steps = [name for name, wanted in (("materials", args.materials), ("meshes", args.meshes)) if wanted] or ["materials", "meshes"]

    blender = find_blender()
    for step in steps:
        command = [blender, "-b", "--factory-startup", "--python", script, "--", step]
        if args.only:
            command += ["--only", args.only]
        code = run(command, ("MATERIAL ", "PIECE ", "SHEET ", "Traceback", "Error", "  File "))
        if code != 0:
            print("[ERROR] Blender step '%s' failed (exit %d)" % (step, code))
            return code

    if args.no_import:
        return 0
    raw = os.path.join(project, "Intermediate", "Art", "Raw")
    command = [find_asset_tool(), "import", "--raw", raw, "--content", os.path.join(project, "Content")]
    if args.force:
        command.append("--force")
    code = run(command, ("[IMPORT", "[ERROR", "[AUTO-MATERIAL", "Imported", "imported", "Summary", "error"))
    if code != 0:
        print("[ERROR] AssetTool import failed (exit %d)" % code)
    return code


if __name__ == "__main__":
    sys.exit(main())
