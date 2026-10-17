#!/usr/bin/env python3
"""Convert a local VPet workshop pet into an Agent Pet pack; never load mod code."""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

from add_sequences import frames_of
from verify_assets import png_size, windows_reserved

ROOT = Path(__file__).resolve().parents[1]
TEMPLATE = ROOT / 'assets/vpet/animations.json'


def write_json(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


def parse_lps(path):
    """Read the data-only subset used by pet metadata (repeated work/move lines retained)."""
    records = {}
    for line in path.read_text(encoding='utf-8-sig').splitlines():
        line = line.strip()
        if not line or line.startswith('///'):
            continue
        parts = line.split(':|')
        key, _, value = parts[0].partition('#')
        fields = {'value': value}
        for part in parts[1:]:
            name, separator, value = part.partition('#')
            if separator:
                fields[name.lower()] = value
        records.setdefault(key.lower(), []).append(fields)
    return records


def discover(mod):
    if mod.is_file() and mod.suffix == '.lps':
        candidates = [mod]
    else:
        candidates = sorted((mod / 'pet').glob('*.lps'))
        if not candidates:
            candidates = sorted(mod.glob('*.lps'))
        if not candidates and mod.with_suffix('.lps').is_file():
            candidates = [mod.with_suffix('.lps')]
    candidates = [p for p in candidates if 'pet' in parse_lps(p)]
    if len(candidates) != 1:
        raise ValueError('Expected one pet definition; pass pet/<name>.lps to select a pet.')
    definition = candidates[0]
    records = parse_lps(definition)
    relative = records['pet'][0].get('path', definition.stem)
    if not relative or '\\' in relative or Path(relative).is_absolute() or '..' in Path(relative).parts:
        raise ValueError('Unsafe graph path in pet definition')
    graphs = definition.parent / relative
    if not graphs.is_dir() or graphs.is_symlink() or definition.is_symlink():
        raise ValueError('Pet definition and graph folder must be regular local files/directories')
    return records, graphs


def convert(mod, folder, pet_id, author=None, license_name=None, terms=None):
    if not re.fullmatch(r'[a-z0-9-]{1,32}', pet_id) or pet_id == 'vpet' or windows_reserved(pet_id):
        raise ValueError('Use a portable lowercase pet id (1–32 letters, digits or hyphens), other than vpet.')
    records, graphs = discover(mod)
    pet = records['pet'][0]
    name = pet.get('petname') or pet.get('value') or pet_id
    author = author or pet.get('author') or records.get('author', [{}])[0].get('value')
    license_name = license_name or pet.get('license') or records.get('license', [{}])[0].get('value')
    if not author or not license_name or terms is None:
        raise ValueError('Supply --author and --license if absent from the mod, and --terms with its licence text.')
    if terms.stat().st_size > 1024 * 1024:
        raise ValueError('Terms must be at most 1 MiB.')
    terms_text = terms.read_text(encoding='utf-8-sig')
    if not terms_text.strip() or len(terms_text.encode('utf-8')) > 1024 * 1024:
        raise ValueError('Terms must contain UTF-8 text, at most 1 MiB.')
    # Copy only PNGs into an isolated input tree. frames_of remains authoritative
    # for index ordering, duplicate runs and durations; unrelated mod files are ignored.
    with tempfile.TemporaryDirectory(prefix='agent-pet-mod-frames-') as temp:
        clean = Path(temp)
        for file in sorted(graphs.rglob('*')):
            if file.is_symlink():
                raise ValueError(f'Symlinks are not supported: {file}')
            if file.is_file() and file.suffix.lower() == '.png':
                relative = file.relative_to(graphs).with_suffix('.png')
                target = clean / relative
                if target.exists():
                    raise ValueError(f'Duplicate PNG filename: {relative}')
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(file, target)
        available = {p.parent.relative_to(clean).as_posix() for p in clean.rglob('*.png')}
        idle = 'Default/Nomal/1'
        if idle not in available:
            defaults = sorted(p for p in available if p == 'Default' or p.startswith('Default/'))
            if not defaults:
                raise ValueError('Mod has no Default idle PNG sequence')
            idle = defaults[0]
        catalog = json.loads(TEMPLATE.read_text(encoding='utf-8'))
        mapping, reasons = {}, {}
        for sequence in catalog['sequences']:
            path = sequence['path']
            normal = re.sub(r'Happy|PoorCondition|Ill', 'Nomal', path)
            chosen = path if path in available else normal if normal in available else idle
            mapping[path] = chosen
            reasons[path] = 'exact' if chosen == path else 'normal' if chosen == normal else 'idle'
        # Keep template sequence identities and all playback shapes. A single
        # copied source can back many fallback identities without duplicating PNGs.
        files, loaded, frame_paths = [], {}, {}
        folder.mkdir(parents=True)
        for sequence_index, path in enumerate(sorted(set(mapping.values()))):
            loaded[path] = frames_of(clean / path)
            for frame_index, (_, duration, file) in enumerate(loaded[path]):
                if file.stat().st_size > 4 * 1024 * 1024:
                    raise ValueError(f'PNG exceeds 4 MiB: {file.name}')
                size = png_size(file)
                if size is None or min(size) <= 0 or max(size) > 2048:
                    raise ValueError(f'PNG dimensions must be 1–2048 pixels: {file.name}')
                if duration <= 0:
                    raise ValueError(f'Frame duration must be positive: {file.name}')
                relative = f'frames/s{sequence_index:04d}/f{frame_index:05d}.png'
                frame_paths[file] = f'assets/{pet_id}/{relative}'
                target = folder / relative
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(file, target)
                data = target.read_bytes()
                files.append({'path': frame_paths[file], 'bytes': len(data),
                              'sha256': hashlib.sha256(data).hexdigest()})
        catalog['asset_root'] = f'assets/{pet_id}'
        for sequence in catalog['sequences']:
            source = mapping[sequence['path']]
            sequence['frames'] = [{'path': frame_paths[file], 'duration_ms': duration}
                                  for _, duration, file in loaded[source]]
            sequence['duration_ms'] = sum(f['duration_ms'] for f in sequence['frames'])
        regions = []
        for key, state in [('pinch', 'pinch'), ('touchhead', 'touch_head'), ('touchbody', 'touch_body')]:
            if key in records:
                fields = records[key][0]
                regions.append({'state': state, 'rect': [int(fields[k]) for k in ('px', 'py', 'sw', 'sh')]})
        catalog['touch']['regions'] = regions
        if 'side' in records:
            for side in ('left', 'right'):
                catalog['touch']['edge'][side]['at'] = int(records['side'][0][side])
        else:
            catalog['touch'].pop('edge', None)
        # Preserve template moves only where the mod supplies the actual art;
        # never slide idle fallback frames around the desktop.
        catalog['moves']['list'] = [move for move in catalog['moves']['list']
            if all(reasons[p] == 'exact' for p in catalog['states'][move['state']])]
        if not catalog['moves']['list']:
            catalog.pop('moves')
        preview = loaded[idle][0][2]
        size = png_size(preview)
        if size is None:
            raise ValueError('Idle frame is not a PNG')
        if max(size) > 512:
            try:
                from PIL import Image
            except ImportError as error:
                raise ValueError('Install Pillow to resize the idle preview: python -m pip install Pillow') from error
            with Image.open(preview) as image:
                image.thumbnail((512, 512))
                image.save(folder / 'preview.png')
        else:
            shutil.copyfile(preview, folder / 'preview.png')
        (folder / 'licenses').mkdir()
        (folder / 'licenses/TERMS.txt').write_text(terms_text, encoding='utf-8')
        metadata = {'kind': 'pet', 'id': pet_id, 'name': name, 'author': author, 'license': license_name,
                    'version': '1.0.0', 'schema': 2, 'minAppVersion': '0.17.0',
                    'terms': 'licenses/TERMS.txt', 'source': 'vpet-mod'}
        write_json(folder / 'pack.json', metadata)
        write_json(folder / 'pet.json', {'schema_version': 1, 'id': pet_id, 'name': name, 'author': author,
                                       'terms': 'TERMS.txt', 'preview': 'preview.png'})
        write_json(folder / 'animations.json', catalog)
        write_json(folder / 'manifest.json', {'schema_version': 1, 'file_count': len(files),
                    'total_bytes': sum(f['bytes'] for f in files), 'files': files})
        state_paths = copy.deepcopy(catalog['states'])
        for state, variants in catalog.get('variants', {}).items():
            state_paths[state] += [p for v in variants for p in v['sequences']]
        for states in catalog.get('moods', {}).values():
            for state, variants in states.items():
                state_paths[state] += [p for v in variants for p in v['sequences']]

        def sequence_references(value):
            if isinstance(value, str):
                return [value] if value in reasons else []
            if isinstance(value, dict):
                return [p for item in value.values() for p in sequence_references(item)]
            if isinstance(value, list):
                return [p for item in value for p in sequence_references(item)]
            return []

        for state, activity in catalog.get('activity', {}).items():
            state_paths[state] += sequence_references(activity)
        cues = json.loads((ROOT / 'src/animation/cues.json').read_text())['cues']
        cue_status = {}
        for cue, contract in cues.items():
            target = catalog.get('cues', {}).get(cue, contract.get('state'))
            states = [target] if isinstance(target, str) else [x['state'] for x in target or []]
            paths = [p for state in states for p in state_paths.get(state, [])]
            cue_status[cue] = 'real' if paths and all(reasons[p] == 'exact' for p in paths) else 'fallback'
        return {'id': pet_id, 'tags': records.get('tag', []), 'raise_points': records.get('raisepoint', []),
                'cues': cue_status, 'sequences': {p: {'source': mapping[p], 'kind': reasons[p]} for p in mapping},
                'unused_folders': sorted(available - set(mapping.values())),
                'notes': ['Raise points retained in report; the runtime has no raise-point field.',
                          'Matching movement art uses template movement speeds and geometry (500-unit coordinates).']}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mod', type=Path)
    parser.add_argument('-o', '--output', required=True, type=Path)
    parser.add_argument('--id', default='converted-pet')
    parser.add_argument('--report', type=Path)
    parser.add_argument('--author')
    parser.add_argument('--license')
    parser.add_argument('--terms', type=Path)
    parser.add_argument('--app', default=shutil.which('agent-pet') or str(ROOT / 'build/agent-pet'))
    args = parser.parse_args()
    if args.report and args.report.resolve() == args.output.resolve():
        parser.error('--report must differ from the output archive')
    try:
        with tempfile.TemporaryDirectory(prefix='agent-pet-convert-') as temp:
            folder = Path(temp) / args.id
            report = convert(args.mod, folder, args.id, args.author, args.license, args.terms)
            subprocess.run([sys.executable, str(ROOT / 'scripts/pack.py'), str(folder), '-o', str(args.output),
                            '--app', args.app], check=True)
        if args.report:
            write_json(args.report, report)
        print(f"{sum(v == 'real' for v in report['cues'].values())} cues with real art; "
              f"{sum(v == 'fallback' for v in report['cues'].values())} cues on fallback; "
              f"{len(report['unused_folders'])} unused folders")
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        parser.exit(1, f'Conversion failed: {error}\n')


if __name__ == '__main__':
    main()
