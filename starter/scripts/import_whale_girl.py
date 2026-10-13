#!/usr/bin/env python3
"""Import the local Whale Girl PNG release. Requires Pillow.

Run from any directory: python3 scripts/import_whale_girl.py [--source PATH]
The existing pet is backed up outside assets before replacement. The source is never modified.
"""
import argparse
import hashlib
import json
import shutil
import tempfile
from pathlib import Path

from PIL import Image

from verify_assets import check_catalog, check_files, check_pet

ROOT = Path(__file__).resolve().parents[1]
SIZE, GROUND, FRAME_MS = 600, 580, 40  # Upstream ANIM_INTERVAL: 25 fps.
ACTIONS = {
    '正面': 'front', '侧面': 'side', '背面': 'back', '转向': 'turn',
    '哭泣': 'cry', '坐下': 'sit', '坐地上生气': 'angry', '起身': 'rise',
    '害羞': 'shy', '开心': 'happy', '思考': 'think', '惊讶': 'surprise',
    '站着睡着': 'sleep', '跳舞': 'dance', '跳跃': 'jump', '打滚': 'roll',
}
# These are complete performances, not authored start/loop/end animations. Hold a meaningful
# middle pose rather than replaying the performance while a session stays busy or needs attention.
HOLD_FRAMES = {'think': 75, 'sleep': 65, 'shy': 45, 'surprise': 45}
ERROR_NEUTRAL_FRAMES = 16  # 640 ms of neutral lead-in; the crying gesture fits the 4 s error window.


def write_json(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


def make_catalog(frame_sets):
    states, playback, sequences = {}, {}, []

    def state(name, paths, mode='once', after='idle', **policy):
        states[name] = paths
        playback[name] = {'mode': mode, 'after': after, **policy}

    def phases(action):
        return [f'{action}/{part}' for part in ('start', 'hold', 'end')]

    state('idle', ['seat/idle'], 'loop')
    state('stand', ['front'])
    for name in ('think', 'read', 'work'):
        state(name, phases('think'), 'phased')
    state('alert', phases('surprise'), 'phased')
    state('oops', ['cry/reaction'], after='previous')
    state('cry_full', ['cry'])  # Retain the complete source performance for the animation preview.
    state('drained', ['seat/hold'], 'loop')
    state('cheer', ['happy'])
    state('greet', ['happy'])
    state('pout', ['angry'], after='stop')
    state('bye', ['back'], after='stop')
    state('nap', phases('sleep'), 'phased')
    state('held', phases('shy'), 'phased')
    state('startle', ['surprise'])
    state('shy', ['shy'])
    state('jump', ['jump'])
    for action in ('dance', 'roll', 'turn', 'back', 'side'):
        state(action, [action])
    state('rest', ['sit', 'seat/hold', 'rise'], 'phased', loops=1)

    def sequence(name, frames):
        owner = next(state for state, used in states.items() if name in used)
        sequences.append({'state': owner, 'path': name, 'frames': frames,
                          'duration_ms': sum(frame['duration_ms'] for frame in frames)})

    for action, frames in frame_sets.items():
        if action in HOLD_FRAMES:
            hold = HOLD_FRAMES[action]
            sequence(f'{action}/start', frames[:hold + 1])
            sequence(f'{action}/hold', [{**frames[hold], 'duration_ms': 1000}])
            sequence(f'{action}/end', frames[hold + 1:])
        if any(action in paths for paths in states.values()):
            sequence(action, frames)
    sequence('cry/reaction', frame_sets['cry'][ERROR_NEUTRAL_FRAMES:])
    sequence('seat/hold', [{**frame_sets['sit'][-1], 'duration_ms': 1000}])
    # Only the settled seated tail of the clip belongs in idle. Play it forward and backward,
    # pausing at each end so the loop stays gentle and never repeats the sit-down gesture.
    seated = frame_sets['sit'][-16:]
    idle = [dict(frame) for frame in seated + seated[-2:0:-1]]
    idle[0]['duration_ms'] = idle[len(seated) - 1]['duration_ms'] = 800
    sequence('seat/idle', idle)
    cues = {'thinking': 'think', 'reading': 'read', 'working': 'work', 'attention': 'alert',
            'exhausted': 'drained', 'error': 'oops', 'turn-finished': 'cheer', 'inactive': 'nap',
            'nap': 'nap', 'start': 'greet', 'quit': 'bye', 'annoyed': 'pout', 'quit-angry': 'pout', 'drag': 'held'}
    for cue, pool in {'celebrate': [('cheer', 3), ('jump', 2), ('dance', 1)],
                      'milestone': [('dance', 2), ('jump', 1)], 'snack': [('shy', 1)],
                      'lunch': [('shy', 1)], 'water': [('shy', 1)], 'danger': [('startle', 1)],
                      'reminder-done': [('cheer', 1)]}.items():
        cues[cue] = [{'state': name, 'weight': weight} for name, weight in pool]
    # Side and back are walk cycles: they are available in the preview and for exiting, but do not
    # play as idle fidgets or stand in for reading. Resting also holds a seated pose instead of sulking.
    fidgets = [{'state': name, 'weight': weight, 'min_idle_s': delay, 'rare': rare}
               for name, weight, delay, rare in [('shy', 3, 45, False),
                                                ('roll', 1, 90, True), ('jump', 1, 90, True),
                                                ('dance', 1, 120, True)]]
    return {'schema_version': 2, 'asset_root': 'assets/whale-girl', 'states': states,
            'sequences': sequences, 'playback': playback, 'cues': cues,
            'ambient': {'sleep_after_s': 600, 'fidgets': fidgets}}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=ROOT.parent / 'pet-sources/packs/whale-png')
    args = parser.parse_args()
    source = args.source.resolve()
    inventory = json.loads((source / 'actions.json').read_text(encoding='utf-8'))
    counts = {action['key']: action['frames'] for action in inventory['actions']}
    # Validate the complete input before changing the existing pet.
    for key in ACTIONS:
        paths = sorted((source / key).glob('frame_*.png'))
        if len(paths) != counts[key] or [p.name for p in paths] != [f'frame_{i:04d}.png' for i in range(counts[key])]:
            raise ValueError(f'Missing or unordered frames: {key}')
        trajectory = source / key / 'traj.json'
        if trajectory.exists() and len(json.loads(trajectory.read_text())) != len(paths):
            raise ValueError(f'Trajectory frame count mismatch: {key}')

    frame_sets, manifest = {}, []

    destination = ROOT / 'assets/whale-girl'
    with tempfile.TemporaryDirectory(prefix='whale-girl-import-') as staging:
        output = Path(staging) / 'whale-girl'
        output.mkdir()
        for key, action in ACTIONS.items():
            paths = sorted((source / key).glob('frame_*.png'))
            trajectory_path = source / key / 'traj.json'
            trajectory = json.loads(trajectory_path.read_text()) if trajectory_path.exists() else [{}] * len(paths)
            frames = []
            for index, (path, offset) in enumerate(zip(paths, trajectory)):
                with Image.open(path) as image:
                    image = image.convert('RGBA')
                    if image.size != (512, 512):
                        raise ValueError(f'Unexpected frame size: {path}')
                    box = image.getbbox()
                    if box is None:
                        raise ValueError(f'Empty frame: {path}')
                    crop = image.crop(box)
                # Retain the source's horizontal alignment and original pixel detail. Align feet to
                # one ground line, as upstream does, and bake its motion into the transparent canvas.
                x = SIZE // 2 + box[0] - 256 + round(offset.get('dx', 0))
                if 'dx' in offset:
                    y = GROUND - 240 - crop.height // 2 + round(offset.get('dy', 0))
                    y = min(y, GROUND - crop.height)  # Upstream clamps rolls at the floor.
                else:
                    y = GROUND - crop.height - round(offset.get('dy', 0))
                if x < 0 or y < 0 or x + crop.width > SIZE or y + crop.height > SIZE:
                    raise ValueError(f'Frame would be clipped: {path}')
                canvas = Image.new('RGBA', (SIZE, SIZE))
                canvas.paste(crop, (x, y))
                relative = f'{action}/_{index:04d}_{FRAME_MS}.png'
                target = output / relative
                target.parent.mkdir(parents=True, exist_ok=True)
                canvas.save(target)
                resource = f'assets/whale-girl/{relative}'
                data = target.read_bytes()
                manifest.append({'path': resource, 'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()})
                frames.append({'path': resource, 'duration_ms': FRAME_MS})
                if action == 'front' and index == 0:
                    canvas.resize((300, 300), Image.Resampling.LANCZOS).save(output / 'preview.png')
            frame_sets[action] = frames
        write_json(output / 'pet.json', {'schema_version': 1, 'id': 'whale-girl', 'name': 'Whale Girl',
                   'author': '上善无形 / ZipZipPipe; animation: Multimodal-AI-Companion',
                   'url': 'https://github.com/oyxdsg/Multimodal-AI-Companion-Assets',
                   'terms': 'WHALE-GIRL-ARTWORK-TERMS.md', 'preview': 'preview.png'})
        catalog = make_catalog(frame_sets)
        write_json(output / 'animations.json', catalog)
        manifest.sort(key=lambda entry: entry['path'])
        write_json(output / 'manifest.json', {'schema_version': 1, 'file_count': len(manifest),
                   'total_bytes': sum(frame['bytes'] for frame in manifest), 'files': manifest})
        errors = []
        identity = check_pet(output, ROOT / 'licenses', errors)
        catalog = json.loads((output / 'animations.json').read_text(encoding='utf-8'))
        check_files(output, Path(staging), identity, catalog, errors)
        check_catalog(catalog, errors)
        if errors:
            raise ValueError('\n'.join(errors))
        if destination.exists():
            backup = Path(tempfile.mkdtemp(prefix='whale-girl-backup-')) / 'whale-girl'
            shutil.move(str(destination), str(backup))
            print(f'Previous pet backed up to {backup}')
        shutil.move(str(output), str(destination))
    print(f'Imported {len(manifest)} frames / {len(catalog["sequences"])} sequences at 25 fps into {destination}')


if __name__ == '__main__':
    main()
