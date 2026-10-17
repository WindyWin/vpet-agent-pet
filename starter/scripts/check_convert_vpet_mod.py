#!/usr/bin/env python3
"""Converter regression checks; tiny generated mods plus the bundled archive mapping."""
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

from convert_vpet_mod import ROOT, TEMPLATE, convert, discover
from add_sequences import frames_of

APP = sys.argv.pop(1) if len(sys.argv) > 1 else None


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

    def test_fallback_and_import_validation(self):
        self.add_frames('IDEL/yawning/Nomal')
        self.add_frames('Unused/Animation')
        (self.mod / 'pet/cat/Default/Nomal/1/A_000_125.png').rename(
            self.mod / 'pet/cat/Default/Nomal/1/猫_000_125.PNG')
        # Executable plugins and non-frame metadata must never enter the pack.
        (self.mod / 'pet/cat/Default/Nomal/1/ignored.dll').write_bytes(b'not code')
        folder = self.base / 'test-cat'
        report = convert(self.mod, folder, 'test-cat', terms=self.terms)
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

    def test_missing_consent_metadata(self):
        with self.assertRaisesRegex(ValueError, 'terms'):
            convert(self.mod, self.base / 'pack', 'cat')

    def test_unsafe_graph_path(self):
        (self.mod / 'pet/cat.lps').write_text('pet#cat:|path#../outside:|')
        with self.assertRaisesRegex(ValueError, 'Unsafe'):
            discover(self.mod)

    def test_symlink_rejected(self):
        link = self.mod / 'pet/cat/Default/Nomal/1/link.png'
        try:
            link.symlink_to(self.png)
        except OSError:
            self.skipTest('Symlinks unavailable')
        with self.assertRaisesRegex(ValueError, 'Symlinks'):
            convert(self.mod, self.base / 'pack', 'cat', terms=self.terms)


if __name__ == '__main__':
    unittest.main()
