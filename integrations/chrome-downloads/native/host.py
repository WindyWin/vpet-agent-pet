#!/usr/bin/env python3
"""Chrome native messaging -> Agent Pet custom events. Stdout is framing only."""
import json
from pathlib import Path
import struct
import subprocess
import sys

MAX_MESSAGE = 4096
EVENTS = {"download_started", "download_completed", "download_interrupted", "ping"}


def read_exact(stream, size):
    data = bytearray()
    while len(data) < size:
        chunk = stream.read(size - len(data))
        if not chunk:
            raise EOFError("Truncated native message")
        data.extend(chunk)
    return bytes(data)


def read_message(stream):
    first = stream.read(1)
    if not first:
        return None
    size = struct.unpack("=I", first + read_exact(stream, 3))[0]
    if not 0 < size <= MAX_MESSAGE:
        raise ValueError("Invalid native message size")
    message = json.loads(read_exact(stream, size).decode("utf-8"))
    if not isinstance(message, dict):
        raise ValueError("Native message must be an object")
    return message


def write_message(stream, message):
    payload = json.dumps(message).encode("utf-8")
    stream.write(struct.pack("=I", len(payload)) + payload)
    stream.flush()


def deliver(message, executable):
    if (not isinstance(message, dict) or set(message) != {"version", "event"}
            or type(message["version"]) is not int or message["version"] != 1
            or not isinstance(message["event"], str) or message["event"] not in EVENTS):
        return {"ok": False, "error": "Invalid download event"}
    name = "chrome_downloads_connection_check" if message["event"] == "ping" else message["event"]
    try:
        result = subprocess.run(
            [executable, "emit", "--custom", name], stdin=subprocess.DEVNULL,
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=3, check=False)
        if result.returncode:
            return {"ok": False, "error": "Cannot reach Agent Pet. Start the desktop app and check its executable path."}
    except (OSError, subprocess.TimeoutExpired):
        return {"ok": False, "error": "Cannot run Agent Pet. Check the native host installation."}
    return {"ok": True}


def main():
    config = json.loads(Path(__file__).with_name("config.json").read_text())
    while True:
        try:
            message = read_message(sys.stdin.buffer)
            if message is None:
                return
            write_message(sys.stdout.buffer, deliver(message, config["executable"]))
        except (EOFError, ValueError, UnicodeError):
            write_message(sys.stdout.buffer, {"ok": False, "error": "Invalid native message"})
            return


if __name__ == "__main__":
    main()
