#!/usr/bin/env python3
"""Compose VPet seated swaps from audited stationary portions of existing artwork.

No pixels are generated or copied. Browsing's standing easel has no seated counterpart;
its transitions intentionally retain the legacy exit/entrance fallback. See the coverage
matrix in docs/vpet-desk-continuity.md. Run after importing tool/long-thinking sequences.
"""
from pathlib import Path
from add_sequences import load, save

ROOT = Path(__file__).resolve().parents[1]
TOOLS = ('working', 'reading', 'editing', 'executing', 'searching', 'delegating')
# Stop before the table tilts. Calligraphy first rolls up and removes the scroll.
OUT_END = dict(working=3, reading=4, editing=17, executing=4, searching=3, delegating=3)
PEN = 'WORK/WorkONE/B/Nomal/1'
DESK = 'Think/Extended/desk_loop'


def compose(catalog):
    # The easel is standing artwork. A seated pen-spin pass has no matching join
    # in either direction, so it must not be offered as a browsing alternate.
    browsing = catalog['activity']['browsing']
    if 'loops' in browsing:
        browsing['loops'] = [choice for choice in browsing['loops'] if choice['sequence'] != PEN]
        if not browsing['loops']:
            browsing.pop('loops')
    bundled = {s['path']: s for s in catalog['sequences']}
    generated = {}

    def frames(name):
        return bundled[name]['frames']

    def sequence(state, name, parts):
        joined = [dict(frame) for part in parts for frame in part]
        generated[name] = {'state': state, 'path': name, 'frames': joined,
                           'duration_ms': sum(f['duration_ms'] for f in joined)}
        return name

    incoming = {t: frames(catalog['states'][t][0])[6:] for t in TOOLS}
    outgoing = {t: frames(catalog['states'][t][2])[:OUT_END[t]] for t in TOOLS}
    incoming['thinking'] = frames('WORK/Desk/ponder_in')
    outgoing['thinking'] = frames('WORK/Desk/ponder_out')
    loop = {t: catalog['states'][t][1] for t in TOOLS}
    loop['thinking'] = DESK
    thinking = catalog['activity']['thinking']
    # Seated tools only remove props; the standing easel retains its full arrival.
    thinking['sustained']['resume'] = {'browsing': 'Think/Extended/from_browsing'}
    for t in (*TOOLS, 'thinking'):
        art = catalog['activity'][t]
        sources = [loop[t]]
        if t != 'thinking':
            sources += [v['sequence'] for v in art.get('loops', [])]
            sources += [v['sequence'] for v in art.get('enter', {}).get('choices', [])]
            for mood in catalog.get('moods', {}).values():
                for choice in mood.get(t, []):
                    sources.append(choice['sequences'][1])
        landings = {}
        if t == 'thinking':
            landings['Think/Extended/cycle'] = DESK
            landings['Think/Extended/from_browsing'] = DESK
        else:
            landings[catalog['states'][t][0]] = loop[t]
        art['continuity'] = {'sources': list(dict.fromkeys(sources)), 'landings': landings}
        if t == 'thinking':
            art['continuity']['exit'] = catalog['states']['working'][2]
        art['handover'] = {}

    for source in (*TOOLS, 'thinking'):
        art = catalog['activity'][source]
        for target in (*TOOLS, 'thinking'):
            if source == target:
                continue
            # Preserve the public names of the original two swaps and tool resumes.
            if target == 'thinking':
                name = {'working': 'WORK/Desk/ponder_in', 'reading': 'WORK/Desk/reading_to_working'}.get(
                    source, f'Think/Extended/from_{source}')
                # ponder_in is itself a neutral pen transition; a writer needs no prop removal.
                parts = [incoming['thinking']] if source == 'working' else [outgoing[source], incoming[target]]
                if source == 'reading':
                    name = 'WORK/Desk/reading_to_thinking'
                thinking['sustained']['resume'][source] = name
                owner = 'thinking'
            else:
                name = f'WORK/Desk/{source}_to_{target}'
                parts = [outgoing[source], incoming[target]]
                owner = source
                art['handover'][target] = name
            sequence(owner, name, parts)
            catalog['activity'][target]['continuity']['landings'][name] = loop[target]
            # A pen-spin pass has already removed the source state's props. Put the pen
            # down before the destination prop arrives, rather than resurrecting old props.
            if source != 'thinking' and PEN in art['continuity']['sources']:
                override = f'WORK/Desk/pen_to_{target}'
                sequence(source, override, [outgoing['thinking'], incoming[target]])
                art['continuity'].setdefault('overrides', {}).setdefault(PEN, {})[target] = override
                catalog['activity'][target]['continuity']['landings'][override] = loop[target]

    # Same named pen transitions are shared by states. The sequence's owner must be
    # one of the states that references it (the verifier permits shared references).
    catalog['sequences'] = [generated.pop(s['path'], s) for s in catalog['sequences']]
    catalog['sequences'] += list(generated.values())
    return catalog


def main():
    folder = ROOT / 'assets/vpet'
    save(folder, 'animations.json', compose(load(folder, 'animations.json')))


if __name__ == '__main__':
    main()
