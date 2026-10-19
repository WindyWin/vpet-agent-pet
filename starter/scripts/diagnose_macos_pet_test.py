#!/usr/bin/env python3
"""Capture a hung macOS pet test before terminating its process group."""
import os
from pathlib import Path
import resource
import signal
import subprocess
import sys


def no_core_dump():
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


def main():
    executable, function = sys.argv[1:]
    log = Path(executable).resolve().parent / f"pets-{function}.txt"
    process = subprocess.Popen(
        [executable, function, "-nocrashhandler", "-o", "-,txt", "-o", f"{log},txt"],
        start_new_session=True,
        preexec_fn=no_core_dump,
    )
    try:
        return process.wait(timeout=30)
    except subprocess.TimeoutExpired:
        print(f"::error::{function} exceeded 30 seconds; capturing its thread stacks.", flush=True)
        try:
            subprocess.run(
                ["sample", str(process.pid), "3", "-file", str(log.with_suffix(".sample.txt"))],
                timeout=10,
                check=False,
            )
        except (OSError, subprocess.TimeoutExpired) as error:
            print(f"Could not capture macOS sample: {error}", flush=True)
        finally:
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            process.wait(timeout=5)
        return 124


if __name__ == "__main__":
    raise SystemExit(main())
