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

bundled_sequences = {sequence['path'] for sequence in animations['sequences']}
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
