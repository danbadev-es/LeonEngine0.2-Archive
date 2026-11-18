#!/usr/bin/env python3
"""
LeonEngine2 - build the engine: libraries, plugins, tools and the engine test executable.

Usage:
  python Tools/build_engine.py --config Debug
  python Tools/build_engine.py --target AssetTool
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


ENGINE_TARGETS = ["RendererTests", "AssetTool", "LightmassTool", "ProjectTool"]


def main() -> int:
    parser = argparse.ArgumentParser(description="Build the LeonEngine2 engine, tools and tests")
    add_common_arguments(parser)
    parser.add_argument("--target", default="", help="Single CMake target (default: tests and tools)")
    args = parser.parse_args()
    code = configure_and_build("Build Engine", args, [args.target] if args.target else ENGINE_TARGETS)
    if code == 0:
        print(f"  Output: {leon.bin_dir(args.config)}")
        print("========================================")
    return code


if __name__ == "__main__":
    sys.exit(main())
