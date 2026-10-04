#!/usr/bin/env python3
"""Build the relocatable Linux release bundle from a trusted local build (not arbitrary ELF files)."""
import argparse
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
QMAKE = None
# Host ABI / graphics drivers remain OS dependencies, not development runtimes.
SYSTEM = re.compile(r"^(?:ld-linux.*|lib(?:c|m|dl|pthread|rt|resolv|util)\.so\..*)$")

QT_NOTICE = """The bundled Qt {version} libraries and plugins are used under the GNU Lesser
General Public License v3 (https://www.gnu.org/licenses/lgpl-3.0.html), unmodified
and dynamically linked; you may replace them in lib/ and plugins/. Corresponding
source: https://download.qt.io/archive/qt/ (qtbase-everywhere-src-{version}).
Libraries listed as unowned in runtime-manifest.json came from this Qt installation.
"""


def run(*args):
    return subprocess.check_output(args, text=True).strip()


def project_version(build):
    cache = (build / "CMakeCache.txt").read_text()
    match = re.search(r"^CMAKE_PROJECT_VERSION:\w+=(.+)$", cache, re.M)
    if not match:
        raise RuntimeError(f"No project version in {build}/CMakeCache.txt")
    return match[1].strip()


def package_owner(path):
    """Return the distro package owning a file, or None (for example an aqt Qt)."""
    # Merged-/usr systems may record a library under /lib rather than /usr/lib.
    candidates = [path] + ([path[4:]] if path.startswith("/usr/lib/") else [])
    for command in (["pacman", "-Qqo"], ["dpkg-query", "-S"]):
        if not shutil.which(command[0]):
            continue
        for candidate in candidates:
            result = subprocess.run([*command, candidate], text=True, capture_output=True)
            if result.returncode == 0:
                # dpkg prints "libfoo1:amd64: /usr/lib/...".
                return result.stdout.split(": ")[0].split(":")[0].strip()
    return None


def collect_notices(owners, qt_prefix, notices):
    for package in sorted(set(owners.values()) - {None}):
        for source in (Path("/usr/share/licenses") / package, Path("/usr/share/doc") / package / "copyright"):
            if source.is_dir():
                shutil.copytree(source, notices / package)
            elif source.is_file():
                (notices / package).mkdir(exist_ok=True)
                shutil.copy2(source, notices / package / "copyright")
    common = Path("/usr/share/licenses/common")
    if shutil.which("pacman") and common.is_dir():
        shutil.copytree(common, notices / "common", symlinks=False)
    # Qt from the online installer/aqt is not package-owned; keep its own notices.
    for candidate in [qt_prefix / "Licenses", *(parent / "Licenses" for parent in qt_prefix.parents[:2])]:
        if candidate.is_dir():
            shutil.copytree(candidate, notices / "qt")
            break
    else:
        if None in owners.values():
            (notices / "qt").mkdir(exist_ok=True)
            text = QT_NOTICE.format(version=run(QMAKE, "-query", "QT_VERSION"))
            if any(name.startswith("libicu") and owner is None for name, owner in owners.items()):
                text += "The libicu* libraries are ICU, under the Unicode License (https://www.unicode.org/license.txt).\n"
            (notices / "qt/NOTICE.txt").write_text(text)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, default=ROOT / "build")
    parser.add_argument("--output", type=Path,
                        help="New directory to create (default: dist/agent-pet-VERSION-linux-ARCH)")
    args = parser.parse_args()
    version = project_version(args.build.resolve())
    name = f"agent-pet-{version}-linux-{platform.machine()}"
    output = (args.output or ROOT / "dist" / name).resolve()
    if output.exists():
        parser.error("Output already exists; select a fresh --output directory")
    for tool in ("cmake", "ldd", "patchelf"):
        if not shutil.which(tool):
            parser.error(f"Missing build tool: {tool}")
    global QMAKE
    QMAKE = os.environ.get("QMAKE") or shutil.which("qmake6") or shutil.which("qmake")
    if not QMAKE:
        parser.error("Missing build tool: qmake6 (or set QMAKE)")
    subprocess.run(["cmake", "--install", str(args.build.resolve()), "--prefix", str(output)], check=True)
    lib = output / "lib"
    lib.mkdir(exist_ok=True)
    plugin_dir = output / "plugins/platforms"
    plugin_dir.mkdir(parents=True)
    qt_plugins = Path(run(QMAKE, "-query", "QT_INSTALL_PLUGINS"))
    binaries = [output / "bin/agent-pet"]
    for name in ("libqxcb.so", "libqoffscreen.so"):
        dest = plugin_dir / name
        shutil.copy2(qt_plugins / "platforms" / name, dest)
        binaries.append(dest)
    (output / "bin/qt.conf").write_text("[Paths]\nPrefix=..\nLibraries=lib\nPlugins=plugins\n")
    sources = {}
    system = {}
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
                system[source.name] = str(source)
                continue
            dest = lib / source.name
            if not dest.exists():
                shutil.copy2(source, dest)
                sources[source.name] = str(source.resolve())
    for binary in [*binaries, *lib.iterdir()]:
        relative = os.path.relpath(lib, binary.parent)
        subprocess.run(["patchelf", "--set-rpath", "$ORIGIN/" + relative, str(binary)], check=True)
    # Preserve distro copyright/license notices for bundled libraries.
    notices = output / "share/agent-pet/runtime-licenses"
    notices.mkdir()
    qt_prefix = Path(run(QMAKE, "-query", "QT_INSTALL_PREFIX"))
    owners = {name: package_owner(path) for name, path in sources.items()}
    owners["libqxcb.so"] = package_owner(str(qt_plugins / "platforms/libqxcb.so"))
    collect_notices(owners, qt_prefix, notices)
    manifest = {"format": "agent-pet linux release", "version": version,
                "architecture": platform.machine(), "libraries": sorted(sources),
                "host_libraries": sorted(system), "host_library_paths": system,
                "distro_packages": sorted(set(owners.values()) - {None}),
                "unowned_libraries": sorted(name for name, owner in owners.items() if owner is None),
                "qt_version": run(QMAKE, "-query", "QT_VERSION"),
                "glibc_build_host": platform.libc_ver()[1]}
    (output / "share/agent-pet/runtime-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    archive = shutil.make_archive(str(output), "gztar", output.parent, output.name)
    print(f"Created {archive}; {len(sources)} bundled libraries. See docs/install.md for host requirements.")


if __name__ == "__main__":
    main()
