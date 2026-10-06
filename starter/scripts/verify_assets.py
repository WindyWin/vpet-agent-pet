#!/usr/bin/env python3
"""Verify the selected VPet artwork and animation catalog after copying the folder.

    python3 scripts/verify_assets.py               # the bundled pack and its catalog
    python3 scripts/verify_assets.py other.json    # another catalog against the same pack
"""
import hashlib
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / 'assets/vpet'
manifest = json.loads((ASSETS / 'manifest.json').read_text())
animations = json.loads((Path(sys.argv[1]) if len(sys.argv) > 1 else ASSETS / 'animations.json').read_text())
available = json.loads((ASSETS / 'available-animations.json').read_text())
errors = []
expected = set()
for entry in manifest['files']:
    rel = Path(entry['path'])
    if rel.is_absolute() or '..' in rel.parts or rel.as_posix() in expected:
        errors.append(f'Invalid manifest path: {entry["path"]}')
        continue
    expected.add(rel.as_posix())
    file = ROOT / rel
    if not file.is_file() or file.is_symlink():
        errors.append(f'Missing file or symlink: {rel}')
    elif file.stat().st_size != entry['bytes'] or hashlib.sha256(file.read_bytes()).hexdigest() != entry['sha256']:
        errors.append(f'Changed file: {rel}')

actual = {p.relative_to(ROOT).as_posix() for p in (ASSETS / 'vup').rglob('*') if p.is_file()}
errors.extend(f'Unlisted file: {path}' for path in sorted(actual - expected))
if len(expected) != manifest['file_count'] or sum(f['bytes'] for f in manifest['files']) != manifest['total_bytes']:
    errors.append('Manifest count or size mismatch')

catalog_paths = set()
for sequence in animations['sequences']:
    if not sequence['frames'] or sequence['duration_ms'] != sum(frame['duration_ms'] for frame in sequence['frames']):
        errors.append(f'Invalid sequence timing: {sequence["path"]}')
    for frame in sequence['frames']:
        catalog_paths.add(frame['path'])
        if frame['path'] not in expected or frame['duration_ms'] <= 0:
            errors.append(f'Invalid catalog frame: {frame["path"]}')
if catalog_paths != expected:
    errors.append('Catalog frames do not match manifest')

def count(value, low=1):
    return isinstance(value, int) and not isinstance(value, bool) and value >= low

bundled_sequences = {sequence['path'] for sequence in animations['sequences']}
for state, paths in animations['states'].items():
    policy = animations.get('playback', {}).get(state, {})
    mode = policy.get('mode')
    if mode not in ('phased', 'loop', 'once') or len(paths) != (3 if mode == 'phased' else 1):
        errors.append(f'Invalid playback mode or phases: {state}')
    if policy.get('after') not in ('idle', 'previous', 'stop'):
        errors.append(f'Invalid completion behavior: {state}')
    if any(path not in bundled_sequences for path in paths):
        errors.append(f'Unknown state sequence: {state}')
    if 'weight' in policy and not count(policy['weight']):
        errors.append(f'Invalid weight: {state}')
    if 'loops' in policy and not (mode == 'phased' and count(policy['loops'])):
        errors.append(f'Invalid loops: {state}')

# Variants are extra weighted ways to play a state; each has the same shape as the state itself.
used = {path for paths in animations['states'].values() for path in paths}
for state, variants in animations.get('variants', {}).items():
    if state not in animations['states'] or not variants:
        errors.append(f'Variants for an unknown or empty state: {state}')
        continue
    for variant in variants:
        paths = variant.get('sequences', [])
        if len(paths) != len(animations['states'][state]) or any(path not in bundled_sequences for path in paths):
            errors.append(f'Invalid variant sequences: {state}')
        if not count(variant.get('weight')):
            errors.append(f'Invalid variant weight: {state}')
        used.update(paths)
# Mood art replaces a state's choices while the pet is happy or poorly; same shape as the state.
moods = animations.get('moods', {})
for mood, states in moods.items():
    if mood not in ('happy', 'poor'):
        errors.append(f'Unknown mood: {mood}')
        continue
    for state, choices in states.items():
        if state not in animations['states'] or not choices:
            errors.append(f'Mood art for an unknown or empty state: {mood}/{state}')
            continue
        for choice in choices:
            paths = choice.get('sequences', [])
            if len(paths) != len(animations['states'][state]) or any(path not in bundled_sequences for path in paths):
                errors.append(f'Invalid mood sequences: {mood}/{state}')
            if not count(choice.get('weight')):
                errors.append(f'Invalid mood weight: {mood}/{state}')
            used.update(paths)
owners = {}
for state, paths in animations['states'].items():
    for path in paths:
        owners.setdefault(path, set()).add(state)
for state, variants in animations.get('variants', {}).items():
    for variant in variants:
        for path in variant.get('sequences', []):
            owners.setdefault(path, set()).add(state)
for states in moods.values():
    for state, choices in states.items():
        for choice in choices:
            for path in choice.get('sequences', []):
                owners.setdefault(path, set()).add(state)

def ends_itself(state):
    policy = animations.get('playback', {}).get(state, {})
    one_shot = policy.get('mode') == 'once' or (policy.get('mode') == 'phased' and count(policy.get('loops')))
    return state in animations['states'] and state != 'idle' and one_shot and policy.get('after') == 'idle'

# Reactions are weighted pools of one-shot states, such as the ways to celebrate a finished turn.
for name, pool in animations.get('reactions', {}).items():
    if not pool:
        errors.append(f'Empty reaction: {name}')
    for reaction in pool:
        if not ends_itself(reaction.get('state')) or not count(reaction.get('weight')):
            errors.append(f'Reaction must end by itself, return to idle and have a weight: {name}/{reaction.get("state")}')

# Ambient fidgets are one-shot states played at random while the pet idles.
ambient = animations.get('ambient', {})
if 'sleep_after_s' in ambient and not count(ambient['sleep_after_s'], 60):
    errors.append('Invalid ambient sleep_after_s')
seen = set()
for fidget in ambient.get('fidgets', []):
    state = fidget.get('state')
    if state in seen or state not in animations['states'] or state == 'idle':
        errors.append(f'Invalid or duplicate fidget: {state}')
    seen.add(state)
    if not ends_itself(state):
        errors.append(f'Fidget must end by itself and return to idle: {state}')
    if not count(fidget.get('weight')) or not count(fidget.get('min_idle_s', 0), 0) or not isinstance(fidget.get('rare', False), bool):
        errors.append(f'Invalid fidget weight, tier or rarity: {state}')
if ambient.get('fidgets') and all(fidget.get('rare') for fidget in ambient['fidgets']):
    errors.append('Ambient needs at least one non-rare fidget')

# Touch reactions are held until released: phased states whose loop has no count. Hit boxes and
# edge lines are in the artwork's own square space of `scale` units.
def held(state):
    policy = animations.get('playback', {}).get(state, {})
    return (state in animations['states'] and state not in ('idle', 'dragging') and state not in seen
            and policy.get('mode') == 'phased' and 'loops' not in policy)

if 'touch' in animations:
    touch = animations['touch']
    scale = touch.get('scale')
    if not count(scale, 10) or scale > 10000:
        errors.append('Invalid touch scale')
        scale = 0
    for region in touch.get('regions', []):
        rect = region.get('rect')
        if not held(region.get('state')):
            errors.append(f'Touch region must hold a phased state until released: {region.get("state")}')
        if (not isinstance(rect, list) or len(rect) != 4 or not all(count(value, 0) for value in rect)
                or rect[2] < 1 or rect[3] < 1 or rect[0] + rect[2] > scale or rect[1] + rect[3] > scale):
            errors.append(f'Invalid touch rectangle: {region.get("state")}')
    for side, state in touch.get('fall', {}).items():
        if side not in ('left', 'right') or not held(state):
            errors.append(f'Invalid fall reaction: {side}')
    for side, edge in touch.get('edge', {}).items():
        if side not in ('left', 'right') or not held(edge.get('state')) or not count(edge.get('at')) or edge['at'] >= scale:
            errors.append(f'Invalid edge reaction: {side}')

# Moves carry the window across the screen during the loop of an ambient fidget. Speeds (units per
# second) and distances are in the artwork's square space of `scale` units.
def number(value, low, high):
    return isinstance(value, (int, float)) and not isinstance(value, bool) and low <= value <= high

if 'moves' in animations:
    moves = animations['moves']
    scale = moves.get('scale')
    if not count(scale, 10) or scale > 10000:
        errors.append('Invalid move scale')
        scale = 0
    moving = set()
    for move in moves.get('list', []):
        state = move.get('state')
        speed = move.get('speed')
        if (not ends_itself(state) or animations['playback'][state].get('mode') != 'phased' or state not in seen
                or state in moving):
            errors.append(f'Move must be a phased ambient fidget that ends by itself, once: {state}')
        moving.add(state)
        if (not isinstance(speed, list) or len(speed) != 2 or not all(number(v, -4 * scale, 4 * scale) for v in speed)
                or speed == [0, 0]):
            errors.append(f'Invalid move speed: {state}')
        if 'mood' in move and move['mood'] not in ('happy', 'poor'):
            errors.append(f'Invalid move mood: {state}')
        wall = move.get('wall', {'side': 'left', 'at': 1})
        if (not isinstance(wall, dict) or wall.get('side') not in ('left', 'right') or not count(wall.get('at'))
                or wall['at'] >= scale):
            errors.append(f'Invalid move wall: {state}')
        for key in ('room', 'near', 'keep'):
            sides = move.get(key, {'left': 0})
            if (not isinstance(sides, dict) or not sides
                    or any(side not in ('left', 'top', 'right', 'bottom') or not number(value, 0, 10 * scale)
                           for side, value in sides.items())):
                errors.append(f'Invalid move {key}: {state}')

# Activity decoration (alternate loops, reactions, desk continuity) for states that end only when asked.
# A sequence it plays counts as used by, and owned by, the state it decorates.
touch = animations.get('touch', {})
touch_states = ({region.get('state') for region in touch.get('regions', [])} | set(touch.get('fall', {}).values())
                | {edge.get('state') for edge in touch.get('edge', {}).values()})
activity = animations.get('activity', {})


def claim(state, path):
    if path not in bundled_sequences:
        return False
    used.add(path)
    owners.setdefault(path, set()).add(state)
    return True


def pool(state, choices, styled=False):
    if not isinstance(choices, list) or not choices:
        return False
    valid = True
    for choice in choices:
        if not isinstance(choice, dict):
            return False
        valid = claim(state, choice.get('sequence')) and valid
        valid = valid and count(choice.get('weight')) and choice['weight'] <= 1000
        valid = valid and (choice.get('style', 'subtle') in ('subtle', 'playful') if styled else 'style' not in choice)
    return valid


for state, entry in activity.items():
    if not held(state) or state in touch_states or not isinstance(entry, dict):
        errors.append(f'Activity must decorate a phased state that ends only when asked: {state}')
        continue
    others = set(activity) - {state}
    valid = 'loops' not in entry or pool(state, entry['loops'], styled=True)
    if 'enter' in entry:
        enter = entry['enter']
        valid = (valid and isinstance(enter, dict) and isinstance(enter.get('from'), list) and bool(enter['from'])
                 and all(source in others for source in enter['from']) and pool(state, enter.get('choices')))
    if 'exit' in entry:
        leaving = entry['exit']
        valid = (valid and isinstance(leaving, dict) and bool(leaving)
                 and all(target in others and pool(state, choices) for target, choices in leaving.items()))
    if 'linger' in entry:
        linger = entry['linger']
        valid = (valid and isinstance(linger, dict) and linger.get('to') in others
                 and count(linger.get('max_s')) and linger['max_s'] <= 60
                 and isinstance(linger.get('loop'), list) and bool(linger['loop'])
                 and all(claim(state, path) for path in linger['loop'])
                 and all(claim(state, linger[key]) for key in ('in', 'out') if key in linger))
    if 'handover' in entry:
        handover = entry['handover']
        valid = (valid and isinstance(handover, dict) and bool(handover)
                 and all(target in others and claim(state, path) for target, path in handover.items()))
    if not valid:
        errors.append(f'Invalid activity: {state}')

if used != bundled_sequences:
    errors.append('State, variant, mood and activity maps do not cover the bundled sequences')
for sequence in animations['sequences']:
    if sequence.get('state') not in owners.get(sequence['path'], set()):
        errors.append(f'Sequence names no state that uses it: {sequence["path"]}')

available_sequences = {sequence['path'] for sequence in available['sequences']}
if len(bundled_sequences) != len(animations['sequences']) or len(available_sequences) != len(available['sequences']):
    errors.append('Duplicate animation sequence')
if bundled_sequences & available_sequences:
    errors.append('Available animations overlap bundled sequences')
if len(available_sequences) != available['missing_sequence_count'] or sum(s['frame_count'] for s in available['sequences']) != available['missing_frame_count']:
    errors.append('Available animation count mismatch')
if sum(s['size_bytes'] for s in available['sequences']) != available['missing_size_bytes']:
    errors.append('Available animation size mismatch')

if errors:
    raise SystemExit('\n'.join(errors))
print(f'OK: {len(expected)} original artwork files, {len(animations["sequences"])} sequences, {manifest["total_bytes"]:,} bytes.')
