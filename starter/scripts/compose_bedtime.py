#!/usr/bin/env python3
"""Compose the approved sleepy completion from imported frames, without copying pixels.

First import WORK/WorkONE/PoorCondition/{A,B,c} and LevelUP/PoorCondition with
add_sequences.py. Re-running this script replaces only the bedtime mappings and composite.
"""
from pathlib import Path
from add_sequences import load, save

ROOT = Path(__file__).resolve().parents[1]


def main():
    folder = ROOT / 'assets/vpet'
    catalog = load(folder, 'animations.json')
    sequences = {entry['path']: entry for entry in catalog['sequences']}
    work = [f'WORK/WorkONE/PoorCondition/{phase}' for phase in ('A', 'B', 'c')]
    acknowledgment = 'LevelUP/PoorCondition'
    yawn = 'IDEL/yawning/PoorCondition'
    for path in work + [acknowledgment, yawn]:
        if path not in sequences:
            raise SystemExit(f'Import {path} with scripts/add_sequences.py first')
    # The source sequence also remains independently previewable. The completion references its
    # frames and the bundled yawn in one one-shot, with no idle frame between the two beats.
    catalog['states']['sleepy_acknowledge'] = [acknowledgment]
    catalog['playback']['sleepy_acknowledge'] = {'mode': 'once', 'after': 'idle'}
    sequences[acknowledgment]['state'] = 'sleepy_acknowledge'
    for path in work:
        sequences[path]['state'] = 'working'
    composite = 'Bedtime/turn_finished'
    frames = sequences[acknowledgment]['frames'] + sequences[yawn]['frames']
    entry = {'path': composite, 'state': 'turn_finished', 'frames': frames,
             'duration_ms': sum(frame['duration_ms'] for frame in frames)}
    catalog['sequences'] = [s for s in catalog['sequences'] if s['path'] != composite] + [entry]
    catalog.setdefault('bedtime', {}).update({
        'working': [{'sequences': work, 'weight': 1}],
        'turn_finished': [{'sequences': [composite], 'weight': 1}],
    })
    save(folder, 'animations.json', catalog)
    print('Composed sleepy completion and tired working art; no new frame files.')


if __name__ == '__main__':
    main()
