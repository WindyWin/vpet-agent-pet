#!/usr/bin/env python3
"""Build the macOS release: an ad-hoc signed Agent Pet.app in a zip, from a trusted local build."""
import argparse
import json
import os
from pathlib import Path
import plistlib
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
BUNDLE_ID = "io.github.windywin.agent-pet"
MINIMUM_MACOS = "11.0"
QT_NOTICE = """The bundled Qt {version} frameworks and plugins are used under the GNU Lesser
General Public License v3 (https://www.gnu.org/licenses/lgpl-3.0.html), unmodified
and dynamically linked; you may replace them in Contents/Frameworks and Contents/PlugIns.
Corresponding source: https://download.qt.io/archive/qt/ (qtbase-everywhere-src-{version}).
"""


def run(*args, **kwargs):
    return subprocess.run(args, check=True, text=True, capture_output=True, **kwargs).stdout.strip()


def project_version(build):
    cache = (build / "CMakeCache.txt").read_text()
    match = re.search(r"^CMAKE_PROJECT_VERSION:\w+=(.+)$", cache, re.M)
    if not match:
        raise RuntimeError(f"No project version in {build}/CMakeCache.txt")
    return match[1].strip()


def make_icon(source, destination):
    """Convert the 256 px PNG to an .icns with sips and iconutil (both part of macOS)."""
    with tempfile.TemporaryDirectory() as temp:
        iconset = Path(temp) / "agent-pet.iconset"
        iconset.mkdir()
        for size in (16, 32, 64, 128, 256):
            run("sips", "-z", str(size), str(size), str(source), "--out", str(iconset / f"icon_{size}x{size}.png"))
            if size <= 128:
                run("sips", "-z", str(size * 2), str(size * 2), str(source), "--out", str(iconset / f"icon_{size}x{size}@2x.png"))
        run("iconutil", "-c", "icns", str(iconset), "-o", str(destination))


def qt_notices(qmake, destination):
    """Qt from the online installer/aqt keeps its notices beside the install; otherwise state the terms."""
    prefix = Path(run(qmake, "-query", "QT_INSTALL_PREFIX"))
    for candidate in (prefix / "Licenses", *(parent / "Licenses" for parent in prefix.parents[:2])):
        if candidate.is_dir():
            shutil.copytree(candidate, destination)
            return
    destination.mkdir(parents=True)
    (destination / "NOTICE.txt").write_text(QT_NOTICE.format(version=run(qmake, "-query", "QT_VERSION")))


def macho_files(bundle):
    for path in sorted(bundle.rglob("*")):
        if path.is_file() and not path.is_symlink() and "Mach-O" in run("file", "-b", str(path)):
            yield path


def drop_build_rpaths(bundle):
    """Remove absolute run paths into the build machine's Qt, so only the bundled frameworks can load."""
    for path in macho_files(bundle):
        commands = run("otool", "-l", str(path)).splitlines()
        rpaths = {commands[i + 2].split()[1] for i, line in enumerate(commands)
                  if line.strip() == "cmd LC_RPATH" and i + 2 < len(commands)}
        for rpath in sorted(rpaths):
            if not rpath.startswith("@"):
                run("install_name_tool", "-delete_rpath", rpath, str(path))


def check_bundle(bundle, universal):
    """Every Mach-O resolves inside the bundle or the OS, and carries both architectures when asked."""
    executable = bundle / "Contents/MacOS/agent-pet"
    for path in macho_files(bundle):
        for line in run("otool", "-L", str(path)).splitlines():
            if line.endswith(":"):  # "<file>:" or, for each slice of a universal file, "<file> (architecture arm64):"
                continue
            library = line.strip().split(" (")[0]
            if not library.startswith(("@rpath/", "@executable_path/", "@loader_path/", "/System/", "/usr/lib/")):
                raise RuntimeError(f"{path.relative_to(bundle)} links outside the bundle: {library}")
        if universal:
            architectures = set(run("lipo", "-archs", str(path)).split())
            if not {"arm64", "x86_64"} <= architectures:
                raise RuntimeError(f"{path.relative_to(bundle)} is not universal: {sorted(architectures)}")
    run("codesign", "--verify", "--deep", "--strict", str(bundle))
    print(run(str(executable), "--version"))
    # Update checks need Qt's TLS backend inside the bundle.
    print(run(str(executable), "--check-update-runtime"))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, default=ROOT / "build")
    parser.add_argument("--output", type=Path, help="Directory for the zip (default: dist)")
    parser.add_argument("--universal", action="store_true", help="Require arm64 and x86_64 in every binary")
    args = parser.parse_args()
    build = args.build.resolve()
    version = project_version(build)
    name = f"agent-pet-{version}-macos-universal"
    output = (args.output or ROOT / "dist").resolve()
    stage = output / name
    archive = output / f"{name}.zip"
    if stage.exists() or archive.exists():
        parser.error(f"{stage} or {archive} already exists; remove it or select another --output")
    qmake = os.environ.get("QMAKE") or shutil.which("qmake6") or shutil.which("qmake")
    if not qmake:
        parser.error("Missing build tool: qmake (or set QMAKE)")
    deploy = Path(run(qmake, "-query", "QT_INSTALL_BINS")) / "macdeployqt"
    for tool in ("codesign", "ditto", "otool", "install_name_tool", "lipo", "sips", "iconutil", "file"):
        if not shutil.which(tool):
            parser.error(f"Missing macOS tool: {tool}")
    if not deploy.exists():
        parser.error(f"Missing {deploy}")

    bundle = stage / "Agent Pet.app"
    contents = bundle / "Contents"
    (contents / "MacOS").mkdir(parents=True)
    resources = contents / "Resources"
    resources.mkdir()
    shutil.copy2(build / "agent-pet", contents / "MacOS/agent-pet")
    # The catalog names the artwork packs; copy exactly those.
    packs = json.loads((build / "artwork-packs.json").read_text())
    for rcc in ["artwork.rcc", *packs]:
        shutil.copy2(build / rcc, resources / rcc)
    shutil.copytree(ROOT / "licenses", resources / "licenses")
    for notice in ("THIRD_PARTY_NOTICES.md", "LICENSE", "NOTICE"):
        shutil.copy2(ROOT / notice, resources / notice)
    qt_notices(qmake, resources / "runtime-licenses/qt")
    make_icon(ROOT / "packaging/agent-pet.png", resources / "agent-pet.icns")
    with (contents / "Info.plist").open("wb") as plist:
        plistlib.dump({
            "CFBundleDevelopmentRegion": "en",
            "CFBundleDisplayName": "Agent Pet",
            "CFBundleExecutable": "agent-pet",
            "CFBundleIconFile": "agent-pet",
            "CFBundleIdentifier": BUNDLE_ID,
            "CFBundleInfoDictionaryVersion": "6.0",
            "CFBundleName": "Agent Pet",
            "CFBundlePackageType": "APPL",
            "CFBundleShortVersionString": version,
            "CFBundleVersion": version,
            "LSApplicationCategoryType": "public.app-category.entertainment",
            "LSMinimumSystemVersion": MINIMUM_MACOS,
            # A desktop pet with a menu bar icon: no Dock icon or application menu.
            "LSUIElement": True,
            "NSHighResolutionCapable": True,
            "NSHumanReadableCopyright": "Agent Pet is Apache-2.0; the VPet artwork has its own terms (see About).",
        }, plist)
    (contents / "PkgInfo").write_text("APPL????")

    # Copies the Qt frameworks and plugins (cocoa, image formats, TLS) and rewrites their load paths.
    subprocess.run([str(deploy), str(bundle), "-verbose=1"], check=True)
    drop_build_rpaths(bundle)
    # Without an Apple Developer ID the bundle is signed ad hoc: Apple silicon runs only signed code,
    # and Gatekeeper still asks the user to approve the first launch (see docs/install.md).
    subprocess.run(["codesign", "--force", "--deep", "--sign", "-", "--timestamp=none", str(bundle)], check=True)
    check_bundle(bundle, args.universal)
    shutil.copy2(ROOT / "packaging/macos/INSTALL.txt", stage / "INSTALL.txt")
    # ditto keeps the bundle's symlinks and extended attributes, as Finder's Archive Utility expects.
    subprocess.run(["ditto", "-c", "-k", "--sequesterRsrc", "--keepParent", str(stage), str(archive)], check=True)
    print(archive)


if __name__ == "__main__":
    main()
