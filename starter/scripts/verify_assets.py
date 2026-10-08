#!/usr/bin/env python3
"""Verify every pet folder: metadata, preview, terms, frame manifest and animation catalog.

    python3 scripts/verify_assets.py                 # every pet under assets/
    python3 scripts/verify_assets.py other.json      # VPet's frames against another catalog
    python3 scripts/verify_assets.py --assets tests/fixtures/pets --licenses .

A pet is a folder with a pet.json (docs/pets.md). These are file-level checks plus the catalog's
playback rules. The C++ Catalog checks the catalog again at load, and the core state contract
(src/animation/core-states.json) is checked only there, by the `pets` test suite.
"""
import argparse
import hashlib
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PET_ID = re.compile(r'[a-z0-9-]{1,32}')
NAME = re.compile(r'[A-Za-z0-9_-][A-Za-z0-9._-]*')  # terms and preview files, and frame folders
# Characters a frame file name cannot carry: they break the build's resource lists (CMake lists, qrc XML),
# the catalog's path check or Windows file names. Control characters, newlines included, are refused too.
UNSAFE = set('"&\'<>;\\[]:*?|')
UNSAFE_TEXT = f'{" ".join(sorted(UNSAFE))} or control characters'
# Names a Windows checkout cannot hold: device names, also with an extension (NUL.png is NUL), and names ending in ".".
WINDOWS_DEVICES = {'CON', 'PRN', 'AUX', 'NUL', *(f'{port}{digit}' for port in ('COM', 'LPT') for digit in range(10))}
RESERVED_TEXT = 'a name Windows reserves (CON, PRN, AUX, NUL, COM0-9 or LPT0-9, with any extension, or ending in ".")'
PET_KEYS = {'schema_version', 'id', 'name', 'author', 'url', 'terms', 'preview'}
METADATA = {'pet.json', 'animations.json', 'manifest.json', 'available-animations.json'}


def png_size(path):
    """Width and height of a PNG file, or None for anything else."""
    header = path.read_bytes()[:24]
    if len(header) < 24 or header[:8] != b'\x89PNG\r\n\x1a\n' or header[12:16] != b'IHDR':
        return None
    return int.from_bytes(header[16:20], 'big'), int.from_bytes(header[20:24], 'big')


def unsafe_frame_name(name):
    """True when a frame file name holds a character the build cannot carry (see UNSAFE)."""
    return any(char in UNSAFE or ord(char) < 32 for char in name)


def windows_reserved(name):
    """True for a file or folder name a Windows checkout cannot hold (see WINDOWS_DEVICES)."""
    return name.split('.')[0].upper() in WINDOWS_DEVICES or name.endswith('.')


def asset_root_inside(asset_root, pet_id):
    """True when a catalog's asset_root is the pet's folder, assets/<id>, or a folder inside it."""
    parts = asset_root.split('/') if isinstance(asset_root, str) else []
    return parts[:2] == ['assets', pet_id] and all(NAME.fullmatch(part) for part in parts[2:])


def check_pet(folder, licenses, errors):
    """pet.json, the terms it names and its preview. Returns pet.json, or {} when it is unreadable."""
    try:
        pet = json.loads((folder / 'pet.json').read_text(encoding='utf-8'))
    except (OSError, ValueError):
        pet = None
    if not isinstance(pet, dict):
        errors.append('pet.json is not a JSON object')
        return {}

    def text(key, limit):
        return isinstance(pet.get(key), str) and 1 <= len(pet[key]) <= limit

    unknown = sorted(set(pet) - PET_KEYS)
    if unknown:
        errors.append(f'Unknown pet.json keys: {", ".join(unknown)}')
    if pet.get('schema_version') != 1:
        errors.append('pet.json schema_version must be 1')
    if (not isinstance(pet.get('id'), str) or not PET_ID.fullmatch(pet['id']) or windows_reserved(pet['id'])
            or pet['id'] != folder.name):
        errors.append('pet.json id must be the folder name: lowercase letters, digits and "-", at most 32, '
                      f'not {RESERVED_TEXT}')
    if not text('name', 64) or not text('author', 128):
        errors.append('pet.json needs a name of 1-64 characters and an author of 1-128')
    if 'url' in pet and not (isinstance(pet['url'], str) and pet['url'].startswith('https://') and len(pet['url']) <= 256):
        errors.append('pet.json url must be an https:// address of at most 256 characters')
    terms = pet.get('terms')
    if not isinstance(terms, str) or not NAME.fullmatch(terms) or not (licenses / terms).is_file():
        errors.append(f'pet.json terms must name a file in {licenses}: {terms}')
    preview = pet.get('preview')
    size = None
    if isinstance(preview, str) and NAME.fullmatch(preview) and preview.endswith('.png') and (folder / preview).is_file():
        size = png_size(folder / preview)
    if size is None or size[0] > 512 or size[1] > 512:
        errors.append(f'pet.json preview must name a PNG of at most 512 x 512 pixels in the pet folder: {preview}')
    return pet


def check_files(folder, assets, pet, animations, errors):
    """The manifest lists exactly the frame files, which lie in subfolders and under the catalog's asset_root."""
    prefix = f'assets/{folder.name}/'
    manifest = json.loads((folder / 'manifest.json').read_text(encoding='utf-8'))
    expected = set()
    for entry in manifest['files']:
        rel = entry['path']
        if not rel.startswith(prefix) or '..' in rel.split('/') or rel in expected:
            errors.append(f'Invalid manifest path: {rel}')
            continue
        expected.add(rel)
        file = assets / rel[len('assets/'):]
        if not file.is_file() or file.is_symlink():
            errors.append(f'Missing file or symlink: {rel}')
        elif file.stat().st_size != entry['bytes'] or hashlib.sha256(file.read_bytes()).hexdigest() != entry['sha256']:
            errors.append(f'Changed file: {rel}')

    actual = set()
    for path in sorted(folder.rglob('*')):
        relative = path.relative_to(folder)
        if windows_reserved(path.name):
            errors.append(f'Windows cannot check out {relative.as_posix()}: {RESERVED_TEXT}')
        if path.is_dir():
            if not NAME.fullmatch(path.name):
                errors.append(f'Folder names use A-Z, a-z, 0-9, ".", "_" and "-": {relative.as_posix()}')
        elif len(relative.parts) == 1:
            # The root holds metadata and the preview; a frame there would never be packed.
            if path.name not in METADATA | {pet.get('preview')}:
                errors.append(f'Unexpected file at the pet folder root: {path.name}')
        elif path.is_file():
            actual.add(prefix + relative.as_posix())
            if unsafe_frame_name(path.name) or path.suffix != '.png':
                errors.append(f'Frames are PNG files without {UNSAFE_TEXT} in their names: {relative.as_posix()!r}')
    errors.extend(f'Unlisted file: {path}' for path in sorted(actual - expected))
    if len(expected) != manifest['file_count'] or sum(f['bytes'] for f in manifest['files']) != manifest['total_bytes']:
        errors.append('Manifest count or size mismatch')

    asset_root = animations.get('asset_root', prefix.rstrip('/'))
    if not asset_root_inside(asset_root, folder.name):
        errors.append(f'asset_root must lie inside {prefix}: {asset_root}')
        asset_root = prefix.rstrip('/')
    catalog_paths = set()
    for sequence in animations['sequences']:
        for frame in sequence['frames']:
            catalog_paths.add(frame['path'])
            if frame['path'] not in expected or not frame['path'].startswith(asset_root + '/'):
                errors.append(f'Invalid catalog frame: {frame["path"]}')
    if catalog_paths != expected:
        errors.append('Catalog frames do not match manifest')


def check_catalog(animations, errors):
    """Playback rules: timing, states, variants, moods, reactions, fidgets, touch, moves and activity."""
    for sequence in animations['sequences']:
        if not sequence['frames'] or sequence['duration_ms'] != sum(frame['duration_ms'] for frame in sequence['frames']):
            errors.append(f'Invalid sequence timing: {sequence["path"]}')
        if any(frame['duration_ms'] <= 0 for frame in sequence['frames']):
            errors.append(f'Invalid frame duration: {sequence["path"]}')

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
    if len(bundled_sequences) != len(animations['sequences']):
        errors.append('Duplicate animation sequence')


def check_available(folder, animations, errors):
    """VPet lists the archive's sequences it does not bundle yet; scripts/add_sequences.py imports from it."""
    available = json.loads((folder / 'available-animations.json').read_text(encoding='utf-8'))
    bundled_sequences = {sequence['path'] for sequence in animations['sequences']}
    available_sequences = {sequence['path'] for sequence in available['sequences']}
    if len(available_sequences) != len(available['sequences']):
        errors.append('Duplicate animation sequence')
    if bundled_sequences & available_sequences:
        errors.append('Available animations overlap bundled sequences')
    if (len(available_sequences) != available['missing_sequence_count']
            or sum(s['frame_count'] for s in available['sequences']) != available['missing_frame_count']):
        errors.append('Available animation count mismatch')
    if sum(s['size_bytes'] for s in available['sequences']) != available['missing_size_bytes']:
        errors.append('Available animation size mismatch')


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('catalog', nargs='?', type=Path, help="another catalog to check against VPet's frames")
    parser.add_argument('--assets', type=Path, default=ROOT / 'assets', help='folder holding the pet folders')
    parser.add_argument('--licenses', type=Path, default=ROOT / 'licenses', help="folder holding the pets' terms")
    args = parser.parse_args()
    folders = sorted(path.parent for path in args.assets.glob('*/pet.json'))
    if not folders:
        raise SystemExit(f'No pet folders (folders with a pet.json) in {args.assets}')
    failures, summary = [], []
    for folder in folders:
        errors = []
        pet = check_pet(folder, args.licenses, errors)
        missing = [name for name in ('animations.json', 'manifest.json') if not (folder / name).is_file()]
        if missing:
            failures.extend(f'{folder.name}: {error}' for error in errors)
            failures.extend(f'{folder.name}: Missing {name}' for name in missing)
            continue
        catalog = args.catalog if args.catalog and folder.name == 'vpet' else folder / 'animations.json'
        unreadable = []
        for name, path in (('animations.json', catalog), ('manifest.json', folder / 'manifest.json')):
            try:
                json.loads(path.read_text(encoding='utf-8'))
            except (OSError, ValueError):
                unreadable.append(f'{name} is not valid JSON')
        if unreadable:
            failures.extend(f'{folder.name}: {error}' for error in errors + unreadable)
            continue
        animations = json.loads(catalog.read_text(encoding='utf-8'))
        try:
            check_files(folder, args.assets, pet, animations, errors)
            check_catalog(animations, errors)
            if (folder / 'available-animations.json').exists():
                check_available(folder, animations, errors)
            summary.append(f'{folder.name} ({len(animations["sequences"])} sequences)')
        except (AttributeError, IndexError, KeyError, TypeError) as error:
            # A missing or mistyped field: report it for this pet and go on to the others.
            errors.append(f'Missing or invalid field in manifest.json, animations.json or available-animations.json '
                          f'({type(error).__name__}: {error})')
        failures.extend(f'{folder.name}: {error}' for error in errors)
    if failures:
        raise SystemExit('\n'.join(failures))
    print(f'OK: {", ".join(summary)}.')


if __name__ == '__main__':
    main()
