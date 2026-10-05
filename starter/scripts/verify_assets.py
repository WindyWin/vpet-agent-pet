#!/usr/bin/env python3
"""Verify the selected VPet artwork and animation catalog after copying the folder."""
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / 'assets/vpet'
manifest = json.loads((ASSETS / 'manifest.json').read_text())
animations = json.loads((ASSETS / 'animations.json').read_text())
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
if used != bundled_sequences:
    errors.append('State, variant and mood maps do not cover the bundled sequences')
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
for sequence in animations['sequences']:
    if sequence.get('state') not in owners.get(sequence['path'], set()):
        errors.append(f'Sequence names no state that uses it: {sequence["path"]}')

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
