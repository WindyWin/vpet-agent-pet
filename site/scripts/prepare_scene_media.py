#!/usr/bin/env python3
"""Prepare controllable web animations and stills. Requires ImageMagick 7.

The build uses the committed output, not the artwork archive. Run from any cwd.
Existing transparent GIF previews are retained as derivation inputs.
"""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
DEST = ROOT / 'site/src/assets/pet'
SOURCE = ROOT / 'assets/vpet/pet/vup'


def run(*args):
    subprocess.run(['magick', *map(str, args)], check=True)


for gif in sorted(DEST.glob('*.gif')):
    run(gif, '-coalesce', '-resize', '300x300>', '-loop', '0',
        '-quality', '82', DEST / f'{gif.stem}.webp')
    run(f'{gif}[0]', DEST / f'{gif.stem}-still.png')

sources = {
    'idle': 'Default/Happy/1',
    'walk': 'MOVE/walk.right/B_Nomal',
    'raised': 'Raise/Raised_Static/B_Happy',
    'water': 'Drink/Happy/back_lay',
}
for name, folder in sources.items():
    frames = sorted((SOURCE / folder).glob('*.png'))
    # Use one crop for the whole sequence to keep the feet and scale stable.
    bounds = subprocess.check_output(
        ['magick', *map(str, frames), '-background', 'none', '-layers',
         'merge', '-trim', '-format', '%wx%h%O', 'info:'], text=True).strip()
    with tempfile.TemporaryDirectory() as temporary:
        command = []
        for index, frame in enumerate(frames):
            output = Path(temporary) / f'{index:03}.png'
            run(frame, '-crop', bounds, '+repage', '-resize', '300x300>', output)
            milliseconds = int(re.search(r'_(\d+)\.png$', frame.name)[1])
            command.extend(['-delay', str(max(1, milliseconds // 10)), str(output)])
        run(*command, '-loop', '0', '-quality', '82', DEST / f'{name}.webp')
        run(Path(temporary) / '000.png', DEST / f'{name}-still.png')
    print(f'Prepared {name}')
