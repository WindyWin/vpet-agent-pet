#!/usr/bin/env python3
"""Install, upgrade and uninstall the Windows setup program silently, into a folder with spaces and with
throwaway Claude Code and Codex configurations, and check that hooks run the way each agent starts them."""
import argparse
import hashlib
import json
import os
import re
from pathlib import Path
import subprocess
import tempfile
import time
import winreg

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("setup", type=Path)
parser.add_argument("--previous-setup", type=Path, required=True, help="An older-version setup containing the updater")
args = parser.parse_args()

MARKER = "--registration agent-pet-v1"
UNRELATED = {"type": "command", "command": "cmd /c exit 0", "timeout": 5}
UNINSTALL_KEY = r"Software\Microsoft\Windows\CurrentVersion\Uninstall\{8441EED6-7EE4-4267-AADE-8840188C4202}_is1"
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


def uninstall_value(name):
    with winreg.OpenKey(winreg.HKEY_CURRENT_USER, UNINSTALL_KEY) as key:
        return winreg.QueryValueEx(key, name)[0]


if login_value() is not None:
    raise SystemExit("An Agent Pet login entry already exists for this user; this check would remove it")

updates = Path(os.environ["APPDATA"]) / "agent-pet" / "updates"
result_file = updates / "result.txt"
if result_file.exists() or (updates / "pending.json").exists() or (updates / "state.json").exists():
    raise SystemExit("Existing update state found; run this check under a disposable Windows user")

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
        command = [str(args.previous_setup), "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART", f"/DIR={prefix}",
                   f"/LOG={root / log}"]
        if tasks is not None:
            command.append(f"/TASKS={tasks}")
        run(*command)

    claude = root / "claude/settings.json"
    claude.parent.mkdir()
    seeded = {"model": "keep-me", "hooks": {"Stop": [{"hooks": [UNRELATED]}]}}
    claude.write_text(json.dumps(seeded, indent=4) + "\n")
    codex = root / "codex/hooks.json"

    # Refuse to write into a folder that holds another program's files.
    foreign = root / "Other App"
    foreign.mkdir()
    (foreign / "notes.txt").write_text("not ours")
    refused = subprocess.run([str(args.setup), "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART", f"/DIR={foreign}",
                              f"/LOG={root / 'foreign.log'}"], env=env, text=True, capture_output=True, timeout=300)
    if refused.returncode == 0 or (foreign / "agent-pet.exe").exists() or (foreign / "notes.txt").read_text() != "not ours":
        raise SystemExit(f"Setup wrote into a foreign folder (exit {refused.returncode})")

    install("desktopicon,claude,codex,login", "install.log")
    for name in ("agent-pet.exe", "agent-pet-cli.exe", "agent-pet-updater.exe", "artwork.rcc", "INSTALL.txt", "LICENSE", "unins000.exe"):
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
    install(None, "upgrade.log")
    if owned(json.loads(claude.read_text())) != claude_hooks or login_value() is None:
        raise SystemExit("Upgrade changed the hooks or the login entry")

    # Exercise the real automatic updater, including its external runtime copy and re-verification.
    previous_version = run(str(cli), "--version").stdout.split()[1]
    match = re.fullmatch(r"agent-pet-(\d+\.\d+\.\d+)-windows-x86_64-setup\.exe", args.setup.name)
    if not match:
        raise SystemExit(f"Unexpected target setup name: {args.setup.name}")
    version = match.group(1)
    if tuple(map(int, previous_version.split("."))) >= tuple(map(int, version.split("."))):
        raise SystemExit(f"Fixture {previous_version} must be older than target {version}")
    if uninstall_value("DisplayVersion") != previous_version:
        raise SystemExit("Fixture executable and uninstall entry disagree")
    selected_tasks = uninstall_value("Inno Setup: Selected Tasks")
    if "desktopicon" not in selected_tasks.split(","):
        raise SystemExit("Fixture did not preserve the selected desktop icon task")
    digest = "sha256:" + hashlib.sha256(args.setup.read_bytes()).hexdigest()
    helper = prefix / "agent-pet-updater.exe"
    canonical = prefix.resolve().as_posix()
    corrupted = subprocess.run([str(helper), "--apply", canonical, str(args.setup.resolve()), "sha256:" + "0" * 64,
                                version, "0", "--version"], env=env, timeout=30)
    if corrupted.returncode != 1 or not result_file.exists() or "checksum" not in result_file.read_text(encoding="utf-8"):
        raise SystemExit(f"Automatic updater did not report the checksum mismatch (exit {corrupted.returncode})")
    result_file.unlink(missing_ok=True)
    run(str(helper), "--apply", canonical, str(args.setup.resolve()), digest, version, "0", "--version")
    deadline = time.monotonic() + 300
    while not result_file.exists():
        if time.monotonic() > deadline:
            raise SystemExit("Automatic updater did not finish")
        time.sleep(1)
    result = result_file.read_text(encoding="utf-8")
    if result != f"Updated to {version}.":
        raise SystemExit(f"Automatic update failed: {result}\n" +
                         (updates / "setup.log").read_text(encoding="utf-8-sig", errors="replace"))
    if owned(json.loads(claude.read_text())) != claude_hooks or owned(json.loads(codex.read_text())) != codex_hooks:
        raise SystemExit("Automatic update changed registered hooks")
    if login_value() != f'"{prefix / "agent-pet.exe"}"':
        raise SystemExit("Automatic update changed the login entry")
    installed_version = run(str(cli), "--version").stdout.split()[1]
    if installed_version != version or installed_version == previous_version:
        raise SystemExit(f"Automatic update did not replace {previous_version} with {version}: {installed_version}")
    if uninstall_value("DisplayVersion") != version:
        raise SystemExit("Automatic update did not update its uninstall version")
    if uninstall_value("Inno Setup: Selected Tasks") != selected_tasks:
        raise SystemExit("Automatic update changed the saved installer task selection")
    if not (prefix / "unins000.dat").is_file():
        raise SystemExit("Automatic update lost its uninstall log")
    result_file.unlink()
    time.sleep(2)

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
    print(f"{args.setup.name}: install, hooks, upgrade, automatic update and uninstall passed")
