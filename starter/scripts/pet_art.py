"""Shared drawing kit for Agent Pet's vector-drawn pets (scripts/draw_kuro.py, scripts/draw_yun.py).

Frames are 1000 x 1000 with VPet's ground line. A pet script defines a Pose, a render(pose) that returns a
cairo surface, and a table of sequences; run() writes them as <sequence>/_NNN_<ms>.png, merging consecutive
identical frames into one longer frame.
"""
import argparse
import io
import math
import random
from pathlib import Path

import cairo
from PIL import Image

SIZE = 1000
GROUND = 962
CX = 500
INK = (0.13, 0.12, 0.20)
GOLD = (1.0, 0.80, 0.25)
CHEEK = (1.0, 0.52, 0.58)
WHITE = (1.0, 1.0, 1.0)
METAL = (0.80, 0.83, 0.88)
LINE = 11


def lerp(a, b, t):
    return a + (b - a) * t


def mix(c1, c2, t):
    return tuple(lerp(a, b, t) for a, b in zip(c1, c2))


def ease(t):
    t = max(0.0, min(1.0, t))
    return t * t * (3 - 2 * t)


def ease_out(t):
    t = max(0.0, min(1.0, t))
    return 1 - (1 - t) ** 3


def back_out(t, s=1.9):
    t = max(0.0, min(1.0, t)) - 1
    return t * t * ((s + 1) * t + s) + 1


def hop(t):
    """0 at both ends, 1 in the middle: a jump arc."""
    t = max(0.0, min(1.0, t))
    return 4 * t * (1 - t)


def osc(t, period=1.0, phase=0.0):
    return math.sin(2 * math.pi * (t / period + phase))


def rgb(ctx, color, alpha=1.0):
    if alpha >= 1:
        ctx.set_source_rgb(*color)
    else:
        ctx.set_source_rgba(*color, alpha)


def fill_stroke(ctx, color, width=LINE, ink=INK, alpha=1.0):
    rgb(ctx, color, alpha)
    ctx.fill_preserve()
    rgb(ctx, ink, alpha)
    ctx.set_line_width(width)
    ctx.stroke()


def round_rect(ctx, x, y, w, h, r):
    r = min(r, w / 2, h / 2)
    ctx.new_sub_path()
    ctx.arc(x + w - r, y + r, r, -math.pi / 2, 0)
    ctx.arc(x + w - r, y + h - r, r, 0, math.pi / 2)
    ctx.arc(x + r, y + h - r, r, math.pi / 2, math.pi)
    ctx.arc(x + r, y + r, r, math.pi, 3 * math.pi / 2)
    ctx.close_path()


def ellipse(ctx, x, y, rx, ry):
    ctx.save()
    ctx.translate(x, y)
    ctx.scale(max(rx, 0.01), max(ry, 0.01))
    ctx.new_sub_path()
    ctx.arc(0, 0, 1, 0, 2 * math.pi)
    ctx.restore()


def capsule(ctx, x1, y1, x2, y2, width, color):
    ctx.set_line_cap(cairo.LINE_CAP_ROUND)
    ctx.move_to(x1, y1)
    ctx.line_to(x2, y2)
    rgb(ctx, INK)
    ctx.set_line_width(width + 2 * LINE)
    ctx.stroke_preserve()
    rgb(ctx, color)
    ctx.set_line_width(width)
    ctx.stroke()


def star(ctx, x, y, r, color, outline=True, points=5, inner=0.45, rot=0):
    for i in range(points * 2):
        a = rot - math.pi / 2 + i * math.pi / points
        rr = r if i % 2 == 0 else r * inner
        (ctx.move_to if i == 0 else ctx.line_to)(x + math.cos(a) * rr, y + math.sin(a) * rr)
    ctx.close_path()
    ctx.set_line_join(cairo.LINE_JOIN_ROUND)
    if outline:
        fill_stroke(ctx, color, LINE * 0.7)
    else:
        rgb(ctx, color)
        ctx.fill()


def heart(ctx, x, y, s, color, outline=True, alpha=1.0):
    ctx.move_to(x, y + s * 0.9)
    ctx.curve_to(x - s * 1.4, y - s * 0.1, x - s * 0.8, y - s * 1.2, x, y - s * 0.45)
    ctx.curve_to(x + s * 0.8, y - s * 1.2, x + s * 1.4, y - s * 0.1, x, y + s * 0.9)
    ctx.close_path()
    if outline:
        fill_stroke(ctx, color, max(4, s * 0.22), alpha=alpha)
    else:
        rgb(ctx, color, alpha)
        ctx.fill()


def thought_bubble(dots=3, size=1.0, pulse=0.0, bulb=0.0):
    def draw(ctx, p):
        if size <= 0.01:
            return
        bx, by = 205, -640
        for i, (x, y, r) in enumerate(((150, -545, 14), (178, -585, 22))):
            if size * 3 > i:
                ellipse(ctx, x, y, r * min(1, size * 2), r * min(1, size * 2))
                fill_stroke(ctx, WHITE, 8)
        if size < 0.5:
            return
        s = back_out((size - 0.5) * 2)
        ctx.save()
        ctx.translate(bx, by)
        ctx.scale(s, s)
        ctx.new_sub_path()
        for k in range(8):
            a = k / 8 * 2 * math.pi
            ctx.arc(math.cos(a) * 92, math.sin(a) * 55, 40, a - 1.4, a + 1.4)
        ctx.close_path()
        fill_stroke(ctx, WHITE, 9)
        if bulb > 0:
            glow = 0.4 + 0.4 * bulb
            ellipse(ctx, 0, -6, 52, 52)
            rgb(ctx, (1, 0.95, 0.5), glow)
            ctx.fill()
            ellipse(ctx, 0, -10, 28, 30)
            fill_stroke(ctx, GOLD, 7)
            round_rect(ctx, -14, 16, 28, 18, 5)
            fill_stroke(ctx, METAL, 6)
        else:
            for i in range(dots):
                up = max(0, math.sin((pulse - i * 0.22) * 2 * math.pi)) * 12
                ellipse(ctx, -44 + i * 44, -up, 13, 13)
                rgb(ctx, INK)
                ctx.fill()
        ctx.restore()
    return draw


def bubble_mark(kind, size=1.0, wobble=0.0, x=190, y=-610):
    """A speech bubble with '!' or '?' in it."""
    def draw(ctx, p):
        if size <= 0.01:
            return
        s = size
        ctx.save()
        ctx.translate(x, y)
        ctx.rotate(wobble * 0.15)
        ctx.scale(s, s)
        ellipse(ctx, 0, 0, 70, 66)
        ctx.move_to(-40, 40)
        ctx.line_to(-74, 92)
        ctx.line_to(-6, 58)
        ctx.close_path()
        fill_stroke(ctx, WHITE, 9)
        ellipse(ctx, 0, 0, 62, 58)
        rgb(ctx, WHITE)
        ctx.fill()
        color = (0.86, 0.25, 0.27) if kind == '!' else INK
        rgb(ctx, color)
        ctx.set_line_cap(cairo.LINE_CAP_ROUND)
        if kind == '!':
            ctx.set_line_width(22)
            ctx.move_to(0, -34)
            ctx.line_to(0, 8)
            ctx.stroke()
            ellipse(ctx, 0, 34, 12, 12)
            ctx.fill()
        else:
            ctx.set_line_width(16)
            ctx.arc(0, -16, 20, math.pi * 1.05, math.pi * 2.35)
            ctx.line_to(0, 12)
            ctx.stroke()
            ellipse(ctx, 0, 36, 10, 10)
            ctx.fill()
        ctx.restore()
    return draw


def smoke(t, x=0, y=-280, spread=1.0, seed=3):
    """A ninja puff of smoke growing and fading for t in 0..1."""
    def draw(ctx, p):
        if t <= 0 or t >= 1:
            return
        rnd = random.Random(seed)
        grow = ease_out(t)
        fade = 1 - ease(max(0, (t - 0.45) / 0.55))
        ctx.save()
        ctx.translate(CX + p.x + x, GROUND + y)
        puffs = [(rnd.uniform(-1, 1), rnd.uniform(-1, 1), rnd.uniform(0.6, 1.1)) for _ in range(11)]
        circles = []
        for ox, oy, r in puffs:
            circles.append((ox * 230 * spread * grow, oy * 190 * spread * grow - 30 * t, r * (60 + 110 * grow) * spread))
        # One cloud: every outline first, then every fill over it, so only the outer edge shows.
        ctx.push_group()
        for px, py, rr in circles:
            ellipse(ctx, px, py, rr + 5, (rr + 5) * 0.92)
        rgb(ctx, (0.55, 0.57, 0.66))
        ctx.fill()
        for px, py, rr in circles:
            ellipse(ctx, px, py, rr - 4, (rr - 4) * 0.92)
        rgb(ctx, (0.95, 0.95, 0.98))
        ctx.fill()
        for px, py, rr in circles:
            ellipse(ctx, px - rr * 0.25, py - rr * 0.3, rr * 0.35, rr * 0.25)
        rgb(ctx, WHITE)
        ctx.fill()
        ctx.pop_group_to_source()
        ctx.paint_with_alpha(fade)
        ctx.restore()
    return draw


def sparkles(t, count=6, radius=330, cy=-330, seed=1, color=GOLD):
    def draw(ctx, p):
        rnd = random.Random(seed)
        for i in range(count):
            a = rnd.uniform(0, 2 * math.pi)
            r = radius * rnd.uniform(0.75, 1.1)
            delay = rnd.uniform(0, 0.4)
            local = (t - delay) / 0.6
            if 0 < local < 1:
                s = math.sin(local * math.pi) * rnd.uniform(18, 32)
                star(ctx, math.cos(a) * r, cy + math.sin(a) * r * 0.8, s, color, points=4, inner=0.35)
    return draw


def confetti(t, seed=5, amount=26):
    colors = [(0.95, 0.35, 0.40), GOLD, (0.40, 0.75, 0.95), (0.55, 0.85, 0.50), (0.80, 0.55, 0.95)]

    def draw(ctx, p):
        rnd = random.Random(seed)
        for i in range(amount):
            x0 = rnd.uniform(-420, 420)
            y0 = rnd.uniform(-860, -620)
            speed = rnd.uniform(500, 760)
            spin = rnd.uniform(-8, 8)
            y = y0 + speed * t
            if y > -40:
                continue
            ctx.save()
            ctx.translate(CX + p.x + x0 + math.sin(t * 6 + i) * 20, GROUND + y)
            ctx.rotate(spin * t + i)
            ctx.rectangle(-9, -5, 18, 10)
            rgb(ctx, colors[i % len(colors)])
            ctx.fill()
            ctx.restore()
    return draw


def hearts_up(t, count=3, seed=2, x=0):
    def draw(ctx, p):
        rnd = random.Random(seed)
        for i in range(count):
            delay = i / count * 0.6
            local = (t - delay) / 0.7
            if 0 < local < 1:
                hx = x + rnd.uniform(-170, 170)
                hy = -560 - local * 180
                heart(ctx, hx + math.sin(local * 6) * 12, hy, 22 * math.sin(min(1, local * 1.5) * math.pi / 2 + 0.2),
                      (0.95, 0.35, 0.45), alpha=1 - ease(max(0, local - 0.6) / 0.4))
    return draw


def zzz(t):
    def draw(ctx, p):
        for i in range(3):
            local = (t + i / 3) % 1
            x = 150 + local * 90 + math.sin(local * 5) * 10
            y = -470 - local * 260
            s = 18 + local * 26
            a = 1 - ease(max(0, local - 0.6) / 0.4)
            rgb(ctx, (0.35, 0.45, 0.75), a)
            ctx.set_line_width(7 + local * 3)
            ctx.set_line_join(cairo.LINE_JOIN_ROUND)
            ctx.set_line_cap(cairo.LINE_CAP_ROUND)
            ctx.move_to(x - s, y - s)
            ctx.line_to(x + s, y - s)
            ctx.line_to(x - s, y + s)
            ctx.line_to(x + s, y + s)
            ctx.stroke()
    return draw


def sweat(t=1.0, side=1):
    def draw(ctx, p):
        if t <= 0:
            return
        x, y = side * 170, -470 + 40 * t
        ctx.move_to(x, y - 30)
        ctx.curve_to(x + 22, y, x + 18, y + 26, x, y + 26)
        ctx.curve_to(x - 18, y + 26, x - 22, y, x, y - 30)
        fill_stroke(ctx, (0.62, 0.84, 1.0), 6)
    return draw


def dizzy(t):
    def draw(ctx, p):
        for i in range(3):
            a = t * 2 * math.pi * 1.5 + i * 2 * math.pi / 3
            star(ctx, math.cos(a) * 150, -590 + math.sin(a) * 34, 20, GOLD, points=5)
    return draw


def battery(level_blink):
    def draw(ctx, p):
        x, y = 190, -620
        round_rect(ctx, x - 60, y - 30, 120, 60, 10)
        fill_stroke(ctx, WHITE, 9)
        round_rect(ctx, x + 62, y - 12, 14, 24, 4)
        fill_stroke(ctx, WHITE, 7)
        if level_blink:
            round_rect(ctx, x - 48, y - 18, 22, 36, 5)
            rgb(ctx, (0.92, 0.28, 0.30))
            ctx.fill()
    return draw


def steam(t, side_count=2):
    def draw(ctx, p):
        for i in range(side_count):
            local = (t + i * 0.5) % 1
            for side in (-1, 1):
                x = side * (150 + local * 80)
                y = -540 - local * 120
                puff(ctx, x, y, 20 + local * 22, 1 - local)
    return draw


def puff(ctx, x, y, r, alpha):
    """A small cloud of three merged circles."""
    parts = ((x - r * 0.7, y + r * 0.15, r * 0.7), (x + r * 0.6, y + r * 0.2, r * 0.65), (x, y - r * 0.2, r * 0.85))
    ctx.push_group()
    for px, py, pr in parts:
        ellipse(ctx, px, py, pr + 5, pr + 5)
    rgb(ctx, (0.55, 0.57, 0.66))
    ctx.fill()
    for px, py, pr in parts:
        ellipse(ctx, px, py, pr - 2, pr - 2)
    rgb(ctx, (0.96, 0.96, 0.98))
    ctx.fill()
    ctx.pop_group_to_source()
    ctx.paint_with_alpha(alpha)


def notes(t):
    def draw(ctx, p):
        for i in range(2):
            local = (t + i * 0.5) % 1
            side = -1 if i == 0 else 1
            x = side * (180 + local * 40)
            y = -520 - local * 200
            a = 1 - ease(max(0, local - 0.6) / 0.4)
            rgb(ctx, INK, a)
            ellipse(ctx, x, y, 16, 12)
            ctx.fill()
            ctx.set_line_width(6)
            ctx.move_to(x + 14, y)
            ctx.line_to(x + 14, y - 58)
            ctx.line_to(x + 36, y - 46)
            ctx.stroke()
    return draw


def png_bytes(surface):
    """The frame as an 8-bit palette PNG with alpha: a third of the size, and the flat-coloured art does not band."""
    raw = io.BytesIO()
    surface.write_to_png(raw)
    raw.seek(0)
    image = Image.open(raw).convert('RGBA').quantize(256, method=Image.Quantize.FASTOCTREE)
    out = io.BytesIO()
    image.save(out, 'PNG', optimize=True)
    return out.getvalue()


def write_sequence(out, name, seq, render):
    folder = out / name
    folder.mkdir(parents=True, exist_ok=True)
    for old in folder.glob('*.png'):
        old.unlink()
    merged = []
    for pose, ms in seq:
        data = png_bytes(render(pose))
        if merged and merged[-1][0] == data:
            merged[-1][1] += ms
        else:
            merged.append([data, ms])
    for index, (data, ms) in enumerate(merged):
        (folder / f'_{index:03d}_{ms}.png').write_bytes(data)
    return len(merged), sum(ms for _, ms in merged)


def run(sequences, render, description):
    parser = argparse.ArgumentParser(description=description, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('out', type=Path, help='folder to write sequence folders into')
    parser.add_argument('names', nargs='*', help='sequences to draw (default: all)')
    args = parser.parse_args()
    names = args.names or list(sequences)
    unknown = [name for name in names if name not in sequences]
    if unknown:
        raise SystemExit(f'Unknown sequences: {", ".join(unknown)}')
    for name in names:
        count, total = write_sequence(args.out, name, sequences[name](), render)
        print(f'{name}: {count} frames, {total} ms')
