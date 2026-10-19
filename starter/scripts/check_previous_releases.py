#!/usr/bin/env python3
"""Check installer upgrades from the two latest stable releases, with digest-verified downloads."""
import argparse
from pathlib import Path
import subprocess
import sys
import tarfile
import shutil
import tempfile
from release_blobs import GitHub, VERSION, DOWNLOAD_BASE, blob_name, sha256


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('tarball', type=Path)
    args = parser.parse_args()
    github = GitHub()
    releases = [r for r in github.releases() if not r['draft'] and not r['prerelease']
                and VERSION.fullmatch(r['tag_name'])]
    releases.sort(key=lambda r: tuple(map(int, r['tag_name'][1:].split('.'))), reverse=True)
    if len(releases) < 2:
        raise ValueError('Two stable releases are required for compatibility checks')
    with tempfile.TemporaryDirectory(prefix='agent-pet-previous-') as temporary:
        for release in releases[:2]:
            name = f"agent-pet-{release['tag_name'][1:]}-linux-x86_64.tar.gz"
            assets = [a for a in github.assets(release) if a['name'] == name]
            if len(assets) != 1:
                raise ValueError('Missing or duplicate previous release package')
            asset = assets[0]
            blob_name(asset.get('digest'))
            if (asset.get('state') != 'uploaded' or not 0 < asset['size'] <= 512 * 1024 * 1024
                    or asset['browser_download_url'] != DOWNLOAD_BASE + release['tag_name'] + '/' + name):
                raise ValueError('Invalid previous release asset')
            path = Path(temporary) / name
            with path.open('wb') as output:
                subprocess.run(['gh', 'api', f"repos/WindyWin/vpet-agent-pet/releases/assets/{asset['id']}",
                                '-H', 'Accept: application/octet-stream'], stdout=output, check=True)
            if path.stat().st_size != asset['size'] or sha256(path) != asset['digest']:
                raise ValueError('Previous release checksum mismatch')
            print(f"Checking upgrade from {release['tag_name']}", flush=True)
            subprocess.run([sys.executable, str(Path(__file__).with_name('check_install.py')),
                            str(args.tarball.resolve()), '--previous-tarball', str(path)], check=True)
            extracted = Path(temporary) / "previous"
            with tarfile.open(path) as archive:
                archive.extractall(extracted, filter="data")
            package = next(extracted.iterdir())
            subprocess.run([sys.executable, str(Path(__file__).with_name("check_update.py")),
                            str(package / "bin/agent-pet-updater"), "--full-only"], check=True)
            shutil.rmtree(extracted)
            path.unlink()


if __name__ == '__main__':
    main()
