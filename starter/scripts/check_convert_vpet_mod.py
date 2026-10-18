#!/usr/bin/env python3
"""Converter regression checks; tiny generated mods plus the bundled archive mapping."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
import zipfile

from convert_vpet_mod import ROOT, TEMPLATE, convert, discover
from add_sequences import frames_of

APP = sys.argv.pop(1) if len(sys.argv) > 1 else None
NATIVE = sys.argv.pop(1) if len(sys.argv) > 1 else None


class ConverterTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        self.mod = self.base / 'mod'
        (self.mod / 'pet').mkdir(parents=True)
        (self.mod / 'pet/cat.lps').write_text(
            'pet#cat:|path#cat:|petname#Test Cat:|author#Artist:|license#CC0-1.0:|\n'
            'tag#cat,test:|\n'
            'touchhead:|px#1:|py#2:|sw#3:|sh#4:|\n'
            'raisepoint:|nomal_x#10:|nomal_y#20:|\n', encoding='utf-8')
        self.terms = self.base / 'terms.txt'
        self.terms.write_text('Test fixture artwork terms.', encoding='utf-8')
        self.png = ROOT / 'tests/fixtures/local-pets/local-cat/idle/_000_100.png'
        self.add_frames('Default/Nomal/1')

    def add_frames(self, path):
        folder = self.mod / 'pet/cat' / path
        folder.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(self.png, folder / 'A_000_125.png')
        shutil.copyfile(self.png, folder / 'A_001_250.png')

    def compare_native(self, folder, report, source=None):
        if not NATIVE:
            return
        native = self.base / 'native' / folder.name
        report_path = self.base / 'native-report.json'
        subprocess.run([NATIVE, str(source or self.mod), str(native), folder.name,
                        str(self.terms), str(report_path)], check=True)
        self.assertEqual(json.loads(report_path.read_text(encoding='utf-8')), report)
        for name in ('animations.json', 'manifest.json', 'pet.json', 'pack.json'):
            self.assertEqual(json.loads((native / name).read_text(encoding='utf-8')),
                             json.loads((folder / name).read_text(encoding='utf-8')), name)
        for file in folder.rglob('*'):
            if file.is_file() and file.suffix != '.json':
                self.assertEqual(file.read_bytes(), (native / file.relative_to(folder)).read_bytes())
        shutil.rmtree(native)

    def native_rejects(self, source=None):
        if NATIVE:
            result = subprocess.run([NATIVE, str(source or self.mod), str(self.base / 'rejected'),
                                     'cat', str(self.terms), str(self.base / 'report.json')],
                                    capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0, result.stdout)

    def test_fallback_and_import_validation(self):
        self.add_frames('IDEL/yawning/Nomal')
        self.add_frames('Unused/Animation')
        (self.mod / 'pet/cat/Default/Nomal/1/A_000_125.png').rename(
            self.mod / 'pet/cat/Default/Nomal/1/猫_000_125.PNG')
        # Executable plugins and non-frame metadata must never enter the pack.
        (self.mod / 'pet/cat/Default/Nomal/1/ignored.dll').write_bytes(b'not code')
        folder = self.base / 'test-cat'
        report = convert(self.mod, folder, 'test-cat', terms=self.terms)
        self.compare_native(folder, report)
        self.assertEqual(report['sequences']['IDEL/yawning/Happy']['kind'], 'normal')
        self.assertEqual(report['sequences']['Think/Nomal/A']['kind'], 'idle')
        self.assertIn('Unused/Animation', report['unused_folders'])
        self.assertEqual(report['raise_points'][0]['nomal_x'], '10')
        catalog = json.loads((folder / 'animations.json').read_text())
        self.assertEqual(catalog['touch']['regions'][0]['rect'], [1, 2, 3, 4])
        self.assertNotIn('moves', catalog)
        self.assertFalse(list(folder.rglob('*.dll')))
        self.assertEqual(catalog['sequences'][0]['duration_ms'], 375)
        self.assertTrue(all(f['path'].startswith('assets/test-cat/') for s in catalog['sequences'] for f in s['frames']))
        if APP:
            subprocess.run([APP, 'validate-pack', str(folder)], check=True)
            archive = self.base / 'cat.agentpet'
            command = [sys.executable, str(ROOT / 'scripts/pack.py'), str(folder), '-o', str(archive), '--app', APP]
            subprocess.run(command, check=True)
            original = archive.read_bytes()
            subprocess.run(command, check=True)
            self.assertEqual(original, archive.read_bytes())

    def test_archive_mapping(self):
        # Mirror every root archive graph with tiny PNGs so this runs in CI
        # without Pillow or hundreds of MiB of temporary artwork.
        source = ROOT.parent / 'assets/vpet/pet/vup'
        if not source.is_dir():
            self.skipTest('Root archive unavailable')
        shutil.rmtree(self.mod / 'pet/cat')
        for file in source.rglob('*.png'):
            target = self.mod / 'pet/cat' / file.relative_to(source)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(self.png, target)
        report = convert(self.mod, self.base / 'test-full', 'test-full', terms=self.terms)
        self.compare_native(self.base / 'test-full', report)
        template = json.loads(TEMPLATE.read_text())
        result = json.loads((self.base / 'test-full/animations.json').read_text())
        for key in ('states', 'playback', 'variants', 'moods', 'cues', 'activity', 'moves'):
            self.assertEqual(result[key], template[key], key)
        if APP:
            subprocess.run([APP, 'validate-pack', str(self.base / 'test-full')], check=True)
        existing = {p.parent.relative_to(source).as_posix() for p in source.rglob('*.png')}
        sequences = {s['path']: s for s in result['sequences']}
        for path, entry in report['sequences'].items():
            self.assertEqual(entry['kind'] == 'exact', path in existing, path)
            if path in existing:
                expected = [duration for _, duration, _ in frames_of(source / path)]
                self.assertEqual([f['duration_ms'] for f in sequences[path]['frames']], expected, path)

    @unittest.skipUnless(sys.platform.startswith('linux'), 'CLI data isolation uses XDG paths')
    def test_cli_consent_and_report(self):
        if not APP:
            self.skipTest('Application unavailable')
        data = self.base / 'data'
        env = dict(os.environ, XDG_DATA_HOME=str(data), XDG_CONFIG_HOME=str(self.base / 'config'))
        command = [APP, 'import', '--vpet-mod', str(self.mod), '--id', 'cli-cat',
                   '--terms', str(self.terms)]
        rejected = subprocess.run(command, input='no\n', text=True, capture_output=True, env=env)
        self.assertEqual(rejected.returncode, 2, rejected.stderr)
        installed = data / 'agent-pet/packs/pets/cli-cat'
        self.assertFalse(installed.exists())
        report = self.base / 'cli-report.json'
        accepted = subprocess.run(command + ['--report', str(report)], input='yes\n',
                                  text=True, capture_output=True, env=env)
        self.assertEqual(accepted.returncode, 0, accepted.stderr)
        self.assertTrue(installed.is_dir())
        self.assertEqual(json.loads(report.read_text())['id'], 'cli-cat')
        self.assertIn('Test fixture artwork terms.', accepted.stdout)
        # A report must never overwrite the supplied licence or an existing file.
        original = self.terms.read_bytes()
        refused = subprocess.run(command + ['--report', str(self.terms)], input='yes\n',
                                 text=True, capture_output=True, env=env)
        self.assertNotEqual(refused.returncode, 0)
        self.assertEqual(self.terms.read_bytes(), original)

    def test_zip_and_duplicate_runs(self):
        self.add_frames('IDEL/yawning/Nomal')
        idle = self.mod / 'pet/cat/Default/Nomal/1'
        shutil.copyfile(self.png, idle / 'B_000_400.png')
        shutil.copyfile(self.png, idle / 'A_000_500.png')
        shutil.copyfile(self.png, idle / '\U00010000_000_300.png')
        shutil.copyfile(self.png, idle / '\ue000_000_200.png')
        (idle / 'ignored.dll').write_bytes(b'not executable artwork')
        folder = self.base / 'zip-cat'
        report = convert(self.mod, folder, 'zip-cat', terms=self.terms)
        archive = self.base / 'mod.zip'
        with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as zip:
            for path in self.mod.rglob('*'):
                if path.is_file():
                    zip.write(path, '猫mod/' + path.relative_to(self.mod).as_posix())
        self.compare_native(folder, report, archive)

    def test_zip_traversal(self):
        archive = self.base / 'unsafe.zip'
        with zipfile.ZipFile(archive, 'w') as zip:
            zip.writestr('../escape.dll', b'ignored but unsafe')
        self.native_rejects(archive)
        self.assertFalse((self.base / 'escape.dll').exists())

    def test_bad_utf8(self):
        (self.mod / 'pet/cat.lps').write_bytes(b'pet#cat:|\xff')
        self.native_rejects()

    def test_zero_duration(self):
        (self.mod / 'pet/cat/Default/Nomal/1/A_000_125.png').rename(
            self.mod / 'pet/cat/Default/Nomal/1/A_000_0.png')
        self.native_rejects()

    def test_missing_consent_metadata(self):
        with self.assertRaisesRegex(ValueError, 'terms'):
            convert(self.mod, self.base / 'pack', 'cat')

    def test_unsafe_graph_path(self):
        (self.mod / 'pet/cat.lps').write_text('pet#cat:|path#../outside:|')
        self.native_rejects()
        with self.assertRaisesRegex(ValueError, 'Unsafe'):
            discover(self.mod)

    def test_symlink_rejected(self):
        link = self.mod / 'pet/cat/Default/Nomal/1/link.png'
        try:
            link.symlink_to(self.png)
        except OSError:
            self.skipTest('Symlinks unavailable')
        self.native_rejects()
        with self.assertRaisesRegex(ValueError, 'Symlinks'):
            convert(self.mod, self.base / 'pack', 'cat', terms=self.terms)


if __name__ == '__main__':
    unittest.main()
