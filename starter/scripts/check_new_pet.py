#!/usr/bin/env python3
"""Scaffold a pet with new_pet.py in a temporary folder, add a sequence with add_sequences.py, verify both.

Run by CTest (pet-scaffold). Also checks what the scripts refuse: an existing folder, missing terms, an
oversized preview, and folder names the build cannot carry.
"""
import json
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SCRIPTS = ROOT / 'scripts'


def write_png(path, width, height, rgba):
    """A solid-colour RGBA PNG."""
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    row = b'\x00' + bytes(rgba) * width
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0))
                     + chunk(b'IDAT', zlib.compress(row * height)) + chunk(b'IEND', b''))


def run(script, *args, ok=True):
    """Runs a script; fails unless it succeeds (or, with ok=False, fails). Returns its output."""
    result = subprocess.run([sys.executable, str(SCRIPTS / script), *map(str, args)], capture_output=True, text=True)
    if (result.returncode == 0) != ok:
        raise SystemExit(f'{script} {" ".join(map(str, args))} {"failed" if ok else "succeeded"}:\n'
                         f'{result.stdout}{result.stderr}')
    return result.stdout + result.stderr


def expect(condition, message):
    if not condition:
        raise SystemExit(message)


def main():
    with tempfile.TemporaryDirectory() as temporary:
        base = Path(temporary)
        assets, licenses, art = base / 'assets', base / 'licenses', base / 'art'
        assets.mkdir()
        licenses.mkdir()
        (licenses / 'CAT-TERMS.md').write_text('Test artwork terms.\n')
        write_png(art / 'idle/_000_100.png', 16, 16, (200, 120, 40, 255))
        write_png(art / 'idle/_001_150.png', 16, 16, (210, 130, 50, 255))
        write_png(art / 'wave/_000_80.png', 16, 16, (40, 120, 200, 255))
        write_png(art / 'bad name/_000_80.png', 16, 16, (0, 0, 0, 255))
        write_png(art / 'huge/_000_100.png', 600, 600, (0, 0, 0, 255))
        write_png(art / 'semi/a;b_000_80.png', 16, 16, (0, 0, 0, 255))
        write_png(art / 'good/_000_80.png', 16, 16, (90, 90, 90, 255))
        common = ['--assets', assets, '--licenses', licenses]
        cat = ['--name', 'Cat', '--author', 'Tester', '--terms', 'CAT-TERMS.md']

        run('new_pet.py', 'cat', *cat, '--idle', art / 'idle', *common)
        run('verify_assets.py', *common)
        catalog = json.loads((assets / 'cat/animations.json').read_text())
        contract = json.loads((ROOT / 'src/animation/core-states.json').read_text())['states']
        expect(list(catalog['states']) == list(contract), 'Every core state, in the contract file order')
        expect(catalog['states']['thinking'] == ['idle'] * 3, 'A phased state lists its sequence three times')
        expect(catalog['playback']['tool_error'] == {'mode': 'once', 'after': 'previous'}, 'Contract shapes')
        expect(json.loads((assets / 'cat/pet.json').read_text())['preview'] == 'preview.png', 'Preview file')

        # A new sequence is refused until a state uses it.
        run('add_sequences.py', '--pet', 'cat', '--assets', assets, '--source', art, 'wave')
        expect('do not cover the bundled sequences' in run('verify_assets.py', *common, ok=False), 'Unused sequence')
        catalog = json.loads((assets / 'cat/animations.json').read_text())
        catalog['states']['starting'] = ['wave']
        for sequence in catalog['sequences']:
            if sequence['path'] == 'wave':
                sequence['state'] = 'starting'
        (assets / 'cat/animations.json').write_text(json.dumps(catalog, indent=2) + '\n')
        run('verify_assets.py', *common)

        # Refusals leave nothing behind.
        expect('Already exists' in run('new_pet.py', 'cat', *cat, '--idle', art / 'idle', *common, ok=False), 'Existing')
        expect('terms' in run('new_pet.py', 'dog', '--name', 'Dog', '--author', 'Tester', '--terms', 'DOG.md',
                              '--idle', art / 'idle', *common, ok=False), 'Missing terms')
        expect('--preview' in run('new_pet.py', 'big', *cat, '--idle', art / 'huge', *common, ok=False), 'Big preview')
        expect('Invalid id' in run('new_pet.py', 'Big', *cat, '--idle', art / 'idle', *common, ok=False), 'Bad id')
        expect(not (assets / 'dog').exists() and not (assets / 'big').exists(), 'Refused pets leave no folder')
        expect('Sequence folders use' in run('add_sequences.py', '--pet', 'cat', '--assets', assets, '--source', art,
                                             'bad name', ok=False), 'A folder name with a space')
        expect('Frames are PNG files without' in run('add_sequences.py', '--pet', 'cat', '--assets', assets,
                                                     '--source', art, 'semi', ok=False), 'A frame name with ";"')
        expect(not (assets / 'cat/semi').exists(), 'A refused sequence leaves no folder')
        expect('Frames are PNG files without' in run('new_pet.py', 'semi', *cat, '--idle', art / 'semi', *common,
                                                     ok=False), 'new_pet.py refuses the same frame name')
        expect(not (assets / 'semi').exists(), 'A refused pet leaves no folder')

        # One call with a good and a bad sequence copies neither, and changes no catalog or manifest.
        kept = {name: (assets / 'cat' / name).read_bytes() for name in ('animations.json', 'manifest.json')}
        expect('Frames are PNG files without' in run('add_sequences.py', '--pet', 'cat', '--assets', assets, '--source', art,
                                                     'good', 'semi', ok=False), 'A good and a bad sequence')
        expect(not (assets / 'cat/good').exists(), 'The good sequence was copied before the bad one was refused')
        expect(all((assets / 'cat' / name).read_bytes() == data for name, data in kept.items()), 'Catalog or manifest changed')

        # A pet folder without its catalog fails, by name, together with its pet.json errors.
        (assets / 'nocat').mkdir()
        (assets / 'nocat/pet.json').write_text('{"schema_version": 1, "id": "nocat", "name": "No catalog", "author": "Tester",'
                                               ' "terms": "NONE.md", "preview": "preview.png"}\n')
        message = run('verify_assets.py', *common, ok=False)
        expect('nocat: Missing animations.json' in message and 'nocat: pet.json terms must name' in message,
               f'A pet without animations.json must fail by name, with its pet.json errors:\n{message}')
        (assets / 'nocat/animations.json').write_text('{')
        (assets / 'nocat/manifest.json').write_text('{}')
        expect('nocat: animations.json is not valid JSON' in run('verify_assets.py', *common, ok=False), 'Malformed catalog')
        shutil.rmtree(assets / 'nocat')

        # Files the build cannot carry are reported.
        (assets / 'cat/notes.txt').write_text('stray\n')
        expect('Unexpected file at the pet folder root' in run('verify_assets.py', *common, ok=False), 'Stray file')
        (assets / 'cat/notes.txt').unlink()
        odd_names = ['a;b_000_80.png', 'a[1]_000_80.png']
        if sys.platform != 'win32':  # Windows file names cannot hold control characters
            odd_names.append('a\nb_000_80.png')
        for odd in odd_names:
            write_png(assets / 'cat/idle' / odd, 16, 16, (0, 0, 0, 255))
            expect('Frames are PNG files without' in run('verify_assets.py', *common, ok=False), f'Frame name {odd!r}')
            (assets / 'cat/idle' / odd).unlink()
        run('verify_assets.py', *common)
        write_png(assets / 'cat/odd name/_000_80.png', 16, 16, (0, 0, 0, 255))
        expect('Folder names use' in run('verify_assets.py', *common, ok=False), 'Folder with a space')
        # Scaffolding the mini test fixture again reproduces it; tests/pets_tests.cpp plays every core state of it.
        fixture = ROOT / 'tests/fixtures/pets/mini'
        again = base / 'again'
        again.mkdir()
        run('new_pet.py', 'mini', '--name', 'Mini', '--author', 'Agent Pet tests', '--terms', 'LICENSE',
            '--idle', fixture / 'idle', '--assets', again, '--licenses', ROOT)
        for name in ('pet.json', 'animations.json', 'manifest.json'):  # Parsed: checkouts may convert line endings.
            expect(json.loads((again / 'mini' / name).read_text()) == json.loads((fixture / name).read_text()),
                   f'new_pet.py no longer reproduces tests/fixtures/pets/mini/{name}')
        expect((again / 'mini/preview.png').read_bytes() == (fixture / 'preview.png').read_bytes(), 'mini preview')
    print('OK: new_pet.py, add_sequences.py --pet, verify_assets.py and the mini fixture')


if __name__ == '__main__':
    main()
