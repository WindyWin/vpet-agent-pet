#!/usr/bin/env python3
"""Offline publication/retention checks using real deterministic component archives."""
import copy
from datetime import datetime, timezone
import io
import json
import os
from pathlib import Path
import tempfile
import unittest
from contextlib import redirect_stdout
from package import write_components
import release_blobs as blobs


class GitHub:
    def __init__(self):
        self.store = dict(id=1, tag_name=blobs.BLOB_TAG, prerelease=True, draft=False)
        self.release_list = [self.store]
        self.inventory = {1: []}
        self.manifests = {}
        self.uploads = []

    def releases(self):
        return self.release_list

    def assets(self, release):
        return self.inventory[release['id']]

    def upload(self, path):
        self.uploads.append(path.name)
        self.inventory[1].append(dict(id=len(self.uploads), name=path.name, state='uploaded',
                                     size=path.stat().st_size, digest=blobs.sha256(path),
                                     created_at='2025-01-01T00:00:00Z'))

    def download_manifest(self, release, asset):
        return self.manifests[release['id']]

    def release(self, manifest, **flags):
        ident = len(self.release_list) + 1
        self.release_list.append(dict(id=ident, tag_name='v' + manifest['version'], **flags))
        self.inventory[ident] = [dict(name='agent-pet-components.json')]
        self.manifests[ident] = copy.deepcopy(manifest)


class Tests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.stage = self.root / 'stage'
        self.pack = 'artwork-' + 'a' * 64
        for name, data in {'bin/agent-pet': b'app', 'lib/libqt.so': b'qt',
                           'share/agent-pet/runtime-licenses/qt/SOURCES.txt': b'v1.0.0 source URL',
                           'share/agent-pet/artwork.rcc': b'catalog',
                           f'share/agent-pet/{self.pack}.rcc': b'frames'}.items():
            path = self.stage / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        self.github = GitHub()

    def package(self, version='1.0.0'):
        for path in self.root.glob('*-components.json'):
            path.unlink()
        return write_components(self.stage, version, 'x86_64')

    def test_releases_reuse_all_unchanged_blobs(self):
        first = self.package()
        self.assertEqual(len(blobs.publish(self.root, self.github)), 3)
        (self.stage / 'bin/agent-pet').write_bytes(b'new app')
        (self.stage / 'share/agent-pet/runtime-licenses/qt/SOURCES.txt').write_bytes(b'v1.0.1 source URL')
        for path in self.stage.rglob('*'):
            if path.is_file():
                os.utime(path, (1234567890, 1234567890))
                path.chmod(0o600)
        second = self.package('1.0.1')
        self.assertEqual(blobs.blob_references(first), blobs.blob_references(second))
        self.assertEqual(blobs.publish(self.root, self.github), [])
        (self.stage / f'share/agent-pet/{self.pack}.rcc').write_bytes(b'new frames')
        self.package('1.0.2')
        self.assertEqual(len(blobs.publish(self.root, self.github)), 1)

    def test_corruption_fails_before_upload(self):
        self.package()
        next((self.root / 'blobs').iterdir()).write_bytes(b'corrupt')
        with self.assertRaises(ValueError):
            blobs.publish(self.root, self.github)
        self.assertEqual(self.github.uploads, [])

    def test_existing_asset_must_match_digest_and_size(self):
        self.package()
        blobs.publish(self.root, self.github)
        for field, value in [('digest', 'sha256:' + '0' * 64), ('size', 1), ('state', 'new')]:
            asset = self.github.inventory[1][0]
            original = asset[field]
            asset[field] = value
            with self.assertRaises(ValueError):
                blobs.publish(self.root, self.github)
            asset[field] = original

    def test_limit_and_warning(self):
        self.package()
        self.github.inventory[1] = [dict(name=f'unknown-{i}') for i in range(897)]
        with redirect_stdout(io.StringIO()) as output:
            blobs.publish(self.root, self.github)
        self.assertIn('::warning::', output.getvalue())
        self.github.inventory[1].extend(dict(name=f'extra-{i}') for i in range(101))
        with self.assertRaises(ValueError):
            blobs.publish(self.root, self.github)

    def test_retention_drafts_grace_and_unknown_assets(self):
        for version in ['1.0.0', '1.0.1', '1.0.2', '1.0.3']:
            (self.stage / f'share/agent-pet/{self.pack}.rcc').write_text(version)
            manifest = self.package(version)
            blobs.publish(self.root, self.github)
            self.github.release(manifest, draft=version == '1.0.3', prerelease=False)
        old = blobs.blob_references(self.github.manifests[2]).keys() - blobs.blob_references(self.github.manifests[3]).keys()
        now = datetime(2026, 1, 1, tzinfo=timezone.utc)
        plan, retained = blobs.prune_plan(self.github, keep=2, now=now)
        self.assertEqual({a['name'] for a in plan}, old)
        self.assertEqual(retained, ['v1.0.2', 'v1.0.1', 'v1.0.3'])
        plan[0]['created_at'] = '2025-12-30T00:00:00Z'
        self.github.inventory[1].append(dict(name='operator-notes.txt'))
        self.assertEqual(blobs.prune_plan(self.github, keep=2, now=now)[0], [])
        self.github.manifests[3]['format'] = 99
        with self.assertRaises(ValueError):
            blobs.prune_plan(self.github, keep=2, now=now)

    def test_missing_manifest_and_reference_fail_closed(self):
        manifest = self.package()
        blobs.publish(self.root, self.github)
        self.github.release(manifest)
        self.github.release(self.package('1.0.1'))
        self.github.inventory[1].pop()
        with self.assertRaises(ValueError):
            blobs.prune_plan(self.github, keep=2)
        self.github.inventory[2] = []
        with self.assertRaises(ValueError):
            blobs.prune_plan(self.github, keep=2)
        with self.assertRaises(ValueError):
            blobs.prune_plan(self.github, keep=1)

    def test_url_and_name_validation(self):
        manifest = self.package()
        entry = next(e for e in manifest['components'] if e['name'] == 'runtime')
        for url in [entry['url'] + '?x=1', entry['url'].replace('github.com', 'evil.test'),
                    entry['url'].replace('/release-blobs/', '/main/')]:
            invalid = copy.deepcopy(manifest)
            next(e for e in invalid['components'] if e['name'] == 'runtime')['url'] = url
            with self.assertRaises(ValueError):
                blobs.blob_references(invalid)
        for format_ in (1, 2):
            legacy = copy.deepcopy(manifest)
            legacy['format'] = format_
            if format_ == 1:
                legacy['components'] = [e for e in legacy['components'] if e['name'] in ('app', 'runtime', 'artwork')]
            for e in legacy['components']:
                e['archive'] = f"agent-pet-1.0.0-linux-x86_64-{e['name']}.tar.gz"
                e.pop('url')
            self.assertEqual(blobs.blob_references(legacy), {})

    def test_pagination(self):
        github = blobs.GitHub()
        requests = []
        def api(endpoint):
            requests.append(endpoint)
            return list(range(100)) if len(requests) == 1 else [100]
        github.api = api
        self.assertEqual(len(github.assets({'id': 1})), 101)
        self.assertTrue(requests[-1].endswith('page=2'))


if __name__ == '__main__':
    unittest.main()
