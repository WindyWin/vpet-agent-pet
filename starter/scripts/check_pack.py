#!/usr/bin/env python3
"""Exercise the actual headless importer with adversarial ZIPs and authoring round trips."""
import json
import os
from pathlib import Path
import shutil
import stat
import struct
import zlib
import subprocess
import sys
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
APP = Path(sys.argv[1]).resolve()


def run(*args, success=True, input='yes\n'):
    result = subprocess.run([str(APP), *map(str, args)], input=input, text=True, capture_output=True, env=ENV)
    assert (result.returncode == 0) == success, (args, result.returncode, result.stdout, result.stderr)
    return result


with tempfile.TemporaryDirectory() as temporary:
    work = Path(temporary)
    ENV = dict(os.environ, XDG_DATA_HOME=str(work / 'data'), XDG_CONFIG_HOME=str(work / 'config'),
               LOCALAPPDATA=str(work / 'data'), APPDATA=str(work / 'config'))
    source = work / 'local-cat'
    shutil.copytree(ROOT / 'tests/fixtures/local-pets/local-cat', source)
    subprocess.run([sys.executable, str(ROOT / 'scripts/new_pet.py'), 'previous', '--local',
                    '--name', 'Scaffold Cat', '--author', 'Test author', '--terms', 'TERMS.txt',
                    '--licenses', str(source / 'licenses'), '--idle', str(source / 'idle')], check=True, env=ENV)
    run('validate-pack', work / 'data/agent-pet/packs/pets/previous')
    previous = work / 'data/agent-pet/packs/pets/previous'
    previous_archive = work / 'previous.agentpet'
    subprocess.run([sys.executable, str(ROOT / 'scripts/pack.py'), str(previous), '-o', str(previous_archive), '--app', str(APP)], check=True, env=ENV)
    run('import', previous_archive)  # identity must not collide with transaction backup names
    run('remove', 'pet', 'previous')
    archive = work / 'cat.agentpet' 
    command = [sys.executable, str(ROOT / 'scripts/pack.py'), str(source), '-o', str(archive), '--app', str(APP)]
    subprocess.run(command, check=True, env=ENV)
    first = archive.read_bytes()
    subprocess.run(command, check=True, env=ENV)
    assert archive.read_bytes() == first, 'Archive must be deterministic'
    run('import', archive, input='no\n', success=False)
    run('import', archive)
    # Locate Qt's platform-specific per-user data directory under our isolated roots.
    installed = next(path for path in work.rglob('pack.json') if '/packs/pets/local-cat/' in path.as_posix()).parent
    original = (installed / 'manifest.json').read_bytes()
    run('import', archive)  # explicit equal-version replacement
    assert (installed / 'manifest.json').read_bytes() == original

    def malicious(name, entries, compression=zipfile.ZIP_STORED):
        path = work / (name + '.agentpet')
        with zipfile.ZipFile(path, 'w', compression=compression) as output:
            for fixture in source.rglob('*'):
                if fixture.is_file():
                    output.write(fixture, fixture.relative_to(source))
            for entry, data in entries:
                output.writestr(entry, data)
        run('import', path, success=False)
        assert (installed / 'manifest.json').read_bytes() == original, name

    for path in ['../escape.txt', '/absolute.txt', 'C:/drive.txt', 'a\\b.txt', 'CON.txt', 'a/../../escape.txt']:
        malicious('path', [(path, 'bad')])
    link = zipfile.ZipInfo('link.txt'); link.create_system = 3; link.external_attr = (stat.S_IFLNK | 0o777) << 16
    malicious('symlink', [(link, '../outside')])
    malicious('executable', [('run.sh', 'bad')])
    malicious('duplicate', [('A.txt', 'a'), ('a.txt', 'b')])
    malicious('case-parent', [('A/a.txt', 'a'), ('a/b.txt', 'b')])
    malicious('collision', [('a/b.txt', 'a'), ('a', 'b')])
    malicious('bomb', [('large.txt', b'0' * (1024 * 1024))], zipfile.ZIP_DEFLATED)
    malicious('count', [(f'{i}.txt', '') for i in range(20001)])
    bad = work / 'corrupt.agentpet'; corrupt = bytearray(first); corrupt[50] ^= 0xff; bad.write_bytes(corrupt)
    run('import', bad, success=False)
    assert (installed / 'manifest.json').read_bytes() == original
    # A structurally valid archive with a bad manifest must also preserve the old pack.
    with zipfile.ZipFile(work / 'invalid.agentpet', 'w') as output:
        for path in source.rglob('*'):
            if path.is_file():
                output.writestr(path.relative_to(source).as_posix(), b'{}' if path.name == 'manifest.json' else path.read_bytes())
    run('import', work / 'invalid.agentpet', success=False)
    assert (installed / 'manifest.json').read_bytes() == original
    # Deflated archives from ordinary ZIP tools also round trip.
    with zipfile.ZipFile(work / 'deflated.agentpet', 'w', compression=zipfile.ZIP_DEFLATED) as output:
        for path in source.rglob('*'):
            if path.is_file():
                output.write(path, path.relative_to(source))
    run('import', work / 'deflated.agentpet')
    # The first compressed read consumes exactly 64 KiB and produces exactly
    # 64 KiB without ending the stream. A second inflate call needs more input
    # (Z_BUF_ERROR), rather than indicating corruption.
    compressor = zlib.compressobj(wbits=-15)
    prefix = compressor.compress(b'A' * 15) + compressor.flush(zlib.Z_SYNC_FLUSH)
    assert len(prefix) == 10
    block = b'B' * 65521
    compressed = prefix + struct.pack('<BHH', 0, len(block), 65535 - len(block)) + block
    assert len(compressed) == 65536
    final_block = b'\x01\x00\x00\xff\xff'
    contents = b'A' * 15 + block
    assert zlib.decompress(compressed + final_block, -15) == contents

    def boundary_archive(path, ending):
        entries = [(item.relative_to(source).as_posix(), item.read_bytes(), None)
                   for item in source.rglob('*') if item.is_file()]
        entries.append(('boundary.txt', contents, compressed + ending))
        records, directory = bytearray(), bytearray()
        for name, data, packed in entries:
            name = name.encode('utf-8')
            method = 8 if packed is not None else 0
            if packed is None:
                packed = data
            crc = zlib.crc32(data)
            offset = len(records)
            records += struct.pack('<IHHHHHIIIHH', 0x04034b50, 20, 0, method, 0, 0,
                                   crc, len(packed), len(data), len(name), 0) + name + packed
            directory += struct.pack('<IHHHHHHIIIHHHHHII', 0x02014b50, 20, 20, 0, method, 0, 0,
                                     crc, len(packed), len(data), len(name), 0, 0, 0, 0, 0, offset) + name
        count = len(entries)
        path.write_bytes(records + directory + struct.pack('<IHHHHIIH', 0x06054b50, 0, 0,
                                                          count, count, len(directory), len(records), 0))

    boundary_archive(work / 'boundary.agentpet', final_block)
    run('import', work / 'boundary.agentpet')
    assert (installed / 'boundary.txt').read_bytes() == contents
    boundary_archive(work / 'truncated.agentpet', b'')
    run('import', work / 'truncated.agentpet', success=False)
    assert (installed / 'boundary.txt').read_bytes() == contents
    plugin = work / 'example'
    shutil.copytree(ROOT / 'tests/fixtures/plugins/example', plugin)
    metadata = json.loads((plugin / 'plugin.json').read_text())
    metadata.pop('schema_version')
    metadata.update(kind='plugin', schema=2, minAppVersion='0.17.0', terms='TERMS.txt')
    (plugin / 'pack.json').write_text(json.dumps(metadata))
    (plugin / 'TERMS.txt').write_text('Test artwork terms.\n')
    plugin_archive = work / 'plugin.agentpet'
    subprocess.run([sys.executable, str(ROOT / 'scripts/pack.py'), str(plugin), '-o', str(plugin_archive), '--app', str(APP)], check=True, env=ENV)
    run('import', plugin_archive)
    run('import', plugin_archive)
    run('remove', 'plugin', 'example')
    run('remove', 'pet', 'vpet', success=False)
    run('remove', 'pet', 'local-cat')
    assert not installed.exists()
    run('remove', 'pet', 'local-cat', success=False)
    print('Pack archive security, deterministic round trip, replacement and removal passed.')
