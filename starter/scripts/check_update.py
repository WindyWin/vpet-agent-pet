#!/usr/bin/env python3
"""Exercise the update helper with disposable installations and simulated apps."""
import hashlib
import io
import json
import platform
import os
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile
from package import write_components

helper = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix="pet-update-") as temporary:
    root = Path(temporary)
    runtime = root / "runtime"
    runtime.mkdir(mode=0o700)
    env = dict(os.environ, HOME=str(root), XDG_DATA_HOME=str(root / "data"), XDG_RUNTIME_DIR=str(runtime))
    updates = root / "data/agent-pet/updates"
    updates.mkdir(parents=True)
    app = root / "installed pet"
    (app / "bin").mkdir(parents=True)
    receipt = f"version=old\nprefix={app}\nfile=/unchanged/desktop\nlink=/unchanged/bin\n"
    (app / ".agent-pet-install").write_text(receipt)
    old = b"#!/bin/sh\nexit 0\n"
    executable = app / "bin/agent-pet"
    executable.write_bytes(old)
    executable.chmod(0o755)
    settings = root / "data/agent-pet/preferences.json"
    settings.write_text('{"preserve":"settings"}')

    def package(script, extra=None):
        path = root / "update.tar.gz"
        with tarfile.open(path, "w:gz") as archive:
            item = tarfile.TarInfo("agent-pet-99.1.0/bin/agent-pet")
            item.size, item.mode = len(script), 0o755
            archive.addfile(item, io.BytesIO(script))
            if extra:
                archive.addfile(*extra)
        return path, "sha256:" + hashlib.sha256(path.read_bytes()).hexdigest()

    def apply(path, digest):
        return subprocess.run([str(helper), "--apply", str(app), str(path), digest, "99.1.0", "0"],
                              env=env, text=True, capture_output=True, timeout=40)

    good = b'''#!/bin/sh
if [ "$1" = --version ]; then echo 'agent-pet 99.1.0 (test)'; exit 0; fi
if [ "$1" = --update-health ]; then
  mkdir -p "$XDG_DATA_HOME/agent-pet/updates"
  echo ready > "$XDG_DATA_HOME/agent-pet/updates/health-$2"
  sleep 0.3
  exit 0
fi
exit 1
'''
    path, digest = package(good)
    result = apply(path, "sha256:" + "0" * 64)
    assert result.returncode != 0 and executable.read_bytes() == old, result

    for name, kind in [("agent-pet-99.1.0/../../escape", tarfile.REGTYPE),
                       ("agent-pet-99.1.0/link", tarfile.SYMTYPE),
                       ("agent-pet-99.1.0/hardlink", tarfile.LNKTYPE)]:
        entry = tarfile.TarInfo(name)
        entry.type = kind
        if kind in (tarfile.SYMTYPE, tarfile.LNKTYPE):
            entry.linkname = "/tmp"
        path, digest = package(good, (entry, None))
        result = apply(path, digest)
        assert result.returncode != 0 and executable.read_bytes() == old, result
        assert not (root / "escape").exists()

    broken = b"#!/bin/sh\nif [ \"$1\" = --version ]; then echo 'agent-pet 99.1.0 (test)'; exit 0; fi\nexit 1\n"
    path, digest = package(broken)
    result = apply(path, digest)
    assert result.returncode != 0 and executable.read_bytes() == old, result
    assert "restored" in (updates / "result.txt").read_text(), result
    assert (app / ".agent-pet-install").read_text() == receipt
    assert not Path(str(app) + ".update-transaction.json").exists()

    path, digest = package(good)
    result = apply(path, digest)
    assert result.returncode == 0, result
    assert executable.read_bytes() == good
    assert "file=/unchanged/desktop\nlink=/unchanged/bin" in (app / ".agent-pet-install").read_text()
    assert settings.read_text() == '{"preserve":"settings"}'
    assert not Path(str(app) + ".update-transaction.json").exists()
    assert not list(root.glob("installed pet.update-*"))
    # Component updates use the real publisher and installer. An old full-package
    # installation can immediately supply matching files without an old manifest.
    bundle = root / "bundle"
    for directory in ("bin", "lib", "share/agent-pet"):
        (bundle / directory).mkdir(parents=True, exist_ok=True)
        (app / directory).mkdir(parents=True, exist_ok=True)
    for relative, content in (("bin/agent-pet-updater", b"helper"),
                              ("lib/libtest.so", b"runtime"),
                              ("share/agent-pet/artwork.rcc", b"artwork")):
        (bundle / relative).write_bytes(content)
        (app / relative).write_bytes(content)
    (app / "obsolete-file").write_text("removed in target")
    target = bundle / "bin/agent-pet"

    def component_package(script):
        target.write_bytes(script)
        target.chmod(0o755)
        manifest = write_components(bundle, "99.1.0", platform.machine())
        manifest_path = root / f"agent-pet-99.1.0-linux-{platform.machine()}-components.json"
        # No runtime/artwork archives are available: matching files must be reused.
        for component in manifest["components"]:
            if component["name"] != "app":
                (root / component["archive"]).unlink()
        return manifest, manifest_path

    def apply_components(manifest_path):
        checksum = "sha256:" + hashlib.sha256(manifest_path.read_bytes()).hexdigest()
        return subprocess.run([str(helper), "--apply-components", str(app), str(manifest_path), checksum, "99.1.0", "0"],
                              env=env, text=True, capture_output=True, timeout=40)

    first_manifest, manifest_path = component_package(broken)
    result = apply_components(manifest_path)
    assert result.returncode != 0 and executable.read_bytes() == good, result
    assert (app / "lib/libtest.so").read_bytes() == b"runtime"
    assert (app / "obsolete-file").exists()  # Rollback restores the entire old tree.

    changed = good.replace(b"(test)", b"(components)")
    manifest, manifest_path = component_package(changed)
    for name in ("runtime", "artwork"):
        first = next(c for c in first_manifest["components"] if c["name"] == name)
        second = next(c for c in manifest["components"] if c["name"] == name)
        assert first == second, "Unchanged components must be reproducible"
    runtime_file = app / "lib/libtest.so"
    runtime_file.unlink()
    runtime_file.symlink_to(bundle / "lib/libtest.so")
    result = apply_components(manifest_path)
    assert result.returncode != 0 and executable.read_bytes() == good, result
    runtime_file.unlink()
    runtime_file.write_bytes(b"runtime")
    (app / "share/agent-pet/artwork.rcc").write_bytes(b"damaged")
    result = apply_components(manifest_path)
    assert result.returncode != 0 and executable.read_bytes() == good, result
    (app / "share/agent-pet/artwork.rcc").write_bytes(b"artwork")

    app_component = next(c for c in manifest["components"] if c["name"] == "app")
    app_archive = root / app_component["archive"]
    archive_bytes = app_archive.read_bytes()
    app_archive.write_bytes(b"corrupt")
    result = apply_components(manifest_path)
    assert result.returncode != 0 and executable.read_bytes() == good, result
    app_archive.write_bytes(archive_bytes)

    # A valid archive digest is insufficient: staged files must match the manifest too.
    original_manifest = manifest_path.read_bytes()
    app_component["files"][0]["digest"] = "sha256:" + "0" * 64
    manifest_path.write_text(json.dumps(manifest))
    result = apply_components(manifest_path)
    assert result.returncode != 0 and executable.read_bytes() == good, result
    manifest_path.write_bytes(original_manifest)

    result = apply_components(manifest_path)
    assert result.returncode == 0 and executable.read_bytes() == changed, result
    assert (app / "lib/libtest.so").read_bytes() == b"runtime"
    assert (app / "share/agent-pet/artwork.rcc").read_bytes() == b"artwork"
    assert not (app / "obsolete-file").exists()
    assert not manifest_path.exists() and not app_archive.exists()
    assert settings.read_text() == '{"preserve":"settings"}'
    assert "file=/unchanged/desktop\nlink=/unchanged/bin" in (app / ".agent-pet-install").read_text()
    assert not list(root.glob("installed pet.update-*"))
    # Migrate the legacy monolith to packs, then change only one sequence.
    packs = ["artwork-" + digit * 64 for digit in ("a", "b")]
    for name in packs:
        (bundle / f"share/agent-pet/{name}.rcc").write_bytes(name.encode())
    (bundle / "share/agent-pet/artwork.rcc").write_bytes(b"catalog and pack index")
    manifest = write_components(bundle, "99.1.0", platform.machine())
    assert manifest["format"] == 2
    for component in manifest["components"]:
        if component["name"] in ("app", "runtime"):
            (root / component["archive"]).unlink()
    result = apply_components(manifest_path)
    assert result.returncode == 0, result
    (bundle / f"share/agent-pet/{packs[1]}.rcc").write_bytes(b"changed sequence")
    manifest = write_components(bundle, "99.1.0", platform.machine())
    for component in manifest["components"]:
        if component["name"] != packs[1]:
            (root / component["archive"]).unlink()
    result = apply_components(manifest_path)
    assert result.returncode == 0, result
    assert (app / f"share/agent-pet/{packs[0]}.rcc").read_bytes() == packs[0].encode()
    assert (app / f"share/agent-pet/{packs[1]}.rcc").read_bytes() == b"changed sequence"
print("Full/component updates: checksums, reuse, corrupt files, path safety, rollback and replacement passed")
