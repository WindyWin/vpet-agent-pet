#!/usr/bin/env python3
"""Verify or catalog bundled VPet assets; no dependency on the original checkout."""
import argparse
import hashlib
import json
import re
import struct
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / 'assets/vpet/manifest.json'


def verify():
    manifest = json.loads(MANIFEST.read_text())
    errors = []
    expected = {entry['path'] for entry in manifest['files']}
    paths = list((ROOT / 'assets/vpet/pet').rglob('*'))
    actual = {p.relative_to(ROOT).as_posix() for p in paths if p.is_file()}
    for p in paths:
        if p.is_symlink():
            errors.append(f'Symlink: {p.relative_to(ROOT)}')
    for entry in manifest['files']:
        p = ROOT / entry['path']
        if not p.is_file():
            errors.append(f'Missing: {entry["path"]}')
            continue
        if p.stat().st_size != entry['bytes'] or hashlib.sha256(p.read_bytes()).hexdigest() != entry['sha256']:
            errors.append(f'Changed: {entry["path"]}')
    errors.extend(f'Unlisted: {p}' for p in sorted(actual - expected))
    if len(expected) != len(manifest['files']) or len(expected) != manifest['file_count']:
        errors.append('Manifest file count or duplicate paths')
    if sum(entry['bytes'] for entry in manifest['files']) != manifest['total_bytes']:
        errors.append('Manifest byte total mismatch')
    if errors:
        raise SystemExit('\n'.join(errors))
    print(f'Verified {len(expected):,} files; {manifest["total_bytes"]:,} bytes; no symlinks.')


def catalog():
    pet = ROOT / 'assets/vpet/pet/vup'
    sequences = defaultdict(list)
    for frame in sorted(pet.rglob('*.png')):
        match = re.search(r'_(\d+)$', frame.stem)
        with frame.open('rb') as stream:
            header = stream.read(24)
        width, height = struct.unpack('>II', header[16:24])
        sequences[frame.parent.relative_to(pet).as_posix()].append({
            'file': frame.name,
            'duration_ms': int(match[1]) if match else None,
            'width': width, 'height': height,
        })
    categories = defaultdict(lambda: [0, 0])
    result = []
    for path, frames in sorted(sequences.items()):
        categories[path.split('/')[0]][0] += len(frames)
        categories[path.split('/')[0]][1] += 1
        valid = all(f['duration_ms'] is not None and f['duration_ms'] > 0 for f in frames)
        result.append({'path': path, 'timing_valid': valid,
                       'duration_ms': sum(f['duration_ms'] for f in frames) if valid else None,
                       'frames': frames})
    (ROOT / 'assets/vpet/animations.json').write_text(json.dumps({
        'schema_version': 1, 'pet_root': 'assets/vpet/pet/vup', 'sequences': result
    }, ensure_ascii=False, indent=2) + '\n')
    lines = ['# Bundled VPet asset catalog', '',
             'Paths below are relative to `assets/vpet/pet/vup/` in this project.', '',
             f'{sum(len(frames) for frames in sequences.values()):,} PNG frames, {len(sequences)} sequences, {len(categories)} categories.', '',
             'See `assets/vpet/animations.json` for individual frame names, dimensions, and durations.', '',
             '## Categories', '', '| Category | Frames | Sequences |', '| --- | ---: | ---: |']
    for name, counts in sorted(categories.items()):
        lines.append(f'| {name} | {counts[0]} | {counts[1]} |')
    lines += ['', '## All sequences', '', '| Sequence | Frames | Duration (ms) |', '| --- | ---: | ---: |']
    for sequence in result:
        duration = sequence['duration_ms'] if sequence['timing_valid'] else 'Missing timing'
        lines.append(f'| `{sequence["path"]}` | {len(sequence["frames"])} | {duration} |')
    (ROOT / 'docs').mkdir(exist_ok=True)
    (ROOT / 'docs/ASSETS.md').write_text('\n'.join(lines) + '\n')
    print(f'Cataloged {len(sequences)} sequences; wrote docs/ASSETS.md and assets/vpet/animations.json.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=['verify', 'catalog'])
    args = parser.parse_args()
    {'verify': verify, 'catalog': catalog}[args.command]()
