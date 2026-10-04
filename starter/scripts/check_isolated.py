#!/usr/bin/env python3
"""Run a copied bundle with only host glibc visible, no host Qt or checkout."""
import argparse
import json
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("bundle", type=Path)
args = parser.parse_args()
bundle = args.bundle.resolve()
manifest = json.loads((bundle / "share/agent-pet/runtime-manifest.json").read_text())
command = ["bwrap", "--unshare-all", "--die-with-parent", "--new-session",
           "--ro-bind", str(bundle), "/opt/Agent Pet", "--proc", "/proc", "--dev", "/dev",
           "--tmpfs", "/tmp", "--dir", "/home/test", "--dir", "/usr/lib",
           "--symlink", "usr/lib", "/lib", "--symlink", "usr/lib", "/lib64",
           "--clearenv", "--setenv", "HOME", "/home/test",
           "--setenv", "QT_QPA_PLATFORM", "offscreen",
           "--setenv", "QT_LOGGING_RULES", "*.debug=false",
           "--chdir", "/tmp"]
for name in manifest["host_libraries"]:
    command += ["--ro-bind", str((Path("/usr/lib") / name).resolve()), "/usr/lib/" + name]
binary = ["/opt/Agent Pet/bin/agent-pet"]
subprocess.run(command + binary + ["--smoke-test"], check=True, timeout=30)
# Exercise packaged headless entry points without a display, interpreter, client,
# host Qt installation, or checkout visible inside the namespace.
for provider in ("claude", "codex"):
    hook = subprocess.run(command + binary + ["hook", "--provider", provider],
                          input=json.dumps({"session_id": "package-test",
                                            "hook_event_name": "UserPromptSubmit"}),
                          text=True, capture_output=True, check=True, timeout=2)
    assert not hook.stdout and not hook.stderr, hook
    preview = subprocess.run(command + binary + ["integration", "preview",
                             "--provider", provider], text=True, capture_output=True,
                             check=True, timeout=2)
    config = json.loads(preview.stdout)["proposed_config"]
    assert config["hooks"]["Stop"][0]["hooks"][0]["command"].startswith(
        "'/opt/Agent Pet/bin/agent-pet' hook --provider " + provider)
    enabled = subprocess.run(command + binary + ["integration", "enable",
                             "--provider", provider, "--config", "/tmp/hooks.json"],
                             text=True, capture_output=True, check=True, timeout=2)
    assert json.loads(enabled.stdout)["changed"]
print("Packaged Claude/Codex headless callbacks and integration setup: passed")
