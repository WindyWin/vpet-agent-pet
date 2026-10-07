#!/usr/bin/env python3
"""Install, upgrade and uninstall the Windows setup program silently, into a folder with spaces and with
throwaway Claude Code and Codex configurations, and check that hooks run the way each agent starts them."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time
import winreg

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("setup", type=Path)
args = parser.parse_args()

MARKER = "--registration agent-pet-v1"
UNRELATED = {"type": "command", "command": "cmd /c exit 0", "timeout": 5}
RUN_KEY = r"Software\Microsoft\Windows\CurrentVersion\Run"


def handlers(config):
    return [handler for groups in config.get("hooks", {}).values() for group in groups for handler in group["hooks"]]


def owned(config):
    return [handler for handler in handlers(config)
            if handler.get("args", [])[-2:] == ["--registration", "agent-pet-v1"] or handler.get("command", "").endswith(MARKER)]


def login_value():
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, RUN_KEY) as key:
            return winreg.QueryValueEx(key, "Agent Pet")[0]
    except FileNotFoundError:
        return None


if login_value() is not None:
    raise SystemExit("An Agent Pet login entry already exists for this user; this check would remove it")

with tempfile.TemporaryDirectory(prefix="agent pet check ") as temporary:
    root = Path(temporary)
    env = dict(os.environ, CLAUDE_CONFIG_DIR=str(root / "claude"), CODEX_HOME=str(root / "codex"),
               XDG_RUNTIME_DIR=str(root / "runtime"))  # Hooks send to a separate pipe, away from a running pet.
    prefix = root / "My Apps" / "AgentPet"
    cli = prefix / "agent-pet-cli.exe"

    def run(*command, **kwargs):
        result = subprocess.run(command, env=env, text=True, capture_output=True, timeout=300, **kwargs)
        if result.returncode:
            raise SystemExit(f"{command} failed ({result.returncode}):\n{result.stdout}{result.stderr}")
        return result

    def install(tasks, log):
        run(str(args.setup), "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART", f"/DIR={prefix}", f"/TASKS={tasks}",
            f"/LOG={root / log}")

    claude = root / "claude/settings.json"
    claude.parent.mkdir()
    seeded = {"model": "keep-me", "hooks": {"Stop": [{"hooks": [UNRELATED]}]}}
    claude.write_text(json.dumps(seeded, indent=4) + "\n")
    codex = root / "codex/hooks.json"

    install("claude,codex,login", "install.log")
    for name in ("agent-pet.exe", "agent-pet-cli.exe", "artwork.rcc", "INSTALL.txt", "LICENSE", "unins000.exe"):
        if not (prefix / name).is_file():
            raise SystemExit(f"Missing installed file {name}")
    print(run(str(cli), "--version").stdout.strip())

    # Claude Code runs the exec form directly; Codex runs its command through cmd.exe.
    config = json.loads(claude.read_text())
    if config["model"] != "keep-me" or UNRELATED not in handlers(config):
        raise SystemExit(f"Unrelated Claude Code settings changed: {config}")
    claude_hooks = owned(config)
    if not claude_hooks or any(Path(handler["command"]) != cli for handler in claude_hooks):
        raise SystemExit(f"Unexpected Claude Code hooks: {claude_hooks}")
    codex_hooks = owned(json.loads(codex.read_text()))
    if not codex_hooks or any(" " in handler["command"].split(" hook ")[0] for handler in codex_hooks):
        raise SystemExit(f"Unexpected Codex hooks: {codex_hooks}")
    payload = json.dumps({"session_id": "check", "hook_event_name": "SessionStart"})
    first = claude_hooks[0]
    for command in ([first["command"], *first["args"]], ["cmd.exe", "/e:ON", "/v:OFF", "/d", "/c", codex_hooks[0]["command"]]):
        result = run(*command, input=payload)
        if result.stdout or result.stderr:
            raise SystemExit(f"Hook {command} wrote output: {result.stdout}{result.stderr}")
    if login_value() != f'"{prefix / "agent-pet.exe"}"':
        raise SystemExit(f"Unexpected login entry: {login_value()!r}")

    # Upgrading in place keeps one set of hooks and the login entry.
    install("", "upgrade.log")
    if owned(json.loads(claude.read_text())) != claude_hooks or login_value() is None:
        raise SystemExit("Upgrade changed the hooks or the login entry")

    run(str(prefix / "unins000.exe"), "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART")
    # The uninstaller continues from a temporary copy; wait for it to finish removing files.
    deadline = time.monotonic() + 120
    while (prefix / "agent-pet.exe").exists() or (prefix / "unins000.exe").exists():
        if time.monotonic() > deadline:
            raise SystemExit("Uninstall did not remove the installed files")
        time.sleep(1)
    time.sleep(2)
    if json.loads(claude.read_text()) != seeded:
        raise SystemExit(f"Uninstall did not restore Claude Code settings: {claude.read_text()}")
    if owned(json.loads(codex.read_text())):
        raise SystemExit(f"Uninstall left Codex hooks: {codex.read_text()}")
    if login_value() is not None:
        raise SystemExit("Uninstall left the login entry")
    print(f"{args.setup.name}: install, hooks, upgrade and uninstall passed")
