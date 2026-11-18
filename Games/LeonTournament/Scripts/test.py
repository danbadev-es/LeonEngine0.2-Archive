#!/usr/bin/env python3
"""Build and execute the LeonTournament gameplay test suite."""

from __future__ import annotations

import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "Tools"))
import _leon_paths as leon  # noqa: E402


def main() -> int:
    print("[TEST] Compiling and running LeonTournament integration tests...")
    if leon.configure() != 0 or leon.build_targets(None, ["LeonTournamentTests"]) != 0:
        return 1
    exe = leon.find_executable("LeonTournamentTests", leon.default_config())
    if not os.path.isfile(exe):
        print(f"[ERROR] Test executable not found at {exe}")
        return 1
    return subprocess.run([exe] + sys.argv[1:], cwd=ROOT).returncode


if __name__ == "__main__":
    sys.exit(main())
