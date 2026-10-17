#!/usr/bin/env python3
"""Validate with Agent Pet, then create a deterministic .agentpet ZIP.

pack.py <folder> -o <file.agentpet> [--app /path/to/agent-pet]
The app supplies the same pet/plugin and compatibility checks used by import.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import zipfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('folder', type=Path)
    parser.add_argument('-o', '--output', required=True, type=Path)
    parser.add_argument('--app', default=shutil.which('agent-pet') or str(Path(__file__).resolve().parents[1] / 'build/agent-pet'))
    args = parser.parse_args()
    folder, output = args.folder.absolute(), args.output.absolute()
    if folder == output or folder in output.parents:
        parser.error('Write the archive outside the source folder.')
    if output.suffix != '.agentpet':
        parser.error('Output must end in .agentpet.')
    try:
        subprocess.run([args.app, 'validate-pack', str(folder)], check=True)
    except (OSError, subprocess.CalledProcessError) as error:
        parser.exit(1, f'Pack validation failed: {error}\n')
    output.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(dir=output.parent, suffix='.tmp')
    os.close(fd)
    try:
        with zipfile.ZipFile(temporary, 'w', compression=zipfile.ZIP_STORED, allowZip64=False) as archive:
            for path in sorted(folder.rglob('*')):
                if path.is_symlink():
                    raise ValueError(f'Symlink is not allowed: {path}')
                if path.is_dir():
                    continue
                info = zipfile.ZipInfo(path.relative_to(folder).as_posix(), date_time=(1980, 1, 1, 0, 0, 0))
                info.create_system = 3
                info.external_attr = 0o100644 << 16
                archive.writestr(info, path.read_bytes())
        os.replace(temporary, output)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)
    print(f'Created {output}')


if __name__ == '__main__':
    main()
