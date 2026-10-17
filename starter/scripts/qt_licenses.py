#!/usr/bin/env python3
"""Shared Qt notices and checksum-verified source downloads for official releases."""
import argparse
import hashlib
import os
from pathlib import Path
import re
import shutil
import subprocess
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
# QtBase supplies Core/Gui/Widgets/Network/DBus and the platform/TLS plugins.
# macdeployqt/windeployqt may also deploy SVG and additional image format plugins.
MODULES = ("qtbase", "qtsvg", "qtimageformats")


def source_urls(version):
    if not re.fullmatch(r"6\.\d+\.\d+", version):
        raise ValueError(f"Expected a public Qt 6 release version, got {version!r}")
    series = version.rsplit(".", 1)[0]
    base = f"https://download.qt.io/archive/qt/{series}/{version}/submodules"
    return [f"{base}/{module}-everywhere-src-{version}.tar.xz" for module in MODULES]


def qt_notices(qmake, destination):
    def query(key):
        return subprocess.check_output([str(qmake), "-query", key], text=True).strip()

    prefix = Path(query("QT_INSTALL_PREFIX"))
    version = query("QT_VERSION")
    custom_sources = os.environ.get("QT_SOURCE_URLS", "").split()
    # Distro/Homebrew Qt may contain patches: upstream's version alone is insufficient.
    if not custom_sources and (prefix == Path("/usr") or "homebrew" in str(prefix)
                               or "linuxbrew" in str(prefix)
                               or "Cellar" in prefix.parts):
        raise RuntimeError("Bundling system Qt requires QT_SOURCE_URLS: URLs of the exact "
                           "source packages, patches and build scripts. See docs/qt-licensing.md.")
    urls = custom_sources or source_urls(version)
    if any(not url.startswith("https://") for url in urls):
        raise ValueError("QT_SOURCE_URLS must contain HTTPS download URLs")
    destination.mkdir(parents=True, exist_ok=True)
    for candidate in (prefix / "Licenses", *(parent / "Licenses" for parent in prefix.parents[:2])):
        if candidate.is_dir():
            shutil.copytree(candidate, destination / "sdk", dirs_exist_ok=True)
            break
    for name in ("LGPL-3.0.txt", "GPL-3.0.txt", "QT-NOTICE.txt"):
        shutil.copy2(ROOT / "licenses" / name, destination / name)
    text = f"Qt {version} corresponding source\n\n"
    text += "Download the complete source archives below at no charge:\n"
    text += "\n".join(urls) + "\n"
    if not custom_sources:
        app_version = re.search(r"project\(AgentPet VERSION ([\d.]+)",
                                (ROOT / "CMakeLists.txt").read_text())[1]
        text += ("\nOfficial releases use the unmodified Qt SDK. Copies of these archives\n"
                 "are also available as assets on the same release as the binaries:\n"
                 f"https://github.com/WindyWin/vpet-agent-pet/releases/tag/v{app_version}\n"
                 "QtBase covers Core, Gui, Widgets, Network, DBus and platform/TLS plugins;\n"
                 "QtSvg and QtImageFormats cover optional deployed image plugins.\n")
    text += "\nPreserve source availability for as long as you distribute these binaries.\n"
    (destination / "SOURCES.txt").write_text(text, encoding="utf-8")


def download_sources(version, destination):
    destination.mkdir(parents=True, exist_ok=True)
    checksums = []
    for url in source_urls(version):
        name = url.rsplit("/", 1)[1]
        with urllib.request.urlopen(url + ".sha256", timeout=120) as response:
            expected = response.read().decode().split()[0].lower()
        if not re.fullmatch(r"[0-9a-f]{64}", expected):
            raise RuntimeError(f"Invalid SHA-256 from {url}.sha256")
        target = destination / name
        partial = target.with_suffix(target.suffix + ".part")
        digest = hashlib.sha256()
        try:
            with urllib.request.urlopen(url, timeout=120) as response, partial.open("wb") as output:
                while chunk := response.read(1024 * 1024):
                    digest.update(chunk)
                    output.write(chunk)
            if digest.hexdigest() != expected:
                raise RuntimeError(f"SHA-256 mismatch for {name}")
            partial.replace(target)
        finally:
            partial.unlink(missing_ok=True)
        checksums.append(f"{expected}  {name}\n")
        print(f"Verified {name}")
    (destination / "qt-source-SHA256SUMS.txt").write_text("".join(checksums), encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--download-sources", required=True, metavar="QT_VERSION")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    download_sources(args.download_sources, args.output)
