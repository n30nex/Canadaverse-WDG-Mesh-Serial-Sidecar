#!/usr/bin/env python3
"""Create the local bridge environment, install dependencies, and configure it."""

from pathlib import Path
import os
import subprocess
import sys
import venv


ROOT = Path(__file__).resolve().parent
VENV = ROOT / ".venv"


def main() -> int:
    if not VENV.exists():
        print(f"Creating {VENV}...")
        venv.EnvBuilder(with_pip=True).create(VENV)
    python = VENV / ("Scripts/python.exe" if os.name == "nt" else "bin/python")
    subprocess.run(
        [str(python), "-m", "pip", "install", "-r", str(ROOT / "requirements.txt")],
        check=True,
    )
    return subprocess.run(
        [
            str(python),
            str(ROOT / "tools" / "wdg_mesh_bridge.py"),
            "configure",
            *sys.argv[1:],
        ]
    ).returncode


if __name__ == "__main__":
    raise SystemExit(main())
