#!/usr/bin/env python3
"""Exercise the update helper with disposable installations and simulated apps."""
import hashlib
import io
import os
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile

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
print("Update checksums, archive safety, startup rollback and successful replacement: passed")
