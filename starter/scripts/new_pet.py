#!/usr/bin/env python3
"""Start a new pet folder that plays one idle sequence for every state the app needs.

    python3 scripts/new_pet.py cat --name "Cat" --author "Jane Doe" --terms CAT-ARTWORK-TERMS.md --idle ~/art/cat/idle

The idle folder holds PNG frames named with their index and duration in milliseconds, like VPet's
(_000_125.png). They are copied to assets/<id>/idle/, and pet.json, manifest.json and animations.json
are written. Every state in src/animation/core-states.json plays that idle sequence with the playback
the app expects, so the pet runs at once. The Settings tile is the first idle frame unless --preview
names another PNG; it must be at most 512 x 512 pixels. The art's terms must already be in licenses/.
Then add sequences with scripts/add_sequences.py --pet <id>, map them in animations.json and run
scripts/verify_assets.py; docs/pets.md walks through it.
"""
import argparse
import hashlib
import json
import re
import shutil
from pathlib import Path

from add_sequences import frames_of
from verify_assets import png_size

ROOT = Path(__file__).resolve().parents[1]
CONTRACT = ROOT / 'src/animation/core-states.json'
FILE_NAME = re.compile(r'[A-Za-z0-9_-][A-Za-z0-9._-]*')


def write_json(path, document):
    path.write_text(json.dumps(document, indent=2) + '\n', encoding='utf-8')


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('id', help='folder name and identifier: lowercase letters, digits and "-", at most 32')
    parser.add_argument('--name', required=True, help='name shown on the Settings tile, at most 64 characters')
    parser.add_argument('--author', required=True, help='artwork credit shown in About, at most 128 characters')
    parser.add_argument('--url', help='https:// page About links with the credit (optional)')
    parser.add_argument('--terms', required=True, help="file name of the art's terms in licenses/")
    parser.add_argument('--idle', required=True, type=Path, help='folder holding the idle sequence frames')
    parser.add_argument('--preview', type=Path, help='PNG tile for Settings (default: the first idle frame)')
    parser.add_argument('--assets', type=Path, default=ROOT / 'assets', help='folder holding the pet folders')
    parser.add_argument('--licenses', type=Path, default=ROOT / 'licenses', help="folder holding the pets' terms")
    args = parser.parse_args()

    if not re.fullmatch(r'[a-z0-9-]{1,32}', args.id):
        raise SystemExit(f'Invalid id: {args.id} (lowercase letters, digits and "-", at most 32)')
    if not 1 <= len(args.name) <= 64 or not 1 <= len(args.author) <= 128:
        raise SystemExit('--name needs 1-64 characters and --author 1-128')
    if args.url is not None and not (args.url.startswith('https://') and len(args.url) <= 256):
        raise SystemExit('--url must be an https:// address of at most 256 characters')
    if not FILE_NAME.fullmatch(args.terms) or not (args.licenses / args.terms).is_file():
        raise SystemExit(f"Put the art's terms in {args.licenses / args.terms} first")
    folder = args.assets / args.id
    if folder.exists():
        raise SystemExit(f'Already exists: {folder}')
    frames = frames_of(args.idle)
    preview = args.preview or frames[0][2]
    size = png_size(preview)
    if size is None or size[0] > 512 or size[1] > 512:
        described = 'not a PNG' if size is None else f'{size[0]} x {size[1]} pixels'
        raise SystemExit(f'The preview {preview} is {described}; pass --preview with a PNG of at most 512 x 512 pixels')

    (folder / 'idle').mkdir(parents=True)
    files, sequence = [], {'state': 'idle', 'path': 'idle', 'frames': [], 'duration_ms': 0}
    for _, duration, file in frames:
        shutil.copyfile(file, folder / 'idle' / file.name)
        data = (folder / 'idle' / file.name).read_bytes()
        relative = f'assets/{args.id}/idle/{file.name}'
        files.append({'path': relative, 'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()})
        sequence['frames'].append({'path': relative, 'duration_ms': duration})
        sequence['duration_ms'] += duration
    shutil.copyfile(preview, folder / 'preview.png')

    pet = {'schema_version': 1, 'id': args.id, 'name': args.name, 'author': args.author}
    if args.url:
        pet['url'] = args.url
    pet.update({'terms': args.terms, 'preview': 'preview.png'})
    states, playback = {}, {}
    for state, shape in json.loads(CONTRACT.read_text(encoding='utf-8'))['states'].items():
        states[state] = ['idle'] * (3 if shape['mode'] == 'phased' else 1)
        playback[state] = {'mode': shape['mode'], 'after': shape['after']}
    write_json(folder / 'pet.json', pet)
    write_json(folder / 'manifest.json', {'schema_version': 1, 'file_count': len(files),
                                           'total_bytes': sum(f['bytes'] for f in files), 'files': files})
    write_json(folder / 'animations.json', {'schema_version': 1, 'asset_root': f'assets/{args.id}', 'states': states,
                                             'sequences': [sequence], 'playback': playback})
    print(f'Created {folder}: its {len(frames)} idle frames play every core state. '
          f'Add sequences with scripts/add_sequences.py --pet {args.id}, then run scripts/verify_assets.py.')


if __name__ == '__main__':
    main()
