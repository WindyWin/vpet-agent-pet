#!/usr/bin/env python3
"""Install, upgrade and uninstall a release tarball under a throwaway HOME in a path with spaces,
non-interactively and through the installer's plain prompts."""
import argparse
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import tarfile
import tempfile
import time

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

# Interactive installer in plain-prompt mode, answered from a script, in a fresh HOME.
# Only Claude Code is present, so the checklist offers: menu entry, command link,
# Claude Code and autostart. PATH excludes any real claude or codex command.
with tempfile.TemporaryDirectory(prefix="agent pet tui ") as temporary:
    root = Path(temporary)
    home = root / "home"
    (home / ".claude").mkdir(parents=True)
    runtime = root / "runtime"
    runtime.mkdir(mode=0o700)
    env = {"HOME": str(home), "PATH": "/usr/bin:/bin", "LANG": "C.UTF-8", "AGENT_PET_UI": "plain",
           "QT_QPA_PLATFORM": "offscreen", "XDG_RUNTIME_DIR": str(runtime)}
    with tarfile.open(args.tarball) as archive:
        archive.extractall(root / "Downloads", filter="data")
    package = next((root / "Downloads").iterdir())
    prefix = root / "Apps" / "agent-pet"
    app = prefix / "bin/agent-pet"
    link = home / ".local/bin/agent-pet"
    claude = home / ".claude/settings.json"
    preferences = home / ".local/share/agent-pet/preferences.json"

    def answer(command, *answers, code=0):
        result = subprocess.run(command, env=env, text=True, capture_output=True, timeout=60,
                                input="".join(line + "\n" for line in answers))
        if result.returncode != code:
            raise SystemExit(f"{command} exited {result.returncode}, expected {code}:\n{result.stdout}{result.stderr}")
        return result

    def autostart():
        return json.loads(subprocess.run([str(app), "autostart", "status"], env=env, text=True,
                                         capture_output=True, check=True).stdout)

    # Cancelling at the summary changes nothing.
    answer([str(package / "install.sh"), "--interactive"], str(prefix), "", "", "", "", "n", code=1)
    assert not prefix.exists() and not link.exists() and not claude.exists()
    # Fresh install: keep the defaults, turn on autostart and hide when idle.
    result = answer([str(package / "install.sh"), "--interactive"], str(prefix), "", "", "", "y", "2", "")
    print(result.stdout.strip().splitlines()[-1])
    assert "Connected Claude Code" in result.stdout and "Autostart on agent session start: on" in result.stdout
    assert "Claude Code hooks: enable" in result.stdout and "Codex" not in result.stdout, result.stdout
    assert "~/.local/bin is not on your PATH" in result.stderr
    assert link.resolve() == app.resolve()
    commands = owned(json.loads(claude.read_text()), "claude")
    assert commands and all(command.startswith(f"'{app}' hook") for command in commands), commands
    assert not (home / ".codex").exists()
    assert autostart() == {"autostart": True, "when_idle": "hide", "preferences": str(preferences), "changed": False}

    # Autostart from the installed package: a session start launches the pet detached.
    if not args.skip_launch:
        launch = dict(env, WAYLAND_DISPLAY="agent-pet-check")
        hook = subprocess.run(f"\"{app}\" hook --provider claude | cat", shell=True, env=launch, text=True,
                              capture_output=True, timeout=5,
                              input=json.dumps({"session_id": "check", "hook_event_name": "SessionStart"}))
        assert hook.returncode == 0 and not hook.stdout and not hook.stderr, hook
        probe = json.dumps({"version": 1, "provider": "claude", "session_id": "check", "kind": "prompt"})

        def executable(pid):
            try:
                return os.readlink(f"/proc/{pid}/exe")
            except OSError:  # Another user's process, or gone.
                return None

        def pets():
            return [int(pid) for pid in os.listdir("/proc") if pid.isdigit() and executable(pid) == str(app.resolve())]
        try:
            deadline = time.monotonic() + 20
            while subprocess.run([str(app), "emit"], env=env, input=probe, text=True, capture_output=True).returncode:
                assert time.monotonic() < deadline, "autostarted pet never started listening"
                time.sleep(0.2)
            assert len(pets()) == 1, pets()
        finally:  # Never leave a detached pet behind, even when a check fails.
            for pid in pets():
                os.kill(pid, signal.SIGTERM)
            deadline = time.monotonic() + 10
            while pets():
                assert time.monotonic() < deadline, "autostarted pet did not stop"
                time.sleep(0.1)
        print("Hook autostarted the installed pet")

    # Upgrade: the checklist starts from what is set up; unchecking Claude Code removes its hooks.
    # The location defaults to the installation behind the command link.
    result = answer([str(package / "install.sh"), "--interactive"], "", "", "", "n", "", "", "")
    print(result.stdout.strip().splitlines()[-1])
    assert "Claude Code hooks: remove" in result.stdout and "Removed Agent Pet hooks for Claude Code" in result.stdout
    assert not owned(json.loads(claude.read_text()), "claude")
    assert autostart()["autostart"] and autostart()["when_idle"] == "hide"
    # Upgrade again, unchecking autostart.
    answer([str(package / "install.sh"), "--interactive"], "", "", "", "", "n", "")
    assert not autostart()["autostart"]

    # Non-interactive installs leave hooks and autostart alone unless asked.
    before = (claude.read_bytes(), preferences.read_bytes())
    answer([str(package / "install.sh"), "--prefix", str(prefix)])
    assert (claude.read_bytes(), preferences.read_bytes()) == before
    answer([str(package / "install.sh"), "--prefix", str(prefix), "--claude", "--autostart", "--when-idle", "quit"])
    assert owned(json.loads(claude.read_text()), "claude")
    assert autostart()["autostart"] and autostart()["when_idle"] == "quit"
    refused = answer([str(package / "install.sh"), "--prefix", str(prefix), "--when-idle", "hide"], code=2)
    assert "--autostart" in refused.stderr

    # Interactive uninstall: remove hooks (default on), delete settings (default off: turn on),
    # keep the command link (default on: turn off).
    answer([str(prefix / "uninstall.sh"), "--interactive"], "", "y", "n", "")
    assert not prefix.exists() and not preferences.parent.exists()
    assert not owned(json.loads(claude.read_text()), "claude")
    assert link.is_symlink(), "an unchecked command link stays"
    link.unlink()
print("Interactive install, upgrade, autostart and uninstall (plain prompts): passed")
