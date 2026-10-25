#!/usr/bin/env python3
"""Bundle a thinking performance from upstream VPet frames, preserving their timing and artwork.

Usage: python3 scripts/compose_long_thinking.py --source /path/to/vup
Composed entry, tail loop and tool transitions share source frames with their original paths and license.
"""
import argparse
import hashlib
import shutil
from pathlib import Path

from add_sequences import frames_of, load, save

ROOT = Path(__file__).resolve().parents[1]
PARTS = [
    'Think/Nomal/C',
    'Say/Self/A', 'Say/Self/B_1', 'Say/Self/B_2', 'Say/Self/C',
    'WORK/WorkONE/A_Nomal', 'WORK/WorkONE/B/Nomal/2',
]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    args = parser.parse_args()
    folder = ROOT / 'assets/vpet'
    catalog = load(folder, 'animations.json')
    manifest = load(folder, 'manifest.json')
    available = load(folder, 'available-animations.json')
    bundled = {s['path']: s for s in catalog['sequences']}
    performances = {'Think/Extended/cycle': PARTS, 'Think/Extended/desk_loop': ['WORK/WorkONE/B/Nomal/2']}
    resume = {'working': 'WORK/Desk/ponder_in', 'reading': 'WORK/Desk/reading_to_working'}
    for state in ['editing', 'executing', 'searching', 'browsing', 'delegating']:
        name = f'Think/Extended/from_{state}'
        performances[name] = [catalog['states'][state][2], 'WORK/WorkONE/A_Nomal', 'WORK/WorkONE/B/Nomal/2']
        resume[state] = name
    missing = list(dict.fromkeys(part for part in PARTS if part not in bundled))
    # Validate all source folders before writing anything.
    source = {part: frames_of(args.source / part) for part in missing}
    files = {f['path']: f for f in manifest['files']}
    imported = {}
    for part in missing:
        imported[part] = []
        for _, duration, frame in source[part]:
            relative = f'assets/vpet/vup/{part}/{frame.name}'
            target = ROOT / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(frame, target)
            data = target.read_bytes()
            files[relative] = {'path': relative, 'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()}
            imported[part].append({'path': relative, 'duration_ms': duration})
    catalog['sequences'] = [s for s in catalog['sequences'] if s['path'] not in performances]
    for name, parts in performances.items():
        frames = [frame for part in parts for frame in
                  (bundled[part]['frames'] if part in bundled else imported[part])]
        catalog['sequences'].append({'state': 'thinking', 'path': name, 'frames': frames,
                                    'duration_ms': sum(f['duration_ms'] for f in frames)})
        print(f'{name}: {len(frames)} frames, {sum(f["duration_ms"] for f in frames)} ms')
    catalog['activity']['thinking']['sustained'] = {
        'after_ms': 3000, 'sequence': 'Think/Extended/cycle',
        'loop': 'Think/Extended/desk_loop', 'resume': resume,
    }
    manifest['files'] = list(files.values())
    manifest['file_count'] = len(files)
    manifest['total_bytes'] = sum(f['bytes'] for f in files.values())
    available['sequences'] = [s for s in available['sequences'] if s['path'] not in missing]
    available['missing_sequence_count'] = len(available['sequences'])
    available['missing_frame_count'] = sum(s['frame_count'] for s in available['sequences'])
    available['missing_size_bytes'] = sum(s['size_bytes'] for s in available['sequences'])
    from compose_desk_continuity import compose
    save(folder, 'animations.json', compose(catalog))
    save(folder, 'manifest.json', manifest)
    save(folder, 'available-animations.json', available)


if __name__ == '__main__':
    main()
