#!/usr/bin/env python3
"""
LeonEngine2 - build (and optionally run) a game project.

Usage:
  python Scripts/build_project.py --project Projects/LeonHorror/LeonHorror.lproject
  python Scripts/build_project.py --project Projects/Sandbox/Sandbox.lproject --run
  python Scripts/build_project.py --project D:/Games/MyGame/MyGame.lproject
  set LEON_PROJECT=... && python Scripts/build_project.py --run

Games inside Projects/ are part of the main build tree. A project that lives elsewhere is added to
the tree through LEON_EXTERNAL_GAME_DIRS.
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import _leon_paths as leon  # noqa: E402


def add_common_arguments(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--clean", action="store_true", help="Delete the build tree before configuring")
    parser.add_argument("--rebuild", action="store_true", help="Clean-first build")
    parser.add_argument("--config", default=leon.default_config(), choices=leon.BUILD_CONFIGS,
                        help="Build configuration")


def configure_and_build(title: str, args: argparse.Namespace, targets: list[str], defines: list[str] | None = None) -> int:
    os.chdir(leon.engine_root())
    print("========================================")
    print(f"   LeonEngine2 - {title}")
    print("========================================")
    print(f"  Configuration: {args.config}")
    print(f"  Build Dir:     {leon.build_dir(args.config)}")
    print(f"  Targets:       {' '.join(targets) if targets else '(all)'}")
    print("----------------------------------------")
    if leon.configure(args.config, clean=args.clean, defines=defines) != 0:
        print("[ERROR] CMake configuration failed!")
        return 1
    start = time.time()
    code = leon.build_targets(args.config, targets, rebuild=args.rebuild and not args.clean)
    if code != 0:
        print("----------------------------------------")
        print(f"[ERROR] Build failed with exit code {code}!")
        print("========================================")
        return code
    print("----------------------------------------")
    print(f"[SUCCESS] Build completed in {(time.time() - start) * 1000:.1f} ms.")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Build a LeonEngine2 project (.lproject)")
    parser.add_argument("--project", default=os.environ.get("LEON_PROJECT", ""),
                        help="Path to .lproject (or set LEON_PROJECT)")
    parser.add_argument("--target", default="", help="CMake target (default: ProjectName)")
    parser.add_argument("--run", action="store_true", help="Launch the project executable after build")
    add_common_arguments(parser)
    args = parser.parse_args()

    os.chdir(leon.engine_root())
    try:
        project = leon.require_project(args.project)
    except SystemExit as e:
        print(e)
        return 1

    target = args.target or leon.project_name(project)
    project_dir = leon.project_dir(project)
    in_repo_games = os.path.normcase(os.path.join(leon.engine_root(), "Projects"))
    defines = None
    if os.path.normcase(os.path.dirname(project_dir)) != in_repo_games:
        defines = ["LEON_EXTERNAL_GAME_DIRS=" + project_dir.replace("\\", "/")]

    print(f"  Project:       {project}")
    code = configure_and_build("Build Project", args, [target], defines)
    if code != 0:
        return code
    exe_path = leon.find_executable(target, args.config)
    print(f"  Executable: {exe_path}")
    print("========================================")

    if args.run:
        if not os.path.isfile(exe_path):
            print(f"[ERROR] Executable not found at {exe_path}")
            return 1
        print(f"\n[LAUNCH] {exe_path} --project={project}")
        return subprocess.run([exe_path, f"--project={project}"], cwd=leon.engine_root()).returncode
    return 0


if __name__ == "__main__":
    sys.exit(main())
