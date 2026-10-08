#!/usr/bin/env python3
"""Collect the packs of pets that are not bundled, named by content, for the long-lived `pets` release.

    python3 scripts/pet_blobs.py build dist/pet-blobs

Reads every pets/<id>/packs.json in the build folder. A pack that artwork-packs.json does not list belongs to a
pet that downloads on demand (AGENT_PET_BUNDLED_PETS in CMakeLists.txt); it is copied to <out>/<sha256>.rcc after
checking its size and SHA-256 against the tree. <out>/blobs.txt lists the copied file names, one per line (empty
when every pet is bundled). The release workflow uploads the ones the `pets` release does not have yet; content
never changes under a name, so nothing is uploaded twice. docs/pets.md describes the hash tree.
"""
import argparse
import hashlib
import json
import shutil
import sys
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('build', type=Path, help='the CMake build folder')
    parser.add_argument('out', type=Path, help='a new folder for the blobs')
    args = parser.parse_args()
    bundled = set(json.loads((args.build / 'artwork-packs.json').read_text()))
    if args.out.exists():
        raise SystemExit(f'Already exists: {args.out}')
    args.out.mkdir(parents=True)
    names = []
    for tree_path in sorted(args.build.glob('pets/*/packs.json')):
        tree = json.loads(tree_path.read_text())
        pet = tree_path.parent.name
        on_demand = 0
        for pack in tree['packs']:
            if f'{pack["name"]}.rcc' in bundled:
                continue
            on_demand += 1
            data = (args.build / f'{pack["name"]}.rcc').read_bytes()
            if len(data) != pack['bytes'] or hashlib.sha256(data).hexdigest() != pack['sha256']:
                raise SystemExit(f'{pet}: {pack["name"]}.rcc does not match its tree; rebuild')
            name = f'{pack["sha256"]}.rcc'
            if name not in names:
                shutil.copyfile(args.build / f'{pack["name"]}.rcc', args.out / name)
                names.append(name)
        print(f'{pet}: {on_demand} on-demand of {len(tree["packs"])} packs')
    (args.out / 'blobs.txt').write_text(''.join(f'{name}\n' for name in names))
    print(f'{len(names)} on-demand pack(s) in {args.out}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
