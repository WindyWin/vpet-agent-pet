#!/usr/bin/env python3
"""Install a per-user Chrome native host and Agent Pet data-only pack."""
import argparse
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import sys

HOST = "vn.agent_pet.chrome_downloads"
ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--extension-id", required=True, help="ID from chrome://extensions")
    parser.add_argument("--agent-pet", required=True, help="Full path to the installed Agent Pet executable")
    parser.add_argument("--browser", choices=["chrome", "chromium"], default="chrome")
    parser.add_argument("--install-dir", type=Path)
    parser.add_argument("--manifest-dir", type=Path)
    parser.add_argument("--plugins-dir", type=Path)
    args = parser.parse_args()
    if sys.platform not in ("linux", "darwin"):
        parser.error("This installer currently supports Linux and macOS only.")
    if not re.fullmatch("[a-p]{32}", args.extension_id):
        parser.error("Extension ID must be the 32-letter ID from chrome://extensions.")
    executable = Path(args.agent_pet).expanduser().resolve()
    if not executable.is_file() or not os.access(executable, os.X_OK):
        parser.error("--agent-pet must point to an executable file.")
    home = Path.home()
    if sys.platform == "darwin":
        data = home / "Library/Application Support"
        browser = "Google/Chrome" if args.browser == "chrome" else "Chromium"
        manifest_dir = data / browser / "NativeMessagingHosts"
    else:
        data = Path(os.environ.get("XDG_DATA_HOME", home / ".local/share"))
        config = Path(os.environ.get("XDG_CONFIG_HOME", home / ".config"))
        manifest_dir = config / ("google-chrome" if args.browser == "chrome" else "chromium") / "NativeMessagingHosts"
    install = (args.install_dir or data / "agent-pet/chrome-downloads").expanduser().resolve()
    manifest_dir = (args.manifest_dir or manifest_dir).expanduser().resolve()
    plugins = (args.plugins_dir or data / "agent-pet/plugins").expanduser().resolve()
    install.mkdir(parents=True, exist_ok=True)
    manifest_dir.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(ROOT / "native/host.py", install / "host.py")
    (install / "config.json").write_text(json.dumps({"executable": str(executable)}, indent=2) + "\n")
    launcher = install / "launch-host"
    launcher.write_text(f"#!/bin/sh\nexec {shlex.quote(sys.executable)} {shlex.quote(str(install / 'host.py'))}\n")
    launcher.chmod(0o700)
    manifest = {
        "name": HOST, "description": "Agent Pet Chrome download events", "path": str(launcher),
        "type": "stdio", "allowed_origins": [f"chrome-extension://{args.extension_id}/"]
    }
    (manifest_dir / f"{HOST}.json").write_text(json.dumps(manifest, indent=2) + "\n")
    pack = plugins / "chrome-downloads"
    pack.mkdir(parents=True, exist_ok=True)
    for name in ("plugin.json", "animations.json", "events.json"):
        shutil.copyfile(ROOT / "plugin/chrome-downloads" / name, pack / name)
    print(f"Installed native host: {manifest_dir / (HOST + '.json')}")
    print(f"Installed plugin: {pack}")
    print("Enable Chrome Downloads in Agent Pet Settings → Plugins, restart the pet, then check the extension connection.")


if __name__ == "__main__":
    main()
