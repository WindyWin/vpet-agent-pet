#!/usr/bin/env python3
"""Bring pet catalogs (assets/<id>/animations.json) up to the schema this source tree reads.

    python3 scripts/migrate_catalog.py assets/*/animations.json tests/fixtures/pets/*/animations.json

Schema 1 to 2: the "reactions" section becomes reaction cues in a new "cues" section, under the cue names of
src/animation/cues.json (turn_finished -> celebrate, long_turn -> long-turn, ...). State cues keep their default
states, so the pet plays exactly as before. A catalog already at schema 2 is left alone. docs/pets.md lists the cues.
"""
import argparse
import json
import sys
from pathlib import Path

SCHEMA = 2
# Schema 1 reaction pools and the reaction cues that replace them.
REACTIONS = {
    'turn_finished': 'celebrate',
    'long_turn': 'long-turn',
    'friday_evening': 'friday-evening',
    'late_night': 'late-night',
    'leave_work': 'leave-work',
    'eye_break': 'eye-break',
    'reminder_done': 'reminder-done',
}
SAME = {'snack', 'milestone', 'birthday', 'may20', 'monday', 'sleep', 'konami', 'danger', 'water'}


def migrate(document):
    """The catalog at schema 2, keeping its key order; raises ValueError for one this script cannot read."""
    schema = document.get('schema_version')
    if schema == SCHEMA:
        return document
    if schema != 1:
        raise ValueError(f'unsupported schema_version {schema!r}')
    if 'cues' in document:
        raise ValueError('schema 1 catalog already has a "cues" section')
    cues = {}
    reactions = document.get('reactions', {})
    if not isinstance(reactions, dict):
        raise ValueError('"reactions" must be an object')
    for name, pool in reactions.items():
        if name not in REACTIONS and name not in SAME:
            raise ValueError(f'unknown reaction pool {name!r}')
        cues[REACTIONS.get(name, name)] = pool
    migrated = {}
    for key, value in document.items():
        if key == 'schema_version':
            migrated[key] = SCHEMA
        elif key == 'reactions':
            migrated['cues'] = cues
        else:
            migrated[key] = value
    return migrated


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('catalogs', nargs='+', type=Path)
    args = parser.parse_args()
    failed = False
    for path in args.catalogs:
        try:
            document = json.loads(path.read_text(encoding='utf-8'))
            migrated = migrate(document)
        except (OSError, ValueError) as error:
            print(f'{path}: {error}', file=sys.stderr)
            failed = True
            continue
        if migrated is document:
            print(f'{path}: already at schema {SCHEMA}')
            continue
        path.write_text(json.dumps(migrated, indent=2) + '\n', encoding='utf-8')
        print(f'{path}: migrated to schema {SCHEMA}')
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
