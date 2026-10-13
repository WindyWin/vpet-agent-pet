#!/usr/bin/env python3
"""Flatten VPet's eating/drinking layers into ordinary reminder animations.

Requires ImageMagick 7 (magick); the app needs no new renderer or dependency.
Source: ../assets/vpet/pet/vup, including info.lps. Rebuild with:
    python3 scripts/compose_vpet_reminders.py

The sandwich and water glass below are original SVG drawings under the app's
Apache-2.0 license. The character layers retain the VPet artwork terms.
The generated sequences, frame timings and hashes are updated in the catalog
and manifest. Source layer sequences stay in available-animations.json because
the application bundles only their flattened derivatives.
"""
import argparse
import hashlib
import json
import re
import shutil
import struct
import subprocess
import tempfile
from pathlib import Path

from add_sequences import frames_of

ROOT = Path(__file__).resolve().parents[1]
SVG = {
    'eat': '''<svg xmlns="http://www.w3.org/2000/svg" width="100" height="100" viewBox="0 0 100 100">
      <g stroke="#583f37" stroke-width="3" stroke-linejoin="round">
        <path fill="#bd793f" d="M10 31 Q11 16 30 19 L78 33 Q93 38 89 48 L83 73 Q79 83 66 79 L17 61 Q7 55 10 45Z"/>
        <path fill="#fff0c6" d="M15 32 Q17 22 29 25 L76 39 Q86 42 84 49 L79 69 Q76 76 67 73 L22 57 Q13 53 15 46Z"/>
        <path fill="#7bbd60" d="M12 48 L26 43 34 51 47 48 56 56 71 53 86 59 81 67 69 64 56 69 42 60 27 63 12 56Z"/>
        <path fill="#f8cb65" d="M12 58 L84 65 78 75 57 76 50 83 20 70Z"/>
        <path fill="#e7ac67" d="M11 63 L78 75 76 84 65 88 18 76 Q8 73 11 63Z"/>
      </g></svg>''',
    'drink': '''<svg xmlns="http://www.w3.org/2000/svg" width="100" height="100" viewBox="0 0 100 100">
      <path fill="#edfaff" stroke="#49687d" stroke-width="3" d="M25 18 L77 18 71 82 Q50 92 31 82Z"/>
      <path fill="#66bfdf" d="M30 42 Q43 36 53 42 T73 42 L67 79 Q49 85 35 79Z"/>
      <ellipse cx="51" cy="18" rx="26" ry="6" fill="#d3f1ff" stroke="#49687d" stroke-width="3"/>
      <path d="M35 30 L39 71" fill="none" stroke="#ffffff" stroke-width="5" stroke-linecap="round"/>
    </svg>''',
}


def run(*args):
    subprocess.run(['magick', *map(str, args)], check=True, capture_output=True)


def timeline(directory):
    result, at = [], 0
    for _, duration, frame in frames_of(directory):
        result.append((at, frame))
        at += duration
    return result, at


def at_frame(frames, at):
    return next(frame for start, frame in reversed(frames) if start <= at)


def keyframes(metadata, action):
    line = next(line for line in metadata.read_text().splitlines()
                if line.startswith(f'FoodAnimation#{action}:') and 'mode#nomal:' in line.lower())
    result, at = [], 0
    pose = [0, 0, 60, 0, 0]
    for _, values in re.findall(r'a(\d+)#([^:]+):', line):
        fields = [float(value) for value in values.split(',')]
        at += int(fields[0])
        if len(fields) > 1:
            # Position/size remain at the previous pose when omitted; rotation
            # and opacity default to upright/visible at each explicit pose.
            pose = pose.copy()
            pose[3:] = [0, 1]
            pose[:len(fields) - 1] = fields[1:]
        result.append((at, pose.copy()))
    result.insert(0, (0, [*result[0][1][:4], 0]))
    return result


def pose_at(keys, at):
    for (start, a), (end, b) in zip(keys, keys[1:]):
        if at <= end:
            progress = max(0, min(1, (at - start) / (end - start)))
            return [x + (y - x) * progress for x, y in zip(a, b)]
    return keys[-1][1]


def compose(source, destination, action, temporary):
    back, duration = timeline(source / ('Eat/Nomal/back_lay' if action == 'eat' else 'Drink/Nomal/back_lay'))
    front, front_duration = timeline(source / ('Eat/Nomal/front_lay' if action == 'eat' else 'Drink/front_lay'))
    if front_duration != duration:
        raise ValueError(f'{action}: front/back durations disagree')
    metadata = source / ('Eat/Nomal/info.lps' if action == 'eat' else 'Drink/info.lps')
    keys = keyframes(metadata, action)
    # info.lps uses the pet's 500-unit space; the PNG canvases are 1000 px.
    scale = struct.unpack('>I', back[0][1].read_bytes()[16:20])[0] / 500
    item = temporary / f'{action}.svg'
    item.write_text(SVG[action])
    # Include every character boundary and interpolate the item at 20 fps.
    cuts = sorted({0, duration, *range(0, duration, 50), *(at for at, _ in back), *(at for at, _ in front),
                   *(at for at, _ in keys if at < duration)})
    destination.mkdir(parents=True, exist_ok=True)
    result = []
    previous_digest = None
    for start, end in zip(cuts, cuts[1:]):
        x, y, width, angle, opacity = pose_at(keys, start)
        x, y, width = x * scale, y * scale, width * scale
        # Fade the item away as the last character frames return to idle.
        opacity *= min(1, max(0, (duration - start) / 250))
        prop = temporary / 'prop.png'
        run('-background', 'none', item, '-resize', f'{max(1, round(width))}x', '-rotate', angle,
            '-channel', 'A', '-evaluate', 'multiply', max(0, min(1, opacity)), '+channel', prop)
        # Rotation grows the prop's canvas; preserve its centre at the keyframed position.
        dimensions = subprocess.run(['magick', 'identify', '-format', '%w %h', str(prop)],
                                    check=True, capture_output=True, text=True).stdout.split()
        px, py = round(x - (int(dimensions[0]) - width) / 2), round(y - (int(dimensions[1]) - width) / 2)
        frame = temporary / 'frame.png'
        run(at_frame(back, start), prop, '-geometry', f'{px:+d}{py:+d}', '-compose', 'over', '-composite',
            at_frame(front, start), '-geometry', '+0+0', '-compose', 'over', '-composite', '-strip', frame)
        data = frame.read_bytes()
        digest = hashlib.sha256(data).hexdigest()
        if digest == previous_digest:
            result[-1][1] += end - start
        else:
            result.append([data, end - start])
        previous_digest = digest
    written = []
    for index, (data, duration) in enumerate(result):
        path = destination / f'_{index:03}_{duration}.png'
        path.write_bytes(data)
        written.append((path, duration))
    return written


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=ROOT.parent / 'assets/vpet/pet/vup')
    parser.add_argument('--assets', type=Path, default=ROOT / 'assets/vpet')
    args = parser.parse_args()
    if not shutil.which('magick'):
        raise SystemExit('ImageMagick 7 (magick) is required to regenerate the frames.')
    catalog_path, manifest_path = args.assets / 'animations.json', args.assets / 'manifest.json'
    catalog, manifest = json.loads(catalog_path.read_text()), json.loads(manifest_path.read_text())
    with tempfile.TemporaryDirectory(prefix='vpet-reminders-') as scratch:
        for action in SVG:
            sequence = f'Reminders/{action}'
            relative = f'assets/vpet/vup/{sequence}'
            target = args.assets / 'vup' / sequence
            if target.exists():
                for old in target.glob('_*.png'):
                    old.unlink()
            files = compose(args.source, target, action, Path(scratch))
            entry = {'state': f'reminder_{action}', 'path': sequence, 'frames': [], 'duration_ms': 0}
            manifest['files'] = [f for f in manifest['files'] if not f['path'].startswith(relative + '/')]
            for path, duration in files:
                data = path.read_bytes()
                name = relative + '/' + path.name
                entry['frames'].append({'path': name, 'duration_ms': duration})
                entry['duration_ms'] += duration
                manifest['files'].append({'path': name, 'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()})
            catalog['sequences'] = [s for s in catalog['sequences'] if s['path'] != sequence] + [entry]
            catalog['states'][f'reminder_{action}'] = [sequence]
            catalog['playback'][f'reminder_{action}'] = {'mode': 'once', 'after': 'idle'}
            catalog['cues']['lunch' if action == 'eat' else 'water'] = [{'state': f'reminder_{action}', 'weight': 1}]
            print(f'{sequence}: {len(files)} frames, {entry["duration_ms"]} ms')
    manifest['file_count'] = len(manifest['files'])
    manifest['total_bytes'] = sum(f['bytes'] for f in manifest['files'])
    for path, document in [(catalog_path, catalog), (manifest_path, manifest)]:
        path.write_text(json.dumps(document, indent=2) + '\n')


if __name__ == '__main__':
    main()
