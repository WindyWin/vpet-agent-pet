#!/usr/bin/env python3
"""Render a standalone moving before/after audit for all 56 activity transitions.

python3 scripts/preview_desk_continuity.py --output /tmp/vpet-desk.html \
    --baseline /tmp/vpet-before.json
Requires ImageMagick. Embeds resized copies for review only; pack artwork is untouched.
"""
import argparse
import base64
import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
STATES = ('working', 'reading', 'editing', 'executing', 'searching', 'browsing', 'delegating', 'thinking')
PEN = 'WORK/WorkONE/B/Nomal/1'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--baseline', type=Path)
    args = parser.parse_args()
    current = json.loads((ROOT / 'assets/vpet/animations.json').read_text())
    before = json.loads(args.baseline.read_text()) if args.baseline else current
    images = {}

    def trace(catalog, source, target, pen):
        sequences = {s['path']: s['frames'] for s in catalog['sequences']}
        art = catalog['activity'][source]
        first = ('Think/Extended/desk_loop' if source == 'thinking' else
                 PEN if pen and any(v['sequence'] == PEN for v in art.get('loops', [])) else catalog['states'][source][1])
        visual = art.get('continuity', {})
        resume = catalog['activity'][target].get('sustained', {}).get('resume', {}).get(source)
        swap = visual.get('overrides', {}).get(first, {}).get(target, resume or art.get('handover', {}).get(target))
        if swap:
            last = catalog['activity'][target].get('continuity', {}).get('landings', {}).get(swap,
                   'Think/Extended/desk_loop' if resume else catalog['states'][target][1])
            names = [first, swap, last]
        else:
            names = [first, visual.get('exit', catalog['states'][source][2]), *catalog['states'][target][:2]]
        frames = []
        for name in names:
            for frame in sequences[name]:
                path = frame['path']
                if path not in images:
                    data = subprocess.check_output(['magick', str(ROOT / path), '-resize', '256x256', 'png:-'])
                    images[path] = 'data:image/png;base64,' + base64.b64encode(data).decode()
                frames.append([path, frame['duration_ms'], name])
        return frames

    rows = []
    for source in STATES:
        for target in STATES:
            if source == target:
                continue
            for pen in (False, True):
                rows.append(dict(source=source, target=target, pen=pen,
                                 before=trace(before, source, target, pen), after=trace(current, source, target, pen)))
    html = '''<!doctype html><meta charset="utf-8"><title>VPet desk continuity audit</title>
<style>body{font:16px system-ui;max-width:1000px;margin:30px auto;background:#eee;color:#222}
button,select{font:inherit;margin:4px;padding:7px}main{display:flex;gap:24px}figure{margin:0}
canvas{background:#ddd}pre{white-space:pre-wrap;max-width:460px;font-size:12px}
#matrix{display:grid;grid-template-columns:repeat(8,1fr);font-size:11px}#matrix button{font-size:11px}
</style><h1>VPet desk continuity</h1><p>All 42 directed tool pairs and 14 tool/thinking pairs.
Browsing retains the standing easel fallback. Thinking starts at its seated tail loop.</p>
<label>Source pose <select id="pose"><option value="false">Ordinary loop</option><option value="true">Playful pen spin</option></select></label>
<button id="pause">Pause</button><button id="restart">Restart</button><button id="reference">Reference sequence</button>
<label>Speed <select id="speed"><option value="1">1×</option><option value="0.5">0.5×</option><option value="2">2×</option></select></label>
<h2 id="title"></h2><main><figure><figcaption>Before</figcaption><canvas id="before" width="256" height="256"></canvas><pre id="beforeInfo"></pre></figure>
<figure><figcaption>After</figcaption><canvas id="after" width="256" height="256"></canvas><pre id="afterInfo"></pre></figure></main>
<h2>Coverage matrix</h2><div id="matrix"></div><script>
const rows=ROWS, data=IMAGES, cache={};
for(const [path,url] of Object.entries(data)){const im=new Image();im.src=url;cache[path]=im;}
let source='working',target='editing',paused=false,reference=false,start=performance.now();
const referenceStates=['working','executing','searching','executing','working','executing','searching','working'];
function reset(){start=performance.now();}
for(const s of STATES)for(const t of STATES){const b=document.createElement('button');b.textContent=s===t?s:s+' → '+t;b.disabled=s===t;
 b.onclick=()=>{source=s;target=t;reference=false;reset()};document.querySelector('#matrix').append(b);}
document.querySelector('#reference').onclick=()=>{reference=true;reset()};
document.querySelector('#pose').onchange=reset;document.querySelector('#restart').onclick=reset;
document.querySelector('#pause').onclick=()=>{paused=!paused;document.querySelector('#pause').textContent=paused?'Play':'Pause';reset()};
function paint(now){if(!paused){const row=rows.find(r=>r.source===source&&r.target===target&&r.pen===JSON.parse(document.querySelector('#pose').value));
 document.querySelector('#title').textContent=reference?'Reference sequence: '+referenceStates.join(' → '):source+' → '+target+(source==='browsing'||target==='browsing'?' (art gap: fallback)':'');
 for(const side of ['before','after']){const fs=reference?referenceStates.slice(1).flatMap((t,i)=>rows.find(r=>r.source===referenceStates[i]&&r.target===t&&r.pen===JSON.parse(document.querySelector('#pose').value))[side]):row[side],total=fs.reduce((s,f)=>s+f[1],0);let ms=((now-start)*Number(document.querySelector('#speed').value))%total;
 let i=0;while(i<fs.length-1&&ms>=fs[i][1])ms-=fs[i++][1];const f=fs[i],c=document.getElementById(side),ctx=c.getContext('2d');ctx.clearRect(0,0,256,256);
 if(cache[f[0]].complete)ctx.drawImage(cache[f[0]],0,0,256,256);
 document.getElementById(side+'Info').textContent=f[2]+'\\nFrame '+(i+1)+' / '+fs.length+' · '+total+' ms\\n'+f[0];}}
 requestAnimationFrame(paint);}
requestAnimationFrame(paint);</script>'''
    html = html.replace('ROWS', json.dumps(rows)).replace('IMAGES', json.dumps(images)).replace('STATES', json.dumps(STATES))
    args.output.write_text(html)
    print(f'{args.output}: 56 pairs × 2 source poses, {len(images)} shared preview images')


if __name__ == '__main__':
    main()
