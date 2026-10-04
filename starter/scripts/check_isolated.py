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
command += ["/opt/Agent Pet/bin/agent-pet", "--smoke-test"]
subprocess.run(command, check=True, timeout=30)
