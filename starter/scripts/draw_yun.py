#!/usr/bin/env python3
"""Draw Yun, the little cultivator pet, frame by frame.

    pip install pycairo pillow
    python3 scripts/draw_yun.py OUT              # every sequence, as OUT/<sequence>/_NNN_<ms>.png
    python3 scripts/draw_yun.py OUT idle descend # only these sequences

Yun is original artwork made for Agent Pet in the style of xianxia (cultivation) stories and dedicated to the
public domain (licenses/YUN-ARTWORK-TERMS.md). Like Kuro (scripts/draw_kuro.py) it is drawn from vector shapes
with the shared kit in scripts/pet_art.py, so this script is the art's source: change it, render again, then copy
the frames in with scripts/add_sequences.py --pet yun.
"""
import math
import random
from dataclasses import dataclass, field, replace

import cairo

from pet_art import (CHEEK, CX, GROUND, INK, SIZE, WHITE, back_out, ease, ease_out, ellipse, fill_stroke,
                     heart, hearts_up, hop, lerp, mix, notes, osc, puff, rgb, ease_in, round_rect, run, sparkles, star, sweat,
                     zzz)

ROBE = (0.13, 0.12, 0.17)          # black robe
ROBE_SHADE = (0.07, 0.06, 0.10)
ROBE_LIGHT = (0.24, 0.22, 0.31)
TRIM = (0.72, 0.10, 0.16)          # deep crimson trim
TRIM_DARK = (0.45, 0.05, 0.10)
SASH = (0.55, 0.07, 0.12)
INNER = (0.92, 0.90, 0.94)         # the white inner collar
HAIR = (0.90, 0.91, 0.96)          # platinum hair
HAIR_LIGHT = (1.0, 1.0, 1.0)
HAIR_SHADE = (0.70, 0.71, 0.82)
SKIN = (0.99, 0.91, 0.86)
JADE = (0.20, 0.17, 0.26)          # obsidian jade
JADE_DARK = (0.08, 0.06, 0.12)
VERMILION = (0.92, 0.12, 0.20)
QI = (0.98, 0.22, 0.32)            # crimson qi
QI_DARK = (0.55, 0.25, 0.85)       # violet qi
BRONZE = (0.30, 0.28, 0.34)        # black iron cauldron
BRONZE_DARK = (0.16, 0.15, 0.20)
STEEL = (0.30, 0.31, 0.38)         # dark steel blade
EDGE = (1.0, 0.32, 0.38)
PAPER = (0.12, 0.11, 0.15)         # black talisman paper
PETAL = (0.90, 0.10, 0.20)         # red spider lily petals
GOLD = (0.92, 0.70, 0.30)
LINE = 13


@dataclass
class Pose:
    x: float = 0
    lift: float = 0
    squash: float = 1
    lean: float = 0            # degrees around the feet
    tilt: float = 0            # head tilt in degrees
    head_dy: float = 0
    sit: float = 0             # 1 sits cross-legged, the robe spread round
    eyes: str = 'open'
    eye_open: float = 1
    look: tuple = (0, 0)
    brows: str = 'sharp'
    blush: float = 0.12
    mouth: str = 'smirk'
    mouth_open: float = 0.5
    sleeve_l: float = 10       # degrees outward from hanging
    sleeve_r: float = 10
    hand_l: tuple = None       # a hand held in front, (x, y) in body space
    hand_r: tuple = None
    wind: float = 0.2
    flutter: float = 0
    drag: float = 0            # cloth pulled up (+) or down (-) by the body's motion; see follow_through
    hair_up: float = 0         # hair lifted by a surge of qi
    soot: float = 0            # scorched by lightning
    tint: float = 0
    pale: float = 0
    sword: float = 0           # 0..1, the flying sword under the feet
    trail: float = 0           # speed streaks behind the sword
    alpha: float = 1
    scale: float = 1
    face: int = 1
    shadow: bool = True
    back: list = field(default_factory=list)
    front: list = field(default_factory=list)
    over: list = field(default_factory=list)


# ---------------------------------------------------------------- Yun, in body space: feet at y = 0, up is negative

HEAD = (0, -378)
FACE_R = 148
NECK = (0, -235)
EYE_Y = -362
EYE_DX = 54


def ribbon(ctx, x, y, angle, length, width, wind, flutter, color, outline=True, taper=0.5, sway=1.0, drag=0.0):
    """A strip of cloth or hair starting at (x, y), heading at `angle` degrees (0 = right, 90 = down)."""
    n = 16
    a = math.radians(angle)
    pts = []
    for i in range(n + 1):
        u = i / n
        wave = math.sin(flutter * 2 * math.pi + u * 5) * 24 * u * sway
        pts.append((x + math.cos(a) * length * u - math.sin(a) * wave,
                    y + math.sin(a) * length * u + math.cos(a) * wave + (1 - wind) * 22 * u * u - drag * 90 * u * u))
    left, right = [], []
    for i, (px, py) in enumerate(pts):
        nx, ny = pts[min(i + 1, n)][0] - pts[max(i - 1, 0)][0], pts[min(i + 1, n)][1] - pts[max(i - 1, 0)][1]
        d = math.hypot(nx, ny) or 1
        w = width * (1 - taper * i / n) / 2
        left.append((px - ny / d * w, py + nx / d * w))
        right.append((px + ny / d * w, py - nx / d * w))
    ctx.move_to(*left[0])
    for q in left[1:]:
        ctx.line_to(*q)
    for q in reversed(right):
        ctx.line_to(*q)
    ctx.close_path()
    if outline:
        fill_stroke(ctx, color, LINE * 0.85)
    else:
        rgb(ctx, color)
        ctx.fill()


def draw_back_hair(ctx, p):
    angle = lerp(100, 170, p.wind) - 40 * p.hair_up
    ribbon(ctx, -40, -420 + p.head_dy, angle, 300, 92, p.wind, p.flutter, HAIR, taper=0.55, drag=p.drag)
    # Ribbon tied round the bun.
    bx, by = -30, -560 + p.head_dy
    ribbon(ctx, bx, by, lerp(130, 185, p.wind), 190, 30, p.wind, p.flutter + 0.2, TRIM, sway=1.3, drag=p.drag)
    ribbon(ctx, bx + 4, by + 8, lerp(115, 172, p.wind), 160, 26, p.wind, p.flutter + 0.5, ROBE, sway=1.3, drag=p.drag)


def draw_mantle(ctx, p):
    """A short black mantle with a crimson lining, streaming back from the shoulders."""
    angle = lerp(105, 172, p.wind)
    for dx, length, phase in ((-60, 196, 0.1), (-30, 170, 0.4)):
        ribbon(ctx, dx, -222, angle, length, 104, p.wind, p.flutter + phase, TRIM, taper=0.35, sway=0.8, drag=p.drag)
        ribbon(ctx, dx + 6, -226, angle - 3, length - 22, 82, p.wind, p.flutter + phase, ROBE, outline=False,
               taper=0.4, sway=0.8, drag=p.drag)


def draw_back_sword(ctx, p):
    """The sheathed sword slung across the back, hilt over the right shoulder; it is the one Yun flies on."""
    if p.sword > 0:
        return
    ctx.save()
    ctx.translate(30, -330)
    ctx.rotate(math.radians(36))
    round_rect(ctx, -26, -60, 52, 330, 22)
    fill_stroke(ctx, ROBE_SHADE, LINE * 0.8)
    ctx.rectangle(-26, -10, 52, 16)
    rgb(ctx, GOLD)
    ctx.fill()
    round_rect(ctx, -46, -80, 92, 24, 9)
    fill_stroke(ctx, GOLD, 8)
    round_rect(ctx, -15, -190, 30, 112, 12)
    fill_stroke(ctx, TRIM, 8)
    ellipse(ctx, 0, -198, 16, 16)
    fill_stroke(ctx, GOLD, 7)
    sw = math.sin(p.flutter * 2 * math.pi) * 10
    ctx.move_to(0, -210)
    ctx.curve_to(-20 + sw, -240, -30 + sw, -270, -24 + sw, -300)
    rgb(ctx, VERMILION)
    ctx.set_line_width(10)
    ctx.stroke()
    ctx.restore()


def robe_outline(ctx, p):
    if p.sit > 0.5:
        top, hem, half_top, half_hem = -170, -6, 92, 200
    else:
        top, hem, half_top, half_hem = -238, -14, 86, 150
    sway = math.sin(p.flutter * 2 * math.pi) * 10 * p.wind
    ctx.move_to(-half_top, top)
    ctx.curve_to(-half_top - 30, top + 80, -half_hem + 10 + sway, hem - 70, -half_hem + sway, hem)
    # A wavy hem.
    steps = 6
    for k in range(steps):
        x0 = -half_hem + sway + (2 * half_hem) * k / steps
        x1 = -half_hem + sway + (2 * half_hem) * (k + 1) / steps
        ctx.curve_to(x0 + (x1 - x0) * 0.3, hem + 14, x0 + (x1 - x0) * 0.7, hem + 14, x1, hem)
    ctx.curve_to(half_hem - 10 + sway, hem - 70, half_top + 30, top + 80, half_top, top)
    ctx.close_path()
    return top, hem, half_hem


def draw_robe(ctx, p):
    # Little shoes peeking out.
    if p.sit < 0.5:
        for side in (-1, 1):
            ellipse(ctx, side * 50, -10, 40, 18)
            fill_stroke(ctx, ROBE_SHADE, LINE * 0.8)
    top, hem, half_hem = robe_outline(ctx, p)
    grad = cairo.LinearGradient(-150, 0, 150, 0)
    robe = mix(ROBE, (0.45, 0.06, 0.10), p.tint * 0.6)
    grad.add_color_stop_rgb(0, *ROBE_SHADE)
    grad.add_color_stop_rgb(0.4, *mix(robe, ROBE_LIGHT, 0.7))
    grad.add_color_stop_rgb(1, *ROBE_SHADE)
    ctx.set_source(grad)
    ctx.fill_preserve()
    rgb(ctx, INK)
    ctx.set_line_width(LINE)
    ctx.stroke()
    # Trim along the hem.
    ctx.save()
    robe_outline(ctx, p)
    ctx.clip()
    ctx.rectangle(-260, hem - 30, 520, 60)
    rgb(ctx, TRIM)
    ctx.fill()
    # Cross collar.
    for width, color in ((34, INNER), (18, TRIM)):
        ctx.move_to(-62, top - 4)
        ctx.line_to(34, top + 92)
        ctx.move_to(62, top - 4)
        ctx.line_to(-8, top + 70)
        rgb(ctx, color)
        ctx.set_line_width(width)
        ctx.stroke()
    # Gold thread along the trim.
    ctx.move_to(-260, hem - 22)
    ctx.line_to(260, hem - 22)
    rgb(ctx, GOLD)
    ctx.set_line_width(4)
    ctx.stroke()
    # Sash.
    sash_y = top + 78
    ctx.rectangle(-200, sash_y, 400, 28)
    rgb(ctx, SASH)
    ctx.fill_preserve()
    rgb(ctx, INK)
    ctx.set_line_width(7)
    ctx.stroke()
    ctx.restore()
    # Jade pendant with a red tassel.
    px, py = 42, sash_y + 46
    ctx.move_to(px, sash_y + 20)
    ctx.line_to(px, py - 18)
    rgb(ctx, VERMILION)
    ctx.set_line_width(5)
    ctx.stroke()
    ellipse(ctx, px, py, 20, 20)
    fill_stroke(ctx, JADE, 7)
    ellipse(ctx, px, py, 6, 6)
    rgb(ctx, INK)
    ctx.fill()
    swing = math.sin(p.flutter * 2 * math.pi) * 8
    ctx.move_to(px - 8, py + 20)
    ctx.line_to(px - 10 + swing, py + 64)
    ctx.line_to(px + 10 + swing, py + 64)
    ctx.line_to(px + 8, py + 20)
    ctx.close_path()
    fill_stroke(ctx, VERMILION, 5)


def shoulder(side, p):
    return side * 78, (-212 if p.sit < 0.5 else -146)


def sleeve(ctx, side, sx, sy, ex, ey, hand=True):
    """A wide sleeve from the shoulder to the hand at (ex, ey)."""
    dx, dy = ex - sx, ey - sy
    d = math.hypot(dx, dy) or 1
    nx, ny = -dy / d, dx / d
    w0, w1 = 26, 62
    ctx.move_to(sx + nx * w0, sy + ny * w0)
    ctx.line_to(ex + nx * w1 + dx / d * 6, ey + ny * w1 + dy / d * 6)
    ctx.curve_to(ex + dx / d * 30, ey + dy / d * 30, ex + dx / d * 30, ey + dy / d * 30,
                 ex - nx * w1 + dx / d * 6, ey - ny * w1 + dy / d * 6)
    ctx.line_to(sx - nx * w0, sy - ny * w0)
    ctx.close_path()
    fill_stroke(ctx, ROBE, LINE * 0.9)
    ctx.move_to(ex + nx * (w1 - 6), ey + ny * (w1 - 6))
    ctx.line_to(ex - nx * (w1 - 6), ey - ny * (w1 - 6))
    rgb(ctx, TRIM)
    ctx.set_line_width(14)
    ctx.stroke()
    if hand:
        ellipse(ctx, ex + dx / d * 22, ey + dy / d * 22, 22, 22)
        fill_stroke(ctx, SKIN, LINE * 0.8)


def draw_sleeve_angle(ctx, side, angle, p):
    sx, sy = shoulder(side, p)
    a = math.radians(angle)
    sleeve(ctx, side, sx, sy, sx + side * math.sin(a) * 130, sy + math.cos(a) * 130)


def draw_sleeve_hand(ctx, side, pos, p):
    sx, sy = shoulder(side, p)
    sleeve(ctx, side, sx, sy, pos[0], pos[1])


def draw_head(ctx, p):
    hx, hy = HEAD
    # Bun with a gold ring and a jade hairpin.
    ellipse(ctx, 0, -548, 52, 46)
    fill_stroke(ctx, HAIR)
    ctx.move_to(-30, -520)
    ctx.curve_to(-10, -540, 10, -540, 30, -520)
    rgb(ctx, HAIR_SHADE)
    ctx.set_line_width(6)
    ctx.stroke()
    round_rect(ctx, -40, -512, 80, 22, 8)
    fill_stroke(ctx, GOLD, 7)
    ctx.set_line_cap(cairo.LINE_CAP_ROUND)
    ctx.move_to(-86, -566)
    ctx.line_to(82, -530)
    rgb(ctx, INK)
    ctx.set_line_width(20)
    ctx.stroke()
    ctx.move_to(-86, -566)
    ctx.line_to(82, -530)
    rgb(ctx, JADE)
    ctx.set_line_width(10)
    ctx.stroke()
    ellipse(ctx, 90, -528, 14, 14)
    fill_stroke(ctx, VERMILION, 6)

    # Side locks behind the face.
    lift = p.hair_up
    for side in (-1, 1):
        ctx.save()
        ctx.translate(side * 132, -430)
        ctx.rotate(side * (0.08 + 0.9 * lift))
        ctx.move_to(-22, 0)
        ctx.curve_to(-30, 70, -20, 130, side * 4, 170)
        ctx.curve_to(26, 120, 30, 60, 24, 0)
        ctx.close_path()
        fill_stroke(ctx, HAIR, LINE * 0.85)
        ctx.restore()

    skin = mix(mix(SKIN, (1.0, 0.68, 0.62), p.tint), (0.86, 0.90, 0.97), p.pale)
    ellipse(ctx, hx, hy, FACE_R + 4, FACE_R - 6)
    rgb(ctx, skin)
    ctx.fill_preserve()
    rgb(ctx, INK)
    ctx.set_line_width(LINE)
    ctx.stroke()
    if p.soot > 0:
        rnd = random.Random(4)
        for _ in range(7):
            ellipse(ctx, rnd.uniform(-110, 110), hy + rnd.uniform(-20, 90), rnd.uniform(14, 30), rnd.uniform(8, 16))
            rgb(ctx, (0.25, 0.24, 0.28), 0.55 * p.soot)
            ctx.fill()

    ctx.save()
    ellipse(ctx, hx, hy, FACE_R + 4, FACE_R - 6)
    ctx.clip()
    ellipse(ctx, hx, -430, 170, 40)
    rgb(ctx, (0.55, 0.40, 0.50), 0.16)
    ctx.fill()
    ctx.restore()
    # Hair cap with parted bangs.
    ctx.move_to(-156, -360)
    ctx.curve_to(-170, -480, -90, -535, 0, -535)
    ctx.curve_to(90, -535, 170, -480, 156, -360)
    # Sharp, pointed bangs parted at the centre so the mark shows.
    right = [(156, -360), (146, -322), (124, -420), (104, -370), (86, -440), (62, -384), (46, -452), (26, -402), (14, -464)]
    bang = right + [(-x, y) for x, y in reversed(right)]
    for x, y in bang:
        ctx.line_to(x, y)
    ctx.close_path()
    fill_stroke(ctx, HAIR)
    ctx.move_to(-90, -500)
    ctx.curve_to(-50, -520, -20, -522, 10, -520)
    rgb(ctx, HAIR_SHADE)
    ctx.set_line_width(9)
    ctx.stroke()
    if p.soot > 0.3:
        # Frazzled tufts.
        rgb(ctx, HAIR)
        for k in range(9):
            a = math.pi + k * math.pi / 8
            x, y = math.cos(a) * 150, -400 + math.sin(a) * 140
            ctx.move_to(x, y)
            ctx.line_to(x + math.cos(a + 0.3) * 50 * p.soot, y + math.sin(a + 0.3) * 50 * p.soot)
            ctx.line_to(x + math.cos(a) * 20, y + math.sin(a) * 20)
            ctx.close_path()
            fill_stroke(ctx, HAIR, 6)
    # Vermilion mark between the bangs.
    ctx.move_to(0, -458)
    ctx.curve_to(10, -440, 12, -426, 0, -414)
    ctx.curve_to(-12, -426, -10, -440, 0, -458)
    ctx.close_path()
    rgb(ctx, VERMILION)
    ctx.fill()

    if p.blush > 0:
        for side in (-1, 1):
            ellipse(ctx, side * 86, -322, 26, 12)
            rgb(ctx, CHEEK, 0.7 * min(1, p.blush))
            ctx.fill()
    lx, ly = p.look
    for side in (-1, 1):
        draw_eye(ctx, side * EYE_DX + lx, EYE_Y + ly, p.eyes, p, side)
    if p.brows:
        draw_brows(ctx, p)
    draw_mouth(ctx, p, lx * 0.4, -306 + ly * 0.3)


def draw_eye(ctx, x, y, kind, p, side):
    rx, ry = 19, 27
    o = p.eye_open
    ctx.set_line_cap(cairo.LINE_CAP_ROUND)
    ctx.set_line_join(cairo.LINE_JOIN_ROUND)
    if kind in ('open', 'glow') and o < 0.18:
        kind = 'line'
    rgb(ctx, INK)
    if kind in ('open', 'glow'):
        almond_eye(ctx, x, y, side, o, kind == 'glow', p.look)
    elif kind == 'line':
        ctx.move_to(x - 20, y + 6)
        ctx.line_to(x + 20, y + 6)
        ctx.set_line_width(8)
        ctx.stroke()
    elif kind == 'happy':
        ctx.move_to(x - 22, y + 10)
        ctx.curve_to(x - 12, y - 16, x + 12, y - 16, x + 22, y + 10)
        ctx.set_line_width(9)
        ctx.stroke()
    elif kind == 'closed':
        ctx.move_to(x - 22, y)
        ctx.curve_to(x - 12, y + 16, x + 12, y + 16, x + 22, y)
        ctx.set_line_width(8)
        ctx.stroke()
        ctx.move_to(x + side * 20, y + 2)
        ctx.line_to(x + side * 28, y - 4)
        ctx.set_line_width(5)
        ctx.stroke()
    elif kind == 'squeeze':
        ctx.move_to(x - 18 * side, y - 14)
        ctx.line_to(x + 14 * side, y)
        ctx.line_to(x - 18 * side, y + 14)
        ctx.set_line_width(8)
        ctx.stroke()
    elif kind == 'spiral':
        ctx.set_line_width(5)
        for i in range(60):
            a = i * 0.33 + p.flutter * 6
            r = 2 + i * 0.36
            (ctx.move_to if i == 0 else ctx.line_to)(x + math.cos(a) * r * side, y + math.sin(a) * r)
        ctx.stroke()
    elif kind == 'wide':
        ellipse(ctx, x, y, 24, 28)
        fill_stroke(ctx, WHITE, 6)
        rgb(ctx, INK)
        ellipse(ctx, x, y + 2, 8, 9)
        ctx.fill()
    elif kind == 'heart':
        heart(ctx, x, y - 2, 21, (0.93, 0.27, 0.38))
    elif kind == 'star':
        star(ctx, x, y, 25, GOLD, rot=0.1 * side)
    elif kind == 'sleepy':
        ctx.save()
        ctx.rectangle(x - 40, y + ry - 2 * ry * o, 80, 80)
        ctx.clip()
        ellipse(ctx, x, y, rx, ry)
        ctx.fill()
        ctx.restore()
        ctx.move_to(x - rx - 6, y + ry - 2 * ry * o)
        ctx.line_to(x + rx + 6, y + ry - 2 * ry * o)
        ctx.set_line_width(7)
        ctx.stroke()


def almond_eye(ctx, x, y, side, o, glowing, look):
    """A long, sharp eye: white, a crimson slit-pupil iris and a heavy upper lid that flicks at the outer corner."""
    ix, iy = x - side * 27, y + 6
    ox, oy = x + side * 31, y - 10
    top, low = 27 * o, 13 * o

    def outline():
        ctx.move_to(ix, iy)
        ctx.curve_to(ix + side * 14, y - top, ox - side * 14, y - top - 6, ox, oy)
        ctx.curve_to(ox - side * 10, y + low + 4, ix + side * 12, y + low + 6, ix, iy)
        ctx.close_path()

    outline()
    rgb(ctx, WHITE)
    ctx.fill()
    ctx.save()
    outline()
    ctx.clip()
    cx, cy = x + look[0] * 0.3 + side * 2, y - 2
    iris = cairo.LinearGradient(0, cy - 20, 0, cy + 20)
    iris.add_color_stop_rgb(0, *TRIM_DARK)
    iris.add_color_stop_rgb(1, *(QI if not glowing else mix(QI, WHITE, 0.4)))
    ellipse(ctx, cx, cy, 15, 21)
    ctx.set_source(iris)
    ctx.fill()
    ellipse(ctx, cx, cy, 3.5, 14)
    rgb(ctx, INK)
    ctx.fill()
    ellipse(ctx, cx - 5, cy - 8, 4.5, 5)
    rgb(ctx, WHITE)
    ctx.fill()
    ctx.restore()
    if glowing:
        glow(ctx, x, y, 56, QI, 0.55)
    # Heavy upper lid with a flick, thin lower lid.
    ctx.move_to(ix - side * 2, iy + 2)
    ctx.curve_to(ix + side * 14, y - top - 2, ox - side * 14, y - top - 8, ox, oy)
    ctx.line_to(ox + side * 15, oy - 9)
    rgb(ctx, INK)
    ctx.set_line_width(10)
    ctx.stroke()
    ctx.move_to(ix + side * 6, iy + 2)
    ctx.curve_to(ix + side * 14, y + low + 6, ox - side * 12, y + low + 4, ox - side * 4, oy + 4)
    ctx.set_line_width(4)
    ctx.stroke()


def draw_brows(ctx, p):
    rgb(ctx, INK)
    ctx.set_line_width(8)
    ctx.set_line_cap(cairo.LINE_CAP_ROUND)
    for side in (-1, 1):
        bx, by = side * EYE_DX + p.look[0] * 0.5, -404
        if p.brows == 'angry':
            ctx.move_to(bx - side * 24, by - 10)
            ctx.line_to(bx + side * 4, by + 6)
        elif p.brows == 'worried':
            ctx.move_to(bx - side * 24, by + 6)
            ctx.line_to(bx + side * 16, by - 6)
        elif p.brows == 'sharp':
            ctx.move_to(bx + side * 26, by - 10)
            ctx.line_to(bx - side * 20, by + 2)
        elif p.brows == 'calm':
            ctx.move_to(bx - 20, by - 2)
            ctx.curve_to(bx - 6, by - 8, bx + 6, by - 8, bx + 20, by - 2)
        ctx.stroke()


def draw_mouth(ctx, p, x, y):
    rgb(ctx, INK)
    ctx.set_line_width(7)
    ctx.set_line_cap(cairo.LINE_CAP_ROUND)
    o = p.mouth_open
    if p.mouth == 'smirk':
        ctx.move_to(x - 12, y + 1)
        ctx.curve_to(x - 2, y + 4, x + 8, y + 3, x + 15, y - 6)
        ctx.stroke()
    elif p.mouth == 'smile':
        ctx.move_to(x - 15, y - 3)
        ctx.curve_to(x - 6, y + 7, x + 6, y + 7, x + 15, y - 3)
        ctx.stroke()
    elif p.mouth == 'cat':
        ctx.move_to(x - 18, y - 3)
        ctx.curve_to(x - 14, y + 7, x - 3, y + 7, x, y - 1)
        ctx.curve_to(x + 3, y + 7, x + 14, y + 7, x + 18, y - 3)
        ctx.stroke()
    elif p.mouth == 'flat':
        ctx.move_to(x - 12, y)
        ctx.line_to(x + 12, y)
        ctx.stroke()
    elif p.mouth == 'frown':
        ctx.move_to(x - 15, y + 5)
        ctx.curve_to(x - 6, y - 6, x + 6, y - 6, x + 15, y + 5)
        ctx.stroke()
    elif p.mouth == 'wavy':
        ctx.move_to(x - 18, y)
        ctx.curve_to(x - 10, y - 8, x - 4, y + 8, x, y)
        ctx.curve_to(x + 4, y - 8, x + 10, y + 8, x + 18, y)
        ctx.stroke()
    elif p.mouth in ('open', 'o', 'yawn'):
        w = {'open': 18, 'o': 9, 'yawn': 16}[p.mouth] * (0.5 + o)
        h = {'open': 14, 'o': 9, 'yawn': 24}[p.mouth] * (0.4 + o)
        ellipse(ctx, x, y + h * 0.3, w, h)
        fill_stroke(ctx, (0.60, 0.20, 0.26), 6)
        if p.mouth != 'o':
            ellipse(ctx, x, y + h * 0.9, w * 0.55, h * 0.4)
            rgb(ctx, (1.0, 0.58, 0.62))
            ctx.fill()
    elif p.mouth == 'chew':
        ctx.move_to(x - 12, y)
        ctx.line_to(x - 4, y + 6 * o)
        ctx.line_to(x + 4, y)
        ctx.line_to(x + 12, y + 6 * o)
        ctx.stroke()


def flying_sword(ctx, p):
    if p.sword <= 0:
        return
    s = ease(p.sword)
    ctx.save()
    ctx.translate(0, 18)
    ctx.scale(s, 1)
    if p.trail > 0:
        g = cairo.LinearGradient(-520, 0, -160, 0)
        g.add_color_stop_rgba(0, *QI, 0)
        g.add_color_stop_rgba(1, *QI, 0.55 * p.trail)
        ctx.set_source(g)
        ctx.move_to(-520, -10)
        ctx.line_to(-160, -16)
        ctx.line_to(-160, 16)
        ctx.line_to(-520, 10)
        ctx.close_path()
        ctx.fill()
    # Blade.
    ctx.move_to(-150, -13)
    ctx.line_to(200, -11)
    ctx.line_to(250, 0)
    ctx.line_to(200, 11)
    ctx.line_to(-150, 13)
    ctx.close_path()
    fill_stroke(ctx, STEEL, LINE * 0.8)
    glow(ctx, 40, 0, 0, EDGE, 0)
    ctx.move_to(-140, 0)
    ctx.line_to(215, 0)
    rgb(ctx, EDGE)
    ctx.set_line_width(5)
    ctx.stroke()
    # Guard, grip and tassel.
    round_rect(ctx, -170, -32, 22, 64, 8)
    fill_stroke(ctx, GOLD, 7)
    round_rect(ctx, -250, -11, 82, 22, 9)
    fill_stroke(ctx, TRIM, 7)
    ellipse(ctx, -258, 0, 12, 12)
    fill_stroke(ctx, GOLD, 6)
    sw = math.sin(p.flutter * 2 * math.pi) * 14
    ctx.move_to(-266, 4)
    ctx.curve_to(-290, 30, -300 + sw, 50, -310 + sw, 72)
    rgb(ctx, VERMILION)
    ctx.set_line_width(9)
    ctx.stroke()
    ctx.restore()


def draw_yun(ctx, p):
    ctx.save()
    ctx.translate(CX + p.x, GROUND - p.lift)
    ctx.scale(p.face * p.scale / max(p.squash, 0.2) ** 0.45, p.scale * p.squash)
    ctx.rotate(math.radians(p.lean) * p.face)
    for prop in p.back:
        prop(ctx, p)
    flying_sword(ctx, p)
    sit_drop = 0
    ctx.save()
    ctx.translate(0, sit_drop)
    ctx.save()
    ctx.translate(NECK[0], NECK[1] + p.head_dy + (66 if p.sit > 0.5 else 0))
    ctx.rotate(math.radians(p.tilt))
    ctx.translate(-NECK[0], -NECK[1])
    draw_back_hair(ctx, p)
    ctx.restore()
    if p.sit < 0.5:
        draw_mantle(ctx, p)
    draw_back_sword(ctx, p)
    if p.hand_l is None:
        draw_sleeve_angle(ctx, -1, p.sleeve_l, p)
    if p.hand_r is None:
        draw_sleeve_angle(ctx, 1, p.sleeve_r, p)
    draw_robe(ctx, p)
    ctx.save()
    ctx.translate(NECK[0], NECK[1] + p.head_dy + (66 if p.sit > 0.5 else 0))
    ctx.rotate(math.radians(p.tilt))
    ctx.translate(-NECK[0], -NECK[1])
    draw_head(ctx, p)
    ctx.restore()
    if p.hand_l is not None:
        draw_sleeve_hand(ctx, -1, p.hand_l, p)
    if p.hand_r is not None:
        draw_sleeve_hand(ctx, 1, p.hand_r, p)
    for prop in p.front:
        prop(ctx, p)
    ctx.restore()
    ctx.restore()


def render(p):
    surface = cairo.ImageSurface(cairo.FORMAT_ARGB32, SIZE, SIZE)
    ctx = cairo.Context(surface)
    ctx.set_line_join(cairo.LINE_JOIN_ROUND)
    if p.alpha > 0 and p.scale > 0.01:
        if p.shadow:
            k = 1 / (1 + p.lift / 220)
            ellipse(ctx, CX + p.x, GROUND + 2, 165 * p.scale * k * (1 + 0.3 * p.sit), 20 * k)
            rgb(ctx, (0, 0, 0), 0.15 * k * p.alpha)
            ctx.fill()
        ctx.push_group()
        draw_yun(ctx, p)
        ctx.pop_group_to_source()
        ctx.paint_with_alpha(p.alpha)
    for prop in p.over:
        prop(ctx, p)
    return surface


# ---------------------------------------------------------------- props (body space unless noted)

def glow(ctx, x, y, r, color, alpha):
    g = cairo.RadialGradient(x, y, 1, x, y, r)
    g.add_color_stop_rgba(0, *color, alpha)
    g.add_color_stop_rgba(1, *color, 0)
    ctx.set_source(g)
    ctx.arc(x, y, r, 0, 2 * math.pi)
    ctx.fill()


def qi_orbs(t, layer, count=5, radius=250, cy=-300, color=QI, appear=1.0, gather=0.0):
    """Orbs circling Yun; `layer` 'back' draws the far half, 'front' the near half. `appear` fades them in one
    after another; `gather` spirals them into the body."""
    def draw(ctx, p):
        for i in range(count):
            grow = max(0.0, min(1.0, appear * count - i))
            if grow <= 0:
                continue
            a = 2 * math.pi * (t + i / count) + gather * 3
            depth = math.sin(a)
            if (depth < 0) != (layer == 'back'):
                continue
            rad = radius * (1 - gather)
            x, y = math.cos(a) * rad, lerp(cy, -320, gather) + depth * 60 * (1 - gather)
            r = (15 + 6 * depth) * back_out(grow) * (1 - 0.6 * gather)
            if r <= 0.5:
                continue
            for k in range(1, 5):
                b = a - k * 0.12
                glow(ctx, math.cos(b) * rad, lerp(cy, -320, gather) + math.sin(b) * 60 * (1 - gather),
                     r * (1 - k * 0.15) * 1.6, color, 0.25 - k * 0.05)
            glow(ctx, x, y, r * 2.6, color, 0.45)
            ellipse(ctx, x, y, r, r)
            rgb(ctx, mix(color, WHITE, 0.6))
            ctx.fill()
    return draw


def formation(t, strength=1.0, color=QI, radius=260):
    """A glowing ring on the ground, turning."""
    def draw(ctx, p):
        if strength <= 0:
            return
        ctx.save()
        ctx.translate(0, -4 + p.lift)
        ctx.scale(1, 0.24)
        for r, w in ((radius, 8), (radius * 0.78, 5)):
            ctx.arc(0, 0, r * strength, 0, 2 * math.pi)
            rgb(ctx, color, 0.75 * strength)
            ctx.set_line_width(w / 0.24 * 0.4)
            ctx.stroke()
        for k in range(12):
            a = 2 * math.pi * (k / 12 + t * 0.25)
            r0, r1 = radius * 0.8 * strength, radius * 0.97 * strength
            ctx.move_to(math.cos(a) * r0, math.sin(a) * r0)
            ctx.line_to(math.cos(a + 0.1) * r1, math.sin(a + 0.1) * r1)
            rgb(ctx, color, 0.9 * strength)
            ctx.set_line_width(14)
            ctx.stroke()
        ctx.restore()
    return draw


def lotus(bloom):
    """Petals open one after another, outer ones first, each with a small overshoot."""
    def draw(ctx, p):
        if bloom <= 0:
            return
        for k, (a, s) in enumerate(((-70, 0.8), (70, 0.8), (-40, 1), (40, 1), (0, 1.1))):
            b = back_out(max(0.0, min(1.0, bloom * 1.8 - k * 0.2)))
            if b <= 0.01:
                continue
            ctx.save()
            ctx.translate(0, -8)
            ctx.rotate(math.radians(a))
            ctx.scale(b * s, b * s)
            ctx.move_to(0, 0)
            ctx.curve_to(-46, -40, -30, -120, 0, -150)
            ctx.curve_to(30, -120, 46, -40, 0, 0)
            fill_stroke(ctx, PETAL if k < 4 else mix(PETAL, WHITE, 0.4), 7)
            ctx.restore()
    return draw


def jade_slip(x, y, angle=0.0, light=0.5):
    def draw(ctx, p):
        ctx.save()
        ctx.translate(x, y)
        ctx.rotate(angle)
        glow(ctx, 0, 0, 110, QI, 0.4 * light)
        round_rect(ctx, -34, -60, 68, 120, 12)
        fill_stroke(ctx, mix(JADE, WHITE, 0.25), 8)
        rgb(ctx, QI)
        ctx.set_line_width(5)
        for k in range(4):
            ctx.move_to(-16 + k * 11, -40)
            ctx.line_to(-16 + k * 11, 40)
            ctx.stroke()
        ctx.restore()
    return draw


def glyph_stream(t, x0, y0, x1, y1, count=5):
    """Little glowing glyphs drifting from (x0, y0) into (x1, y1)."""
    def draw(ctx, p):
        rnd = random.Random(7)
        for i in range(count):
            u = (t + i / count) % 1
            x = lerp(x0, x1, u) + math.sin(u * 7 + i) * 18
            y = lerp(y0, y1, u)
            a = math.sin(u * math.pi)
            s = 12
            ctx.save()
            ctx.translate(x, y)
            ctx.rotate(rnd.uniform(-0.4, 0.4))
            rgb(ctx, mix(QI, WHITE, 0.2), a)
            ctx.set_line_width(5)
            ctx.move_to(-s, -s)
            ctx.line_to(s, -s)
            ctx.move_to(0, -s)
            ctx.line_to(0, s)
            ctx.move_to(-s, s * 0.2)
            ctx.line_to(s, s * 0.4)
            ctx.stroke()
            ctx.restore()
    return draw


def bamboo_scroll(open_=1.0):
    def draw(ctx, p):
        w = lerp(40, 260, ease(open_))
        y = -168
        n = max(2, int(w // 26))
        for k in range(n):
            x = -w / 2 + k * w / n
            round_rect(ctx, x, y - 70, w / n - 3, 140, 6)
            fill_stroke(ctx, (0.86, 0.80, 0.55), 5)
            rgb(ctx, INK, 0.6)
            ctx.set_line_width(4)
            for j in range(4):
                ctx.move_to(x + w / n / 2 - 1, y - 52 + j * 30)
                ctx.line_to(x + w / n / 2 - 1, y - 40 + j * 30)
                ctx.stroke()
    return draw


def cauldron(t, rise=1.0, fire=1.0, pill=0.0, shake=0.0, pill_stretch=0.0):
    """A bronze alchemy cauldron in front of Yun, fire beneath and wisps above."""
    def draw(ctx, p):
        if rise <= 0:
            return
        ctx.save()
        ctx.translate(shake, (1 - ease(min(rise, 1.0))) * 240 - 30 * max(0.0, rise - 1))
        ctx.rectangle(-400, -600, 800, 600)
        ctx.clip()
        # Fire.
        if fire > 0:
            for k, (x, h) in enumerate(((-50, 70), (0, 96), (50, 70))):
                fl = h * fire * (0.85 + 0.15 * math.sin(t * 2 * math.pi * 3 + k))
                ctx.move_to(x - 26, -8)
                ctx.curve_to(x - 30, -8 - fl * 0.5, x - 6, -8 - fl * 0.7, x, -8 - fl)
                ctx.curve_to(x + 6, -8 - fl * 0.7, x + 30, -8 - fl * 0.5, x + 26, -8)
                ctx.close_path()
                fill_stroke(ctx, (1.0, 0.55, 0.25), 6)
                ctx.move_to(x - 12, -8)
                ctx.curve_to(x - 12, -8 - fl * 0.3, x, -8 - fl * 0.5, x, -8 - fl * 0.6)
                ctx.curve_to(x, -8 - fl * 0.5, x + 12, -8 - fl * 0.3, x + 12, -8)
                ctx.close_path()
                rgb(ctx, GOLD)
                ctx.fill()
        # Legs and belly.
        for side in (-1, 1):
            ctx.move_to(side * 100, -90)
            ctx.line_to(side * 120, -4)
            rgb(ctx, INK)
            ctx.set_line_width(30)
            ctx.stroke()
            ctx.move_to(side * 100, -90)
            ctx.line_to(side * 120, -4)
            rgb(ctx, BRONZE_DARK)
            ctx.set_line_width(14)
            ctx.stroke()
        ctx.move_to(-140, -210)
        ctx.curve_to(-160, -120, -110, -70, 0, -70)
        ctx.curve_to(110, -70, 160, -120, 140, -210)
        ctx.close_path()
        grad = cairo.LinearGradient(-150, 0, 150, 0)
        grad.add_color_stop_rgb(0, *BRONZE_DARK)
        grad.add_color_stop_rgb(0.4, *BRONZE)
        grad.add_color_stop_rgb(1, *BRONZE_DARK)
        ctx.set_source(grad)
        ctx.fill_preserve()
        rgb(ctx, INK)
        ctx.set_line_width(LINE)
        ctx.stroke()
        round_rect(ctx, -160, -230, 320, 30, 12)
        fill_stroke(ctx, BRONZE, LINE * 0.9)
        for side in (-1, 1):
            round_rect(ctx, side * 130 - 16, -280, 32, 54, 10)
            fill_stroke(ctx, BRONZE, 8)
        # A cloud pattern on the belly.
        ctx.move_to(-60, -140)
        ctx.curve_to(-60, -170, -20, -170, -20, -140)
        ctx.curve_to(-20, -170, 20, -170, 20, -140)
        ctx.curve_to(20, -170, 60, -170, 60, -140)
        rgb(ctx, BRONZE_DARK)
        ctx.set_line_width(7)
        ctx.stroke()
        ctx.restore()
        # Wisps.
        if rise >= 1:
            for i in range(2):
                u = (t + i * 0.5) % 1
                puff(ctx, (-150 + 300 * i) + math.sin(u * 6) * 16, -250 - u * 150, 18 + u * 26, (1 - u) * 0.9)
        if pill > 0:
            k = ease_out(min(1, pill * 1.3))
            x, y = 210 * k, -250 - 300 * k
            glow(ctx, x, y, 90, QI, 0.6 * pill)
            ctx.save()
            ctx.translate(x, y)
            ctx.rotate(math.atan2(-300, 210) + math.pi / 2)
            ellipse(ctx, 0, 0, 24 * (1 - 0.3 * pill_stretch), 24 * (1 + 0.6 * pill_stretch))
            ctx.restore()
            fill_stroke(ctx, mix(QI, WHITE, 0.4), 7)
            star(ctx, x + 46, y - 30, 14 * pill, WHITE, outline=False, points=4, inner=0.3)
    return draw


def fan_flame(phase):
    """Speed marks by the hand fanning qi into the fire."""
    def draw(ctx, p):
        rgb(ctx, QI, 0.8)
        ctx.set_line_width(7)
        ctx.set_line_cap(cairo.LINE_CAP_ROUND)
        for k in range(3):
            u = (phase + k / 3) % 1
            ctx.move_to(170 - u * 60, -230 + k * 26 + u * 70)
            ctx.line_to(140 - u * 60, -220 + k * 26 + u * 80)
            ctx.stroke()
    return draw


def talisman(x, y, angle=0.0, light=0.6, size=1.0, mark='!'):
    def draw(ctx, p):
        if size <= 0.01:
            return
        ctx.save()
        ctx.translate(x, y)
        ctx.rotate(angle)
        ctx.scale(size, size)
        glow(ctx, 0, 0, 130, QI, 0.35 * light)
        round_rect(ctx, -44, -84, 88, 168, 6)
        fill_stroke(ctx, PAPER, 8)
        rgb(ctx, VERMILION)
        ctx.set_line_width(7)
        ctx.set_line_cap(cairo.LINE_CAP_ROUND)
        if mark == '!':
            ctx.set_line_width(16)
            ctx.move_to(0, -52)
            ctx.line_to(0, 14)
            ctx.stroke()
            ellipse(ctx, 0, 44, 10, 10)
            ctx.fill()
        else:
            ctx.move_to(-24, -56)
            ctx.line_to(24, -56)
            ctx.move_to(0, -66)
            ctx.line_to(0, 50)
            ctx.move_to(-22, -10)
            ctx.curve_to(-6, 4, 6, -24, 22, -10)
            ctx.move_to(-20, 30)
            ctx.line_to(20, 40)
            ctx.stroke()
        ctx.restore()
    return draw


def storm(t, strike):
    """A dark cloud overhead and, when strike > 0, a bolt down onto Yun (canvas space)."""
    def draw(ctx, p):
        cx, cy = CX + p.x, GROUND - 720
        darkness = min(1, t * 3)
        ctx.push_group()
        for dx, dy, r in ((-90, 10, 70), (0, -20, 90), (90, 10, 72), (-40, 40, 60), (50, 40, 62)):
            ellipse(ctx, cx + dx, cy + dy, r + 6, r * 0.8 + 6)
        rgb(ctx, INK)
        ctx.fill()
        for dx, dy, r in ((-90, 10, 70), (0, -20, 90), (90, 10, 72), (-40, 40, 60), (50, 40, 62)):
            ellipse(ctx, cx + dx, cy + dy, r, r * 0.8)
        rgb(ctx, (0.36, 0.36, 0.46))
        ctx.fill()
        ctx.pop_group_to_source()
        ctx.paint_with_alpha(darkness)
        if strike > 0:
            glow(ctx, cx, GROUND - 450, 420, (0.85, 0.9, 1.0), 0.45 * strike)
            pts = [(cx, cy + 50), (cx - 40, cy + 140), (cx + 20, cy + 150), (cx - 30, cy + 260), (cx + 30, cy + 250),
                   (cx - 10, GROUND - 500)]
            for width, color in ((34, INK), (20, (1.0, 0.95, 0.55)), (8, WHITE)):
                ctx.move_to(*pts[0])
                for q in pts[1:]:
                    ctx.line_to(*q)
                rgb(ctx, color, strike)
                ctx.set_line_width(width)
                ctx.set_line_join(cairo.LINE_JOIN_MITER)
                ctx.stroke()
            ctx.set_line_join(cairo.LINE_JOIN_ROUND)
    return draw


def smoke_wisps(t, strength=1.0):
    def draw(ctx, p):
        for i in range(3):
            u = (t + i / 3) % 1
            puff(ctx, -80 + i * 80 + math.sin(u * 5 + i) * 20, -560 - u * 150, 16 + u * 22, (1 - u) * strength)
    return draw


def petals(t, seed=9, amount=18, area=420):
    def draw(ctx, p):
        rnd = random.Random(seed)
        for i in range(amount):
            x0 = rnd.uniform(-area, area)
            y0 = rnd.uniform(-900, -600)
            speed = rnd.uniform(380, 560)
            y = y0 + speed * t
            if y > -20:
                continue
            ctx.save()
            ctx.translate(CX + p.x + x0 + math.sin(t * 5 + i) * 34, GROUND + y)
            ctx.rotate(t * rnd.uniform(-6, 6) + i)
            ctx.scale(1, 0.55 + 0.45 * math.sin(t * 9 + i))
            ctx.move_to(0, -16)
            ctx.curve_to(14, -10, 12, 10, 0, 16)
            ctx.curve_to(-12, 10, -14, -10, 0, -16)
            fill_stroke(ctx, PETAL, 3, ink=TRIM_DARK)
            ctx.restore()
    return draw


def light_pillar(strength):
    def draw(ctx, p):
        if strength <= 0:
            return
        x = 0
        g = cairo.LinearGradient(x - 220, 0, x + 220, 0)
        g.add_color_stop_rgba(0, *QI, 0)
        g.add_color_stop_rgba(0.5, *mix(QI, WHITE, 0.35), 0.55 * strength)
        g.add_color_stop_rgba(1, *QI, 0)
        ctx.set_source(g)
        ctx.rectangle(x - 220, -GROUND - p.lift, 440, GROUND + p.lift)
        ctx.fill()
    return draw


def aura(t, color, strength=1.0):
    """Flickering flame-shaped aura behind Yun."""
    def draw(ctx, p):
        if strength <= 0:
            return
        for k in range(9):
            a = math.pi + k * math.pi / 8
            h = (130 + 50 * math.sin(t * 2 * math.pi * 2 + k * 1.7)) * strength
            x, y = math.cos(a) * 190, -300 + math.sin(a) * 260
            ctx.move_to(x - 40, y + 30)
            ctx.curve_to(x - 40, y - h * 0.4, x, y - h * 0.6, x + math.cos(a) * 10, y - h)
            ctx.curve_to(x + 10, y - h * 0.6, x + 40, y - h * 0.4, x + 40, y + 30)
            ctx.close_path()
            rgb(ctx, color, 0.45 * strength)
            ctx.fill()
        glow(ctx, 0, -320, 320, color, 0.35 * strength)
    return draw


def butterfly(ctx, x, y, flap, color, size=1.0, angle=0.0):
    ctx.save()
    ctx.translate(x, y)
    ctx.rotate(angle)
    ctx.scale(size, size)
    w = 0.25 + 0.75 * abs(math.cos(flap * 2 * math.pi))
    for side in (-1, 1):
        ctx.save()
        ctx.scale(side * w, 1)
        ctx.move_to(0, 0)
        ctx.curve_to(18, -40, 50, -34, 42, -6)
        ctx.curve_to(40, 8, 14, 6, 0, 0)
        ctx.curve_to(12, 12, 36, 18, 26, 34)
        ctx.curve_to(16, 42, 4, 24, 0, 0)
        fill_stroke(ctx, color, 5)
        ctx.restore()
    ctx.move_to(0, -10)
    ctx.line_to(0, 22)
    rgb(ctx, INK)
    ctx.set_line_width(6)
    ctx.stroke()
    ctx.restore()


def butterflies(t, count, gather=0.0, seed=13):
    """Butterflies scattering outwards (gather 0) or flying back together (gather 1), in body space."""
    colors = [QI, PETAL, QI_DARK, (0.30, 0.27, 0.38), GOLD]

    def draw(ctx, p):
        # Drawn in canvas space (as an `over` prop) so it shows while Yun is gone.
        ctx.save()
        ctx.translate(CX + p.x, GROUND - p.lift)
        rnd = random.Random(seed)
        for i in range(count):
            a = rnd.uniform(0, 2 * math.pi)
            r = rnd.uniform(160, 330)
            home = (rnd.uniform(-90, 90), rnd.uniform(-480, -120))
            away = (math.cos(a) * r, -330 + math.sin(a) * r * 0.8 - 80 * t)
            u = gather
            x = lerp(home[0], away[0], 1 - u) + math.sin(t * 6 + i) * 14
            y = lerp(home[1], away[1], 1 - u) + math.cos(t * 5 + i) * 10
            butterfly(ctx, x, y, t * 4 + i * 0.3, colors[i % len(colors)], size=0.9, angle=math.sin(t * 3 + i) * 0.4)
        ctx.restore()
    return draw


def flute(x0, y0, x1, y1):
    def draw(ctx, p):
        ctx.set_line_cap(cairo.LINE_CAP_ROUND)
        ctx.move_to(x0, y0)
        ctx.line_to(x1, y1)
        rgb(ctx, INK)
        ctx.set_line_width(30)
        ctx.stroke()
        ctx.move_to(x0, y0)
        ctx.line_to(x1, y1)
        rgb(ctx, JADE)
        ctx.set_line_width(16)
        ctx.stroke()
        for k in range(4):
            u = 0.35 + k * 0.13
            ellipse(ctx, lerp(x0, x1, u), lerp(y0, y1, u), 4, 4)
            rgb(ctx, INK)
            ctx.fill()
        ctx.move_to(lerp(x0, x1, 0.95), lerp(y0, y1, 0.95))
        ctx.line_to(lerp(x0, x1, 0.95) + 6, lerp(y0, y1, 0.95) + 50)
        rgb(ctx, VERMILION)
        ctx.set_line_width(6)
        ctx.stroke()
    return draw


def peach(bites, x, y):
    def draw(ctx, p):
        if bites >= 3:
            return
        ctx.save()
        ctx.translate(x, y)
        ctx.move_to(0, -48)
        ctx.curve_to(40, -50, 56, 0, 30, 30)
        ctx.curve_to(16, 44, -16, 44, -30, 30)
        ctx.curve_to(-56, 0, -40, -50, 0, -48)
        ctx.close_path()
        g = cairo.LinearGradient(0, -50, 0, 44)
        g.add_color_stop_rgb(0, 1.0, 0.55, 0.62)
        g.add_color_stop_rgb(1, 1.0, 0.86, 0.66)
        ctx.set_source(g)
        ctx.fill_preserve()
        rgb(ctx, INK)
        ctx.set_line_width(7)
        ctx.stroke()
        ctx.move_to(0, -48)
        ctx.curve_to(-8, -20, -6, 10, 4, 36)
        rgb(ctx, (0.85, 0.40, 0.48))
        ctx.set_line_width(5)
        ctx.stroke()
        ctx.move_to(0, -48)
        ctx.curve_to(-30, -76, -56, -64, -50, -50)
        ctx.curve_to(-30, -40, -12, -46, 0, -48)
        fill_stroke(ctx, JADE, 5)
        for b in range(bites):
            ellipse(ctx, 28 - b * 24, -38 + b * 4, 22, 20)
            ctx.set_operator(cairo.OPERATOR_CLEAR)
            ctx.fill()
            ctx.set_operator(cairo.OPERATOR_OVER)
        ctx.restore()
    return draw


def tea_cup(level, x, y, tilt=0.0):
    def draw(ctx, p):
        ctx.save()
        ctx.translate(x, y)
        ctx.rotate(tilt)
        ctx.move_to(-44, -30)
        ctx.line_to(44, -30)
        ctx.curve_to(40, 14, 24, 30, 0, 30)
        ctx.curve_to(-24, 30, -40, 14, -44, -30)
        ctx.close_path()
        fill_stroke(ctx, (0.80, 0.92, 0.86), 7)
        if level > 0:
            ellipse(ctx, 0, -28 + (1 - level) * 24, 36 * (0.6 + 0.4 * level), 6)
            rgb(ctx, (0.62, 0.78, 0.40))
            ctx.fill()
        ctx.move_to(-30, 0)
        ctx.curve_to(-14, -10, 14, 10, 30, 0)
        rgb(ctx, TRIM)
        ctx.set_line_width(5)
        ctx.stroke()
        ctx.restore()
        if level > 0:
            for i in range(2):
                ctx.move_to(x - 10 + i * 20, y - 50)
                ctx.curve_to(x - 20 + i * 20, y - 70, x + i * 20, y - 80, x - 10 + i * 20, y - 100)
                rgb(ctx, WHITE, 0.8)
                ctx.set_line_width(6)
                ctx.stroke()
    return draw


def peach_bun(flame):
    """A longevity peach bun with a candle, on the ground beside Yun."""
    def draw(ctx, p):
        x, y = 250, -8
        ellipse(ctx, x, y - 6, 80, 16)
        fill_stroke(ctx, (0.80, 0.92, 0.86), 6)
        ctx.move_to(x, y - 120)
        ctx.curve_to(x + 60, y - 110, x + 70, y - 30, x, y - 16)
        ctx.curve_to(x - 70, y - 30, x - 60, y - 110, x, y - 120)
        ctx.close_path()
        g = cairo.LinearGradient(0, y - 120, 0, y - 16)
        g.add_color_stop_rgb(0, 1.0, 0.62, 0.70)
        g.add_color_stop_rgb(1, 1.0, 0.95, 0.88)
        ctx.set_source(g)
        ctx.fill_preserve()
        rgb(ctx, INK)
        ctx.set_line_width(8)
        ctx.stroke()
        round_rect(ctx, x - 7, y - 170, 14, 54, 5)
        fill_stroke(ctx, VERMILION, 6)
        ctx.move_to(x, y - 206 - 6 * flame)
        ctx.curve_to(x + 15, y - 186, x + 10, y - 172, x, y - 172)
        ctx.curve_to(x - 10, y - 172, x - 15, y - 186, x, y - 206 - 6 * flame)
        fill_stroke(ctx, GOLD, 5)
    return draw


def cloud(x, y, w=1.0, alpha=1.0):
    def draw(ctx, p):
        parts = ((-120, 10, 60), (-50, -20, 80), (40, -24, 78), (115, 8, 58), (0, 20, 70))
        ctx.push_group()
        for dx, dy, r in parts:
            ellipse(ctx, x + dx * w, y + dy, r * w + 6, r * 0.7 + 6)
        rgb(ctx, INK)
        ctx.fill()
        for dx, dy, r in parts:
            ellipse(ctx, x + dx * w, y + dy, r * w, r * 0.7)
        rgb(ctx, (0.27, 0.25, 0.35))
        ctx.fill()
        for dx, dy, r in parts[:3]:
            ellipse(ctx, x + dx * w - r * 0.2, y + dy - r * 0.25, r * 0.35 * w, r * 0.16)
        rgb(ctx, (0.62, 0.30, 0.42))
        ctx.fill()
        ctx.pop_group_to_source()
        ctx.paint_with_alpha(alpha)
    return draw


def leaves(t, seed=17, amount=14):
    """Leaves swirling up and away (canvas space)."""
    def draw(ctx, p):
        rnd = random.Random(seed)
        for i in range(amount):
            a0 = rnd.uniform(0, 2 * math.pi)
            r = 60 + 360 * ease_out(t) * rnd.uniform(0.6, 1.1)
            a = a0 + t * 4
            x = CX + p.x + math.cos(a) * r
            y = GROUND - 330 + math.sin(a) * r * 0.6 - 260 * t * rnd.uniform(0.5, 1)
            alpha = 1 - ease(max(0, (t - 0.55) / 0.45))
            ctx.save()
            ctx.translate(x, y)
            ctx.rotate(a * 2)
            ctx.move_to(0, -18)
            ctx.curve_to(16, -8, 12, 12, 0, 18)
            ctx.curve_to(-12, 12, -16, -8, 0, -18)
            rgb(ctx, JADE if i % 2 else TRIM, alpha)
            ctx.fill_preserve()
            rgb(ctx, JADE_DARK, alpha)
            ctx.set_line_width(4)
            ctx.stroke()
            ctx.restore()
    return draw


def heart_qi(size, pulse):
    def draw(ctx, p):
        if size <= 0:
            return
        glow(ctx, 0, -200, 160 * size, (1.0, 0.55, 0.65), 0.4 + 0.2 * pulse)
        heart(ctx, 0, -205, 54 * size * (1 + 0.06 * pulse), (1.0, 0.45, 0.55))
    return draw


def orbit_sword(t, layer):
    def draw(ctx, p):
        a = 2 * math.pi * t
        depth = math.sin(a)
        if (depth < 0) != (layer == 'back'):
            return
        x, y = math.cos(a) * 250, -330 + depth * 70
        ctx.save()
        ctx.translate(x, y)
        ctx.rotate(a + math.pi / 2)
        s = 0.55 + 0.1 * depth
        ctx.scale(s, s)
        g = cairo.LinearGradient(0, 0, -300, 0)
        g.add_color_stop_rgba(0, *QI, 0.6)
        g.add_color_stop_rgba(1, *QI, 0)
        ctx.set_source(g)
        ctx.rectangle(-300, -10, 300, 20)
        ctx.fill()
        ctx.move_to(-120, -12)
        ctx.line_to(160, -10)
        ctx.line_to(200, 0)
        ctx.line_to(160, 10)
        ctx.line_to(-120, 12)
        ctx.close_path()
        fill_stroke(ctx, STEEL, 9)
        round_rect(ctx, -140, -30, 20, 60, 8)
        fill_stroke(ctx, GOLD, 8)
        round_rect(ctx, -210, -10, 72, 20, 8)
        fill_stroke(ctx, TRIM, 8)
        ctx.restore()
    return draw


def flash(strength):
    """A white flash round Yun (canvas space). It fades out before the frame's edge, so the transparent window
    never shows a square."""
    def draw(ctx, p):
        x, y = CX + p.x, GROUND - p.lift - 330
        g = cairo.RadialGradient(x, y, 40, x, y, 430)
        g.add_color_stop_rgba(0, 1, 1, 1, 0.85 * strength)
        g.add_color_stop_rgba(0.6, 1, 0.97, 0.9, 0.35 * strength)
        g.add_color_stop_rgba(1, 1, 1, 1, 0)
        ctx.set_source(g)
        ctx.arc(x, y, 430, 0, 2 * math.pi)
        ctx.fill()
    return draw


def sleeve_smear(angle):
    """A dark arc trailing the right sleeve as it snaps across."""
    def draw(ctx, p):
        sx, sy = shoulder(1, p)
        a1 = math.radians(angle)
        ctx.new_sub_path()
        ctx.arc(sx, sy, 150, math.pi / 2 - a1, math.pi / 2 - a1 + 0.9)
        ctx.arc_negative(sx, sy, 95, math.pi / 2 - a1 + 0.9, math.pi / 2 - a1)
        ctx.close_path()
        rgb(ctx, ROBE, 0.45)
        ctx.fill()
    return draw


def spin_smear(angle):
    """Curved streaks round the body during a fast tumble."""
    def draw(ctx, p):
        rgb(ctx, QI, 0.5)
        ctx.set_line_width(14)
        ctx.set_line_cap(cairo.LINE_CAP_ROUND)
        for k in range(3):
            a = math.radians(angle) + k * 2.1
            ctx.new_sub_path()
            ctx.arc(0, -320, 250 + 18 * k, a, a + 0.9)
            ctx.stroke()
    return draw


def speed_lines(strength):
    def draw(ctx, p):
        rgb(ctx, QI, 0.6 * strength)
        ctx.set_line_width(8)
        ctx.set_line_cap(cairo.LINE_CAP_ROUND)
        for k, y in enumerate((-480, -340, -200)):
            x = -230 - k * 20
            ctx.move_to(x, y)
            ctx.line_to(x - 110 * strength, y)
            ctx.stroke()
    return draw


# ---------------------------------------------------------------- sequences: lists of (Pose, duration_ms)

BASE = Pose()


def breathe(t, amount=0.016):
    return 1 + amount * math.sin(2 * math.pi * t)


SALUTE = dict(hand_l=(-24, -190), hand_r=(24, -196))     # fist in palm, the cultivator's greeting
SEAL = dict(hand_l=(-30, -120), hand_r=(30, -120))       # hands resting on the knees, sitting


def idle(kind='plain'):
    """Idle loops. The floating one drifts on a slow sine; robe, sleeves and ribbon trail it (follow_through)."""
    out = []
    n = 18
    for i in range(n):
        t = i / n
        p = replace(BASE, squash=breathe(t), flutter=t - 0.2, wind=0.2 + 0.06 * osc(t), sleeve_l=10 + 2 * osc(t, 1, -0.15),
                    sleeve_r=10 + 2 * osc(t, 1, 0.35), tilt=1.2 * math.sin(2 * math.pi * (t - 0.15)))
        if kind == 'float':
            p = replace(p, lift=30 + 14 * osc(t), sleeve_l=22 + 6 * osc(t, 1, -0.2), sleeve_r=22 + 6 * osc(t, 1, -0.25),
                        eyes='closed' if 0.35 < t < 0.75 else 'open', brows='calm' if 0.35 < t < 0.75 else 'sharp',
                        wind=0.35, back=[formation(t, 0.5)])
        elif kind == 'breeze':
            gust = hop(t)
            p = replace(p, wind=0.2 + 0.75 * gust, flutter=t * 2, sleeve_l=10 + 30 * gust, sleeve_r=10 + 12 * gust,
                        eyes='happy' if 0.3 < t < 0.7 else 'open', back=[petals(t * 0.6, amount=6, seed=3)])
        elif kind == 'look':
            look = [(0, 0)] * 3 + [(-16, 0)] * 5 + [(16, -2)] * 6 + [(0, 0)] * 4
            p = replace(p, look=look[i], tilt=look[i][0] * 0.3)
        elif kind == 'happy':
            p = replace(p, lift=8 * max(0, osc(t, 0.5)), eyes='happy' if 6 <= i < 12 else 'open', blush=0.85,
                        wind=0.4, back=[petals(t, amount=8, seed=5)], sleeve_l=20, sleeve_r=20)
        elif kind == 'poor':
            p = replace(p, squash=0.95 + 0.01 * osc(t), eyes='sleepy', eye_open=0.55, brows='worried', pale=0.4,
                        tilt=-5, wind=0.0, blush=0.1, mouth='flat', sleeve_l=4, sleeve_r=4)
        out.append((p, 140))
    if kind not in ('poor', 'float'):
        at = 13
        p = out[at][0]
        out[at:at + 1] = [(replace(p, eye_open=0.4), 50), (replace(p, eye_open=0.05), 70), (replace(p, eye_open=0.4), 50)]
    return out


def meditate(phase, lotus_loop=False):
    """Dip, sit and rise on an ease-out while the orbs appear one by one; orbit; spiral the orbs in, glow, land."""
    if phase == 'start':
        out = [(replace(BASE, squash=0.94, eyes='open', brows='calm'), 80),
               (replace(BASE, squash=0.97, sit=1, eyes='closed', brows='calm', back=[formation(0.1, 0.3)], **SEAL), 80)]
        for k, u in enumerate((0.35, 0.65, 0.85, 0.96, 1.0)):
            t = (k + 1) / 5
            out.append((replace(BASE, sit=1, lift=30 * u, eyes='closed', brows='calm', flutter=t,
                                back=[formation(t, u), qi_orbs(t * 0.2, 'back', appear=t)],
                                front=[qi_orbs(t * 0.2, 'front', appear=t)], **SEAL), (70, 70, 80, 100, 120)[k]))
        return out
    if phase == 'loop':
        out = []
        n = 16
        for i in range(n):
            t = i / n
            color = QI_DARK if lotus_loop else QI
            back = [formation(t, color=color), qi_orbs(t, 'back', color=color)]
            front = [qi_orbs(t, 'front', color=color)]
            if lotus_loop:
                back.insert(1, lotus(min(1, t * 2.2)))
                front.append(petals(t, amount=8, seed=21))
            out.append((replace(BASE, sit=1, lift=30 + 8 * osc(t), eyes='closed', brows='calm', mouth='cat', flutter=t,
                                wind=0.35, squash=breathe(t, 0.01), blush=0.3, back=back, front=front, **SEAL), 110))
        return out
    out = []
    for k, g in enumerate((0.35, 0.7, 1.0)):
        out.append((replace(BASE, sit=1, lift=30, eyes='closed', brows='calm', flutter=k / 3,
                            back=[formation(0.5, 1 - g * 0.6), qi_orbs(0.2, 'back', gather=g)],
                            front=[qi_orbs(0.2, 'front', gather=g)], **SEAL), 70))
    out += [(replace(BASE, sit=1, lift=30, eyes='glow', brows='sharp', back=[formation(0.6, 0.3)], **SEAL), 140),
            (replace(BASE, sit=1, lift=12, eyes='open', **SEAL), 80),
            (replace(BASE, squash=0.94), 80),
            (replace(BASE), 100)]
    return out


SLIP_AT = (175, -520)      # the jade slip floats here while Yun reads it with the mind
CHEST = dict(hand_l=(-26, -176), hand_r=(26, -182))


def jade(phase, alt=False):
    """The slip rises from the chest on a curve, floats and pulses while glyphs flow in, then drops into the sleeve."""
    def slip_path(u):
        return (lerp(0, SLIP_AT[0], u) + 60 * math.sin(math.pi * u), lerp(-200, SLIP_AT[1], ease_out(u)))
    if phase == 'start':
        out = []
        for i, (u, ms) in enumerate(zip((0.15, 0.4, 0.7, 0.92, 1.04, 1.0), (60, 60, 70, 80, 80, 100))):
            h = ease(min(1, (i + 1) / 3))
            x, y = slip_path(min(u, 1.0))
            y -= 18 * max(0.0, u - 1) / 0.04
            out.append((replace(BASE, eyes='closed' if i > 3 else 'open', look=(10 * h, -8 * h), flutter=i / 6,
                                hand_l=(lerp(-80, -26, h), lerp(-110, -176, h)), hand_r=(lerp(80, 26, h), lerp(-110, -182, h)),
                                front=[jade_slip(x, y, -0.2 * min(u, 1), min(1, u))]), ms))
        return out
    if phase == 'loop' and not alt:
        out = []
        n = 16
        for i in range(n):
            t = i / n
            pulse = max(0.0, math.sin(2 * math.pi * t * 2))
            out.append((replace(BASE, eyes='closed', brows='calm', mouth='flat', tilt=3 * osc(t), flutter=t,
                                squash=breathe(t, 0.01), **CHEST,
                                front=[jade_slip(SLIP_AT[0], SLIP_AT[1] + 10 * osc(t), -0.2 + 0.05 * osc(t), 0.5 + 0.5 * pulse),
                                       glyph_stream(t * 2, SLIP_AT[0] - 20, SLIP_AT[1] + 20, 10, -440)]), 120))
        return out
    if phase == 'loop':
        out = []
        n = 16
        for i in range(n):
            t = i / n
            u = (t * 2) % 1
            out.append((replace(BASE, hand_l=(-135, -168), hand_r=(135, -168), look=(lerp(18, -18, ease(u)), 12),
                                tilt=4 + 2 * osc(t), flutter=t, front=[bamboo_scroll()],
                                eye_open=0.1 if i in (7, 15) else 1), 120))
        return out
    out = []
    for i, u in enumerate((0.85, 0.55, 0.25, 0.0)):
        h = 1 - ease((i + 1) / 4)
        x, y = slip_path(u)
        out.append((replace(BASE, eyes='open', flutter=i / 4,
                            hand_l=(lerp(-80, -26, h), lerp(-110, -176, h)), hand_r=(lerp(80, 26, h), lerp(-110, -182, h)),
                            front=[jade_slip(x, y + 60 * (1 - u), -0.2 * u, u)] if u > 0 else []), (70, 60, 60, 90)[i]))
    out.append((replace(BASE), 90))
    return out


FAN_AT = (205, -280)


def alchemy(phase, pill_loop=False):
    out = []
    if phase == 'start':
        for i in range(7):
            t = (i + 1) / 7
            u = ease(t)
            p = replace(BASE, hand_r=(lerp(80, FAN_AT[0], u), lerp(-110, FAN_AT[1], u) - 50 * math.sin(math.pi * u)),
                        look=(0, 10 * t), flutter=t, head_dy=-20 * t,
                        front=[cauldron(t, rise=back_out(t), fire=max(0, t * 2 - 1))])
            out.append((p, (60, 60, 60, 70, 80, 90, 110)[i]))
        return out
    if phase == 'loop' and not pill_loop:
        n = 12
        for i in range(n):
            t = i / n
            fan = osc(t * 2)
            out.append((replace(BASE, hand_r=(FAN_AT[0] + 14 * fan, FAN_AT[1] + 12 * fan), sleeve_l=20, look=(0, 12),
                                head_dy=-20, brows='calm', mouth='smirk', flutter=t, wind=0.35,
                                front=[cauldron(t, pill=0), fan_flame(t * 2)]), 100))
        return out
    if phase == 'loop':
        held = replace(BASE, hand_r=(FAN_AT[0], FAN_AT[1]), sleeve_l=20, look=(0, 12), head_dy=-20, brows='calm',
                       mouth='smirk', wind=0.35)
        # Two shudders, the pill shoots out on an arc, hangs at the top, sparkles.
        beats = [(0, 0.0, 'wide', 90), (14, 0.0, 'wide', 70), (-14, 0.0, 'wide', 70), (12, 0.0, 'wide', 70),
                 (-10, 0.0, 'wide', 70), (0, 0.25, 'wide', 50), (0, 0.6, 'glow', 50), (0, 0.85, 'glow', 60),
                 (0, 1.0, 'glow', 160), (0, 1.0, 'glow', 120), (0, 1.0, 'glow', 120), (0, 1.0, 'glow', 100)]
        for i, (shake, pill, eyes, ms) in enumerate(beats):
            t = i / len(beats)
            up = ease(min(1, pill * 1.5))
            speed = 1.0 if 0 < pill < 0.9 else 0.0
            p = replace(held, eyes=eyes, flutter=t, hand_r=None if pill > 0 else held.hand_r,
                        sleeve_r=lerp(60, 150, up), sleeve_l=lerp(20, 150, up), squash=1 + 0.04 * up,
                        front=[cauldron(t, pill=pill, shake=shake, pill_stretch=speed),
                               sparkles((i - 8) / 4, count=6, radius=170, cy=-560, seed=8, color=GOLD)])
            out.append((p, ms))
        return out
    for i in range(6):
        t = (i + 1) / 6
        u = ease(t)
        p = replace(BASE, hand_r=(lerp(FAN_AT[0], 80, u), lerp(FAN_AT[1], -110, u)) if t < 1 else None,
                    look=(0, 10 * (1 - t)), flutter=t, head_dy=-20 * (1 - t),
                    front=[cauldron(t, rise=1 - ease_in(t), fire=max(0, 1 - t * 2))])
        out.append((p, 70))
    return out


def jade_to_alchemy():
    """Reading to working without standing up: slip away and cauldron up at the same time; the hand moves on an arc."""
    out = []
    for i in range(10):
        t = (i + 1) / 10
        a = ease(min(1, t * 1.4))
        hx = lerp(26, FAN_AT[0], a)
        hy = lerp(-182, FAN_AT[1], a) - 70 * math.sin(math.pi * a)
        props = [cauldron(t, rise=back_out(max(0.0, min(1.0, t * 1.5 - 0.4))) if t > 0.27 else 0,
                          fire=max(0, t * 2 - 1))]
        if a < 1:
            props.append(jade_slip(lerp(SLIP_AT[0], 120, a), lerp(SLIP_AT[1], -200, a), -0.2, 1 - a))
        out.append((replace(BASE, hand_l=(lerp(-26, -80, a), lerp(-176, -110, a)), hand_r=(hx, hy), eyes='open',
                            look=(0, 12 * t), head_dy=-20 * t, flutter=t, front=props), 80 if i < 9 else 120))
    return out


def alchemy_to_jade():
    out = []
    for i in range(10):
        t = (i + 1) / 10
        a = ease(max(0, t * 1.6 - 0.6))
        props = [cauldron(t, rise=max(0, 1 - t * 1.6), fire=max(0, 1 - t * 2))]
        if a > 0:
            props.append(jade_slip(lerp(120, SLIP_AT[0], a), lerp(-200, SLIP_AT[1], a), -0.2, a))
        out.append((replace(BASE, hand_l=(lerp(-80, -26, a), lerp(-110, -176, a)),
                            hand_r=(lerp(FAN_AT[0], 26, a), lerp(FAN_AT[1], -182, a)),
                            eyes='closed' if t > 0.85 else 'open', head_dy=-20 * (1 - t), flutter=t, front=props), 90))
    return out


def alert(phase):
    """The wrist flicks back, then the talisman spins in on an arc and stops with an overshoot; it floats; it folds."""
    tx, ty = 230, -560
    if phase == 'start':
        out = [(replace(BASE, hand_r=(60, -170), eyes='sharp', flutter=0.1, wind=0.4), 90)]
        for k, u in enumerate((0.3, 0.6, 0.85, 1.0, 1.0)):
            x = lerp(-60, tx, u) + 30 * math.sin(math.pi * u)
            y = lerp(-250, ty, ease_out(u)) - 90 * math.sin(math.pi * u)
            over = (0, 0, 0, 1.12, 1.0)[k]
            out.append((replace(BASE, hand_r=(lerp(60, 170, ease(u)), lerp(-170, -400, ease(u))), eyes='wide' if k < 3 else 'open',
                                flutter=u, wind=0.4,
                                front=[talisman(x, y, 0.1 + (1 - u) * 6, size=over or lerp(0.4, 1, u), light=u)]),
                        (50, 50, 60, 70, 90)[k]))
        return out
    if phase == 'loop':
        out = []
        n = 10
        for i in range(n):
            t = i / n
            out.append((replace(BASE, hand_r=(170, -400 - 12 * osc(t, 0.5)), eyes='open', look=(10, -6), brows='calm',
                                mouth='smirk', flutter=t * 2, wind=0.45, tilt=-4,
                                front=[talisman(tx, ty + 12 * osc(t), 0.1 + 0.08 * osc(t), light=0.6 + 0.4 * osc(t, 0.5))]), 90))
        return out
    out = []
    for i, (s, ms) in enumerate(((1.1, 60), (0.6, 60), (0.2, 60), (0.0, 80))):
        t = (i + 1) / 4
        out.append((replace(BASE, hand_r=(lerp(170, 80, t), lerp(-400, -120, t)) if t < 1 else None, flutter=t,
                            front=[talisman(tx, ty, 0.1, size=s)] if s else []), ms))
    return out


def tribulation():
    """The cloud gathers slowly, a white flash, the strike, the scorched squash held 0.25 s, then a slow recovery."""
    out = []
    for k in range(5):
        u = (k + 1) / 5
        out.append((replace(BASE, look=(0, -14 * u), eyes='wide' if u > 0.5 else 'open', brows='worried',
                            over=[storm(u * 0.9, 0)], flutter=u), (120, 120, 110, 100, 140)[k]))
    out.append((replace(BASE, look=(0, -14), eyes='squeeze', brows='worried', squash=1.1, hair_up=0.7,
                        over=[storm(1, 1), flash(1.0)], sleeve_l=130, sleeve_r=130), 50))
    out.append((replace(BASE, eyes='squeeze', squash=1.08, hair_up=0.6, soot=0.6, over=[storm(1, 0.7)],
                        sleeve_l=120, sleeve_r=120), 60))
    out.append((replace(BASE, eyes='spiral', soot=1, hair_up=0.4, squash=0.84, mouth='wavy', back=[smoke_wisps(0.1)],
                        over=[storm(0.8, 0)], wind=0), 250))
    for k in range(5):
        u = (k + 1) / 5
        out.append((replace(BASE, eyes='spiral', soot=1, hair_up=0.3, squash=0.88 + 0.02 * osc(u, 0.5), mouth='wavy',
                            tilt=6 * osc(u, 0.5), back=[smoke_wisps(0.1 + u * 0.6)], over=[storm(0.8 * (1 - u), 0)],
                            flutter=u, wind=0), 100))
    for k, (soot, sq, eyes) in enumerate(((0.8, 0.94, 'squeeze'), (0.5, 0.98, 'squeeze'), (0.2, 1.0, 'open'))):
        out.append((replace(BASE, eyes=eyes, soot=soot, squash=sq, brows='worried', mouth='wavy',
                            hand_r=(60, -320) if k < 2 else None, front=[sweat(1)]), 130))
    out.append((replace(BASE), 120))
    return out


def deviation():
    """Out of qi: slumped and pale, the violet aura flickering on an irregular beat, shallow uneven breath."""
    flicker = (0.55, 0.62, 0.4, 0.7, 0.45, 0.48, 0.66, 0.38, 0.6, 0.72, 0.42, 0.5, 0.58, 0.47)
    breath = (0, 0.4, 0.8, 1.0, 0.7, 0.3, 0.1, 0.0, 0.3, 0.9, 0.6, 0.2, 0.05, 0.0)
    durations = (150, 130, 170, 140, 120, 180, 150, 130, 160, 140, 170, 120, 150, 140)
    out = []
    for i in range(14):
        t = i / 14
        out.append((replace(BASE, sit=1, squash=0.965 + 0.012 * breath[i], eyes='sleepy', eye_open=0.42 + 0.12 * breath[i],
                            brows='worried', pale=0.5, tilt=-8 + 2 * breath[i], wind=0.0, mouth='wavy', blush=0,
                            flutter=t, back=[aura(t, (0.55, 0.35, 0.75), flicker[i])], front=[sweat(0.6, side=-1)],
                            **SEAL), durations[i]))
    return out


def breakthrough():
    """Crouch while the orbs gather, launch stretched as the pillar erupts, hang at the top, land squashed."""
    plan = [  # lift, squash, arms, eyes, pillar, gather, ms
        (0, 0.92, 20, 'closed', 0.0, 0.0, 90), (0, 0.86, 30, 'closed', 0.1, 0.5, 100), (0, 0.84, 30, 'closed', 0.2, 1.0, 80),
        (50, 1.14, 120, 'glow', 0.8, 1.0, 50), (95, 1.1, 150, 'glow', 1.0, 1.0, 60), (112, 1.03, 160, 'glow', 1.0, 1.0, 150),
        (100, 1.02, 155, 'glow', 0.9, 1.0, 80), (60, 1.06, 140, 'glow', 0.6, 1.0, 60), (15, 1.06, 120, 'happy', 0.3, 1.0, 50),
        (0, 0.84, 90, 'happy', 0.1, 1.0, 90), (0, 1.04, 40, 'happy', 0.0, 1.0, 90), (0, 1.0, 20, 'happy', 0.0, 1.0, 160)]
    total = sum(p[-1] for p in plan)
    out, at = [], 0
    for i, (lift, sq, arms, eyes, pillar, gather, ms) in enumerate(plan):
        t = at / total
        back = [light_pillar(pillar), sparkles(t, count=8, seed=4, color=GOLD)]
        front = []
        if i < 3:
            back.append(qi_orbs(t, 'back', gather=gather * 0.9))
            front.append(qi_orbs(t, 'front', gather=gather * 0.9))
        out.append((replace(BASE, lift=lift, squash=sq, eyes=eyes, blush=0.6, sleeve_l=arms, sleeve_r=arms,
                            hair_up=0.4 * pillar, wind=0.4 + 0.5 * pillar, flutter=t * 2, back=back, front=front,
                            over=[petals(t, amount=12, seed=2)] if i >= 4 else []), ms))
        at += ms
    return out


def cloudnap(phase):
    out = []
    base_lift = 120
    if phase == 'start':
        s = dict(**SEAL, wind=0.1, shadow=True)
        rises = (0.15, 0.4, 0.7, 0.9, 1.0, 1.0, 1.0, 1.0)
        heads = [(0, 0, 0.8, 'sleepy'), (0, 0, 0.6, 'sleepy'), (-3, 0, 0.4, 'sleepy'), (-12, 10, 0.1, 'sleepy'),
                 (2, -4, 0.8, 'open'), (-5, 0, 0.4, 'sleepy'), (-9, 6, 0.1, 'sleepy'), (-10, 4, 0, 'closed')]
        for i, (r, (tilt, dy, eo, eyes)) in enumerate(zip(rises, heads)):
            out.append((replace(BASE, sit=1 if i > 0 else 0, lift=base_lift * ease(r), eyes=eyes, eye_open=eo, tilt=tilt,
                                head_dy=dy, flutter=i / 8, back=[cloud(0, 6, 0.5 + 0.5 * ease(r), ease(r))], **s),
                        (100, 100, 110, 160, 90, 140, 160, 180)[i]))
        return out
    if phase == 'loop':
        n = 16
        for i in range(n):
            t = i / n
            bob = 10 * osc(t)
            out.append((replace(BASE, sit=1, lift=base_lift + bob, eyes='closed', tilt=-10 + 2 * osc(t, 1, -0.1), wind=0.1,
                                mouth='o', mouth_open=0.2 + 0.1 * osc(t), blush=0.6, flutter=t * 0.5,
                                back=[cloud(0, 6), zzz(t)], **SEAL), 150))
        return out
    steps = [('wide', 1, base_lift, 100), ('open', 1, base_lift * 0.7, 100), ('open', 0, base_lift * 0.3, 90),
             ('open', 0, 0, 80), ('happy', 0, 0, 160)]
    for i, (eyes, sit, lift, ms) in enumerate(steps):
        t = (i + 1) / len(steps)
        extra = SEAL if sit else dict(sleeve_l=lerp(120, 10, t), sleeve_r=lerp(120, 10, t))
        out.append((replace(BASE, sit=sit, lift=lift, eyes=eyes, squash=0.88 if i == 3 else 1, flutter=t,
                            back=[cloud(0, 6, 1 - t * 0.5, 1 - t)] if t < 1 else [], **extra), ms))
    return out


def descend():
    """Dives in on the sword with an ease-out, overshoots the stop and bobs back, hops off squashed, salutes and holds."""
    out = []
    for k in range(8):
        u = ease_out((k + 1) / 8)
        out.append((replace(BASE, lift=lerp(640, 0, u) + 20 - 14 * math.sin(math.pi * min(1, u * 1.05)) * (u > 0.9),
                            sword=1, trail=1 - u, lean=-8 * (1 - u), wind=0.9 * (1 - u) + 0.2, flutter=k / 4,
                            eyes='sharp', sleeve_l=40, sleeve_r=40, alpha=min(1, (k + 1) / 2)), 60))
    out += [(replace(BASE, lift=8, sword=1, squash=0.94, eyes='sharp', sleeve_l=30, sleeve_r=30), 70),
            (replace(BASE, lift=26, sword=1, squash=1.02, eyes='open', sleeve_l=30, sleeve_r=30), 70),
            (replace(BASE, lift=40, sword=0.6, squash=1.08, eyes='open', sleeve_l=60, sleeve_r=60), 70),
            (replace(BASE, lift=0, sword=0.1, squash=0.86, eyes='happy'), 80),
            (replace(BASE, squash=1.03, eyes='open', **SALUTE), 90),
            (replace(BASE, tilt=6, lean=2, eyes='closed', **SALUTE), 90),
            (replace(BASE, tilt=12, lean=5, eyes='closed', blush=0.4, **SALUTE,
                     back=[sparkles(0.4, count=5, seed=8, color=QI)]), 320),
            (replace(BASE, tilt=4, lean=1, eyes='open', **SALUTE), 90),
            (replace(BASE), 140)]
    return out


def ascend():
    """Bow, the sword slides under the feet, a crouch, then the climb accelerates (ease-in) and fades out."""
    out = [(replace(BASE, eyes='open', **SALUTE), 90),
           (replace(BASE, tilt=8, lean=3, eyes='closed', **SALUTE), 90),
           (replace(BASE, tilt=12, lean=5, eyes='closed', **SALUTE), 260),
           (replace(BASE, tilt=3, eyes='open', **SALUTE), 90),
           (replace(BASE, sword=0.4, eyes='sharp', sleeve_l=30, sleeve_r=30), 70),
           (replace(BASE, sword=1, lift=8, eyes='sharp', sleeve_l=40, sleeve_r=40), 70),
           (replace(BASE, sword=1, lift=4, squash=0.88, eyes='sharp', sleeve_l=50, sleeve_r=50), 120)]
    for k in range(7):
        u = ease_in((k + 1) / 7)
        out.append((replace(BASE, sword=1, lift=10 + 720 * u, squash=1.08 if k < 4 else 1.04, trail=min(1, 0.3 + u),
                            lean=-8 * min(1, u * 3), alpha=1 - ease(max(0, (u - 0.55) / 0.45)), wind=0.95,
                            flutter=k / 3, eyes='happy', sleeve_l=40, sleeve_r=40), (90, 80, 70, 60, 55, 50, 50)[k]))
    out.append((replace(BASE, alpha=0, shadow=False), 200))
    return out


def fury(leave=False):
    """Annoyed: the aura flares in pulses with a jitter, then holds the glare. Leaving: turns away, snaps the sleeve
    across on an arc and is gone in a whirl of leaves."""
    red = (0.95, 0.30, 0.30)
    if not leave:
        pulses = (0.2, 0.5, 0.35, 0.8, 0.6, 1.0, 0.85, 1.0, 1.0, 1.0, 1.0, 1.0)
        jitter = (0, 6, -6, 5, -5, 4, -4, 2, 0, 0, 0, 0)
        out = []
        for i, (s, j) in enumerate(zip(pulses, jitter)):
            t = i / len(pulses)
            out.append((replace(BASE, x=j, tint=min(1, s * 1.1), brows='angry', mouth='frown', hair_up=0.5 * s,
                                wind=0.5 + 0.4 * s, flutter=t * 3, sleeve_l=lerp(10, 50, s), sleeve_r=lerp(10, 50, s),
                                squash=1 + 0.03 * s, back=[aura(t, red, s)]), 70 if i < 8 else 120))
        return out
    out = [(replace(BASE, tint=0.7, brows='angry', mouth='frown', eyes='closed', tilt=-6, sleeve_r=20, wind=0.6,
                    back=[aura(0.1, red, 0.6)]), 90),
           (replace(BASE, tint=0.7, brows='angry', mouth='frown', eyes='closed', tilt=-12, sleeve_r=0, lean=-3, wind=0.6,
                    back=[aura(0.2, red, 0.6)]), 120)]
    for k, ang in enumerate((40, 110, 160, 150)):
        out.append((replace(BASE, tint=0.7, brows='angry', mouth='frown', eyes='closed', tilt=-12, sleeve_r=ang,
                            lean=3 if k < 2 else 1, wind=0.8, flutter=k / 3, front=[sleeve_smear(ang)] if k < 3 else [],
                            over=[leaves(0.05 + k * 0.08)]), 50))
    for k in range(8):
        u = (k + 1) / 8
        out.append((replace(BASE, tint=0.7, brows='angry', mouth='frown', eyes='closed', tilt=-12, sleeve_r=lerp(150, 20, u),
                            alpha=1 - ease(min(1, u * 1.4)), wind=0.9, flutter=u * 3, over=[leaves(0.3 + u * 0.7)]), 85))
    out.append((replace(BASE, alpha=0, shadow=False), 200))
    return out


def held(phase):
    out = []
    if phase == 'start':
        for i in range(5):
            t = (i + 1) / 5
            out.append((replace(BASE, squash=lerp(1, 1.1, t), lift=40 * t, eyes='wide', brows='worried',
                                sleeve_l=lerp(10, 140, t), sleeve_r=lerp(10, 140, t), shadow=False, wind=0.0,
                                flutter=t), 60))
    elif phase == 'loop':
        n = 12
        for i in range(n):
            t = i / n
            out.append((replace(BASE, squash=1.08 + 0.02 * osc(t, 0.5), lift=40, lean=9 * osc(t), eyes='closed',
                                brows='worried', mouth='wavy', sleeve_l=130 + 25 * osc(t, 0.5),
                                sleeve_r=130 + 25 * osc(t, 0.5, 0.5), shadow=False, wind=0.0, flutter=t * 2,
                                front=[sweat(1, side=-1)]), 80))
    else:
        for i, (sq, lift, eyes) in enumerate(((1.0, 20, 'wide'), (0.86, 0, 'squeeze'), (1.04, 0, 'open'), (1.0, 0, 'open'))):
            out.append((replace(BASE, squash=sq, lift=lift, eyes=eyes, sleeve_l=40 - i * 10, sleeve_r=40 - i * 10,
                                flutter=i / 4), 70))
    return out


def pat(phase):
    out = []
    if phase == 'start':
        for i in range(4):
            t = (i + 1) / 4
            out.append((replace(BASE, squash=lerp(1, 0.93, t), eyes='happy' if t > 0.4 else 'open', blush=lerp(0.4, 1, t),
                                tilt=5 * t), 60))
    elif phase == 'loop':
        n = 10
        for i in range(n):
            t = i / n
            out.append((replace(BASE, squash=0.93 + 0.02 * osc(t, 0.5), eyes='happy', mouth='cat', blush=1, tilt=5 * osc(t),
                                flutter=t, back=[petals(t, amount=6, seed=31, area=260), hearts_up(t, count=2, seed=6)]), 90))
    else:
        for i, sq in enumerate((1.05, 0.98, 1.0)):
            out.append((replace(BASE, squash=sq, eyes='happy' if i < 2 else 'open', blush=0.7), 70))
    return out


def tickle(phase):
    out = []
    if phase == 'start':
        for i, sq in enumerate((0.92, 1.06, 0.97)):
            out.append((replace(BASE, squash=sq, eyes='squeeze', mouth='open', lean=4 - i * 4, hand_r=(40, -300)), 60))
    elif phase == 'loop':
        n = 10
        for i in range(n):
            t = i / n
            out.append((replace(BASE, squash=1 + 0.035 * osc(t, 0.25), lean=5 * osc(t, 0.5), eyes='squeeze', mouth='open',
                                blush=0.9, hand_r=(40, -300 + 6 * osc(t, 0.25)), sleeve_l=40 + 10 * osc(t, 0.5),
                                flutter=t, back=[sparkles(t, count=4, radius=260, seed=12, color=QI)]), 75))
    else:
        for i, sq in enumerate((1.04, 0.98, 1.0)):
            out.append((replace(BASE, squash=sq, eyes='happy' if i < 2 else 'open', blush=0.6), 70))
    return out


def tumble(phase):
    """Thrown: a fast tumble with smears, the sword appears; surf it, wobbling; land and salute. Faces right."""
    if phase == 'start':
        out = []
        for k, ang in enumerate((-50, -130, -220, -310, -360)):
            # Spin about the middle of the body rather than the feet: move the feet so the centre stays put.
            th = math.radians(ang)
            out.append((replace(BASE, lean=ang, x=-300 * math.sin(th), lift=40 + 300 - 300 * math.cos(th), eyes='squeeze', brows='worried', sleeve_l=140, sleeve_r=140,
                                shadow=False, sword=max(0, (k - 2) / 2), wind=0.9, flutter=k / 2,
                                back=[spin_smear(ang)] if k < 4 else []), 55))
        return out
    if phase == 'loop':
        out = []
        n = 10
        for i in range(n):
            t = i / n
            out.append((replace(BASE, lift=40 + 6 * osc(t, 0.5), lean=-8 + 7 * osc(t), sword=1, trail=0.7, eyes='wide',
                                brows='worried', mouth='o', sleeve_l=100 + 30 * osc(t, 0.5),
                                sleeve_r=80 + 30 * osc(t, 0.5, 0.5), shadow=False, wind=1.0, flutter=t * 3,
                                back=[speed_lines(0.8)]), 70))
        return out
    return [(replace(BASE, lift=24, sword=1, eyes='open', sleeve_l=60, sleeve_r=60), 60),
            (replace(BASE, lift=0, sword=0.5, squash=0.86, eyes='squeeze', sleeve_l=40, sleeve_r=40), 80),
            (replace(BASE, sword=0, squash=1.04, eyes='happy'), 70),
            (replace(BASE, eyes='happy', **SALUTE), 150)]


def peek(phase):
    """Hides behind a cloud at the screen edge and peeks out; the peek is held longer than the duck. Faces right."""
    pose = dict(x=-30, lean=8, tilt=12, look=(14, 0), blush=0.3, sleeve_r=60, wind=0.1)
    if phase == 'start':
        return [(replace(BASE, **{**pose, 'tilt': 10 * t, 'lean': 8 * t}, flutter=t,
                         front=[cloud(60, -110, 0.9, ease(t))]), ms)
                for t, ms in ((0.4, 60), (0.8, 70), (1.0, 100))]
    if phase == 'loop':
        plan = [(1.0, 0, 'open', 260), (1.0, 0, 'happy', 300), (1.0, 2, 'happy', 160), (0.4, 14, 'closed', 90),
                (0.1, 22, 'closed', 160), (0.6, 10, 'open', 90), (1.0, 0, 'open', 300)]
        out = []
        for i, (out_amt, dy, eyes, ms) in enumerate(plan):
            out.append((replace(BASE, **{**pose, 'lean': 8 * out_amt, 'tilt': 12 * out_amt, 'x': -30 - 40 * (1 - out_amt)},
                                head_dy=dy, eyes=eyes, mouth='cat', flutter=i / 7,
                                front=[cloud(60, -110, 0.9)]), ms))
        return out
    return [(replace(BASE, **{**pose, 'tilt': 12 * (1 - t), 'lean': 8 * (1 - t), 'x': -30 * (1 - t)}, flutter=t,
                     front=[cloud(60, -110, 0.9, 1 - t)]), ms)
            for t, ms in ((0.4, 70), (0.8, 70), (1.0, 100))]


def mount(phase, angle=0.0):
    """Flying-sword travel: step on with a dip, a loop pass tilted by `angle` (degrees, + climbs), step off squashed."""
    if phase == 'start':
        return [(replace(BASE, sword=0.5, eyes='sharp', sleeve_l=20, sleeve_r=20, wind=0.3), 60),
                (replace(BASE, sword=1, lift=10, eyes='sharp', sleeve_l=30, sleeve_r=30, wind=0.4), 60),
                (replace(BASE, sword=1, lift=4, squash=0.9, eyes='sharp', sleeve_l=40, sleeve_r=40, wind=0.4), 100),
                (replace(BASE, sword=1, lift=24, squash=1.06, eyes='sharp', sleeve_l=50, sleeve_r=35, wind=0.7), 60),
                (replace(BASE, sword=1, lift=30, eyes='sharp', sleeve_l=55, sleeve_r=30, wind=0.85), 70)]
    if phase == 'loop':
        out = []
        n = 8
        for i in range(n):
            t = i / n
            out.append((replace(BASE, sword=1, lift=30 + 8 * osc(t), lean=-angle * 0.5 - 4, trail=1, eyes='sharp',
                                mouth='smirk', look=(10, -6 * (angle > 0) + 6 * (angle < 0)), sleeve_l=60 + 8 * osc(t, 1, -0.2),
                                sleeve_r=30, wind=1.0, flutter=t * 2, back=[speed_lines(0.7 + 0.3 * osc(t))]), 70))
        return out
    return [(replace(BASE, sword=1, lift=30, lean=4, eyes='sharp', sleeve_l=50, sleeve_r=40, wind=0.7), 60),
            (replace(BASE, sword=1, lift=40, squash=1.06, eyes='open', sleeve_l=60, sleeve_r=50, wind=0.5), 60),
            (replace(BASE, sword=0.4, lift=0, squash=0.86, eyes='happy', sleeve_l=30, sleeve_r=30, wind=0.4), 80),
            (replace(BASE, sword=0, squash=1.03, eyes='happy', wind=0.3), 80),
            (replace(BASE), 100)]


def petal_rain():
    out = []
    n = 20
    for i in range(n):
        t = i / (n - 1)
        spin = osc(t)
        p = replace(BASE, eyes='happy', blush=0.9, sleeve_l=90 + 60 * max(0, spin), sleeve_r=90 + 60 * max(0, -spin),
                    lean=6 * spin, tilt=8 * spin, lift=14 * abs(osc(t, 0.5)), wind=0.6, flutter=t * 3,
                    over=[petals(t, amount=24, seed=40)])
        out.append((p, 90))
    out.append((replace(BASE, eyes='happy', blush=0.7), 150))
    return out


def play_flute(phase):
    out = []
    pose = dict(hand_l=(60, -300), hand_r=(170, -280), eyes='closed', brows='calm', mouth='o', mouth_open=0.1)
    fl = flute(20, -306, 250, -270)
    if phase == 'start':
        for i in range(4):
            t = ease_out((i + 1) / 4)
            out.append((replace(BASE, hand_l=(lerp(-80, 60, t), lerp(-120, -300, t)), hand_r=(lerp(80, 170, t), lerp(-120, -280, t)),
                                eyes='open' if t < 0.6 else 'closed', front=[fl] if t > 0.4 else [], flutter=t), (60, 70, 80, 110)[i]))
        return out
    if phase == 'loop':
        n = 12
        for i in range(n):
            t = i / n
            sway = math.sin(2 * math.pi * t)
            sway = math.copysign(abs(sway) ** 0.7, sway)
            out.append((replace(BASE, **pose, tilt=6 * sway, lean=3 * sway, wind=0.5, flutter=t * 2 - 0.2, blush=0.3,
                                front=[fl], back=[notes(t), petals(t, amount=6, seed=44, area=300)]), 110))
        return out
    for i in range(4):
        t = ease((i + 1) / 4)
        out.append((replace(BASE, hand_l=(lerp(60, -80, t), lerp(-300, -120, t)), hand_r=(lerp(170, 80, t), lerp(-280, -120, t)),
                            eyes='happy', mouth='smirk', front=[fl] if t < 0.6 else [], flutter=t), (70, 70, 80, 140)[i]))
    return out


def butterfly_trick():
    """Salute; Yun breaks into butterflies from the edges in, they flutter round, fly back and rebuild Yun."""
    out = [(replace(BASE, eyes='closed', mouth='smirk', **SALUTE), 120)]
    for k in range(6):
        u = (k + 1) / 6
        out.append((replace(BASE, eyes='closed', mouth='smirk', **SALUTE, alpha=1 - ease(u), shadow=u < 0.7,
                            over=[butterflies(u * 0.3, 1 + int(8 * u), gather=1 - ease(u))], flutter=u), 70))
    for k in range(10):
        u = (k + 1) / 10
        out.append((replace(BASE, alpha=0, shadow=False, over=[butterflies(0.3 + u * 0.6, 9, gather=0)]), 90))
    for k in range(6):
        u = (k + 1) / 6
        out.append((replace(BASE, eyes='happy', alpha=ease(u), sleeve_l=lerp(150, 60, u), sleeve_r=lerp(150, 60, u),
                            over=[butterflies(0.9 + u * 0.3, 9 - int(8 * u), gather=ease(u))], flutter=u, shadow=u > 0.3), 70))
    out += [(replace(BASE, eyes='happy', squash=0.94, sleeve_l=140, sleeve_r=140), 90),
            (replace(BASE, eyes='happy', squash=1.02, sleeve_l=120, sleeve_r=120), 160),
            (replace(BASE), 120)]
    return out


def snack():
    out = []
    plan = []
    for i in range(4):
        t = (i + 1) / 4
        plan.append(dict(hand=(lerp(120, 100, t), lerp(-120, -220, t)), bites=0, eyes='wide' if i < 2 else 'happy', ms=80))
    for b in range(3):
        plan.append(dict(hand=(46, -290), bites=b, eyes='closed', mouth='open', ms=110))
        plan.append(dict(hand=(100, -220), bites=b + 1, eyes='happy', mouth='chew', ms=110))
        plan.append(dict(hand=(100, -220), bites=b + 1, eyes='happy', mouth='chew', open_=0.2, ms=110))
    plan.append(dict(hand=None, bites=3, eyes='happy', mouth='cat', ms=220))
    for i, step in enumerate(plan):
        hand = step['hand']
        p = replace(BASE, hand_r=hand, eyes=step['eyes'], mouth=step.get('mouth', 'smile'),
                    mouth_open=1 - step.get('open_', 0.6), blush=0.8, flutter=i / len(plan),
                    front=[peach(step['bites'], hand[0] + 6, hand[1] - 50)] if hand else [],
                    back=[hearts_up(i / len(plan), count=2, seed=3)] if i > len(plan) - 5 else [])
        out.append((p, step['ms']))
    return out


def tea():
    out = []
    n = 20
    for i in range(n):
        t = i / (n - 1)
        if t < 0.25:
            u = ease(t / 0.25)
            hand, level, tilt, eyes = (lerp(120, 60, u), lerp(-120, -230, u)), 1.0, 0, 'open'
        elif t < 0.7:
            u = (t - 0.25) / 0.45
            hand, level, tilt, eyes = (44, -268), 1 - u * 0.9, -0.45, 'closed'
        else:
            u = ease((t - 0.7) / 0.3)
            hand, level, tilt, eyes = (lerp(60, 120, u), lerp(-230, -120, u)), 0.1, 0, 'happy'
        p = replace(BASE, hand_r=hand if t < 0.98 else None, eyes=eyes, mouth='o' if 0.25 <= t < 0.7 else 'cat',
                    mouth_open=0.2, tilt=-6 if 0.25 <= t < 0.7 else 0, blush=0.6, flutter=t,
                    front=[tea_cup(level, hand[0] + 4, hand[1] - 40, tilt)] if t < 0.98 else [],
                    back=[sparkles((t - 0.7) / 0.3, count=4, seed=16, color=JADE)] if t > 0.7 else [])
        out.append((p, 90))
    return out


def yawn():
    out = []
    plan = [(1.0, 10, 'open', 0.0, 'cat'), (1.04, 60, 'sleepy', 0.3, 'yawn'), (1.1, 150, 'squeeze', 1.0, 'yawn'),
            (1.12, 165, 'squeeze', 1.0, 'yawn'), (1.12, 165, 'squeeze', 1.0, 'yawn'), (1.08, 120, 'closed', 0.5, 'yawn'),
            (0.97, 40, 'sleepy', 0.0, 'cat'), (1.0, 10, 'open', 0.0, 'smile')]
    for i, (sq, arms, eyes, mo, mouth) in enumerate(plan):
        out.append((replace(BASE, squash=sq, sleeve_l=arms, sleeve_r=arms, eyes=eyes, eye_open=0.55 if eyes == 'sleepy' else 1,
                            mouth=mouth, mouth_open=mo, tilt=-4 if 2 <= i <= 5 else 0, flutter=i / len(plan),
                            front=[sweat(1, side=-1)] if 3 <= i <= 5 else []), 150 if 2 <= i <= 4 else 110))
    return out


def startled():
    plan = [(0.9, 0, 'wide', 30), (1.18, 110, 'wide', 160), (1.1, 150, 'wide', 170), (1.04, 90, 'wide', 150),
            (0.86, 0, 'wide', 90), (1.04, 0, 'open', 60), (0.98, 0, 'open', 30), (1.0, 0, 'open', 10)]
    out = []
    for i, (sq, lift, eyes, arms) in enumerate(plan):
        t = i / len(plan)
        out.append((replace(BASE, squash=sq, lift=lift, eyes=eyes, brows='worried', mouth='o', sleeve_l=arms,
                            sleeve_r=arms, hair_up=0.4 if lift else 0, wind=0.8 if lift else 0.3, flutter=t * 2,
                            back=[orbit_sword(t * 1.5, 'back')] if i < 5 else [],
                            front=[orbit_sword(t * 1.5, 'front')] if i < 5 else [sweat(1)]), 70 if i < 5 else 120))
    return out


def love():
    out = []
    n = 18
    for i in range(n):
        t = i / (n - 1)
        out.append((replace(BASE, eyes='heart' if 0.1 < t < 0.9 else 'happy', blush=1, tilt=6 * osc(t), lean=3 * osc(t),
                            hand_l=(-70, -190), hand_r=(70, -190), squash=breathe(t * 2, 0.03), flutter=t,
                            back=[hearts_up(t, count=4, seed=21)],
                            front=[heart_qi(back_out(min(1, t * 3)), osc(t * 2))]), 100))
    return out


def birthday():
    out = []
    n = 24
    for i in range(n):
        t = i / (n - 1)
        u = (t * 3) % 1
        jump = 40 * hop(u) if 0.25 < t < 0.85 else 0
        out.append((replace(BASE, lift=jump, squash=1.04 if jump > 15 else 1, eyes='happy', blush=0.9,
                            sleeve_l=150 if 0.25 < t < 0.85 else 40, sleeve_r=150 if 0.25 < t < 0.85 else 40, wind=0.5,
                            flutter=t * 3, back=[peach_bun(abs(osc(t * 4)))],
                            over=[petals(t, amount=16, seed=7)] if t > 0.2 else []), 100))
    return out


def taichi():
    """A slow tai chi form: arms float up, push out to the side, sink and gather."""
    out = []
    n = 18
    for i in range(n):
        t = i / (n - 1)
        a = 2 * math.pi * t
        hl = (-120 + 60 * math.cos(a), -200 - 90 * math.sin(a))
        hr = (120 - 60 * math.cos(a + 0.6), -200 - 90 * math.sin(a + 0.6))
        out.append((replace(BASE, hand_l=hl, hand_r=hr, lean=5 * math.sin(a), squash=1 - 0.04 * math.sin(a * 2) ** 2,
                            eyes='closed', brows='calm', mouth='cat', wind=0.4, flutter=t * 2,
                            back=[qi_orbs(t, 'back', count=3, radius=200)], front=[qi_orbs(t, 'front', count=3, radius=200)]),
                    110))
    out.append((replace(BASE, eyes='happy'), 140))
    return out


def shy():
    out = []
    n = 14
    for i in range(n):
        t = i / (n - 1)
        h = ease(min(1, t * 3)) if t < 0.85 else 1 - ease((t - 0.85) / 0.15)
        p = replace(BASE, hand_r=(lerp(120, 40, h), lerp(-120, -300, h)) if h > 0.05 else None, eyes='happy', blush=1,
                    tilt=8 * osc(t * 1.5), lean=2 * osc(t * 1.5), flutter=t, back=[hearts_up(t, count=2, seed=51)])
        out.append((p, 100))
    return out


def look_around():
    plan = [((0, 0), 0, 140), ((-18, 0), -6, 260), ((-18, -4), -6, 260), ((0, 0), 0, 120), ((18, 0), 6, 260),
            ((18, -4), 6, 300), ((0, 0), 0, 140)]
    return [(replace(BASE, look=look, tilt=tilt, flutter=i / len(plan)), ms) for i, (look, tilt, ms) in enumerate(plan)]


def sword_dance():
    """The floating sword orbits, speeding up then slowing (ease-in-out), eyes following; a held salute to finish."""
    out = []
    n = 16
    for i in range(n):
        t = ease(i / (n - 1))
        phase = t * 2
        out.append((replace(BASE, hand_r=(150, -300), hand_l=(-30, -200), look=(16 * math.cos(2 * math.pi * phase), -6),
                            eyes='open' if i < 13 else 'happy', brows='calm', mouth='smirk', wind=0.6, flutter=i / 8,
                            back=[orbit_sword(phase, 'back')], front=[orbit_sword(phase, 'front')]), 85))
    out += [(replace(BASE, eyes='happy', squash=0.96, **SALUTE), 90), (replace(BASE, eyes='closed', tilt=8, **SALUTE), 320),
            (replace(BASE), 120)]
    return out


def mirrored(seq):
    return [(replace(p, face=-p.face), ms) for p, ms in seq]


SEQUENCES = {
    'idle': lambda: idle(),
    'idle_float': lambda: idle('float'),
    'idle_breeze': lambda: idle('breeze'),
    'idle_look': lambda: idle('look'),
    'idle_happy': lambda: idle('happy'),
    'idle_poor': lambda: idle('poor'),
    'meditate/start': lambda: meditate('start'),
    'meditate/loop': lambda: meditate('loop'),
    'meditate/lotus': lambda: meditate('loop', lotus_loop=True),
    'meditate/end': lambda: meditate('end'),
    'jade/start': lambda: jade('start'),
    'jade/loop': lambda: jade('loop'),
    'jade/scroll': lambda: jade('loop', alt=True),
    'jade/end': lambda: jade('end'),
    'jade/to_alchemy': jade_to_alchemy,
    'alchemy/start': lambda: alchemy('start'),
    'alchemy/loop': lambda: alchemy('loop'),
    'alchemy/pill': lambda: alchemy('loop', pill_loop=True),
    'alchemy/end': lambda: alchemy('end'),
    'alchemy/to_jade': alchemy_to_jade,
    'talisman/start': lambda: alert('start'),
    'talisman/loop': lambda: alert('loop'),
    'talisman/end': lambda: alert('end'),
    'tribulation': tribulation,
    'deviation': deviation,
    'breakthrough': breakthrough,
    'cloudnap/start': lambda: cloudnap('start'),
    'cloudnap/loop': lambda: cloudnap('loop'),
    'cloudnap/end': lambda: cloudnap('end'),
    'descend': descend,
    'ascend': ascend,
    'fury': fury,
    'fury_vanish': lambda: fury(leave=True),
    'held/start': lambda: held('start'),
    'held/loop': lambda: held('loop'),
    'held/end': lambda: held('end'),
    'pat/start': lambda: pat('start'),
    'pat/loop': lambda: pat('loop'),
    'pat/end': lambda: pat('end'),
    'tickle/start': lambda: tickle('start'),
    'tickle/loop': lambda: tickle('loop'),
    'tickle/end': lambda: tickle('end'),
    'tumble/start_right': lambda: tumble('start'),
    'tumble/right': lambda: tumble('loop'),
    'tumble/end_right': lambda: tumble('end'),
    'tumble/start_left': lambda: mirrored(tumble('start')),
    'tumble/left': lambda: mirrored(tumble('loop')),
    'tumble/end_left': lambda: mirrored(tumble('end')),
    'peek/start_left': lambda: peek('start'),
    'peek/left': lambda: peek('loop'),
    'peek/end_left': lambda: peek('end'),
    'peek/start_right': lambda: mirrored(peek('start')),
    'peek/right': lambda: mirrored(peek('loop')),
    'peek/end_right': lambda: mirrored(peek('end')),
    'petal_rain': petal_rain,
    'flute/start': lambda: play_flute('start'),
    'flute/loop': lambda: play_flute('loop'),
    'flute/end': lambda: play_flute('end'),
    'butterflies': butterfly_trick,
    'peach': snack,
    'tea': tea,
    'yawn': yawn,
    'startled': startled,
    'love': love,
    'birthday': birthday,
    'taichi': taichi,
    'shy': shy,
    'look': look_around,
    'sword_dance': sword_dance,
    'fly/mount_right': lambda: mount('start'),
    'fly/right': lambda: mount('loop'),
    'fly/rise_right': lambda: mount('loop', angle=20),
    'fly/dive_right': lambda: mount('loop', angle=-20),
    'fly/dismount_right': lambda: mount('end'),
    'fly/mount_left': lambda: mirrored(mount('start')),
    'fly/left': lambda: mirrored(mount('loop')),
    'fly/rise_left': lambda: mirrored(mount('loop', angle=20)),
    'fly/dive_left': lambda: mirrored(mount('loop', angle=-20)),
    'fly/dismount_left': lambda: mirrored(mount('end')),
}


if __name__ == '__main__':
    run(SEQUENCES, render, __doc__)
