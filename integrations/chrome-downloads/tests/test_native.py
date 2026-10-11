import importlib.util
import io
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("host", ROOT / "native/host.py")
host = importlib.util.module_from_spec(spec)
spec.loader.exec_module(host)


class NativeTests(unittest.TestCase):
    def test_framing_and_limits(self):
        stream = io.BytesIO()
        host.write_message(stream, {"version": 1, "event": "download_completed"})
        stream.seek(0)
        self.assertEqual(host.read_message(stream)["event"], "download_completed")
        self.assertIsNone(host.read_message(stream))
        for size in (0, 4097, 2**32 - 1):
            with self.assertRaises(ValueError):
                host.read_message(io.BytesIO(struct.pack("=I", size)))
        with self.assertRaises(EOFError):
            host.read_message(io.BytesIO(struct.pack("=I", 20) + b"{}"))
        with self.assertRaises(ValueError):
            host.read_message(io.BytesIO(struct.pack("=I", 4) + b"null"))

    def test_allowlist_rejects_payload_and_invalid_types(self):
        invalid = [None, [], {}, {"version": True, "event": "ping"},
                   {"version": 1, "event": []}, {"version": 1, "event": "rm -rf /"},
                   {"version": 1, "event": "download_started", "filename": "secret"}]
        with patch.object(host.subprocess, "run") as run:
            for message in invalid:
                self.assertFalse(host.deliver(message, "/unused")["ok"])
            run.assert_not_called()

    def test_delivery_and_failure(self):
        with patch.object(host.subprocess, "run") as run:
            run.return_value.returncode = 0
            self.assertTrue(host.deliver({"version": 1, "event": "download_completed"}, "/pet path")["ok"])
            self.assertEqual(run.call_args.args[0], ["/pet path", "emit", "--custom", "download_completed"])
            run.return_value.returncode = 1
            self.assertFalse(host.deliver({"version": 1, "event": "ping"}, "/pet path")["ok"])
            run.side_effect = subprocess.TimeoutExpired("pet", 3)
            self.assertFalse(host.deliver({"version": 1, "event": "ping"}, "/pet path")["ok"])

    def test_installed_host_end_to_end_with_spaces_in_paths(self):
        with tempfile.TemporaryDirectory(prefix="pet host ") as folder:
            base = Path(folder)
            fake = base / "fake pet"
            fake.write_text(f"#!{sys.executable}\nimport sys\nfrom pathlib import Path\nPath(__file__).with_name('arguments.json').write_text(__import__('json').dumps(sys.argv[1:]))\n")
            fake.chmod(0o700)
            subprocess.run([sys.executable, str(ROOT / "native/install.py"),
                            "--extension-id", "a" * 32, "--agent-pet", str(fake),
                            "--install-dir", str(base / "host files"),
                            "--manifest-dir", str(base / "manifests"),
                            "--plugins-dir", str(base / "plugins")], check=True, capture_output=True)
            manifest = json.loads((base / "manifests/vn.agent_pet.chrome_downloads.json").read_text())
            self.assertEqual(manifest["allowed_origins"], ["chrome-extension://" + "a" * 32 + "/"])
            stream = io.BytesIO()
            host.write_message(stream, {"version": 1, "event": "download_completed"})
            result = subprocess.run([manifest["path"]], input=stream.getvalue(), capture_output=True, check=True)
            self.assertEqual(host.read_message(io.BytesIO(result.stdout)), {"ok": True})
            self.assertEqual(json.loads((base / "arguments.json").read_text()), ["emit", "--custom", "download_completed"])
            self.assertTrue((base / "plugins/chrome-downloads/events.json").is_file())


if __name__ == "__main__":
    unittest.main()
