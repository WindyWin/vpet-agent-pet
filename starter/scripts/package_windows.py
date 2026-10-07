#!/usr/bin/env python3
"""Build the Windows release from a trusted local MSVC build: a portable zip and a per-user setup program."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import zipfile

from package_macos import project_version, qt_notices, run

ROOT = Path(__file__).resolve().parents[1]
EXECUTABLES = ("agent-pet.exe", "agent-pet-cli.exe")
ISCC_LOCATIONS = (Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / "Inno Setup 6/ISCC.exe",
                  Path(os.environ.get("ProgramFiles", r"C:\Program Files")) / "Inno Setup 6/ISCC.exe")


def compiler_runtime(destination):
    """Copy the Visual C++ runtime beside the executables (app-local deployment), from the
    redistributable folder the Visual Studio developer environment names."""
    redist = os.environ.get("VCToolsRedistDir")
    if not redist:
        raise RuntimeError("VCToolsRedistDir is not set; run from a Visual Studio developer environment")
    folders = sorted((Path(redist) / "x64").glob("Microsoft.VC*.CRT"))
    if not folders:
        raise RuntimeError(f"No Microsoft.VC*.CRT folder under {redist}\\x64")
    for library in folders[-1].glob("*.dll"):
        shutil.copy2(library, destination / library.name)


def isolated_environment():
    """Only Windows itself on PATH and no Qt overrides, so the staged files must be complete."""
    system = os.environ.get("SystemRoot", r"C:\Windows")
    environment = {key: value for key, value in os.environ.items()
                   if not key.upper().startswith("QT_") and key.upper() != "PATH"}
    environment["PATH"] = os.pathsep.join([system + r"\System32", system, system + r"\System32\Wbem"])
    return environment


def check_stage(stage, version, smoke_test):
    environment = isolated_environment()
    cli = str(stage / "agent-pet-cli.exe")
    reported = run(cli, "--version", env=environment)
    if not reported.startswith(f"agent-pet {version} "):
        raise RuntimeError(f"Staged app reports {reported!r}")
    print(reported)
    # Update checks need Qt's TLS backend among the deployed plugins.
    print(run(cli, "--check-update-runtime", env=environment))
    if smoke_test:
        # Loads the platform plugin, the artwork and the tray icon, then exercises input recovery.
        print(run(cli, "--smoke-test", env=environment, timeout=120))


def write_zip(stage, archive):
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as output:
        for path in sorted(stage.rglob("*")):
            if path.is_file():
                output.write(path, Path(stage.name) / path.relative_to(stage))


def find_iscc():
    found = os.environ.get("ISCC") or shutil.which("iscc")
    if found:
        return found
    for candidate in ISCC_LOCATIONS:
        if candidate.is_file():
            return str(candidate)
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, default=ROOT / "build")
    parser.add_argument("--output", type=Path, help="Directory for the zip and setup program (default: dist)")
    parser.add_argument("--smoke-test", action="store_true", help="Also run the staged pet's --smoke-test (needs a desktop)")
    args = parser.parse_args()
    if sys.platform != "win32":
        parser.error("Windows packages are built on Windows")
    build = args.build.resolve()
    version = project_version(build)
    name = f"agent-pet-{version}-windows-x86_64"
    output = (args.output or ROOT / "dist").resolve()
    stage = output / name
    archive = output / f"{name}.zip"
    setup = output / f"{name}-setup.exe"
    existing = [str(path) for path in (stage, archive, setup) if path.exists()]
    if existing:
        parser.error(f"{', '.join(existing)} already exists; remove it or select another --output")
    qmake = os.environ.get("QMAKE") or shutil.which("qmake6") or shutil.which("qmake")
    if not qmake:
        parser.error("Missing build tool: qmake (or set QMAKE)")
    deploy = Path(run(qmake, "-query", "QT_INSTALL_BINS")) / "windeployqt.exe"
    if not deploy.exists():
        parser.error(f"Missing {deploy}")
    iscc = find_iscc()
    if not iscc:
        parser.error("Missing Inno Setup 6 (ISCC.exe); install it or set ISCC")

    stage.mkdir(parents=True)
    for executable in EXECUTABLES:
        shutil.copy2(build / executable, stage / executable)
    # The catalog names the artwork packs; copy exactly those, beside the executables.
    packs = json.loads((build / "artwork-packs.json").read_text())
    for rcc in ["artwork.rcc", *packs]:
        shutil.copy2(build / rcc, stage / rcc)
    shutil.copytree(ROOT / "licenses", stage / "licenses")
    for notice in ("THIRD_PARTY_NOTICES.md", "LICENSE", "NOTICE"):
        shutil.copy2(ROOT / notice, stage / notice)
    qt_notices(qmake, stage / "runtime-licenses/qt")
    shutil.copy2(ROOT / "packaging/windows/INSTALL.txt", stage / "INSTALL.txt")
    # Copies the Qt libraries and the plugins both executables need (windows platform, styles, image
    # formats, TLS backends). The pet translates itself; Qt's own catalogs are not used.
    subprocess.run([str(deploy), "--release", "--no-translations", "--no-system-d3d-compiler", "--no-opengl-sw",
                    "--dir", str(stage), *(str(stage / executable) for executable in EXECUTABLES)], check=True)
    compiler_runtime(stage)
    check_stage(stage, version, args.smoke_test)
    write_zip(stage, archive)
    subprocess.run([iscc, "/Q", f"/DAppVersion={version}", f"/DSourceDir={stage}", f"/DOutputDir={output}",
                    f"/DOutputBaseFilename={setup.stem}", str(ROOT / "packaging/windows/agent-pet.iss")], check=True)
    print(archive)
    print(setup)


if __name__ == "__main__":
    main()
