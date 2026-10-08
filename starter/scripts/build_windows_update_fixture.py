#!/usr/bin/env python3
"""Build a disposable older-version Windows setup for the real automatic-upgrade check."""
import argparse
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if sys.platform != "win32":
        parser.error("The upgrade fixture must be built on Windows")
    output = args.output.resolve()
    with tempfile.TemporaryDirectory(prefix="pet-old-build-") as temporary:
        source = Path(temporary) / "source"
        shutil.copytree(ROOT, source, ignore=shutil.ignore_patterns(
            "build", "build-*", "dist", "dist-*", "__pycache__", ".git"))
        cmake = source / "CMakeLists.txt"
        content, count = re.subn(r"project\(AgentPet VERSION \d+\.\d+\.\d+ LANGUAGES CXX\)",
                                "project(AgentPet VERSION 0.0.1 LANGUAGES CXX)", cmake.read_text(), count=1)
        if count != 1:
            raise RuntimeError("Could not set the disposable fixture's project version")
        cmake.write_text(content)
        build = Path(temporary) / "build"
        subprocess.run(["cmake", "-S", str(source), "-B", str(build), "-G", "Ninja",
                        "-DCMAKE_BUILD_TYPE=Release", "-DAGENT_PET_REVISION=update-fixture"], check=True)
        subprocess.run(["cmake", "--build", str(build), "--target", "agent-pet", "agent-pet-cli",
                        "agent-pet-updater", "--parallel", "2"], check=True)
        subprocess.run([sys.executable, str(source / "scripts/package_windows.py"), "--build", str(build),
                        "--output", str(output)], check=True)


if __name__ == "__main__":
    main()
