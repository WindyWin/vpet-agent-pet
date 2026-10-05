#!/usr/bin/env python3
"""Copy sequences from the full VPet archive into the bundled pack.

    python3 scripts/add_sequences.py IDEL/yawning/Nomal Default/Nomal/2
    python3 scripts/add_sequences.py --source /path/to/vup IDEL/Meow/Nomal/1

Each sequence is copied frame by frame, recorded in manifest.json with its size and
SHA-256, added to the `sequences` list of animations.json with the durations parsed from
its filenames, and removed from available-animations.json. Mapping a new sequence to a
state, variant or fidget is a separate, hand-made edit of animations.json: add it to
`states` or `variants` and set its `state` field. verify_assets.py rejects the pack until
every bundled sequence is used and names its state. Keep the artwork notices.
"""
import argparse
import hashlib
import json
import re
import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / 'assets/vpet'
FRAME = re.compile(r'_(\d+)_(\d+)\.png$')


def load(name):
    return json.loads((ASSETS / name).read_text())


def save(name, document):
    (ASSETS / name).write_text(json.dumps(document, indent=2) + '\n')


def frames_of(directory):
    """Frames of one sequence folder as (index, duration_ms, path), ordered by index.

    A few folders join two numbered runs, such as FLA_000.. then FLB_000.. in MOVE/fall.*/C_*;
    when indexes repeat, frames are ordered by the name before the index, then by index."""
    if not directory.is_dir():
        raise SystemExit(f'Source folder not found: {directory} (point --source at the original VPet sequences)')
    found = []
    for file in directory.iterdir():
        match = FRAME.search(file.name)
        if file.is_symlink() or not file.is_file() or file.suffix != '.png' or not match:
            raise SystemExit(f'Unexpected entry in {directory}: {file.name}')
        found.append((file.name[:match.start()], int(match[1]), int(match[2]), file))
    keys = {(prefix, index) for prefix, index, _, _ in found}
    if not found or len(keys) != len(found):
        raise SystemExit(f'No frames or duplicate frame indexes in {directory}')
    by_run = len({index for _, index, _, _ in found}) != len(found)
    found.sort(key=lambda frame: (frame[0], frame[1]) if by_run else frame[1])
    return [(index, duration, file) for _, index, duration, file in found]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('sequences', nargs='+', help='sequence path, e.g. IDEL/yawning/Nomal')
    parser.add_argument('--source', type=Path, default=ROOT.parent / 'assets/vpet/pet/vup',
                        help='folder holding the original sequences (default: the full archive next to starter/)')
    args = parser.parse_args()

    manifest, animations, available = load('manifest.json'), load('animations.json'), load('available-animations.json')
    listed = {s['path']: s for s in available['sequences']}
    bundled = {s['path'] for s in animations['sequences']}
    requested = []
    for path in args.sequences:
        if path in bundled:
            raise SystemExit(f'Already bundled: {path}')
        if path not in listed:
            raise SystemExit(f'Not in available-animations.json: {path}')
        if not listed[path]['timing_valid']:
            raise SystemExit(f'Frames without durations cannot play: {path}')
        if path in requested:
            raise SystemExit(f'Listed twice: {path}')
        requested.append(path)

    for path in requested:
        source = args.source / path
        frames = frames_of(source)
        entry = {'path': path, 'frames': [], 'duration_ms': 0}
        target = ASSETS / 'vup' / path
        target.mkdir(parents=True, exist_ok=True)
        for _, duration, file in frames:
            shutil.copyfile(file, target / file.name)
            data = (target / file.name).read_bytes()
            relative = f'assets/vpet/vup/{path}/{file.name}'
            manifest['files'].append({'path': relative, 'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()})
            entry['frames'].append({'path': relative, 'duration_ms': duration})
            entry['duration_ms'] += duration
        if len(frames) != listed[path]['frame_count'] or entry['duration_ms'] != listed[path]['total_duration_ms']:
            raise SystemExit(f'{path} does not match its available-animations.json entry')
        animations['sequences'].append(entry)
        print(f'{path}: {len(frames)} frames, {entry["duration_ms"]} ms, {listed[path]["size_bytes"] / 1e6:.1f} MB')

    available['sequences'] = [s for s in available['sequences'] if s['path'] not in requested]
    available['missing_sequence_count'] = len(available['sequences'])
    available['missing_frame_count'] = sum(s['frame_count'] for s in available['sequences'])
    available['missing_size_bytes'] = sum(s['size_bytes'] for s in available['sequences'])
    manifest['file_count'] = len(manifest['files'])
    manifest['total_bytes'] = sum(f['bytes'] for f in manifest['files'])
    save('manifest.json', manifest)
    save('animations.json', animations)
    save('available-animations.json', available)
    print(f'Bundled pack: {manifest["file_count"]} files, {manifest["total_bytes"]:,} bytes. '
          'Now map the new sequences in animations.json and run scripts/verify_assets.py.')


if __name__ == '__main__':
    main()
