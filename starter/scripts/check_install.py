#!/usr/bin/env python3
"""Install, upgrade and uninstall a release tarball under a throwaway HOME in a path with spaces."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("tarball", type=Path)
parser.add_argument("--skip-launch", action="store_true", help="Skip the 20-second offscreen smoke test")
args = parser.parse_args()

UNRELATED = {"type": "command", "command": "/usr/bin/true unrelated-hook", "timeout": 5}


def owned(config, provider):
    marker = f"hook --provider {provider} --registration agent-pet-v1"
    return [handler["command"] for groups in config.get("hooks", {}).values()
            for group in groups for handler in group["hooks"] if handler.get("command", "").endswith(marker)]


with tempfile.TemporaryDirectory(prefix="agent pet check ") as temporary:
    root = Path(temporary)
    home = root / "home"
    home.mkdir()
    env = {"HOME": str(home), "PATH": os.environ["PATH"], "LANG": "C.UTF-8",
           "QT_QPA_PLATFORM": "offscreen", "XDG_RUNTIME_DIR": str(root / "runtime")}
    (root / "runtime").mkdir(mode=0o700)
    def run(*command):
        result = subprocess.run(command, env=env, text=True, capture_output=True, timeout=60)
        if result.returncode:
            raise SystemExit(f"{command} failed ({result.returncode}):\n{result.stdout}{result.stderr}")
        return result

    with tarfile.open(args.tarball) as archive:
        archive.extractall(root / "Downloads", filter="data")
    package = next((root / "Downloads").iterdir())
    prefix = root / "My Apps" / "agent-pet"
    data = home / ".local/share"
    desktop = data / "applications/agent-pet.desktop"
    icon = data / "icons/hicolor/256x256/apps/agent-pet.png"
    link = home / ".local/bin/agent-pet"
    app = prefix / "bin/agent-pet"

    # Unrelated configuration that every step must preserve.
    claude = home / ".claude/settings.json"
    claude.parent.mkdir()
    seeded = {"model": "keep-me", "hooks": {"Stop": [{"hooks": [UNRELATED]}]}}
    claude.write_text(json.dumps(seeded, indent=4) + "\n")

    # Refuse to replace a directory that is not an earlier installation.
    prefix.mkdir(parents=True)
    (prefix / "notes.txt").write_text("not ours")
    refused = subprocess.run([str(package / "install.sh"), "--prefix", str(prefix)], env=env,
                             text=True, capture_output=True)
    assert refused.returncode != 0 and (prefix / "notes.txt").exists(), refused
    shutil.rmtree(prefix)

    print(run(str(package / "install.sh"), "--prefix", str(prefix)).stdout.strip())
    assert app.is_file() and icon.is_file() and link.resolve() == app.resolve()
    assert f'Exec="{app}"' in desktop.read_text(), desktop.read_text()
    if shutil.which("desktop-file-validate"):
        run("desktop-file-validate", str(desktop))
    assert run(str(link), "--version").stdout.startswith("agent-pet ")

    for provider in ("claude", "codex"):
        run(str(link), "integration", "enable", "--provider", provider)
    config = json.loads(claude.read_text())
    commands = owned(config, "claude")
    assert commands and all(command.startswith(f"'{app}' hook") for command in commands), commands
    assert config["model"] == "keep-me" and UNRELATED in config["hooks"]["Stop"][0]["hooks"]
    if not args.skip_launch:
        smoke = run(str(app), "--smoke-test")
        assert "automatic input recovery: passed" in smoke.stdout, smoke.stdout

    preferences = data / "agent-pet/preferences.json"
    preferences.parent.mkdir(parents=True, exist_ok=True)
    preferences.write_text('{"size": 240}\n')
    claude_before = claude.read_bytes()
    codex_before = (home / ".codex/hooks.json").read_bytes()
    # Upgrade in place: settings, hook files and launcher survive; files are replaced.
    (prefix / "lib/stale-from-old-version.so").write_text("old")
    print(run(str(package / "install.sh"), "--prefix", str(prefix)).stdout.strip())
    assert not (prefix / "lib/stale-from-old-version.so").exists()
    assert preferences.read_text() == '{"size": 240}\n'
    assert claude.read_bytes() == claude_before and (home / ".codex/hooks.json").read_bytes() == codex_before
    assert desktop.is_file() and link.resolve() == app.resolve()
    assert not list(prefix.parent.glob("agent-pet.*")), list(prefix.parent.iterdir())

    print(run(str(prefix / "uninstall.sh")).stdout.strip())
    assert not prefix.exists() and not desktop.exists() and not icon.exists() and not link.is_symlink()
    config = json.loads(claude.read_text())
    assert config == seeded, config
    assert not owned(json.loads((home / ".codex/hooks.json").read_text()), "codex")
    assert preferences.is_file(), "uninstall must keep settings unless --purge-settings"

    run(str(package / "install.sh"), "--prefix", str(prefix), "--no-desktop", "--no-bin-link")
    run(str(prefix / "uninstall.sh"), "--purge-settings")
    assert not prefix.exists() and not preferences.parent.exists()
    assert json.loads(claude.read_text()) == seeded
print("Install, upgrade, integrations and uninstall from a path with spaces: passed")
