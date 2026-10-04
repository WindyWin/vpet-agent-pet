#!/usr/bin/env python3
"""Build a private Linux prototype bundle from a trusted local build (not arbitrary ELF files)."""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
# Host ABI / graphics drivers remain OS dependencies, not development runtimes.
SYSTEM = re.compile(r"^(?:ld-linux.*|lib(?:c|m|dl|pthread|rt|resolv|util)\.so\..*)$")


def run(*args):
    return subprocess.check_output(args, text=True).strip()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, default=ROOT / "build")
    parser.add_argument("--output", type=Path, default=ROOT / "dist/agent-pet-m5")
    args = parser.parse_args()
    output = args.output.resolve()
    if output.exists():
        parser.error("Output already exists; select a fresh --output directory")
    for tool in ("cmake", "qmake6", "ldd", "patchelf"):
        if not shutil.which(tool):
            parser.error(f"Missing build tool: {tool}")
    subprocess.run(["cmake", "--install", str(args.build.resolve()), "--prefix", str(output)], check=True)
    lib = output / "lib"
    lib.mkdir(exist_ok=True)
    plugin_dir = output / "plugins/platforms"
    plugin_dir.mkdir(parents=True)
    qt_plugins = Path(run("qmake6", "-query", "QT_INSTALL_PLUGINS"))
    binaries = [output / "bin/agent-pet"]
    for name in ("libqxcb.so", "libqoffscreen.so"):
        dest = plugin_dir / name
        shutil.copy2(qt_plugins / "platforms" / name, dest)
        binaries.append(dest)
    (output / "bin/qt.conf").write_text("[Paths]\nPrefix=..\nLibraries=lib\nPlugins=plugins\n")
    sources = {}
    system = set()
    # ldd resolves the transitive closure; collect each plugin's closure as well.
    for binary in binaries:
        dependencies = run("ldd", str(binary))
        if "not found" in dependencies:
            raise RuntimeError(dependencies)
        for line in dependencies.splitlines():
            match = re.search(r"(?:=>\s+)?(/\S+)\s+\(", line)
            if not match:
                continue
            source = Path(match[1])
            if SYSTEM.match(source.name):
                system.add(source.name)
                continue
            dest = lib / source.name
            if not dest.exists():
                shutil.copy2(source, dest)
                sources[source.name] = str(source.resolve())
    for binary in [*binaries, *lib.iterdir()]:
        relative = os.path.relpath(lib, binary.parent)
        subprocess.run(["patchelf", "--set-rpath", "$ORIGIN/" + relative, str(binary)], check=True)
    # Preserve available distro copyright/license notices for bundled libraries.
    notices = output / "share/agent-pet/runtime-licenses"
    notices.mkdir()
    packages = set()
    if shutil.which("pacman"):
        for source in [*sources.values(), str(qt_plugins / "platforms/libqxcb.so")]:
            packages.add(run("pacman", "-Qqo", source))
        for package in sorted(packages):
            source = Path("/usr/share/licenses") / package
            if source.is_dir():
                shutil.copytree(source, notices / package)
        common = Path("/usr/share/licenses/common")
        if common.is_dir():
            shutil.copytree(common, notices / "common", symlinks=False)
    manifest = {"format": "private M4 prototype", "libraries": sorted(sources),
                "host_libraries": sorted(system), "distro_packages": sorted(packages),
                "qt_version": run("qmake6", "-query", "QT_VERSION")}
    (output / "share/agent-pet/runtime-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    archive = shutil.make_archive(str(output), "gztar", output.parent, output.name)
    print(f"Created {archive}; {len(sources)} bundled libraries. See docs/architecture.md for host ABI limits.")


if __name__ == "__main__":
    main()
