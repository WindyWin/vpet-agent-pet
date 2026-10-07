#!/usr/bin/env python3
"""Copy frame sequences into a pet's folder.

    python3 scripts/add_sequences.py IDEL/yawning/Nomal Default/Nomal/2      # VPet, from the full archive
    python3 scripts/add_sequences.py --source /path/to/vup IDEL/Meow/Nomal/1
    python3 scripts/add_sequences.py --pet cat --source ~/art/cat sit/start sit/loop

A sequence is a folder of PNG frames named with their index and duration in milliseconds
(_000_125.png). Each is copied frame by frame under the catalog's asset_root, recorded in the
pet's manifest.json with its size and SHA-256, and added to the `sequences` list of its
animations.json with the durations parsed from its filenames. VPet's sequences must be listed in
available-animations.json, which they then leave. Mapping a new sequence to a state, variant or
fidget is a separate, hand-made edit of animations.json: add it to `states` or `variants` and set
its `state` field. verify_assets.py rejects the pet until every bundled sequence is used and names
its state. Keep the artwork notices. docs/pets.md covers pets other than VPet.
"""
import argparse
import hashlib
import json
import re
import shutil
from pathlib import Path

from verify_assets import UNSAFE_TEXT, unsafe_frame_name

ROOT = Path(__file__).resolve().parents[1]
FRAME = re.compile(r'(?:^|_)(\d+)_(\d+)\.png$')  # WORK/Study/B_4_Nomal names its frame 000_1250.png
SEQUENCE = re.compile(r'[A-Za-z0-9_-][A-Za-z0-9._-]*(?:/[A-Za-z0-9_-][A-Za-z0-9._-]*)*')


def load(folder, name):
    return json.loads((folder / name).read_text(encoding='utf-8'))


def save(folder, name, document):
    (folder / name).write_text(json.dumps(document, indent=2) + '\n', encoding='utf-8')


def frames_of(directory):
    """Frames of one sequence folder as (index, duration_ms, path), ordered by index.

    A few folders join two numbered runs, such as FLA_000.. then FLB_000.. in MOVE/fall.*/C_*;
    when indexes repeat, frames are ordered by the name before the index, then by index.
    WORK/Study/B_3_Nomal stores one picture twice under the same index with two durations; a
    repeated index is accepted only for byte-identical files, which then play in name order."""
    if not directory.is_dir():
        raise SystemExit(f'Source folder not found: {directory}')
    found = []
    for file in directory.iterdir():
        match = FRAME.search(file.name)
        if file.is_symlink() or not file.is_file() or file.suffix != '.png' or not match:
            raise SystemExit(f'Unexpected entry in {directory}: {file.name!r}')
        if unsafe_frame_name(file.name):
            raise SystemExit(f'Frames are PNG files without {UNSAFE_TEXT} in their names: {file.name!r} in {directory}')
        found.append((file.name[:match.start()], int(match[1]), int(match[2]), file))
    pictures = {}
    for prefix, index, _, file in found:
        pictures.setdefault((prefix, index), set()).add(file.read_bytes())
    if not found or any(len(same) != 1 for same in pictures.values()):
        raise SystemExit(f'No frames or duplicate frame indexes in {directory}')
    by_run = len({index for _, index, _, _ in found}) != len(pictures)
    found.sort(key=lambda frame: (frame[0], frame[1], frame[3].name) if by_run else (frame[1], frame[3].name))
    return [(index, duration, file) for _, index, duration, file in found]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('sequences', nargs='+', help='sequence folder under the source, e.g. IDEL/yawning/Nomal')
    parser.add_argument('--pet', default='vpet', help='pet folder to add to (default: vpet)')
    parser.add_argument('--assets', type=Path, default=ROOT / 'assets', help='folder holding the pet folders')
    parser.add_argument('--source', type=Path,
                        help="folder holding the sequences (VPet's default: the full archive next to starter/)")
    args = parser.parse_args()

    folder = args.assets / args.pet
    vpet = args.pet == 'vpet'
    if not (folder / 'pet.json').is_file():
        raise SystemExit(f'Not a pet folder: {folder} (start one with scripts/new_pet.py)')
    source = args.source or (ROOT.parent / 'assets/vpet/pet/vup' if vpet else None)
    if source is None:
        raise SystemExit('--source is required for pets other than VPet')
    manifest, animations = load(folder, 'manifest.json'), load(folder, 'animations.json')
    available = load(folder, 'available-animations.json') if vpet else None
    listed = {s['path']: s for s in available['sequences']} if vpet else {}
    bundled = {s['path'] for s in animations['sequences']}
    asset_root = animations.get('asset_root', f'assets/{args.pet}')
    requested = []
    for path in args.sequences:
        if not SEQUENCE.fullmatch(path):
            raise SystemExit(f'Sequence folders use A-Z, a-z, 0-9, ".", "_" and "-": {path}')
        if path in bundled:
            raise SystemExit(f'Already bundled: {path}')
        if vpet and path not in listed:
            raise SystemExit(f'Not in available-animations.json: {path}')
        if vpet and not listed[path]['timing_valid']:
            raise SystemExit(f'Frames without durations cannot play: {path}')
        if path in requested:
            raise SystemExit(f'Listed twice: {path}')
        requested.append(path)

    # Read and validate every sequence's frames before copying any, so a refusal leaves nothing behind.
    found = {path: frames_of(source / path) for path in requested}
    for path in requested:
        if vpet and (len(found[path]) != listed[path]['frame_count']
                     or sum(duration for _, duration, _ in found[path]) != listed[path]['total_duration_ms']):
            raise SystemExit(f'{path} does not match its available-animations.json entry')
    for path in requested:
        frames = found[path]
        entry = {'path': path, 'frames': [], 'duration_ms': 0}
        target = args.assets / asset_root.removeprefix('assets/') / path
        target.mkdir(parents=True, exist_ok=True)
        size = 0
        for _, duration, file in frames:
            shutil.copyfile(file, target / file.name)
            data = (target / file.name).read_bytes()
            relative = f'{asset_root}/{path}/{file.name}'
            manifest['files'].append({'path': relative, 'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()})
            entry['frames'].append({'path': relative, 'duration_ms': duration})
            entry['duration_ms'] += duration
            size += len(data)
        animations['sequences'].append(entry)
        print(f'{path}: {len(frames)} frames, {entry["duration_ms"]} ms, {size / 1e6:.1f} MB')

    if vpet:
        available['sequences'] = [s for s in available['sequences'] if s['path'] not in requested]
        available['missing_sequence_count'] = len(available['sequences'])
        available['missing_frame_count'] = sum(s['frame_count'] for s in available['sequences'])
        available['missing_size_bytes'] = sum(s['size_bytes'] for s in available['sequences'])
        save(folder, 'available-animations.json', available)
    manifest['file_count'] = len(manifest['files'])
    manifest['total_bytes'] = sum(f['bytes'] for f in manifest['files'])
    save(folder, 'manifest.json', manifest)
    save(folder, 'animations.json', animations)
    print(f'{args.pet}: {manifest["file_count"]} files, {manifest["total_bytes"]:,} bytes. '
          'Now map the new sequences in animations.json and run scripts/verify_assets.py.')


if __name__ == '__main__':
    main()
