#!/usr/bin/env python3
"""Draw Kuro, the little ninja pet, frame by frame.

    pip install pycairo pillow
    python3 scripts/draw_kuro.py OUT              # every sequence, as OUT/<sequence>/_NNN_<ms>.png
    python3 scripts/draw_kuro.py OUT idle hello   # only these sequences

Kuro is original artwork made for Agent Pet and dedicated to the public domain (licenses/KURO-ARTWORK-TERMS.md).
Everything is drawn with vector shapes on 1000 x 1000 canvases whose ground line matches VPet's, so this script is
the art's source: change it, render again, then copy the frames in with scripts/add_sequences.py --pet kuro.
Consecutive identical frames are merged into one longer frame.
"""
import math
from dataclasses import dataclass, field, replace

import cairo

from pet_art import (CHEEK, CX, GOLD, GROUND, INK, LINE, SIZE, WHITE, back_out, battery, bubble_mark, capsule, confetti,
                     dizzy, ease, ease_out, ellipse, fill_stroke, heart, hearts_up, hop, lerp, mix, notes, osc, rgb,
                     round_rect, run, smoke, sparkles, star, steam, sweat, thought_bubble, zzz, tween, ease_in, linear)


HOOD = (0.20, 0.21, 0.36)
HOOD_LIGHT = (0.27, 0.29, 0.47)
MASK = (0.24, 0.26, 0.42)
GI = (0.22, 0.24, 0.40)
GI_LIGHT = (0.31, 0.33, 0.52)
SKIN = (1.0, 0.87, 0.76)
SKIN_SHADE = (0.96, 0.76, 0.66)
SCARF = (0.86, 0.25, 0.27)
SCARF_DARK = (0.68, 0.16, 0.20)
WRAP = (0.90, 0.88, 0.84)
BAND = (0.36, 0.38, 0.50)
METAL = (0.80, 0.83, 0.88)
METAL_DARK = (0.55, 0.58, 0.66)
BELT = (0.62, 0.64, 0.74)


@dataclass
class Pose:
    x: float = 0               # body offset from the canvas centre
    lift: float = 0            # feet above the ground
    squash: float = 1          # < 1 squashed, > 1 stretched
    lean: float = 0            # degrees, around the feet
    tilt: float = 0            # head tilt in degrees, around the neck
    head_dy: float = 0
    sit: float = 0             # 0 standing .. 1 sitting cross-legged
    run: float = 0             # leg stride phase in radians, used when stride > 0
    stride: float = 0
    tuck: float = 0            # legs pulled up in a jump
    eyes: str = 'open'
    eye_open: float = 1
    look: tuple = (0, 0)
    brows: str = ''
    blush: float = 0.35
    mask: float = 1            # 1 over the mouth, 0 pulled down
    mouth: str = 'smile'
    mouth_open: float = 0.5
    arm_l: float = 12          # degrees outward from hanging down
    arm_r: float = 12
    hand_l: tuple = None       # a hand held in front, (x, y) in body space
    hand_r: tuple = None
    wind: float = 0.15         # how far the scarf and headband tails float
    flutter: float = 0         # phase of the tails' wave
    drag: float = 0            # cloth pulled up (+) or down (-) by the body's motion; see follow_through
    tint: float = 0            # angry red on the face
    pale: float = 0
    alpha: float = 1
    scale: float = 1
    face: int = 1              # -1 mirrors Kuro to face left
    shadow: bool = True
    back: list = field(default_factory=list)    # props drawn behind Kuro, f(ctx, pose)
    front: list = field(default_factory=list)   # props drawn in front, f(ctx, pose)
    over: list = field(default_factory=list)    # props drawn in canvas space after the body transform


# ---------------------------------------------------------------- Kuro, in body space: feet at y = 0, up is negative

NECK = (0, -222)
HEAD = (0, -392)
HEAD_RX, HEAD_RY = 180, 168


def ribbon(ctx, x, y, angle, length, width, wind, flutter, color, dark, sway=1.0, drag=0.0):
    """A cloth tail starting at (x, y), heading at `angle` degrees (0 = right, 90 = down)."""
    pts = []
    n = 14
    a = math.radians(angle)
    for i in range(n + 1):
        u = i / n
        wave = math.sin(flutter * 2 * math.pi + u * 5.5) * 26 * u * sway
        px = x + math.cos(a) * length * u - math.sin(a) * wave
        py = y + math.sin(a) * length * u + math.cos(a) * wave + (1 - wind) * 24 * u * u - drag * 90 * u * u
        pts.append((px, py))
    left, right = [], []
    for i, (px, py) in enumerate(pts):
        nx, ny = (pts[min(i + 1, n)][0] - pts[max(i - 1, 0)][0], pts[min(i + 1, n)][1] - pts[max(i - 1, 0)][1])
        d = math.hypot(nx, ny) or 1
        w = width * (1 - 0.35 * i / n) / 2
        left.append((px - ny / d * w, py + nx / d * w))
        right.append((px + ny / d * w, py - nx / d * w))
    ctx.move_to(*left[0])
    for p in left[1:]:
        ctx.line_to(*p)
    tip = pts[-1]
    end_dir = (pts[-1][0] - pts[-2][0], pts[-1][1] - pts[-2][1])
    d = math.hypot(*end_dir) or 1
    ctx.line_to(tip[0] + end_dir[0] / d * width * 0.45, tip[1] + end_dir[1] / d * width * 0.45)
    for p in reversed(right):
        ctx.line_to(*p)
    ctx.close_path()
    ctx.set_line_join(cairo.LINE_JOIN_ROUND)
    fill_stroke(ctx, color, LINE * 0.9)
    # A fold line down the middle.
    ctx.move_to(*pts[1])
    for p in pts[2:-2]:
        ctx.line_to(*p)
    rgb(ctx, dark, 0.8)
    ctx.set_line_width(5)
    ctx.stroke()


def draw_tails(ctx, p):
    """Scarf and headband tails, behind Kuro, blowing to the left (Kuro faces right)."""
    wind = p.wind
    angle_scarf = lerp(120, 178, wind)
    ribbon(ctx, -70, -205, angle_scarf, 165, 56, wind, p.flutter, SCARF, SCARF_DARK, drag=p.drag)
    ribbon(ctx, -55, -200, angle_scarf - 14, 135, 46, wind, p.flutter + 0.3, SCARF, SCARF_DARK, drag=p.drag)
    hx, hy = HEAD[0] - 150, HEAD[1] - 40 + p.head_dy
    angle_band = lerp(115, 182, wind)
    ribbon(ctx, hx, hy, angle_band, 140, 34, wind, p.flutter + 0.15, BAND, INK, 0.7, drag=p.drag)
    ribbon(ctx, hx + 6, hy + 8, angle_band - 16, 115, 30, wind, p.flutter + 0.45, BAND, INK, 0.7, drag=p.drag)


def draw_lap(ctx):
    # Cross-legged: a wide lap with the soles showing, drawn in front of the torso.
    ellipse(ctx, 0, -46, 150, 46)
    fill_stroke(ctx, GI)
    ctx.move_to(-20, -80)
    ctx.curve_to(-6, -60, 6, -60, 20, -80)
    rgb(ctx, GI_LIGHT)
    ctx.set_line_width(7)
    ctx.stroke()
    for side in (-1, 1):
        ellipse(ctx, side * 92, -30, 40, 22)
        fill_stroke(ctx, (0.18, 0.18, 0.24))


def draw_legs(ctx, p):
    if p.sit > 0.5:
        return
    for side in (-1, 1):
        phase = math.sin(p.run + (0 if side > 0 else math.pi)) * p.stride
        fx = side * 46 + phase * 40
        fy = -min(0.0, -abs(phase) * 0) - max(0.0, math.sin(p.run + (0 if side > 0 else math.pi))) * p.stride * 30
        fy -= p.tuck * 40
        top = -92
        ctx.save()
        round_rect(ctx, side * 46 - 30 + phase * 18, top, 60, (fy - 10) - top + 4, 22)
        fill_stroke(ctx, GI)
        ctx.restore()
        ellipse(ctx, fx + side * 6, fy - 16, 44, 22)
        fill_stroke(ctx, (0.18, 0.18, 0.24))


def arm_end(side, angle, length=100):
    sx, sy = side * 92, -188
    a = math.radians(angle)
    return sx, sy, sx + side * math.sin(a) * length, sy + math.cos(a) * length


def draw_arm(ctx, side, angle):
    sx, sy, ex, ey = arm_end(side, angle)
    capsule(ctx, sx, sy, ex, ey, 50, GI)
    ellipse(ctx, ex, ey, 27, 27)
    fill_stroke(ctx, WRAP)


def draw_hand(ctx, side, pos):
    sx, sy = side * 92, -188
    hx, hy = pos
    capsule(ctx, sx, sy, lerp(sx, hx, 0.72), lerp(sy, hy, 0.72), 48, GI)
    ellipse(ctx, hx, hy, 28, 27)
    fill_stroke(ctx, WRAP)
    # Wrapping stripes.
    rgb(ctx, INK, 0.35)
    ctx.set_line_width(4)
    for k in (-8, 6):
        ctx.move_to(hx - 18, hy + k)
        ctx.line_to(hx + 18, hy + k - 6)
        ctx.stroke()


def draw_torso(ctx, p):
    top, bottom = -228, -70
    ctx.move_to(-92, top + 6)
    ctx.curve_to(-118, top + 60, -122, bottom - 30, -110, bottom)
    ctx.line_to(110, bottom)
    ctx.curve_to(122, bottom - 30, 118, top + 60, 92, top + 6)
    ctx.close_path()
    ctx.set_line_join(cairo.LINE_JOIN_ROUND)
    fill_stroke(ctx, GI)
    # Wrap-over collar of the gi.
    ctx.move_to(-60, top + 14)
    ctx.line_to(30, -110)
    rgb(ctx, GI_LIGHT)
    ctx.set_line_width(9)
    ctx.stroke()
    # Belt with a knot.
    round_rect(ctx, -116, -122, 232, 32, 12)
    fill_stroke(ctx, BELT, LINE * 0.8)
    ellipse(ctx, 34, -106, 20, 15)
    fill_stroke(ctx, BELT, LINE * 0.7)
    for dx, dy in ((22, 10), (44, 12)):
        ctx.move_to(34 + dx * 0.4, -96)
        ctx.line_to(34 + dx * 0.4 + dx * 0.3, -96 + dy + 22)
    rgb(ctx, INK)
    ctx.set_line_width(9)
    ctx.stroke()


def draw_scarf_wrap(ctx, p):
    ellipse(ctx, 0, -222, 112, 30)
    fill_stroke(ctx, SCARF)
    rgb(ctx, SCARF_DARK)
    ctx.set_line_width(5)
    ctx.move_to(-80, -214)
    ctx.curve_to(-30, -200, 30, -200, 80, -214)
    ctx.stroke()


def head_path(ctx):
    ellipse(ctx, HEAD[0], HEAD[1], HEAD_RX, HEAD_RY)


def emblem(ctx, x, y, r):
    """Kuro's own mark: a crescent moon with a small star."""
    ctx.new_sub_path()
    ctx.arc(x - 6, y, r, 0, 2 * math.pi)
    ctx.new_sub_path()
    ctx.arc_negative(x + r * 0.45 - 6, y - r * 0.25, r * 0.85, 2 * math.pi, 0)
    ctx.set_fill_rule(cairo.FILL_RULE_EVEN_ODD)
    rgb(ctx, METAL_DARK)
    ctx.fill()
    ctx.set_fill_rule(cairo.FILL_RULE_WINDING)
    star(ctx, x + r * 0.75, y + r * 0.35, r * 0.32, METAL_DARK, outline=False)


EYE_Y = -378
EYE_DX = 58


def draw_eye(ctx, x, y, kind, p, side):
    rx, ry = 17, 24
    o = p.eye_open
    ctx.set_line_cap(cairo.LINE_CAP_ROUND)
    rgb(ctx, INK)
    if kind in ('open', 'sleepy', 'look') and o < 0.18:
        kind = 'line'
    if kind in ('open', 'look'):
        ellipse(ctx, x, y + ry * (1 - o) * 0.5, rx, ry * o)
        ctx.fill()
        if o > 0.5:
            rgb(ctx, WHITE)
            ellipse(ctx, x - 5, y - 9 * o, 6.5, 7.5)
            ctx.fill()
    elif kind == 'sleepy':
        ctx.save()
        ctx.rectangle(x - 40, y + ry - 2 * ry * o, 80, 80)
        ctx.clip()
        ellipse(ctx, x, y, rx, ry)
        ctx.fill()
        ctx.restore()
        ctx.move_to(x - rx - 6, y + ry - 2 * ry * o)
        ctx.line_to(x + rx + 6, y + ry - 2 * ry * o)
        ctx.set_line_width(8)
        ctx.stroke()
    elif kind == 'line':
        ctx.move_to(x - 20, y + 4)
        ctx.line_to(x + 20, y + 4)
        ctx.set_line_width(9)
        ctx.stroke()
    elif kind == 'happy':
        ctx.move_to(x - 22, y + 10)
        ctx.curve_to(x - 12, y - 18, x + 12, y - 18, x + 22, y + 10)
        ctx.set_line_width(10)
        ctx.stroke()
    elif kind == 'closed':
        ctx.move_to(x - 22, y - 2)
        ctx.curve_to(x - 12, y + 18, x + 12, y + 18, x + 22, y - 2)
        ctx.set_line_width(9)
        ctx.stroke()
    elif kind == 'squeeze':
        d = side
        ctx.move_to(x - 18 * d, y - 14)
        ctx.line_to(x + 14 * d, y)
        ctx.line_to(x - 18 * d, y + 14)
        ctx.set_line_width(9)
        ctx.set_line_join(cairo.LINE_JOIN_ROUND)
        ctx.stroke()
    elif kind == 'x':
        ctx.set_line_width(9)
        for a, b in ((-16, -16), (-16, 16)):
            ctx.move_to(x + a, y + b)
            ctx.line_to(x - a, y - b)
            ctx.stroke()
    elif kind == 'spiral':
        ctx.set_line_width(6)
        for i in range(60):
            a = i * 0.32 + p.flutter * 6
            r = 2 + i * 0.36
            (ctx.move_to if i == 0 else ctx.line_to)(x + math.cos(a) * r * side, y + math.sin(a) * r)
        ctx.stroke()
    elif kind == 'wide':
        ellipse(ctx, x, y, 25, 28)
        fill_stroke(ctx, WHITE, 7)
        rgb(ctx, INK)
        ellipse(ctx, x, y + 2, 8, 9)
        ctx.fill()
    elif kind == 'star':
        star(ctx, x, y, 26, GOLD, rot=0.1 * side)
    elif kind == 'heart':
        heart(ctx, x, y - 2, 22, (0.93, 0.27, 0.38))
    elif kind == 'sharp':
        # A narrowed, determined eye.
        ctx.save()
        ctx.rectangle(x - 40, y - 10, 80, 60)
        ctx.clip()
        ellipse(ctx, x, y, rx, ry)
        ctx.fill()
        ctx.restore()
        ctx.move_to(x - 22, y - 14 + 4 * side)
        ctx.line_to(x + 22, y - 14 - 4 * side)
        ctx.set_line_width(8)
        ctx.stroke()


def draw_face(ctx, p):
    hx, hy = HEAD
    # Hood with a soft top light.
    head_path(ctx)
    grad = cairo.LinearGradient(0, hy - HEAD_RY, 0, hy + HEAD_RY)
    grad.add_color_stop_rgb(0, *HOOD_LIGHT)
    grad.add_color_stop_rgb(0.55, *HOOD)
    ctx.set_source(grad)
    ctx.fill_preserve()
    rgb(ctx, INK)
    ctx.set_line_width(LINE)
    ctx.stroke()

    ctx.save()
    head_path(ctx)
    ctx.clip()
    # The opening for the eyes.
    skin = mix(mix(SKIN, (1.0, 0.62, 0.58), p.tint), (0.86, 0.90, 0.97), p.pale)
    face_top, face_bottom = -428, -334
    round_rect(ctx, -150, face_top, 300, face_bottom - face_top, 44)
    rgb(ctx, skin)
    ctx.fill_preserve()
    rgb(ctx, INK)
    ctx.set_line_width(LINE * 0.8)
    ctx.stroke()
    if p.mask < 1:
        # Mask pulled down: the lower face shows.
        drop = (1 - p.mask) * 70
        ellipse(ctx, 0, -330 + drop * 0.2, 125, 40 + drop * 0.7)
        rgb(ctx, skin)
        ctx.fill()
    # Mask cloth, with a fold.
    mask_top = face_bottom + (1 - p.mask) * 78
    ctx.move_to(-200, mask_top)
    ctx.curve_to(-80, mask_top - 6, 80, mask_top - 6, 200, mask_top)
    ctx.line_to(200, 0)
    ctx.line_to(-200, 0)
    ctx.close_path()
    rgb(ctx, MASK)
    ctx.fill_preserve()
    rgb(ctx, INK)
    ctx.set_line_width(LINE * 0.8)
    ctx.stroke()
    ctx.move_to(-90, mask_top + 36)
    ctx.curve_to(-30, mask_top + 46, 30, mask_top + 46, 90, mask_top + 36)
    rgb(ctx, HOOD_LIGHT, 0.7)
    ctx.set_line_width(5)
    ctx.stroke()
    # Forehead band and plate.
    ctx.rectangle(-200, -476, 400, 44)
    rgb(ctx, BAND)
    ctx.fill_preserve()
    rgb(ctx, INK)
    ctx.set_line_width(LINE * 0.8)
    ctx.stroke()
    ctx.restore()
    round_rect(ctx, -66, -482, 132, 56, 12)
    plate = cairo.LinearGradient(0, -482, 0, -426)
    plate.add_color_stop_rgb(0, *mix(METAL, WHITE, 0.35))
    plate.add_color_stop_rgb(1, *METAL)
    ctx.set_source(plate)
    ctx.fill_preserve()
    rgb(ctx, INK)
    ctx.set_line_width(LINE * 0.8)
    ctx.stroke()
    emblem(ctx, 0, -454, 15)
    for sx in (-50, 50):
        ellipse(ctx, sx, -454, 4, 4)
        rgb(ctx, METAL_DARK)
        ctx.fill()

    # Blush on the cheeks just above the mask.
    if p.blush > 0:
        for side in (-1, 1):
            ellipse(ctx, side * 104, -350, 26, 11)
            rgb(ctx, CHEEK, 0.75 * min(1, p.blush))
            ctx.fill()
            if p.blush > 0.8:
                rgb(ctx, CHEEK, 0.9)
                ctx.set_line_width(4)
                for k in (-8, 4, 16):
                    ctx.move_to(side * 104 + k - 6, -344)
                    ctx.line_to(side * 104 + k + 2, -356)
                    ctx.stroke()

    lx, ly = p.look
    for side in (-1, 1):
        draw_eye(ctx, side * EYE_DX + lx, EYE_Y + ly, p.eyes, p, side)
    if p.brows:
        rgb(ctx, INK)
        ctx.set_line_width(9)
        ctx.set_line_cap(cairo.LINE_CAP_ROUND)
        for side in (-1, 1):
            bx = side * EYE_DX + lx * 0.5
            by = -414
            if p.brows == 'angry':
                ctx.move_to(bx - side * 26, by - 8)
                ctx.line_to(bx + side * 2, by + 6)
            elif p.brows == 'worried':
                ctx.move_to(bx - side * 26, by + 6)
                ctx.line_to(bx + side * 18, by - 6)
            elif p.brows == 'raised':
                ctx.move_to(bx - 22, by - 6)
                ctx.curve_to(bx - 8, by - 16, bx + 8, by - 16, bx + 22, by - 6)
            elif p.brows == 'focus':
                ctx.move_to(bx - side * 24, by - 2)
                ctx.line_to(bx + side * 14, by + 2)
            ctx.stroke()
    if p.mask < 0.6:
        draw_mouth(ctx, p, 0, -304 + (1 - p.mask) * 10)
    if p.tint > 0.3:
        # Anger vein on the hood.
        vx, vy, s = 118, -505, 46 * min(1, p.tint)
        rgb(ctx, (0.90, 0.20, 0.24))
        ctx.set_line_width(11)
        for a in range(4):
            ang = a * math.pi / 2 + math.pi / 4
            cx, cy = vx + math.cos(ang) * s * 0.55, vy + math.sin(ang) * s * 0.55
            ctx.arc(cx, cy, s * 0.42, ang + math.pi * 0.75, ang + math.pi * 1.25)
            ctx.new_sub_path()
        ctx.stroke()


def draw_mouth(ctx, p, x, y):
    rgb(ctx, INK)
    ctx.set_line_width(8)
    ctx.set_line_cap(cairo.LINE_CAP_ROUND)
    o = p.mouth_open
    if p.mouth == 'smile':
        ctx.move_to(x - 18, y - 4)
        ctx.curve_to(x - 8, y + 8, x + 8, y + 8, x + 18, y - 4)
        ctx.stroke()
    elif p.mouth == 'cat':
        ctx.move_to(x - 22, y - 4)
        ctx.curve_to(x - 18, y + 8, x - 4, y + 8, x, y - 2)
        ctx.curve_to(x + 4, y + 8, x + 18, y + 8, x + 22, y - 4)
        ctx.stroke()
    elif p.mouth in ('open', 'yawn', 'o'):
        w = {'open': 22, 'yawn': 20, 'o': 10}[p.mouth] * (0.5 + o)
        h = {'open': 16, 'yawn': 28, 'o': 10}[p.mouth] * (0.4 + o)
        ellipse(ctx, x, y + h * 0.3, w, h)
        fill_stroke(ctx, (0.55, 0.18, 0.24), 7)
        if p.mouth != 'o':
            ellipse(ctx, x, y + h * 0.9, w * 0.55, h * 0.4)
            rgb(ctx, (1.0, 0.55, 0.6))
            ctx.fill()
    elif p.mouth == 'chew':
        ctx.move_to(x - 14, y)
        ctx.line_to(x - 4, y + 6 * o)
        ctx.line_to(x + 6, y)
        ctx.line_to(x + 14, y + 6 * o)
        ctx.stroke()


def draw_kuro(ctx, p):
    ctx.save()
    ctx.translate(CX + p.x, GROUND - p.lift)
    ctx.scale(p.face * p.scale / max(p.squash, 0.2) ** 0.45, p.scale * p.squash)
    ctx.rotate(math.radians(p.lean) * p.face)
    sit_drop = 64 if p.sit > 0.5 else 0
    for prop in p.back:
        prop(ctx, p)
    ctx.save()
    ctx.translate(0, sit_drop)
    draw_tails(ctx, p)
    ctx.restore()
    draw_legs(ctx, p)
    ctx.save()
    ctx.translate(0, sit_drop)
    if p.hand_l is None:
        draw_arm(ctx, -1, p.arm_l)
    if p.hand_r is None:
        draw_arm(ctx, 1, p.arm_r)
    draw_torso(ctx, p)
    if p.sit > 0.5:
        ctx.save()
        ctx.translate(0, -sit_drop)
        draw_lap(ctx)
        ctx.restore()
    draw_scarf_wrap(ctx, p)
    ctx.save()
    ctx.translate(NECK[0], NECK[1] + p.head_dy)
    ctx.rotate(math.radians(p.tilt))
    ctx.translate(-NECK[0], -NECK[1])
    draw_face(ctx, p)
    ctx.restore()
    if p.hand_l is not None:
        draw_hand(ctx, -1, p.hand_l)
    if p.hand_r is not None:
        draw_hand(ctx, 1, p.hand_r)
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
            ellipse(ctx, CX + p.x, GROUND + 2, 150 * p.scale * k * (1 + 0.3 * p.sit), 20 * k)
            rgb(ctx, (0, 0, 0), 0.16 * k * p.alpha)
            ctx.fill()
        ctx.push_group()
        draw_kuro(ctx, p)
        ctx.pop_group_to_source()
        ctx.paint_with_alpha(p.alpha)
    for prop in p.over:
        prop(ctx, p)
    return surface


# ---------------------------------------------------------------- props (body space unless noted)

def scroll(open_=1.0, glance=0.0):
    """A scroll held open in front of the chest; open_ above 1 overshoots the full width."""
    def draw(ctx, p):
        w = lerp(30, 250, ease(min(open_, 1.0))) + 250 * max(0.0, open_ - 1)
        y = -168
        if open_ > 0.05:
            round_rect(ctx, -w / 2, y - 62, w, 124, 6)
            fill_stroke(ctx, (0.99, 0.95, 0.84), 9)
            rgb(ctx, INK, 0.55)
            ctx.set_line_width(6)
            ctx.set_line_cap(cairo.LINE_CAP_ROUND)
            for col in range(int(w // 34)):
                cx = w / 2 - 30 - col * 34
                if cx < -w / 2 + 24:
                    break
                for k in range(3):
                    ctx.move_to(cx, y - 44 + k * 32)
                    ctx.line_to(cx, y - 30 + k * 32)
                    ctx.stroke()
        for side in (-1, 1):
            round_rect(ctx, side * w / 2 - 16, y - 76, 32, 152, 14)
            fill_stroke(ctx, (0.62, 0.36, 0.22), 8)
            ellipse(ctx, side * w / 2, y - 76, 18, 8)
            fill_stroke(ctx, GOLD, 6)
            ellipse(ctx, side * w / 2, y + 76, 18, 8)
            fill_stroke(ctx, GOLD, 6)
    return draw


def laptop(lid=1.0, glow=0.5, key=0):
    """A laptop on the ground in front of Kuro, lid towards the viewer; lid above 1 overshoots."""
    def draw(ctx, p):
        base_y = -12
        round_rect(ctx, -170, base_y - 22, 340, 30, 10)
        fill_stroke(ctx, (0.66, 0.69, 0.76), 9)
        h = 150 * ease(min(lid, 1.0)) + 150 * max(0.0, lid - 1)
        if h > 4:
            round_rect(ctx, -150, base_y - 22 - h, 300, h, 14)
            fill_stroke(ctx, (0.80, 0.83, 0.89), 9)
            if h > 60:
                emblem(ctx, 4, base_y - 22 - h / 2, 22)
        if glow > 0 and h > 100:
            g = cairo.RadialGradient(0, -380, 10, 0, -380, 200)
            g.add_color_stop_rgba(0, 0.55, 0.85, 1.0, 0.28 * glow)
            g.add_color_stop_rgba(1, 0.55, 0.85, 1.0, 0)
            ctx.set_source(g)
            ctx.rectangle(-200, -580, 400, 400)
            ctx.fill()
    return draw


def typing_marks(phase, strong=False):
    def draw(ctx, p):
        rgb(ctx, INK, 0.75)
        ctx.set_line_width(6 if strong else 5)
        ctx.set_line_cap(cairo.LINE_CAP_ROUND)
        for side in (-1, 1):
            if (phase + (0 if side > 0 else 0.5)) % 1 < 0.5:
                for k in range(3):
                    a = math.radians(-60 - k * 30) if side > 0 else math.radians(-120 + k * 30)
                    x0, y0 = side * 150, -205
                    ctx.move_to(x0 + math.cos(a) * 30, y0 + math.sin(a) * 30)
                    ctx.line_to(x0 + math.cos(a) * (52 if strong else 44), y0 + math.sin(a) * (52 if strong else 44))
                    ctx.stroke()
    return draw


def shuriken(x, y, angle, size=1.0, ctx_space='body'):
    def draw(ctx, p):
        ctx.save()
        ctx.translate(x, y)
        ctx.rotate(angle)
        ctx.scale(size, size)
        for i in range(4):
            a = i * math.pi / 2
            ctx.move_to(0, 0)
            ctx.line_to(math.cos(a - 0.35) * 22, math.sin(a - 0.35) * 22)
            ctx.line_to(math.cos(a) * 52, math.sin(a) * 52)
            ctx.line_to(math.cos(a + 0.35) * 22, math.sin(a + 0.35) * 22)
            ctx.close_path()
        fill_stroke(ctx, METAL, 6)
        ellipse(ctx, 0, 0, 9, 9)
        rgb(ctx, INK)
        ctx.fill()
        ctx.restore()
    return draw


def onigiri(bites, x, y):
    def draw(ctx, p):
        if bites >= 3:
            return
        ctx.save()
        ctx.translate(x, y)
        ctx.move_to(0, -52)
        ctx.curve_to(30, -50, 60, 20, 50, 34)
        ctx.curve_to(30, 46, -30, 46, -50, 34)
        ctx.curve_to(-60, 20, -30, -50, 0, -52)
        ctx.close_path()
        fill_stroke(ctx, WHITE, 8)
        round_rect(ctx, -26, 6, 52, 40, 6)
        rgb(ctx, (0.15, 0.25, 0.18))
        ctx.fill()
        # Bites taken out of the top right.
        for b in range(bites):
            ellipse(ctx, 26 - b * 22, -40 + b * 6, 22, 20)
            ctx.set_operator(cairo.OPERATOR_CLEAR)
            ctx.fill()
            ctx.set_operator(cairo.OPERATOR_OVER)
        ctx.restore()
    return draw


def teacup(level, x, y, tilt=0):
    def draw(ctx, p):
        ctx.save()
        ctx.translate(x, y)
        ctx.rotate(tilt)
        ctx.move_to(-48, -40)
        ctx.line_to(48, -40)
        ctx.curve_to(46, 10, 30, 36, 0, 36)
        ctx.curve_to(-30, 36, -46, 10, -48, -40)
        ctx.close_path()
        fill_stroke(ctx, (0.93, 0.95, 0.92), 8)
        if level > 0:
            ellipse(ctx, 0, -36 + (1 - level) * 30, 40 * (0.6 + 0.4 * level), 7)
            rgb(ctx, (0.55, 0.78, 0.45))
            ctx.fill()
        ctx.move_to(-40, -6)
        ctx.line_to(40, -6)
        rgb(ctx, (0.45, 0.65, 0.40))
        ctx.set_line_width(7)
        ctx.stroke()
        ctx.restore()
    return draw


def party_hat(ctx, p):
    ctx.save()
    ctx.translate(0, p.head_dy)
    ctx.translate(NECK[0], NECK[1])
    ctx.rotate(math.radians(p.tilt) + 0.18)
    ctx.translate(-NECK[0], -NECK[1])
    ctx.move_to(-62, -540)
    ctx.line_to(26, -700)
    ctx.line_to(78, -526)
    ctx.close_path()
    fill_stroke(ctx, (0.55, 0.75, 0.98), 9)
    for k in range(3):
        u = 0.25 + k * 0.22
        ctx.move_to(lerp(-62, 26, u), lerp(-540, -700, u))
        ctx.line_to(lerp(78, 26, u), lerp(-526, -700, u))
    rgb(ctx, (0.95, 0.40, 0.45))
    ctx.set_line_width(12)
    ctx.stroke()
    ellipse(ctx, 26, -702, 20, 20)
    fill_stroke(ctx, GOLD, 7)
    ctx.restore()


def cake(flame):
    def draw(ctx, p):
        x, y = 255, -10
        round_rect(ctx, x - 70, y - 90, 140, 90, 14)
        fill_stroke(ctx, (1.0, 0.86, 0.72), 9)
        ctx.move_to(x - 70, y - 64)
        for k in range(8):
            ctx.curve_to(x - 70 + k * 17.5 + 4, y - 44, x - 70 + k * 17.5 + 13, y - 44, x - 70 + (k + 1) * 17.5, y - 64)
        ctx.line_to(x + 70, y - 90)
        ctx.line_to(x - 70, y - 90)
        ctx.close_path()
        fill_stroke(ctx, (1.0, 0.62, 0.72), 8)
        round_rect(ctx, x - 7, y - 140, 14, 52, 5)
        fill_stroke(ctx, (0.55, 0.75, 0.98), 6)
        ctx.move_to(x, y - 176 - 6 * flame)
        ctx.curve_to(x + 16, y - 156, x + 10, y - 142, x, y - 142)
        ctx.curve_to(x - 10, y - 142, x - 16, y - 156, x, y - 176 - 6 * flame)
        fill_stroke(ctx, GOLD, 5)
    return draw


def big_star(glow):
    def draw(ctx, p):
        x, y = 0, -720
        g = cairo.RadialGradient(x, y, 10, x, y, 150)
        g.add_color_stop_rgba(0, 1, 0.95, 0.6, 0.55 * glow)
        g.add_color_stop_rgba(1, 1, 0.95, 0.6, 0)
        ctx.set_source(g)
        ctx.arc(x, y, 150, 0, 2 * math.pi)
        ctx.fill()
        star(ctx, x, y, 78, GOLD, inner=0.48)
    return draw


def motion_lines(side, strength=1.0):
    def draw(ctx, p):
        rgb(ctx, INK, 0.5 * strength)
        ctx.set_line_width(8)
        ctx.set_line_cap(cairo.LINE_CAP_ROUND)
        for k, y in enumerate((-420, -300, -160)):
            x = -side * (230 + k * 20)
            ctx.move_to(x, y)
            ctx.line_to(x - side * 90 * strength, y)
            ctx.stroke()
    return draw


def shock_marks(t):
    def draw(ctx, p):
        rgb(ctx, (0.86, 0.25, 0.27))
        ctx.set_line_width(10)
        ctx.set_line_cap(cairo.LINE_CAP_ROUND)
        s = ease_out(t)
        for a in (-150, -120, -90, -60, -30):
            r1, r2 = 220 + 20 * s, 240 + 70 * s
            ra = math.radians(a)
            ctx.move_to(math.cos(ra) * r1, -390 + math.sin(ra) * r1)
            ctx.line_to(math.cos(ra) * r2, -390 + math.sin(ra) * r2)
            ctx.stroke()
    return draw


# ---------------------------------------------------------------- sequences: lists of (Pose, duration_ms)

BASE = Pose()


def breathe(t, amount=0.018):
    return 1 + amount * math.sin(2 * math.pi * t)


def frames(n, ms, fn):
    return [(fn(i / n, i), ms) for i in range(n)]


def idle(kind='plain'):
    """Breathing loop: up over 1.2 s, down over 1.2 s, the head a little behind the body; one blink at 1.9 s."""
    out = []
    n = 16
    for i in range(n):
        t = i / n
        p = replace(BASE, squash=breathe(t), tilt=1.5 * math.sin(2 * math.pi * (t - 0.15)), flutter=t - 0.25,
                    wind=0.15 + 0.05 * osc(t))
        if kind == 'happy':
            p = replace(p, squash=breathe(t, 0.03), lift=6 * max(0, osc(t, 0.5)), eyes='happy' if 6 <= i < 11 else 'open',
                        blush=0.8, arm_l=18 + 6 * osc(t), arm_r=18 + 6 * osc(t, 1, 0.5), wind=0.35)
        elif kind == 'poor':
            p = replace(p, squash=0.95 + 0.012 * osc(t), eyes='sleepy', eye_open=0.55, brows='worried', pale=0.35,
                        tilt=-5, wind=0.0, arm_l=6, arm_r=6, blush=0.1)
        elif kind == 'look':
            look = [(0, 0)] * 3 + [(-16, 0)] * 4 + [(16, -2)] * 5 + [(0, 0)] * 4
            p = replace(p, look=look[i], tilt=look[i][0] * 0.3)
        elif kind == 'breeze':
            gust = hop(t)
            p = replace(p, wind=0.15 + 0.8 * gust, flutter=t * 2, eyes='happy' if 0.3 < t < 0.7 else 'open',
                        lean=-3 * gust)
        out.append((p, 150))
    if kind != 'poor':
        at = 12
        p = out[at][0]
        out[at:at + 1] = [(replace(p, eye_open=0.4), 50), (replace(p, eye_open=0.05), 70), (replace(p, eye_open=0.4), 50)]
    return out


def seal_pose(t, i, eyes='closed', pulse=0.0, bubble=None):
    hands = (-8, -150 - 4 * pulse)
    return replace(BASE, hand_l=(hands[0] - 14, hands[1]), hand_r=(hands[0] + 14, hands[1] - 4), eyes=eyes,
                   brows='focus', squash=breathe(t, 0.012), flutter=t, wind=0.2, front=[], over=[],
                   back=[bubble] if bubble else [])


def think_start():
    """Eyes flick up first, then the hands lock into a seal with a small overshoot as the bubble pops in."""
    rest = replace(BASE, hand_l=(-150, -110), hand_r=(150, -110))
    return tween(rest, [
        (2, 70, ease_out, dict(look=(12, -10), brows='raised')),
        (4, [60, 60, 70, 90], back_out, dict(hand_l=(-22, -150), hand_r=(6, -154), brows=''),
         lambda p, u: replace(p, back=[thought_bubble(size=min(1.0, 0.25 + u))], flutter=u)),
    ])


def think_loop(idea=False):
    """Dots pulse in turn; one finger taps on an uneven beat; a slight sway."""
    taps = {1, 2, 6, 9, 10}
    out = []
    n = 14
    for i in range(n):
        t = i / n
        p = replace(BASE, hand_l=(-22, -150), hand_r=(6, -160 if i in taps else -152), look=(12 + 3 * osc(t), -10),
                    squash=breathe(t, 0.012), lean=2 * osc(t), flutter=t,
                    back=[thought_bubble(pulse=t * 2, bulb=(0.5 + 0.5 * osc(t, 0.5)) if idea else 0)],
                    brows='raised' if idea else '')
        if idea:
            p = replace(p, eyes='wide' if i < 4 else 'happy', blush=0.7)
        out.append((p, 90 if i in taps else 120))
    return out


def think_end():
    """The bubble squashes and pops in a burst; the hands drop, dip past rest and settle."""
    held = replace(BASE, hand_l=(-22, -150), hand_r=(6, -154), look=(12, -10))
    out = [(replace(held, back=[thought_bubble(size=1, scale=1.12, squash=1)]), 70),
           (replace(held, back=[sparkles(0.35, count=6, radius=110, cy=-640, seed=5)], look=(4, -4),
                    hand_l=(-80, -130), hand_r=(70, -132)), 70),
           (replace(BASE, hand_l=(-148, -96), hand_r=(148, -96), back=[sparkles(0.6, count=6, radius=130, cy=-640, seed=5)]),
            80),
           (replace(BASE, squash=0.98), 80),
           (replace(BASE), 90)]
    return out


def read_start():
    """The scroll comes up from below on an ease-out and unrolls with a small overshoot."""
    rest = replace(BASE, hand_l=(-150, -110), hand_r=(150, -110))
    return tween(rest, [
        (3, 70, ease_out, dict(hand_l=(-110, -168), hand_r=(110, -168), look=(0, 8), tilt=3),
         lambda p, u: replace(p, front=[scroll(open_=0.02)])),
        (3, [60, 70, 90], linear, dict(hand_l=(-125, -168), hand_r=(125, -168), tilt=4),
         lambda p, u: replace(p, front=[scroll(open_=(0.6, 1.1, 1.0)[min(2, int(u * 3 - 1e-9))])])),
    ])


def read_loop():
    """Saccades: the eyes rest at the top of a column, sweep down it, and snap to the next; a slow nod; one blink."""
    out = []
    columns = (16, 0, -16)
    for c, x in enumerate(columns):
        steps = [(-2, 160), (-2, 160), (4, 110), (9, 110), (14, 110), (6, 60)]
        for k, (y, ms) in enumerate(steps):
            px = x if k < 5 else columns[(c + 1) % 3]
            p = replace(BASE, hand_l=(-125, -168), hand_r=(125, -168), look=(px, y), tilt=3 + 0.25 * (y + 2),
                        flutter=(c * 6 + k) / 18, squash=breathe((c * 6 + k) / 18, 0.01), front=[scroll()], blush=0.35)
            if c == 1 and k == 1:
                p = replace(p, eye_open=0.1)
            out.append((p, ms))
    return out


def read_end():
    """The scroll rolls shut on an ease-in and drops away."""
    held = replace(BASE, hand_l=(-125, -168), hand_r=(125, -168), look=(0, 8), tilt=4)
    return tween(held, [
        (3, 60, ease_in, dict(hand_l=(-110, -168), hand_r=(110, -168)),
         lambda p, u: replace(p, front=[scroll(open_=1 - u)])),
        (2, 80, ease_out, dict(hand_l=(-150, -110), hand_r=(150, -110), look=(0, 0), tilt=0)),
        (1, 90, linear, dict(hand_l=None, hand_r=None)),
    ])


def work_pose(t, tap_l=0.0, tap_r=0.0, lid=1.0, fast=False, glow=0.6, lean_in=0.0):
    hands_y = -60
    hit = max(tap_l, tap_r)
    return replace(BASE, hand_l=(-140, hands_y - 18 * tap_l), hand_r=(140, hands_y - 18 * tap_r), look=(0, 8),
                   head_dy=-6 - 6 * lean_in, squash=breathe(t, 0.01) - 0.012 * hit, flutter=t,
                   eyes='sharp' if fast else 'open', brows='focus' if fast else '', front=[laptop(lid=lid, glow=glow)],
                   back=[typing_marks(0.0 if tap_r else 0.5, fast)] if lid >= 1 and hit else [])


def work_start():
    """The lid opens with a slight overshoot and Kuro leans in."""
    lids = [0.2, 0.55, 0.9, 1.12, 0.97, 1.0]
    return [(work_pose(i / 6, lid=lid, glow=min(1, lid) * 0.6, lean_in=min(1, i / 4)), ms)
            for i, (lid, ms) in enumerate(zip(lids, [60, 60, 60, 80, 70, 90]))]


def work_loop(fast=False):
    """Taps in an uneven rhythm (tap-tap, pause, tap-tap-tap), the body bobbing on each hit, the glow pulsing."""
    pattern = 'LR-LRL--RLR-' if fast else 'L-R-LR--L-R-'
    out = []
    for i, key in enumerate(pattern):
        t = i / len(pattern)
        p = work_pose(t, tap_l=1.0 if key == 'L' else 0.0, tap_r=1.0 if key == 'R' else 0.0, fast=fast,
                      glow=0.6 + 0.15 * osc(t * 3), lean_in=1)
        out.append((p, (60 if key != '-' else 110) if fast else (90 if key != '-' else 140)))
    return out


def work_end():
    """The lid shuts on an ease-in, Kuro leans back and holds a satisfied squint."""
    lids = [0.8, 0.45, 0.1, 0.0]
    out = [(work_pose(i / 4, lid=lid, glow=lid * 0.6, lean_in=1 - i / 3), 60) for i, lid in enumerate(lids)]
    out.append((replace(BASE, lean=-4, eyes='happy', arm_l=40, arm_r=40, squash=1.03), 120))
    out.append((replace(BASE, lean=-2, eyes='happy', arm_l=30, arm_r=30), 300))
    return out


def alert_start():
    """A quick crouch, a hop with a stretch as the arm shoots up and the '!' pops with overshoot, a squash on landing."""
    plan = [(0.86, 0, 20, 0.0, 90), (1.1, 40, 120, 0.7, 50), (1.08, 62, 165, 1.15, 50), (1.0, 40, 160, 1.0, 50),
            (0.9, 0, 150, 1.0, 60), (1.0, 0, 150, 1.0, 60)]
    return [(replace(BASE, squash=sq, lift=lift, arm_r=arm, eyes='wide', brows='raised', tuck=lift / 120,
                     front=[bubble_mark('!', size=size)] if size else [], flutter=i / 6, wind=0.4), ms)
            for i, (sq, lift, arm, size, ms) in enumerate(plan)]


def alert_loop():
    """The waving hand swings on an arc; each swing lands a small hop with a squash."""
    out = []
    n = 10
    for i in range(n):
        t = i / n
        swing = ease(0.5 + 0.5 * math.sin(2 * math.pi * t * 2))
        bounce = hop((t * 2) % 1)
        p = replace(BASE, lift=14 * bounce, squash=0.94 if bounce < 0.15 else 1.0 + 0.03 * bounce,
                    arm_r=130 + 45 * swing, eyes='open', brows='raised', look=(6, -4),
                    front=[bubble_mark('!', wobble=osc(t))], flutter=t * 2, wind=0.4, tilt=-4 * (swing - 0.5) * 2)
        out.append((p, 80))
    return out


def alert_end():
    out = []
    for i in range(4):
        t = (i + 1) / 4
        p = replace(BASE, arm_r=lerp(150, 12, ease(t)), front=[bubble_mark('!', size=1 - t)], flutter=t)
        out.append((p, 70))
    return out


def oops():
    """Fast hit, held impact, slow settle: a flinch, the squash held 0.2 s, dizzy stars, a slow recovery, a shake-off."""
    puff = lambda u: [smoke(u, x=120, y=-560, spread=0.32)]
    out = [(replace(BASE, squash=1.14, eyes='wide', brows='worried', over=puff(0.15)), 60),
           (replace(BASE, squash=0.82, eyes='squeeze', brows='worried', arm_l=60, arm_r=60, over=puff(0.3)), 200)]
    for k in range(8):
        u = k / 8
        out.append((replace(BASE, squash=0.88 + 0.03 * osc(u, 0.5), eyes='spiral', flutter=u, tilt=8 * osc(u, 0.5),
                            brows='worried', back=[dizzy(u)], front=[sweat(1)], over=puff(0.4 + 0.6 * u), wind=0.0,
                            arm_l=40, arm_r=40), 90))
    for k, sq in enumerate((0.92, 0.96, 0.99)):
        out.append((replace(BASE, squash=sq, eyes='squeeze' if k < 2 else 'open', brows='worried',
                            front=[sweat(1 - k / 3)]), 120))
    for tilt in (8, -5, 2):
        out.append((replace(BASE, tilt=tilt, eyes='squeeze', arm_l=30, arm_r=30), 70))
    out.append((replace(BASE), 120))
    return out


def tired():
    out = []
    n = 14
    for i in range(n):
        t = i / n
        p = replace(BASE, sit=1, squash=0.97 + 0.012 * osc(t), eyes='sleepy', eye_open=0.45 + 0.1 * osc(t),
                    brows='worried', pale=0.45, tilt=-8 + 2 * osc(t), wind=0.0, flutter=t, arm_l=4, arm_r=4,
                    blush=0.0, front=[battery(i % 7 < 4)], shadow=True)
        out.append((p, 150))
    return out


def done():
    out = []
    plan = [(0.86, 0, 30, 'happy'), (0.9, 0, 40, 'happy'), (1.12, 60, 150, 'happy'), (1.1, 120, 165, 'star'),
            (1.04, 150, 170, 'star'), (1.0, 120, 165, 'star'), (1.04, 60, 150, 'happy'), (0.84, 0, 120, 'happy'),
            (1.05, 0, 90, 'happy'), (0.98, 0, 40, 'happy'), (1.0, 0, 20, 'happy'), (1.0, 0, 12, 'open')]
    for i, (sq, lift, arms, eyes) in enumerate(plan):
        t = i / len(plan)
        p = replace(BASE, squash=sq, lift=lift, arm_l=arms, arm_r=arms, eyes=eyes, blush=0.8, tuck=min(1, lift / 120),
                    wind=0.3 + lift / 300, flutter=t * 2, over=[], back=[sparkles(t * 1.2, count=8, seed=4)])
        out.append((p, 90 if i < len(plan) - 1 else 160))
    return out


def sleep_start():
    """Sits, then the classic doze: the head nods, jerks back up, nods again and stays down."""
    s = dict(sit=1, arm_l=6, arm_r=6, wind=0.0)
    return [(replace(BASE, squash=0.98, eyes='sleepy', eye_open=0.7), 110),
            (replace(BASE, **s, squash=0.97, eyes='sleepy', eye_open=0.5, tilt=-3), 120),
            (replace(BASE, **s, squash=0.96, eyes='sleepy', eye_open=0.15, tilt=-12, head_dy=10), 160),
            (replace(BASE, **s, squash=0.98, eyes='open', eye_open=0.8, tilt=2, head_dy=-4), 90),
            (replace(BASE, **s, squash=0.97, eyes='sleepy', eye_open=0.4, tilt=-4), 140),
            (replace(BASE, **s, squash=0.96, eyes='sleepy', eye_open=0.1, tilt=-8, head_dy=6), 160),
            (replace(BASE, **s, squash=0.96, eyes='closed', tilt=-10, head_dy=4, back=[zzz(0.2)]), 180)]


def sleep_loop():
    out = []
    n = 16
    for i in range(n):
        t = i / n
        p = replace(BASE, sit=1, squash=0.96 + 0.015 * osc(t), eyes='closed', tilt=-10 + 2 * osc(t), wind=0.0,
                    flutter=t * 0.5, arm_l=6, arm_r=6, blush=0.55, back=[zzz(t)])
        out.append((p, 150))
    return out


def sleep_end():
    """Startled awake, stands, a big stretch held at the top, then settles."""
    return [(replace(BASE, sit=1, squash=1.04, eyes='wide', brows='raised', tilt=0, arm_l=20, arm_r=20), 100),
            (replace(BASE, sit=1, squash=1.0, eyes='wide', brows='raised', arm_l=20, arm_r=20), 160),
            (replace(BASE, squash=0.94, eyes='open', arm_l=40, arm_r=40), 100),
            (replace(BASE, squash=1.12, eyes='closed', arm_l=160, arm_r=160), 120),
            (replace(BASE, squash=1.15, eyes='closed', arm_l=172, arm_r=172, mask=1), 260),
            (replace(BASE, squash=0.95, eyes='happy', arm_l=50, arm_r=50), 110),
            (replace(BASE, squash=1.02, eyes='happy', arm_l=20, arm_r=20), 100),
            (replace(BASE), 120)]


def hello():
    """A smoke burst; Kuro pops out squashed, stretches up past its height, settles, waves twice; the smoke clears last."""
    sm = lambda u: [smoke(u, y=-290, spread=1.1)]
    out = [(replace(BASE, alpha=0, shadow=False, over=sm(0.12)), 70),
           (replace(BASE, alpha=0, shadow=False, over=sm(0.3)), 70),
           (replace(BASE, squash=0.78, eyes='closed', over=sm(0.42)), 70),
           (replace(BASE, squash=1.16, lift=24, eyes='happy', arm_l=40, arm_r=60, over=sm(0.52)), 70),
           (replace(BASE, squash=0.94, eyes='happy', arm_r=110, over=sm(0.62)), 80),
           (replace(BASE, squash=1.0, eyes='happy', arm_r=150, over=sm(0.7)), 80)]
    for k in range(8):
        u = k / 8
        swing = ease(0.5 + 0.5 * math.sin(2 * math.pi * u * 2))
        out.append((replace(BASE, eyes='happy', arm_r=135 + 40 * swing, blush=0.7, flutter=u * 2, wind=0.35,
                            tilt=-3 * (swing - 0.5) * 2, over=sm(0.75 + 0.25 * u) if u < 0.9 else [],
                            back=[sparkles(u, count=5, seed=8)]), 80))
    out.append((replace(BASE, eyes='open', blush=0.5), 140))
    return out


def bye():
    """Two waves, a hand seal, a crouch, a leap, and Kuro vanishes in smoke at the top of the jump."""
    out = []
    for k in range(8):
        u = k / 8
        swing = ease(0.5 + 0.5 * math.sin(2 * math.pi * u * 2))
        out.append((replace(BASE, eyes='happy', arm_r=135 + 40 * swing, blush=0.7, flutter=u * 2, wind=0.35,
                            tilt=-3 * (swing - 0.5) * 2), 85))
    seal = dict(hand_l=(-20, -150), hand_r=(6, -154), eyes='closed', brows='focus')
    out += [(replace(BASE, **seal), 90), (replace(BASE, **seal, squash=0.98), 120),
            (replace(BASE, **seal, squash=0.84), 140),
            (replace(BASE, **seal, squash=1.14, lift=60, tuck=0.4), 60),
            (replace(BASE, **seal, squash=1.08, lift=120, tuck=0.6, over=[smoke(0.15, y=-410, spread=1.0, seed=9)]), 60),
            (replace(BASE, **seal, squash=1.0, lift=140, tuck=0.6, alpha=0.5, shadow=False,
                     over=[smoke(0.35, y=-430, spread=1.0, seed=9)]), 70)]
    for u in (0.5, 0.65, 0.8, 0.95):
        out.append((replace(BASE, alpha=0, shadow=False, over=[smoke(u, y=-430, spread=1.0, seed=9)]), 80))
    out.append((replace(BASE, alpha=0, shadow=False), 200))
    return out


def angry(leave=False):
    out = []
    if not leave:
        n = 16
        for i in range(n):
            t = i / (n - 1)
            stomp = abs(osc(t, 0.25)) if t < 0.6 else 0
            p = replace(BASE, tint=ease(min(1, t * 2)), brows='angry', eyes='open', squash=1 - 0.06 * stomp,
                        lift=0, arm_l=lerp(12, 60, ease(t)), arm_r=lerp(12, 60, ease(t)), wind=0.5, flutter=t * 3,
                        back=[steam(t * 2)] if t > 0.25 else [], tilt=4 * osc(t, 0.25))
            out.append((p, 85))
        return out
    n = 18
    for i in range(n):
        t = i / (n - 1)
        if t < 0.4:
            u = t / 0.4
            p = replace(BASE, tint=0.7, brows='angry', eyes='closed', tilt=lerp(0, -14, ease(u)), lean=-4 * ease(u),
                        arm_l=60, arm_r=60, wind=0.4, flutter=u, back=[steam(u)])
        else:
            u = (t - 0.4) / 0.6
            p = replace(BASE, tint=0.7, brows='angry', eyes='closed', tilt=-14, alpha=1 - ease(min(1, u * 1.5)),
                        hand_l=(-20, -150), hand_r=(4, -152), over=[smoke(0.05 + u * 0.95, y=-290, seed=11)])
        out.append((p, 85))
    out.append((replace(BASE, alpha=0, shadow=False), 200))
    return out


def held_start():
    out = []
    for i in range(5):
        t = (i + 1) / 5
        p = replace(BASE, squash=lerp(1, 1.14, ease(t)), lift=lerp(0, 40, t), eyes='wide', brows='worried',
                    arm_l=lerp(12, 140, t), arm_r=lerp(12, 140, t), tuck=0.3, shadow=False, wind=0.0, flutter=t)
        out.append((p, 60))
    return out


def held_loop():
    out = []
    n = 12
    for i in range(n):
        t = i / n
        p = replace(BASE, squash=1.12 + 0.03 * osc(t, 0.5), lift=40, lean=10 * osc(t), eyes='open', brows='worried',
                    look=(0, 6), arm_l=130 + 20 * osc(t, 0.5), arm_r=130 + 20 * osc(t, 0.5, 0.5), run=t * 4 * math.pi,
                    stride=0.6, shadow=False, wind=0.0, flutter=t * 2, front=[sweat(1, side=-1)])
        out.append((p, 80))
    return out


def held_end():
    plan = [(1.0, 20, 'wide'), (0.82, 0, 'squeeze'), (1.06, 0, 'squeeze'), (0.97, 0, 'open'), (1.0, 0, 'open')]
    out = []
    for i, (sq, lift, eyes) in enumerate(plan):
        p = replace(BASE, squash=sq, lift=lift, eyes=eyes, brows='worried' if i < 3 else '', arm_l=40 - i * 7,
                    arm_r=40 - i * 7, flutter=i / 5)
        out.append((p, 70))
    return out


def pat(phase):
    out = []
    if phase == 'start':
        for i in range(4):
            t = (i + 1) / 4
            out.append((replace(BASE, squash=lerp(1, 0.92, ease(t)), eyes='happy' if t > 0.4 else 'open',
                                blush=lerp(0.35, 1, t), tilt=lerp(0, 5, t)), 60))
    elif phase == 'loop':
        n = 10
        for i in range(n):
            t = i / n
            out.append((replace(BASE, squash=0.92 + 0.025 * osc(t, 0.5), eyes='happy', blush=1, tilt=5 * osc(t),
                                arm_l=30, arm_r=30, back=[hearts_up(t, count=3, seed=6)], flutter=t), 90))
    else:
        for i, sq in enumerate((1.06, 0.98, 1.0)):
            out.append((replace(BASE, squash=sq, eyes='happy' if i < 2 else 'open', blush=0.7), 70))
    return out


def poke(phase):
    out = []
    if phase == 'start':
        for i, sq in enumerate((0.9, 1.08, 0.96)):
            out.append((replace(BASE, squash=sq, eyes='squeeze', brows='raised', lean=4 - i * 4), 60))
    elif phase == 'loop':
        n = 10
        for i in range(n):
            t = i / n
            out.append((replace(BASE, squash=1 + 0.04 * osc(t, 0.25), lean=5 * osc(t, 0.5), eyes='squeeze', blush=0.9,
                                arm_l=40 + 10 * osc(t, 0.5), arm_r=40 + 10 * osc(t, 0.5, 0.5), flutter=t,
                                mask=1, back=[sparkles(t, count=4, radius=260, seed=12)]), 75))
    else:
        for i, sq in enumerate((1.05, 0.98, 1.0)):
            out.append((replace(BASE, squash=sq, eyes='happy' if i < 2 else 'open', blush=0.6), 70))
    return out


def cheer():
    """Crouch, launch stretched, hold at the top, land squashed; a second, smaller jump; confetti throughout."""
    plan = [  # squash, lift, arms (left, right), ms
        (0.86, 0, (40, 40), 80), (0.84, 0, (30, 30), 70),
        (1.12, 50, (150, 120), 50), (1.1, 95, (165, 150), 60), (1.02, 112, (170, 165), 120),
        (1.05, 85, (165, 150), 60), (1.06, 35, (150, 140), 50), (0.84, 0, (120, 120), 90),
        (1.03, 0, (140, 160), 70), (0.9, 0, (120, 130), 70),
        (1.08, 35, (160, 150), 60), (1.02, 52, (170, 165), 90), (1.05, 25, (160, 150), 60),
        (0.9, 0, (120, 120), 80), (1.02, 0, (60, 60), 90), (1.0, 0, (20, 20), 160)]
    total = sum(ms for *_, ms in plan)
    out, at = [], 0
    for sq, lift, (al, ar), ms in plan:
        out.append((replace(BASE, squash=sq, lift=lift, arm_l=al, arm_r=ar, eyes='happy', blush=0.9, tuck=lift / 110,
                            wind=0.5, flutter=at / 400, over=[confetti(at / total)]), ms))
        at += ms
    return out


def dance(phase):
    out = []
    if phase == 'start':
        for i in range(4):
            t = (i + 1) / 4
            out.append((replace(BASE, squash=lerp(1, 0.94, t), eyes='happy', arm_l=lerp(12, 60, t), arm_r=lerp(12, 60, t)), 70))
    elif phase == 'loop':
        n = 12
        for i in range(n):
            t = i / n
            side = osc(t)
            p = replace(BASE, lean=12 * side, tilt=8 * side, squash=0.96 + 0.05 * abs(osc(t, 0.5)), eyes='happy',
                        arm_l=90 + 70 * max(0, side), arm_r=90 + 70 * max(0, -side), blush=0.7, run=t * 2 * math.pi,
                        stride=0.35, lift=10 * abs(osc(t, 0.5)), wind=0.5, flutter=t * 2, back=[notes(t)])
            out.append((p, 90))
    else:
        for i, (sq, arms) in enumerate(((1.06, 165), (1.06, 165), (0.96, 60), (1.0, 12))):
            out.append((replace(BASE, squash=sq, arm_l=arms, arm_r=arms, eyes='happy' if i < 3 else 'open',
                                back=[sparkles(i / 4, seed=14)]), 110 if i < 2 else 80))
    return out


def snack():
    """Out comes a rice ball, the mask goes down, three bites chewed at an uneven pace, a happy wiggle, mask up."""
    out = []
    mouth_near = (40, -300)
    plan = []
    for i in range(3):
        t = (i + 1) / 3
        plan.append(dict(hand=(lerp(150, 120, ease_out(t)), lerp(-110, -200, ease_out(t))), mask=1, bites=0,
                         eyes='wide' if i < 2 else 'happy', ms=(70, 80, 160)[i]))
    for i in range(2):
        plan.append(dict(hand=(120, -200), mask=0.5 - 0.5 * i, bites=0, eyes='happy', ms=80))
    chews = ((110, 90, 140), (90, 80, 120), (120, 100, 180))
    for b in range(3):
        a, c1, c2 = chews[b]
        plan.append(dict(hand=mouth_near, mask=0, bites=b, eyes='closed', mouth='open', ms=a))
        plan.append(dict(hand=(110, -210), mask=0, bites=b + 1, eyes='happy', mouth='chew', ms=c1))
        plan.append(dict(hand=(110, -210), mask=0, bites=b + 1, eyes='happy', mouth='chew', open_=0.2, ms=c2))
    for lean in (5, -5, 3):
        plan.append(dict(hand=None, mask=0, bites=3, eyes='happy', mouth='cat', lean=lean, ms=90))
    plan.append(dict(hand=None, mask=0.5, bites=3, eyes='happy', mouth='smile', ms=80))
    plan.append(dict(hand=None, mask=1, bites=3, eyes='happy', ms=220))
    for i, step in enumerate(plan):
        hand = step['hand']
        props = [onigiri(step['bites'], hand[0] + 6, hand[1] - 50)] if hand else []
        p = replace(BASE, hand_r=hand, mask=step['mask'], mouth=step.get('mouth', 'smile'), eyes=step['eyes'],
                    mouth_open=1 - step.get('open_', 0.6), blush=0.8, front=props, flutter=i / len(plan),
                    lean=step.get('lean', 0), arm_l=30 if step.get('lean') else 12,
                    back=[hearts_up(i / len(plan), count=2, seed=3)] if i > len(plan) - 6 else [])
        out.append((p, step['ms']))
    return out


def drink():
    out = []
    n = 22
    for i in range(n):
        t = i / (n - 1)
        if t < 0.25:
            u = ease(t / 0.25)
            hand, mask, level, tilt, eyes = (lerp(150, 70, u), lerp(-110, -230, u)), 1 - u, 1.0, 0, 'open'
        elif t < 0.7:
            u = (t - 0.25) / 0.45
            hand, mask, level, tilt, eyes = (52, -270), 0, 1 - u * 0.9, -0.5, 'closed'
        else:
            u = ease((t - 0.7) / 0.3)
            hand, mask, level, tilt, eyes = (lerp(70, 150, u), lerp(-230, -110, u)), u, 0.1, 0, 'happy'
        cup = teacup(level, hand[0] + 4, hand[1] - 46, tilt)
        p = replace(BASE, hand_r=hand if t < 0.98 else None, mask=mask, mouth='o' if 0.25 <= t < 0.7 else 'smile',
                    mouth_open=0.2, eyes=eyes, tilt=-6 if 0.25 <= t < 0.7 else 0, blush=0.6, front=[cup] if t < 0.98 else [],
                    flutter=t, back=[sparkles((t - 0.7) / 0.3, count=4, seed=16)] if t > 0.7 else [])
        out.append((p, 90))
    return out


def yawn():
    out = []
    plan = [(1.0, 12, 'open', 1, 0.0), (1.04, 60, 'sleepy', 0.6, 0.3), (1.1, 150, 'squeeze', 0.0, 1.0),
            (1.12, 165, 'squeeze', 0.0, 1.0), (1.12, 165, 'squeeze', 0.0, 1.0), (1.08, 130, 'closed', 0.3, 0.6),
            (0.97, 40, 'sleepy', 1, 0.0), (1.0, 12, 'sleepy', 1, 0.0), (1.0, 12, 'open', 1, 0.0)]
    for i, (sq, arms, eyes, mask, mo) in enumerate(plan):
        p = replace(BASE, squash=sq, arm_l=arms, arm_r=arms, eyes=eyes, eye_open=0.55 if eyes == 'sleepy' else 1,
                    mask=mask, mouth='yawn', mouth_open=mo, flutter=i / len(plan), tilt=-4 if 2 <= i <= 5 else 0,
                    front=[sweat(1, side=-1)] if 3 <= i <= 5 else [])
        out.append((p, 150 if 2 <= i <= 4 else 110))
    return out


def startled():
    plan = [(0.9, 0, 'wide', 30), (1.2, 110, 'wide', 160), (1.12, 150, 'wide', 170), (1.05, 90, 'wide', 150),
            (0.85, 0, 'wide', 90), (1.04, 0, 'open', 60), (0.98, 0, 'open', 30), (1.0, 0, 'open', 12)]
    out = []
    for i, (sq, lift, eyes, arms) in enumerate(plan):
        t = i / len(plan)
        p = replace(BASE, squash=sq, lift=lift, eyes=eyes, brows='worried', arm_l=arms, arm_r=arms, tuck=lift / 150,
                    wind=0.8 if lift else 0.3, flutter=t * 2, back=[shock_marks(min(1, t * 2))] if i < 5 else [],
                    front=[sweat(1)] if i >= 4 else [])
        out.append((p, 70 if i < 5 else 120))
    return out


def love():
    out = []
    n = 18
    for i in range(n):
        t = i / (n - 1)
        p = replace(BASE, eyes='heart' if 0.1 < t < 0.9 else 'happy', blush=1, tilt=6 * osc(t), lean=3 * osc(t),
                    hand_l=(-70, -170), hand_r=(70, -170), squash=breathe(t * 2, 0.03), flutter=t,
                    back=[hearts_up(t, count=4, seed=21)],
                    front=[lambda ctx, p, s=back_out(min(1, t * 3)): heart(ctx, 0, -175, 46 * s, (0.95, 0.35, 0.45))])
        out.append((p, 100))
    return out


def birthday():
    out = []
    n = 24
    for i in range(n):
        t = i / (n - 1)
        u = (t * 3) % 1
        lift = 40 * hop(u) if 0.25 < t < 0.85 else 0
        p = replace(BASE, lift=lift, squash=1.05 if lift > 15 else 1, eyes='happy', blush=0.9, tuck=lift / 60,
                    arm_l=150 + 15 * osc(t * 3) if 0.25 < t < 0.85 else 40, arm_r=150 + 15 * osc(t * 3, 1, 0.5) if 0.25 < t < 0.85 else 40,
                    front=[party_hat], back=[cake(abs(osc(t * 4)))], over=[confetti(t, seed=7)] if t > 0.2 else [],
                    wind=0.5, flutter=t * 3)
        out.append((p, 100))
    return out


def milestone():
    out = []
    n = 20
    for i in range(n):
        t = i / (n - 1)
        up = ease(min(1, t * 3))
        arms = lerp(12, 160, up)
        star_y = lerp(-140, -700, up)
        star_x = lerp(150, 0, up)
        p = replace(BASE, arm_l=arms, arm_r=arms, eyes='star' if t > 0.3 else 'wide', blush=0.9,
                    squash=lerp(1, 1.06, up) + 0.015 * osc(t * 2), lift=10 * max(0, osc(t * 2)) if t > 0.35 else 0,
                    tuck=0.2 * up, wind=0.5, flutter=t * 2, back=[sparkles(t, count=10, radius=380, cy=-560, seed=31)])
        if up > 0.95:
            p = replace(p, front=[big_star(0.6 + 0.4 * osc(t * 2))])
        else:
            p = replace(p, front=[lambda ctx, p, x=star_x, y=star_y, r=lerp(40, 78, up): star(ctx, x, y, r, GOLD, inner=0.48)])
        if t > 0.9:
            p = replace(p, arm_l=12, arm_r=12, front=[], eyes='happy', lift=0, squash=1, tuck=0)
        out.append((p, 100))
    return out


def stretch():
    out = []
    plan = [(1.0, 12, 'open'), (0.95, 40, 'open'), (1.1, 150, 'closed'), (1.16, 172, 'closed'), (1.16, 172, 'closed'),
            (1.12, 165, 'closed'), (0.94, 50, 'happy'), (1.02, 20, 'happy'), (1.0, 12, 'open')]
    for i, (sq, arms, eyes) in enumerate(plan):
        p = replace(BASE, squash=sq, arm_l=arms, arm_r=arms, eyes=eyes, lean=3 * math.sin(i), flutter=i / len(plan),
                    wind=0.3, back=[sparkles(i / len(plan), count=4, seed=41)] if i >= 6 else [])
        out.append((p, 160 if 3 <= i <= 4 else 110))
    return out


def shy():
    out = []
    n = 14
    for i in range(n):
        t = i / (n - 1)
        hands = ease(min(1, t * 3)) if t < 0.85 else 1 - ease((t - 0.85) / 0.15)
        p = replace(BASE, hand_l=(lerp(-150, -110, hands), lerp(-110, -320, hands)) if hands > 0.05 else None,
                    hand_r=(lerp(150, 110, hands), lerp(-110, -320, hands)) if hands > 0.05 else None,
                    eyes='happy', blush=1, tilt=8 * osc(t * 1.5), lean=2 * osc(t * 1.5), flutter=t,
                    back=[hearts_up(t, count=2, seed=51)])
        out.append((p, 100))
    return out


def look_around():
    out = []
    plan = [((0, 0), 0, 140), ((-18, 0), -6, 260), ((-18, -4), -6, 260), ((0, 0), 0, 120), ((18, 0), 6, 260),
            ((18, -4), 6, 300), ((0, 0), 0, 140)]
    for i, (look, tilt, ms) in enumerate(plan):
        p = replace(BASE, look=look, tilt=tilt, flutter=i / len(plan),
                    front=[bubble_mark('?', size=0.75, x=-210)] if i == 5 else [])
        out.append((p, ms))
    return out


def bounce():
    out = []
    n = 12
    for i in range(n):
        t = i / n
        u = (t * 2) % 1
        lift = 50 * hop(u)
        p = replace(BASE, lift=lift, squash=0.9 if lift < 5 else 1.06, eyes='happy', tuck=lift / 50,
                    arm_l=30 + lift, arm_r=30 + lift, flutter=t * 2, wind=0.4)
        out.append((p, 70))
    out.append((replace(BASE), 100))
    return out


def spin_shuriken():
    out = []
    n = 16
    for i in range(n):
        t = i / (n - 1)
        up = ease(min(1, t * 4)) if t < 0.85 else 1 - ease((t - 0.85) / 0.15)
        hand = (lerp(150, 120, up), lerp(-110, -260, up))
        p = replace(BASE, hand_r=hand if up > 0.05 else None, look=(10 * up, -10 * up), eyes='open' if t < 0.6 else 'happy',
                    brows='focus' if t < 0.6 else '', flutter=t,
                    front=[shuriken(hand[0], hand[1] - 52, t * 14, size=up)] if up > 0.05 else [])
        out.append((p, 80))
    return out


def hop_cycle(phase):
    """A ninja dash: a deep crouch leaning back, low leaps on an arc stretched in the air and squashed on
    touchdown, then a skid stop with the scarf overshooting. Kuro faces right; the left one is mirrored."""
    f = dict(eyes='sharp', brows='focus')
    if phase == 'start':
        return [(replace(BASE, squash=0.94, lean=-3, arm_l=30, arm_r=30, wind=0.3, **f), 60),
                (replace(BASE, squash=0.84, lean=-7, arm_l=60, arm_r=60, wind=0.3, **f), 70),
                (replace(BASE, squash=0.86, lean=-6, arm_l=70, arm_r=70, wind=0.35, **f), 120)]
    if phase == 'loop':
        out = []
        n = 8
        for i in range(n):
            t = i / n
            lift = 70 * hop(t)
            squash = 0.88 if i == 0 else 1.1 if 0.2 < t < 0.8 else 1.0
            out.append((replace(BASE, lift=lift, lean=14, squash=squash, arm_l=100, arm_r=100, run=t * 2 * math.pi,
                                stride=0.8, tuck=lift / 140, wind=0.95, flutter=t * 2,
                                back=[motion_lines(1, 0.6 + 0.4 * hop(t))], **f), 80 if i == 0 else 65))
        return out
    return [(replace(BASE, squash=0.86, lean=-8, arm_l=50, arm_r=50, wind=0.7, drag=0.6, eyes='squeeze'), 80),
            (replace(BASE, squash=1.04, lean=-3, arm_l=30, arm_r=30, wind=0.35, drag=0.9), 80),
            (replace(BASE, squash=1.0, lean=0, arm_l=20, arm_r=20, wind=0.2, drag=-0.3), 90),
            (replace(BASE), 110)]


def mirrored(seq):
    return [(replace(p, face=-1), ms) for p, ms in seq]


SEQUENCES = {
    'idle': lambda: idle(),
    'idle_look': lambda: idle('look'),
    'idle_breeze': lambda: idle('breeze'),
    'idle_happy': lambda: idle('happy'),
    'idle_poor': lambda: idle('poor'),
    'think/start': think_start,
    'think/loop': think_loop,
    'think/idea': lambda: think_loop(idea=True),
    'think/end': think_end,
    'read/start': read_start,
    'read/loop': read_loop,
    'read/end': read_end,
    'work/start': work_start,
    'work/loop': work_loop,
    'work/fast': lambda: work_loop(fast=True),
    'work/end': work_end,
    'alert/start': alert_start,
    'alert/loop': alert_loop,
    'alert/end': alert_end,
    'oops': oops,
    'tired': tired,
    'done': done,
    'sleep/start': sleep_start,
    'sleep/loop': sleep_loop,
    'sleep/end': sleep_end,
    'hello': hello,
    'bye': bye,
    'angry': angry,
    'angry_leave': lambda: angry(leave=True),
    'held/start': held_start,
    'held/loop': held_loop,
    'held/end': held_end,
    'pat/start': lambda: pat('start'),
    'pat/loop': lambda: pat('loop'),
    'pat/end': lambda: pat('end'),
    'poke/start': lambda: poke('start'),
    'poke/loop': lambda: poke('loop'),
    'poke/end': lambda: poke('end'),
    'cheer': cheer,
    'dance/start': lambda: dance('start'),
    'dance/loop': lambda: dance('loop'),
    'dance/end': lambda: dance('end'),
    'snack': snack,
    'drink': drink,
    'yawn': yawn,
    'startled': startled,
    'love': love,
    'birthday': birthday,
    'milestone': milestone,
    'stretch': stretch,
    'shy': shy,
    'look': look_around,
    'bounce': bounce,
    'shuriken': spin_shuriken,
    'dash/start_right': lambda: hop_cycle('start'),
    'dash/right': lambda: hop_cycle('loop'),
    'dash/end_right': lambda: hop_cycle('end'),
    'dash/start_left': lambda: mirrored(hop_cycle('start')),
    'dash/left': lambda: mirrored(hop_cycle('loop')),
    'dash/end_left': lambda: mirrored(hop_cycle('end')),
}


if __name__ == '__main__':
    run(SEQUENCES, render, __doc__)
