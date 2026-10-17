#!/usr/bin/env python3
"""Regression checks for Qt release notices and verified source downloads."""
import hashlib
import io
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import qt_licenses as qt


class QtLicenses(unittest.TestCase):
    def notices(self, prefix, destination, sources=""):
        with patch.dict(os.environ, {"QT_SOURCE_URLS": sources}), patch.object(
                qt.subprocess, "check_output", side_effect=[str(prefix), "6.5.3"]):
            qt.qt_notices("qmake", destination)

    def test_sdk_notices_and_fallback_both_include_complete_terms(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            prefix = root / "sdk/6.5.3/gcc_64"
            for has_sdk_notices in (False, True):
                if has_sdk_notices:
                    (prefix / "Licenses").mkdir(parents=True)
                    (prefix / "Licenses/vendor.txt").write_text("Vendor notice")
                output = root / str(has_sdk_notices)
                self.notices(prefix, output)
                self.assertIn("Version 3, 29 June 2007", (output / "LGPL-3.0.txt").read_text())
                self.assertIn("END OF TERMS AND CONDITIONS", (output / "GPL-3.0.txt").read_text())
                self.assertIn("codesign --force", (output / "QT-NOTICE.txt").read_text())
                source = (output / "SOURCES.txt").read_text()
                for module in ("qtbase", "qtsvg", "qtimageformats"):
                    self.assertIn(f"{module}-everywhere-src-6.5.3.tar.xz", source)
                if has_sdk_notices:
                    self.assertEqual((output / "sdk/vendor.txt").read_text(), "Vendor notice")

    def test_system_qt_requires_matching_sources(self):
        with tempfile.TemporaryDirectory() as temp:
            output = Path(temp) / "notices"
            with self.assertRaisesRegex(RuntimeError, "QT_SOURCE_URLS"):
                self.notices(Path("/usr"), output)
            self.notices(Path("/usr"), output, "https://example.org/qt-source.tar.gz https://example.org/patches.tar.gz")
            source = (output / "SOURCES.txt").read_text()
            self.assertIn("https://example.org/patches.tar.gz", source)
            self.assertNotIn("unmodified", source)
            self.assertNotIn("download.qt.io", source)

    def test_checksum_mismatch_never_publishes_archive(self):
        with tempfile.TemporaryDirectory() as temp:
            output = Path(temp)
            replies = [io.BytesIO(b"0" * 64), io.BytesIO(b"corrupt source")]
            with patch.object(qt.urllib.request, "urlopen", side_effect=replies):
                with self.assertRaisesRegex(RuntimeError, "SHA-256 mismatch"):
                    qt.download_sources("6.5.3", output)
            self.assertEqual(list(output.iterdir()), [])

    def test_verified_sources_are_preserved_with_checksums(self):
        data = b"source archive fixture"
        digest = hashlib.sha256(data).hexdigest()
        replies = [io.BytesIO(value) for _ in range(3) for value in (digest.encode(), data)]
        with tempfile.TemporaryDirectory() as temp:
            output = Path(temp)
            with patch.object(qt.urllib.request, "urlopen", side_effect=replies):
                qt.download_sources("6.5.3", output)
            self.assertEqual(len(list(output.glob("*.tar.xz"))), 3)
            self.assertEqual((output / "qt-source-SHA256SUMS.txt").read_text().count(digest), 3)


if __name__ == "__main__":
    unittest.main()
