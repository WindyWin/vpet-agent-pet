#!/usr/bin/env python3
"""Build the relocatable Linux release bundle from a trusted local build (not arbitrary ELF files)."""
import argparse
import ctypes
import ctypes.util
import gzip
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import stat
import subprocess
import tarfile

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


def write_components(output, version, architecture):
    """Publish independently downloadable components and a complete target file list."""
    groups = {name: [] for name in ("app", "runtime", "artwork")}
    for path in sorted(output.rglob("*")):
        if path.is_symlink():
            raise RuntimeError(f"Release contains a symlink: {path}")
        if not path.is_file():
            continue
        relative = path.relative_to(output).as_posix()
        component = "app"
        if relative in ("share/agent-pet/artwork.rcc", "share/agent-pet/THIRD_PARTY_NOTICES.md") or relative.startswith("share/agent-pet/licenses/"):
            component = "artwork"
        elif relative.startswith(("lib/", "plugins/", "share/agent-pet/runtime-licenses/")):
            component = "runtime"
        if re.fullmatch(r"share/agent-pet/artwork-[0-9a-f]{64}\.rcc", relative):
            component = path.stem
        groups.setdefault(component, []).append(path)
    base = f"agent-pet-{version}-linux-{architecture}"
    manifest = {"format": 2 if any(name.startswith("artwork-") for name in groups) else 1, "version": version, "architecture": architecture, "components": []}
    for name, paths in groups.items():
        archive_path = output.parent / f"{base}-{name}.tar.gz"
        files = []
        # Fixed names, ordering, ownership and times make unchanged components reproducible.
        with archive_path.open("wb") as raw, gzip.GzipFile(filename="", fileobj=raw, mode="wb", mtime=0) as compressed:
            with tarfile.open(fileobj=compressed, mode="w|", format=tarfile.PAX_FORMAT) as archive:
                for path in paths:
                    relative = path.relative_to(output).as_posix()
                    executable = bool(path.stat().st_mode & 0o111)
                    entry = tarfile.TarInfo("agent-pet/" + relative)
                    entry.size, entry.mode = path.stat().st_size, 0o755 if executable else 0o644
                    with path.open("rb") as content:
                        archive.addfile(entry, content)
                    files.append({"path": relative, "size": entry.size, "executable": executable,
                                  "digest": "sha256:" + hashlib.sha256(path.read_bytes()).hexdigest()})
        manifest["components"].append({"name": name, "archive": archive_path.name,
                                       "size": archive_path.stat().st_size, "files": files,
                                       "digest": "sha256:" + hashlib.sha256(archive_path.read_bytes()).hexdigest()})
    (output.parent / f"{base}-components.json").write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n")
    return manifest


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
    binaries = [output / "bin/agent-pet", output / "bin/agent-pet-updater"]
    # HTTPS needs Qt's TLS backend, including its OpenSSL dependency closure.
    tls_dir = output / "plugins/tls"
    tls_dir.mkdir(parents=True)
    tls_plugins = list((qt_plugins / "tls").glob("*.so"))
    if not tls_plugins:
        raise RuntimeError("Qt TLS plugins are required for update checks")
    for source in tls_plugins:
        dest = tls_dir / source.name
        shutil.copy2(source, dest)
        binaries.append(dest)
    for name in ("libqxcb.so", "libqoffscreen.so"):
        dest = plugin_dir / name
        shutil.copy2(qt_plugins / "platforms" / name, dest)
        binaries.append(dest)
    (output / "bin/qt.conf").write_text("[Paths]\nPrefix=..\nLibraries=lib\nPlugins=plugins\n")
    sources = {}
    system = {}
    # ldd resolves the transitive closure; collect each plugin's closure as well.
    def collect_dependencies(binary):
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
    for binary in binaries:
        collect_dependencies(binary)
    # aqt Qt resolves OpenSSL with dlopen. Prefer the SSL runtime beside the
    # crypto library already selected by the package's dependency closure.
    if not any(name.startswith("libssl.so.") for name in sources):
        runtime_paths = []
        for name, source in sources.items():
            if name.startswith("libcrypto.so."):
                candidate = Path(source).with_name(name.replace("libcrypto", "libssl"))
                if candidate.is_file():
                    runtime_paths.append(candidate)
        if not runtime_paths:
            ssl_name = ctypes.util.find_library("ssl")
            if not ssl_name:
                raise RuntimeError("OpenSSL runtime is required for HTTPS updates")
            ssl_runtime = ctypes.CDLL(ssl_name)
            for line in Path("/proc/self/maps").read_text().splitlines():
                fields = line.split()
                if len(fields) >= 6 and re.match(r"libssl\.so\.", Path(fields[-1]).name):
                    runtime_paths.append(Path(fields[-1]))
        for source in set(runtime_paths):
            dest = lib / source.name
            shutil.copy2(source, dest)
            sources[source.name] = str(source.resolve())
            collect_dependencies(source)
    for binary in [*binaries, *lib.iterdir()]:
        binary.chmod(binary.stat().st_mode | stat.S_IWUSR)
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
    components = write_components(output, version, platform.machine())
    for component in components["components"]:
        print(f"{component['name']}: {component['size'] / 1024 / 1024:.2f} MiB")
    print(f"Created {archive}; {len(sources)} bundled libraries. See docs/install.md for host requirements.")


if __name__ == "__main__":
    main()
