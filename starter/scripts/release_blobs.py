#!/usr/bin/env python3
"""Publish immutable release components, or plan/prune unreferenced blobs (dry-run by default)."""
import argparse
from datetime import datetime, timedelta, timezone
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile

REPO = "WindyWin/vpet-agent-pet"
BLOB_TAG = "release-blobs"
DOWNLOAD_BASE = f"https://github.com/{REPO}/releases/download/"
BLOB_BASE = DOWNLOAD_BASE + BLOB_TAG + "/"
BLOB_PATTERN = re.compile(r"sha256-([0-9a-f]{64})\.tar\.gz")
VERSION = re.compile(r"v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)")
MAX_MANIFEST = 4 * 1024 * 1024
MAX_ARCHIVE = 512 * 1024 * 1024
ASSET_LIMIT = 1000
WARN_ASSETS = 900


def sha256(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(chunk)
    return 'sha256:' + digest.hexdigest()


def blob_name(digest):
    if not isinstance(digest, str) or not re.fullmatch(r'sha256:[0-9a-f]{64}', digest):
        raise ValueError('Invalid component SHA-256 digest')
    return 'sha256-' + digest[7:] + '.tar.gz'


def blob_references(manifest):
    """Validate the naming/URL contract before publication or garbage collection."""
    if not isinstance(manifest, dict) or manifest.get('format') not in (1, 2, 3):
        raise ValueError('Unsupported component manifest format; refusing to guess references')
    version, architecture = manifest.get('version'), manifest.get('architecture')
    if not isinstance(version, str) or not VERSION.fullmatch('v' + version):
        raise ValueError('Invalid manifest version')
    if architecture not in ('x86_64', 'aarch64', 'arm64'):
        raise ValueError('Unsupported Linux component architecture')
    entries = manifest.get('components')
    if not isinstance(entries, list) or not 3 <= len(entries) <= 4096:
        raise ValueError('Invalid component list')
    if manifest['format'] == 1 and len(entries) != 3:
        raise ValueError('Format 1 requires exactly three components')
    references, seen = {}, set()
    for entry in entries:
        if not isinstance(entry, dict):
            raise ValueError('Invalid component entry')
        name = entry.get('name')
        if not isinstance(name, str) or not re.fullmatch(r'app|runtime|artwork(?:-[0-9a-f]{64})?', name) or name in seen:
            raise ValueError('Invalid or duplicate component name')
        seen.add(name)
        digest, size, archive = entry.get('digest'), entry.get('size'), entry.get('archive')
        shared_name = blob_name(digest)
        if type(size) is not int or not 0 < size <= MAX_ARCHIVE:
            raise ValueError('Invalid component size')
        local_name = f'agent-pet-{version}-linux-{architecture}-{name}.tar.gz'
        if archive == local_name:
            if manifest['format'] == 3 and entry.get('url') != DOWNLOAD_BASE + f'v{version}/' + archive:
                raise ValueError('Invalid versioned component URL')
        elif manifest['format'] == 3 and name != 'app' and archive == shared_name and entry.get('url') == BLOB_BASE + archive:
            if archive in references and references[archive] != (digest, size):
                raise ValueError('Conflicting shared component metadata')
            references[archive] = (digest, size)
        else:
            raise ValueError('Invalid component archive or URL')
    if not {'app', 'runtime', 'artwork'} <= seen:
        raise ValueError('Incomplete component manifest')
    return references


def load_manifest(path):
    path = Path(path)
    if path.is_symlink() or not path.is_file() or path.stat().st_size > MAX_MANIFEST:
        raise ValueError(f'Invalid manifest file: {path}')
    return json.loads(path.read_text(encoding='utf-8'))


class GitHub:
    """Use gh's authenticated API; all release/asset lists are fully paginated."""
    def api(self, endpoint, method='GET'):
        result = subprocess.run(['gh', 'api', '--method', method, f'repos/{REPO}/{endpoint}'],
                                capture_output=True, check=True)
        return json.loads(result.stdout) if result.stdout else None

    def pages(self, endpoint):
        result = []
        for page in range(1, 101):
            values = self.api(f'{endpoint}?per_page=100&page={page}')
            if not isinstance(values, list):
                raise ValueError('Invalid paginated API response')
            result.extend(values)
            if len(values) < 100:
                return result
        raise ValueError('Pagination limit exceeded; refusing an incomplete inventory')

    def releases(self):
        return self.pages('releases')

    def assets(self, release):
        return self.pages(f'releases/{release["id"]}/assets')

    def create_store(self):
        subprocess.run(['gh', 'release', 'create', BLOB_TAG, '--repo', REPO, '--prerelease', '--latest=false',
                        '--title', 'Shared release components', '--notes',
                        'Immutable SHA-256 component archives. Referenced by versioned components.json manifests. '
                        'Managed by release_blobs.py; never replace an asset.'], check=True)
        return self.api(f'releases/tags/{BLOB_TAG}')

    def upload(self, path):
        subprocess.run(['gh', 'release', 'upload', BLOB_TAG, str(path), '--repo', REPO], check=True)

    def download_manifest(self, release, asset):
        expected = DOWNLOAD_BASE + release['tag_name'] + '/' + asset['name']
        if (asset.get('browser_download_url') != expected or asset.get('state') != 'uploaded'
                or type(asset.get('size')) is not int or not 0 < asset['size'] <= MAX_MANIFEST):
            raise ValueError('Invalid retained manifest asset')
        blob_name(asset.get('digest'))  # Require a GitHub SHA-256 digest before trusting references.
        with tempfile.TemporaryDirectory(prefix='agent-pet-manifest-') as temporary:
            path = Path(temporary) / 'manifest.json'
            with path.open('wb') as output:
                subprocess.run(['gh', 'api', f'repos/{REPO}/releases/assets/{asset["id"]}',
                                '-H', 'Accept: application/octet-stream'], stdout=output, check=True)
            if path.stat().st_size != asset['size'] or sha256(path) != asset['digest']:
                raise ValueError('Retained manifest checksum mismatch')
            return load_manifest(path)

    def delete(self, asset):
        self.api(f'releases/assets/{asset["id"]}', 'DELETE')


def store_release(releases):
    stores = [r for r in releases if r.get('tag_name') == BLOB_TAG]
    if len(stores) > 1:
        raise ValueError('Duplicate blob releases')
    if stores and (not stores[0].get('prerelease') or stores[0].get('draft')):
        raise ValueError('The blob store must be a published prerelease, never a stable release')
    return stores[0] if stores else None


def asset_map(assets):
    result = {}
    for asset in assets:
        if asset['name'] in result:
            raise ValueError('Duplicate release asset names')
        result[asset['name']] = asset
    return result


def check_asset(asset, digest, size):
    if asset.get('state') != 'uploaded' or asset.get('digest') != digest or asset.get('size') != size:
        raise ValueError(f'Existing blob differs from its pinned content: {asset["name"]}')


def publish(directory, github):
    directory = Path(directory)
    manifests = sorted(directory.glob('*-components.json'))
    if not manifests:
        raise ValueError('No component manifests to publish')
    wanted = {}
    for path in manifests:
        manifest = load_manifest(path)
        if manifest['format'] != 3:
            raise ValueError('The publisher requires format 3 manifests')
        for name, metadata in blob_references(manifest).items():
            if name in wanted and wanted[name] != metadata:
                raise ValueError('Conflicting manifests')
            wanted[name] = metadata
    # Check every local blob before creating or changing a release.
    for name, (digest, size) in wanted.items():
        path = directory / 'blobs' / name
        if path.is_symlink() or not path.is_file() or path.stat().st_size != size or sha256(path) != digest:
            raise ValueError(f'Missing or corrupt component: {name}')
    store = store_release(github.releases())
    existing = asset_map(github.assets(store)) if store else {}
    missing = sorted(set(wanted) - existing.keys())
    projected = len(existing) + len(missing)
    if projected >= WARN_ASSETS:
        print(f'::warning::Shared component release would contain {projected}/{ASSET_LIMIT} assets; review a pruning dry run.')
    if projected > ASSET_LIMIT:
        raise ValueError('Shared component release exceeds the 1000-asset limit; prune before publishing')
    for name in set(wanted) & existing.keys():
        check_asset(existing[name], *wanted[name])
    if not store:
        store = github.create_store()
        store_release([store])
    for name in missing:
        github.upload(directory / 'blobs' / name)
    # A successful upload call alone is not enough to reference the blob publicly.
    uploaded = asset_map(github.assets(store))
    for name, metadata in wanted.items():
        if name not in uploaded:
            raise ValueError('Uploaded component absent from the store')
        check_asset(uploaded[name], *metadata)
    print(f'Shared components: uploaded {len(missing)}, reused {len(wanted) - len(missing)}')
    return missing


def prune_plan(github, keep=3, grace_days=14, now=None):
    if keep < 2 or grace_days < 7:
        raise ValueError('Keep at least two releases and a grace period of at least seven days')
    now = now or datetime.now(timezone.utc)
    releases = github.releases()
    store = store_release(releases)
    if not store:
        raise ValueError('Shared component release does not exist')
    stable = [r for r in releases if not r.get('draft') and not r.get('prerelease') and VERSION.fullmatch(r.get('tag_name', ''))]
    stable.sort(key=lambda r: tuple(map(int, r['tag_name'][1:].split('.'))), reverse=True)
    if len(stable) < keep:
        raise ValueError('Too few stable releases for the requested retention window')
    retained = stable[:keep] + [r for r in releases if r['id'] != store['id'] and (r.get('draft') or r.get('prerelease'))]
    referenced = {}
    for release in retained:
        assets = github.assets(release)
        manifests = [a for a in assets if a['name'].endswith('-components.json')]
        # A published version must have its Linux manifest; a draft may be uploading.
        if release in stable[:keep] and not manifests:
            raise ValueError(f'Missing component manifest for retained release {release["tag_name"]}')
        for asset in manifests:
            manifest = github.download_manifest(release, asset)
            if 'v' + manifest.get('version', '') != release['tag_name']:
                raise ValueError('Retained manifest version does not match its release')
            for name, metadata in blob_references(manifest).items():
                if name in referenced and referenced[name] != metadata:
                    raise ValueError("Conflicting retained blob metadata")
                referenced[name] = metadata
    assets = github.assets(store)
    # Validate references exist before planning deletion; incomplete publication must fail closed.
    inventory = asset_map(assets)
    if referenced.keys() - inventory.keys():
        raise ValueError('Retained release references missing shared components')
    for name, metadata in referenced.items():
        check_asset(inventory[name], *metadata)
    cutoff = now - timedelta(days=grace_days)
    deletions = []
    for asset in assets:
        match = BLOB_PATTERN.fullmatch(asset['name'])
        if not match or asset['name'] in referenced or asset.get('state') != 'uploaded':
            continue
        created = datetime.fromisoformat(asset['created_at'].replace('Z', '+00:00'))
        if created.tzinfo is None:
            raise ValueError('Blob timestamp has no timezone')
        if created < cutoff:
            check_asset(asset, 'sha256:' + match[1], asset['size'])
            deletions.append(asset)
    return sorted(deletions, key=lambda a: a['name']), [r['tag_name'] for r in retained]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    upload = sub.add_parser('publish'); upload.add_argument('directory', type=Path)
    prune = sub.add_parser('prune'); prune.add_argument('--keep', type=int, default=3)
    prune.add_argument('--grace-days', type=int, default=14)
    prune.add_argument('--apply', action='store_true', help='Delete the listed unreferenced blobs; default is dry-run')
    args = parser.parse_args()
    github = GitHub()
    try:
        if args.command == 'publish':
            publish(args.directory, github)
        else:
            plan, retained = prune_plan(github, args.keep, args.grace_days)
            print('Retained manifests:', ', '.join(retained))
            print(f'{"Delete" if args.apply else "Would delete"} {len(plan)} blobs:')
            for asset in plan:
                print(asset['name'])
            if args.apply:
                # Fail if a release or asset changed while the operator's plan was computed.
                fresh, current = prune_plan(github, args.keep, args.grace_days)
                if fresh != plan or current != retained:
                    raise ValueError('Pruning plan changed; retry a dry run')
                for asset in plan:
                    github.delete(asset)
    except (ValueError, KeyError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(1, f'Release components: {error}\n')


if __name__ == '__main__':
    main()
