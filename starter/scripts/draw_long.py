#!/usr/bin/env python3
"""Draw Long, the warlord mecha pet, frame by frame.

    pip install pycairo pillow
    python3 scripts/draw_long.py OUT              # every sequence, as OUT/<sequence>/_NNN_<ms>.png
    python3 scripts/draw_long.py OUT idle         # only these sequences

Long follows WindyWin's design sheet and brief: a many-branched gold crest, a closed mask with narrow red eyes,
violet crystal blades and red segmented plumes carried on a backpack frame (plumes, not wings), a violet chest
with a small red core over a gold spearhead plate, a narrow waist with a short uneven skirt, shoulders about
1.6 times the hips on big hinges, violet thighs with red shin blades, clawed feet, and a halberd longer than the
suit whose red head comes off the shaft. Gold is kept to the crest, the chest trim and the joint dots; red to
the plumes, shin blades and halberd head; cyan to one small light per shoulder.

The suit is built from faceted vector plates on a two-bone rig (hips, knees, shoulders, elbows; elbows and
knees always point outwards) with the shared kit in scripts/pet_art.py, so this script is the art's source.
"""
import math
import random
from dataclasses import dataclass, field, replace

import cairo

from pet_art import (CX, GROUND, INK, SIZE, WHITE, back_out, ease, ease_in, ease_out, ellipse, follow_through, hop, lerp,
                     linear, mix, osc, puff, rgb, run, star, tween)

BLACK = (0.17, 0.17, 0.19)
BLACK_LIGHT = (0.30, 0.30, 0.34)
SILVER = (0.66, 0.67, 0.70)
SILVER_LIGHT = (0.86, 0.87, 0.89)
GOLD = (0.79, 0.64, 0.36)
GOLD_LIGHT = (0.96, 0.84, 0.56)
VIOLET = (0.42, 0.25, 0.63)
VIOLET_LIGHT = (0.64, 0.46, 0.88)
RED = (0.66, 0.09, 0.17)
RED_LIGHT = (0.90, 0.25, 0.30)
CYAN = (0.10, 0.80, 0.86)
EYE = (1.0, 0.18, 0.18)
LINE = 9

THIGH, SHIN, FOOT_H = 162, 172, 42
BODY = 0.70                 # the whole suit, so the halberd and plumes fit the canvas
HEAD = 0.84
# Plumes: side, x on the frame, start angle (degrees, -90 straight up), how far each arcs over, length.
PLUMES = ((-1, -66, -112, 146, 600), (-1, -42, -100, 120, 500), (1, 42, -80, 120, 500), (1, 66, -68, 146, 600))
UPPER, FORE = 126, 122


@dataclass
class Pose:
    x: float = 0
    lift: float = 0
    squash: float = 1
    lean: float = 0             # torso tilt, degrees
    tilt: float = 0             # head tilt, degrees
    crouch: float = 0           # 0 standing .. 1 deep crouch
    feet: tuple = ((-64, 0), (64, 0))      # foot positions (left, right), body space
    hands: tuple = ((-150, -378), (150, -392))  # hand targets (left, right), body space
    fist: tuple = (0, 1)        # 1 closed fist, 0 open hand
    eyes: str = 'open'          # open, wide, closed, angry, happy, x, scan
    glow: float = 1             # eye and core glow
    crystal: float = 0.5        # crystal light
    jet: float = 0              # violet energy jets from the crystals
    flicker: float = 0
    wind: float = 0.15          # tendrils streaming back
    flutter: float = 0
    drag: float = 0             # tendrils pulled by vertical motion (pet_art.follow_through)
    spread: float = 0           # tendrils fanned out (power-up)
    weapon: str = 'held'        # held (right hand), free (w_pos), none
    w_angle: float = -6         # halberd angle from vertical, degrees
    w_pos: tuple = None
    w_glow: float = 0
    soot: float = 0
    alpha: float = 1
    scale: float = 1
    face: int = 1
    shadow: bool = True
    ghosts: tuple = ()          # afterimages: (dx, alpha[, dlift])
    spin: float = 0             # whole-suit rotation about the waist, degrees (tumbles)
    look: float = 0             # scan-visor dot, -1 .. 1
    droop: float = 0            # plumes hang (power loss)
    reach: float = 0            # plumes leave their arcs and work at reach_to
    reach_to: tuple = (0, -900)
    tip_glow: float = 0         # plume tips hot (welding)
    w_grip: float = 0           # hand further up the shaft (+) for spins and raised poses
    w_behind: bool = False      # halberd behind the body
    blade: tuple = None         # halberd head off its shaft: (x, y, angle[, glow]) body space
    under: list = field(default_factory=list)
    back: list = field(default_factory=list)
    front: list = field(default_factory=list)
    over: list = field(default_factory=list)


# ---------------------------------------------------------------- drawing helpers

def poly(ctx, pts):
    ctx.move_to(*pts[0])
    for q in pts[1:]:
        ctx.line_to(*q)
    ctx.close_path()


def metal(ctx, pts, base, light, axis=None, width=LINE, highlight=True):
    """A faceted plate: a gradient across it, a dark outline and a bright edge on the lit side."""
    poly(ctx, pts)
    xs, ys = [q[0] for q in pts], [q[1] for q in pts]
    x0, x1, y0, y1 = min(xs), max(xs), min(ys), max(ys)
    if axis == 'v':
        g = cairo.LinearGradient(0, y0, 0, y1)
    else:
        g = cairo.LinearGradient(x0, y0, x1, y1)
    g.add_color_stop_rgb(0, *light)
    g.add_color_stop_rgb(0.55, *base)
    g.add_color_stop_rgb(1, *mix(base, (0, 0, 0), 0.35))
    ctx.set_source(g)
    ctx.fill_preserve()
    rgb(ctx, INK)
    ctx.set_line_width(width)
    ctx.set_line_join(cairo.LINE_JOIN_MITER)
    ctx.stroke()
    ctx.set_line_join(cairo.LINE_JOIN_ROUND)
    if highlight and len(pts) > 2:
        ctx.move_to(*lerp_pt(pts[0], pts[1], 0.12))
        ctx.line_to(*lerp_pt(pts[0], pts[1], 0.88))
        rgb(ctx, WHITE, 0.35)
        ctx.set_line_width(3)
        ctx.stroke()


def lerp_pt(a, b, t):
    return (lerp(a[0], b[0], t), lerp(a[1], b[1], t))


def glow(ctx, x, y, r, color, alpha):
    if alpha <= 0 or r <= 0:
        return
    g = cairo.RadialGradient(x, y, 1, x, y, r)
    g.add_color_stop_rgba(0, *color, alpha)
    g.add_color_stop_rgba(1, *color, 0)
    ctx.set_source(g)
    ctx.arc(x, y, r, 0, 2 * math.pi)
    ctx.fill()


def gem(ctx, x, y, w, h, color, light=1.0):
    pts = [(x, y - h), (x + w, y), (x, y + h), (x - w, y)]
    glow(ctx, x, y, max(w, h) * 2.2, color, 0.35 * light)
    poly(ctx, pts)
    g = cairo.LinearGradient(x - w, y - h, x + w, y + h)
    g.add_color_stop_rgb(0, *mix(color, WHITE, 0.55))
    g.add_color_stop_rgb(0.5, *color)
    g.add_color_stop_rgb(1, *mix(color, (0, 0, 0), 0.4))
    ctx.set_source(g)
    ctx.fill_preserve()
    rgb(ctx, INK)
    ctx.set_line_width(5)
    ctx.stroke()
    poly(ctx, [(x, y - h * 0.7), (x + w * 0.35, y - h * 0.1), (x - w * 0.2, y)])
    rgb(ctx, WHITE, 0.55)
    ctx.fill()


def ik(a, b, l1, l2, bend_side):
    """Two-bone IK in 2D: the middle joint between a and b, bending towards bend_side (+1 right, -1 left)."""
    dx, dy = b[0] - a[0], b[1] - a[1]
    d = max(1e-3, min(math.hypot(dx, dy), l1 + l2 - 1e-3))
    a1 = math.atan2(dy, dx)
    cos_t = (l1 * l1 + d * d - l2 * l2) / (2 * l1 * d)
    t = math.acos(max(-1.0, min(1.0, cos_t)))
    ang = a1 - bend_side * t
    return (a[0] + math.cos(ang) * l1, a[1] + math.sin(ang) * l1)


def bone_frame(ctx, a, b):
    """Move the context so the bone a->b runs down the local y axis from (0, 0); returns its length."""
    ang = math.atan2(b[1] - a[1], b[0] - a[0]) - math.pi / 2
    ctx.translate(*a)
    ctx.rotate(ang)
    return math.hypot(b[0] - a[0], b[1] - a[1])


# ---------------------------------------------------------------- the rig

def rig(p):
    c = p.crouch
    hip_y = -(FOOT_H + (THIGH + SHIN) * (1 - 0.30 * c)) + 4
    pelvis = (0.0, hip_y)
    lean = math.radians(p.lean)
    def up(length, base, extra=0.0):
        a = lean + extra
        return (base[0] + math.sin(a) * length, base[1] - math.cos(a) * length)
    waist = up(70, pelvis)
    chest_top = up(170, waist)
    neck = up(30, chest_top)
    head = up(62, neck)
    def side_pt(base, dx):
        return (base[0] + math.cos(lean) * dx, base[1] + math.sin(lean) * dx)
    shoulders = (side_pt(up(-6, chest_top), -96), side_pt(up(-6, chest_top), 96))
    hips = (side_pt(pelvis, -44), side_pt(pelvis, 44))
    feet = tuple((fx, fy - FOOT_H) for fx, fy in p.feet)
    # Knees point outwards too, so crouches read as a wide mecha stance, never knock-kneed.
    knees = tuple(max((ik(hips[i], feet[i], THIGH, SHIN, b) for b in (-1, 1)), key=lambda e: side * e[0])
                  for i, side in ((0, -1), (1, 1)))
    hands = p.hands
    # Elbows always point outwards, whichever way the hand goes.
    elbows = tuple(max((ik(shoulders[i], hands[i], UPPER, FORE, b) for b in (-1, 1)), key=lambda e: side * e[0])
                   for i, side in ((0, -1), (1, 1)))
    return dict(pelvis=pelvis, waist=waist, chest=chest_top, neck=neck, head=head, shoulders=shoulders, hips=hips,
                knees=knees, ankles=feet, elbows=elbows, hands=hands, lean=lean)


# ---------------------------------------------------------------- parts

def draw_backpack(ctx, p, r):
    """The backpack frame behind the upper back; crystals and tendrils hang from it, not from the shoulders."""
    cx, cy = r['chest']
    ctx.save()
    ctx.translate(cx, cy)
    ctx.rotate(r['lean'])
    metal(ctx, [(-70, 10), (70, 10), (92, 60), (70, 150), (-70, 150), (-92, 60)], BLACK, BLACK_LIGHT)
    for side in (-1, 1):
        metal(ctx, [(side * 60, 18), (side * 104, 30), (side * 110, 70), (side * 74, 64)], BLACK_LIGHT, SILVER,
              width=6, highlight=False)
        ellipse(ctx, side * 96, 50, 12, 12)
        rgb(ctx, GOLD)
        ctx.fill_preserve()
        rgb(ctx, INK)
        ctx.set_line_width(4)
        ctx.stroke()
    ctx.restore()


def frame_pt(r, dx, dy):
    cx, cy = r['chest']
    a = r['lean']
    return (cx + math.cos(a) * dx - math.sin(a) * dy, cy + math.sin(a) * dx + math.cos(a) * dy)


def lacquer(ctx, pts, w):
    """Glossy red lacquer: deep red, a sharp highlight stripe along the top."""
    poly(ctx, pts)
    g = cairo.LinearGradient(0, -w, 0, w)
    g.add_color_stop_rgb(0, *RED_LIGHT)
    g.add_color_stop_rgb(0.45, *RED)
    g.add_color_stop_rgb(1, *mix(RED, (0, 0, 0), 0.45))
    ctx.set_source(g)
    ctx.fill_preserve()
    rgb(ctx, INK)
    ctx.set_line_width(4)
    ctx.stroke()
    xs = [q[0] for q in pts]
    ctx.move_to(min(xs) * 0.7, -w * 0.45)
    ctx.line_to(max(xs) * 0.6, -w * 0.45)
    rgb(ctx, WHITE, 0.7)
    ctx.set_line_width(2.5)
    ctx.stroke()


def violet_plate(ctx, pts):
    """Semi-translucent violet armour: the black frame shows faintly through."""
    poly(ctx, pts)
    xs, ys = [q[0] for q in pts], [q[1] for q in pts]
    g = cairo.LinearGradient(min(xs), min(ys), max(xs), max(ys))
    g.add_color_stop_rgba(0, *VIOLET_LIGHT, 0.92)
    g.add_color_stop_rgba(0.55, *VIOLET, 0.85)
    g.add_color_stop_rgba(1, *mix(VIOLET, (0, 0, 0), 0.35), 0.9)
    ctx.set_source(g)
    ctx.fill_preserve()
    rgb(ctx, INK)
    ctx.set_line_width(LINE)
    ctx.set_line_join(cairo.LINE_JOIN_MITER)
    ctx.stroke()
    ctx.set_line_join(cairo.LINE_JOIN_ROUND)
    ctx.move_to(*lerp_pt(pts[0], pts[1], 0.15))
    ctx.line_to(*lerp_pt(pts[0], pts[1], 0.85))
    rgb(ctx, WHITE, 0.4)
    ctx.set_line_width(3)
    ctx.stroke()


def draw_tendrils(ctx, p, r, layer='back'):
    """04: mechanical pheasant plumes. Each is a chain of red lacquer segments pinned to the top of the backpack
    frame; they rise behind the shoulders and arc out and down, so the back reads from far away. With `reach` the
    tips leave their arcs and come over the shoulders to work at `reach_to`, like extra hands."""
    for k, (side, bx, a0, turn, length) in enumerate(PLUMES):
        x, y = frame_pt(r, bx, 22)
        base = (x, y)
        a = math.radians(a0) + r['lean']
        bend = math.radians(turn) * side * (1 - 0.5 * p.spread + 0.25 * max(0.0, -p.drag) + 0.35 * p.droop)
        n = 18
        seg = length / n
        pts = [(x, y)]
        for i in range(1, n + 1):
            u = i / n
            sway = math.sin(p.flutter * 2 * math.pi + u * 3.0 + k * 1.3) * 0.12 * u
            ai = a + bend * u ** 1.25 + sway * side
            dx = math.cos(ai) - p.wind * 0.9 * u
            dy = math.sin(ai) - p.drag * 0.8 * u + p.droop * 0.9 * u * u
            d = math.hypot(dx, dy) or 1
            x += dx / d * seg
            y += dy / d * seg
            pts.append((x, y))
        working = p.reach_to[0] * side > -100      # only the plumes on the work's side reach (all for the middle)
        in_front = working and p.reach >= 0.5
        if in_front != (layer == 'front'):
            continue
        if p.reach > 0 and working:
            tx, ty = p.reach_to
            tx += (k - 1.5) * 34 + 18 * math.sin(p.flutter * 2 * math.pi * 2 + k * 1.7)
            ty += abs(k - 1.5) * 24 + 14 * math.cos(p.flutter * 2 * math.pi * 2 + k * 2.1)
            cx_, cy_ = base[0] + side * 150, base[1] - 300
            bez = []
            for i in range(n + 1):
                u = i / n
                bez.append(((1 - u) ** 2 * base[0] + 2 * (1 - u) * u * cx_ + u * u * tx,
                            (1 - u) ** 2 * base[1] + 2 * (1 - u) * u * cy_ + u * u * ty))
            w = ease(min(1.0, p.reach))
            pts = [lerp_pt(a_, b_, w) for a_, b_ in zip(pts, bez)]
        for i in range(n, 0, -1):
            (x0, y0), (x1, y1) = pts[i - 1], pts[i]
            w = 15 * (1 - 0.62 * i / n) + 3
            ctx.save()
            ctx.translate((x0 + x1) / 2, (y0 + y1) / 2)
            ctx.rotate(math.atan2(y1 - y0, x1 - x0))
            L = max(4.0, math.hypot(x1 - x0, y1 - y0) * 0.62)
            lacquer(ctx, [(-L, -w), (L * 0.8, -w * 0.9), (L, 0), (L * 0.8, w * 0.9), (-L, w), (-L * 0.75, 0)], w)
            ctx.restore()
        (x0, y0), (x1, y1) = pts[-2], pts[-1]
        ctx.save()
        ctx.translate(x1, y1)
        ctx.rotate(math.atan2(y1 - y0, x1 - x0))
        lacquer(ctx, [(-4, -6), (34, 0), (-4, 6)], 6)
        if working and p.reach > 0.6 and p.tip_glow > 0:
            glow(ctx, 34, 0, 40, (1.0, 0.85, 0.6), 0.8 * p.tip_glow)
        ctx.restore()
        ellipse(ctx, *pts[0], 12, 12)
        rgb(ctx, BLACK_LIGHT)
        ctx.fill_preserve()
        rgb(ctx, INK)
        ctx.set_line_width(4)
        ctx.stroke()


def draw_crystals(ctx, p, r):
    """03: violet blade crystals fanned out of the backpack frame, translucent."""
    for side in (-1, 1):
        sx, sy = frame_pt(r, side * 96, 50)
        for k, (ang, length, w) in enumerate(((-60, 230, 30), (-38, 180, 24), (-82, 160, 22))):
            a = math.radians(ang if side > 0 else -180 - ang) + r['lean']
            ux, uy = math.cos(a), math.sin(a)
            nx, ny = -uy, ux
            tip = (sx + ux * length, sy + uy * length)
            pts = [(sx + nx * w * 0.5, sy + ny * w * 0.5), (sx + ux * length * 0.6 + nx * w, sy + uy * length * 0.6 + ny * w),
                   tip, (sx + ux * length * 0.6 - nx * w, sy + uy * length * 0.6 - ny * w),
                   (sx - nx * w * 0.5, sy - ny * w * 0.5)]
            if p.jet > 0:
                jl = 150 * p.jet * (0.85 + 0.15 * math.sin(p.flicker * 2 * math.pi * 3 + k))
                glow(ctx, sx - ux * jl * 0.4, sy - uy * jl * 0.4 + jl * 0.3, jl * 0.8, VIOLET_LIGHT, 0.4)
            glow(ctx, *lerp_pt((sx, sy), tip, 0.6), w * 2.4, VIOLET_LIGHT, 0.22 * p.crystal)
            poly(ctx, pts)
            g = cairo.LinearGradient(sx, sy, *tip)
            g.add_color_stop_rgba(0, *VIOLET, 0.88)
            g.add_color_stop_rgba(0.7, *mix(VIOLET_LIGHT, WHITE, 0.15 * p.crystal), 0.8)
            g.add_color_stop_rgba(1, *mix(VIOLET_LIGHT, WHITE, 0.45), 0.85)
            ctx.set_source(g)
            ctx.fill_preserve()
            rgb(ctx, INK)
            ctx.set_line_width(5)
            ctx.set_line_join(cairo.LINE_JOIN_MITER)
            ctx.stroke()
            ctx.set_line_join(cairo.LINE_JOIN_ROUND)
            ctx.move_to(*pts[0])
            ctx.line_to(*tip)
            ctx.line_to(*pts[1])
            rgb(ctx, WHITE, 0.3 + 0.3 * p.crystal)
            ctx.set_line_width(2.5)
            ctx.stroke()


def draw_leg(ctx, p, r, i):
    side = -1 if i == 0 else 1
    hip, knee, ankle = r['hips'][i], r['knees'][i], r['ankles'][i]
    ctx.save()
    L = bone_frame(ctx, hip, knee)
    metal(ctx, [(-28, -6), (28, -6), (34, L * 0.5), (22, L), (-22, L), (-34, L * 0.5)], BLACK, BLACK_LIGHT)
    violet_plate(ctx, [(-24, 6), (22, 4), (30, L * 0.5), (12, L * 0.86), (-14, L * 0.86), (-28, L * 0.48)])
    ctx.restore()
    ctx.save()
    ctx.translate(*knee)
    metal(ctx, [(-24, -18), (24, -18), (28, 8), (0, 28), (-28, 8)], BLACK, BLACK_LIGHT)
    ellipse(ctx, 0, 2, 8, 8)
    rgb(ctx, GOLD)
    ctx.fill()
    ctx.restore()
    ctx.save()
    L = bone_frame(ctx, knee, ankle)
    o = -side
    ctx.save()
    ctx.scale(1, 1)
    lacquer_blade = [(o * 26, L * 0.08), (o * 60, L * 0.28), (o * 46, L * 0.74), (o * 28, L * 0.86)]
    poly(ctx, lacquer_blade)
    g = cairo.LinearGradient(o * 26, 0, o * 60, 0)
    g.add_color_stop_rgb(0, *RED_LIGHT)
    g.add_color_stop_rgb(0.5, *RED)
    g.add_color_stop_rgb(1, *mix(RED, (0, 0, 0), 0.4))
    ctx.set_source(g)
    ctx.fill_preserve()
    rgb(ctx, INK)
    ctx.set_line_width(5)
    ctx.stroke()
    ctx.restore()
    metal(ctx, [(-26, 10), (26, 10), (32, L * 0.6), (24, L + 4), (-24, L + 4), (-32, L * 0.6)], BLACK, BLACK_LIGHT)
    metal(ctx, [(-16, 24), (16, 24), (12, L * 0.55), (-12, L * 0.55)], SILVER, SILVER_LIGHT, width=5)
    ctx.restore()
    ax, ay = ankle
    ellipse(ctx, ax, ay - 2, 12, 12)
    rgb(ctx, GOLD)
    ctx.fill_preserve()
    rgb(ctx, INK)
    ctx.set_line_width(4)
    ctx.stroke()
    # Heel spur at the back, toe claws at the front: the weight sits on both.
    metal(ctx, [(ax - side * 30, ay + 10), (ax - side * 58, ay + FOOT_H + 2), (ax - side * 20, ay + FOOT_H - 4)],
          BLACK_LIGHT, SILVER, width=5, highlight=False)
    metal(ctx, [(ax - 34, ay - 4), (ax + 34, ay - 4), (ax + 44, ay + 28), (ax - 44, ay + 28)], BLACK, BLACK_LIGHT)
    for k, dx in enumerate((-28, 0, 28)):
        tip = (ax + dx * 1.45, ay + FOOT_H + 2)
        metal(ctx, [(ax + dx - 10, ay + 24), (ax + dx + 10, ay + 24), tip], GOLD, GOLD_LIGHT, width=4, highlight=False)


def draw_skirt(ctx, p, r):
    """06: a short, asymmetric armoured skirt: longer on the left."""
    px, py = r['pelvis']
    ctx.save()
    ctx.translate(px, py)
    ctx.rotate(r['lean'])
    metal(ctx, [(-20, -26), (-86, -22), (-100, 54), (-64, 66), (-20, 26)], BLACK, BLACK_LIGHT)
    metal(ctx, [(-28, -14), (-80, -12), (-88, 40), (-60, 50)], SILVER, SILVER_LIGHT, width=6)
    metal(ctx, [(20, -26), (80, -22), (90, 22), (58, 32), (20, 14)], BLACK, BLACK_LIGHT)
    metal(ctx, [(28, -14), (74, -12), (80, 14), (56, 22)], SILVER, SILVER_LIGHT, width=6)
    metal(ctx, [(-22, -30), (22, -30), (16, 30), (0, 46), (-16, 30)], BLACK, BLACK_LIGHT)
    ctx.restore()


def draw_torso(ctx, p, r):
    """05: violet chest with gold trim and a small red core; gold spearhead plate over a narrow black waist."""
    wx, wy = r['waist']
    ctx.save()
    ctx.translate(wx, wy)
    ctx.rotate(r['lean'])
    # Narrow waist.
    metal(ctx, [(-36, -4), (36, -4), (42, 70), (-42, 70)], BLACK, BLACK_LIGHT, axis='v')
    # Chest frame and violet plates.
    metal(ctx, [(-100, -150), (100, -150), (88, -64), (44, -8), (-44, -8), (-88, -64)], BLACK, BLACK_LIGHT)
    violet_plate(ctx, [(-88, -140), (-14, -132), (-16, -46), (-46, -16), (-78, -64)])
    violet_plate(ctx, [(88, -140), (14, -132), (16, -46), (46, -16), (78, -64)])
    # Gold trim round the chest: brushed metal.
    metal(ctx, [(-98, -152), (-78, -152), (-20, -98), (20, -98), (78, -152), (98, -152), (26, -82), (-26, -82)],
          GOLD, GOLD_LIGHT, width=6)
    # Gold spearhead on the abdomen, point down.
    metal(ctx, [(-34, -70), (34, -70), (24, -10), (0, 64), (-24, -10)], GOLD, GOLD_LIGHT, width=6)
    poly(ctx, [(-10, -58), (0, -60), (0, 40), (-6, -10)])
    rgb(ctx, WHITE, 0.25)
    ctx.fill()
    # Collar and the neck's ball joint.
    metal(ctx, [(-44, -172), (44, -172), (54, -148), (-54, -148)], SILVER, SILVER_LIGHT, width=6)
    # Small red core.
    glow(ctx, 0, -118, 50, RED_LIGHT, 0.35 * p.glow)
    poly(ctx, [(0, -134), (12, -118), (0, -102), (-12, -118)])
    g = cairo.LinearGradient(-12, -134, 12, -102)
    g.add_color_stop_rgb(0, *mix(RED_LIGHT, WHITE, 0.4))
    g.add_color_stop_rgb(1, *RED)
    ctx.set_source(g)
    ctx.fill_preserve()
    rgb(ctx, INK)
    ctx.set_line_width(4)
    ctx.stroke()
    if p.soot > 0:
        rnd = random.Random(5)
        for _ in range(6):
            ellipse(ctx, rnd.uniform(-80, 80), rnd.uniform(-140, 40), rnd.uniform(14, 26), rnd.uniform(8, 14))
            rgb(ctx, (0.05, 0.05, 0.06), 0.5 * p.soot)
            ctx.fill()
    ctx.restore()


def draw_shoulder(ctx, p, r, i):
    """A big shoulder hinge under a violet pauldron, a small cyan light on top."""
    side = -1 if i == 0 else 1
    sx, sy = r['shoulders'][i]
    ctx.save()
    ctx.translate(sx, sy)
    ctx.rotate(r['lean'] * 0.6)
    ellipse(ctx, 0, 0, 34, 34)
    rgb(ctx, BLACK_LIGHT)
    ctx.fill_preserve()
    rgb(ctx, INK)
    ctx.set_line_width(LINE)
    ctx.stroke()
    ellipse(ctx, 0, 0, 12, 12)
    rgb(ctx, GOLD)
    ctx.fill()
    violet_plate(ctx, [(-side * 26, -50), (side * 44, -46), (side * 66, -6), (side * 56, 40), (side * 10, 30),
                       (-side * 34, -8)])
    metal(ctx, [(side * 42, -42), (side * 64, -6), (side * 54, 2)], BLACK_LIGHT, SILVER, width=5, highlight=False)
    gem(ctx, side * 26, -14, 8, 11, CYAN, p.glow)
    ctx.restore()


def draw_arm(ctx, p, r, i):
    side = -1 if i == 0 else 1
    sh, el, ha = r['shoulders'][i], r['elbows'][i], r['hands'][i]
    ctx.save()
    L = bone_frame(ctx, sh, el)
    metal(ctx, [(-22, 0), (22, 0), (24, L), (-24, L)], BLACK, BLACK_LIGHT)
    ctx.restore()
    ctx.save()
    ctx.translate(*el)
    ellipse(ctx, 0, 0, 20, 20)
    rgb(ctx, BLACK_LIGHT)
    ctx.fill_preserve()
    rgb(ctx, INK)
    ctx.set_line_width(LINE)
    ctx.stroke()
    ellipse(ctx, 0, 0, 7, 7)
    rgb(ctx, GOLD)
    ctx.fill()
    ctx.restore()
    ctx.save()
    L = bone_frame(ctx, el, ha)
    metal(ctx, [(-28, 6), (28, 6), (32, L * 0.7), (22, L - 6), (-22, L - 6), (-32, L * 0.7)], BLACK, BLACK_LIGHT)
    metal(ctx, [(-18, 16), (18, 16), (14, L * 0.6), (-14, L * 0.6)], SILVER, SILVER_LIGHT, width=5)
    metal(ctx, [(-side * 28, L * 0.15), (-side * 46, L * 0.42), (-side * 28, L * 0.62)], BLACK_LIGHT, SILVER, width=5,
          highlight=False)
    ctx.restore()


def draw_hand(ctx, p, r, i):
    """07/08: right fist round the halberd, left mechanical hand."""
    hx, hy = r['hands'][i]
    el = r['elbows'][i]
    ctx.save()
    bone_frame(ctx, el, (hx, hy))
    if p.fist[i] > 0.5:
        metal(ctx, [(-24, -4), (24, -4), (26, 36), (-26, 36)], BLACK, BLACK_LIGHT)
        for k in range(4):
            poly(ctx, [(-22 + k * 12, 30), (-12 + k * 12, 30), (-12 + k * 12, 42), (-22 + k * 12, 42)])
            rgb(ctx, BLACK_LIGHT)
            ctx.fill_preserve()
            rgb(ctx, INK)
            ctx.set_line_width(3)
            ctx.stroke()
    else:
        metal(ctx, [(-22, -4), (22, -4), (24, 24), (-24, 24)], BLACK, BLACK_LIGHT)
        for k in range(4):
            x = -18 + k * 12
            metal(ctx, [(x - 5, 22), (x + 5, 22), (x + 4, 58), (x - 4, 58)], BLACK_LIGHT, SILVER, width=3,
                  highlight=False)
    ctx.restore()


def draw_head(ctx, p, r):
    """01/02: a many-branched gold crest on a black helm; a closed mask with narrow red eyes, separate cheek
    and chin plates, on a ball-jointed neck."""
    nx, ny = r['neck']
    ellipse(ctx, nx, ny, 22, 20)
    rgb(ctx, BLACK_LIGHT)
    ctx.fill_preserve()
    rgb(ctx, INK)
    ctx.set_line_width(LINE)
    ctx.stroke()
    hx, hy = r['head']
    ctx.save()
    ctx.translate(hx, hy)
    ctx.rotate(r['lean'] + math.radians(p.tilt))
    ctx.scale(HEAD, HEAD)
    # Crest: a central blade and three tines a side, like a crown of antlers.
    for side in (-1, 1):
        for k, (bx, by, tx, ty, w) in enumerate(((8, -50, 40, -132, 9), (20, -46, 78, -116, 8),
                                                 (30, -40, 100, -76, 7), (36, -30, 92, -34, 6))):
            metal(ctx, [(side * (bx - w), by), (side * tx, ty), (side * (bx + w), by + 8)], GOLD, GOLD_LIGHT,
                  width=5, highlight=k == 0)
    metal(ctx, [(-10, -50), (0, -166), (10, -50)], GOLD, GOLD_LIGHT, width=5)
    metal(ctx, [(-28, -58), (28, -58), (20, -36), (-20, -36)], GOLD, GOLD_LIGHT, width=5)
    # Helm.
    metal(ctx, [(-50, -44), (0, -60), (50, -44), (58, 2), (42, 46), (-42, 46), (-58, 2)], BLACK, BLACK_LIGHT)
    # Closed mask, narrow eyes.
    metal(ctx, [(-44, -10), (44, -10), (40, 18), (-40, 18)], BLACK_LIGHT, BLACK, width=5, highlight=False)
    draw_eyes(ctx, p)
    for side in (-1, 1):
        metal(ctx, [(side * 10, 20), (side * 50, 12), (side * 46, 38), (side * 14, 50)], SILVER, SILVER_LIGHT, width=5)
    metal(ctx, [(-12, 36), (12, 36), (0, 64)], SILVER, SILVER_LIGHT, width=5, highlight=False)
    ctx.restore()


def draw_eyes(ctx, p):
    g = p.glow
    color = mix(EYE, (0.35, 0.05, 0.05), 1 - g)
    ctx.set_line_cap(cairo.LINE_CAP_ROUND)
    for side in (-1, 1):
        cx, cy = side * 22, 2
        if p.eyes == 'scan':
            continue
        if p.eyes in ('open', 'angry', 'wide'):
            slope = {'open': 5, 'angry': 9, 'wide': 2}[p.eyes]
            h = {'open': 5, 'angry': 4, 'wide': 8}[p.eyes]
            glow(ctx, cx, cy, 26, EYE, 0.45 * g)
            poly(ctx, [(cx - side * 15, cy - slope), (cx + side * 15, cy), (cx + side * 13, cy + h),
                       (cx - side * 13, cy - slope + h)])
            rgb(ctx, color)
            ctx.fill()
        elif p.eyes == 'closed':
            ctx.move_to(cx - 13, cy + 3)
            ctx.line_to(cx + 13, cy + 3)
            rgb(ctx, mix(color, BLACK, 0.5))
            ctx.set_line_width(3)
            ctx.stroke()
        elif p.eyes == 'happy':
            glow(ctx, cx, cy, 24, EYE, 0.4 * g)
            ctx.move_to(cx - 13, cy + 6)
            ctx.curve_to(cx - 6, cy - 4, cx + 6, cy - 4, cx + 13, cy + 6)
            rgb(ctx, color)
            ctx.set_line_width(5)
            ctx.stroke()
        elif p.eyes == 'x':
            for a, b in ((-7, -6), (-7, 6)):
                ctx.move_to(cx + a, cy + 2 + b)
                ctx.line_to(cx - a, cy + 2 - b)
            rgb(ctx, color)
            ctx.set_line_width(4)
            ctx.stroke()
    if p.eyes == 'scan':
        poly(ctx, [(-40, -2), (40, -2), (38, 5), (-38, 5)])
        rgb(ctx, mix(color, (0, 0, 0), 0.45))
        ctx.fill()
        dot = p.look * 32
        glow(ctx, dot, 1, 30, EYE, 0.6 * g)
        poly(ctx, [(dot - 12, -3), (dot + 12, -3), (dot + 10, 6), (dot - 10, 6)])
        rgb(ctx, mix(color, WHITE, 0.3))
        ctx.fill()


def draw_jets(ctx, p, r):
    """Violet thrust from two nozzles under the backpack frame, pointing down the body's own axis."""
    if p.jet <= 0:
        return
    for side in (-1, 1):
        a = r['lean'] - side * math.radians(14)
        x, y = frame_pt(r, side * 124, 120)
        ux, uy = -math.sin(a), math.cos(a)
        length = 260 * p.jet * (0.85 + 0.15 * math.sin(p.flicker * 2 * math.pi * 3 + side))
        tx, ty = x + ux * length, y + uy * length
        glow(ctx, x + ux * length * 0.4, y + uy * length * 0.4, length * 0.6, VIOLET_LIGHT, 0.45)
        for wmul, color, alpha in ((1.0, VIOLET_LIGHT, 0.85), (0.5, mix(VIOLET_LIGHT, WHITE, 0.7), 0.95)):
            w = 30 * wmul
            ctx.move_to(x - uy * w, y + ux * w)
            ctx.curve_to(x - uy * w * 0.8 + ux * length * 0.4, y + ux * w * 0.8 + uy * length * 0.4, tx, ty, tx, ty)
            ctx.curve_to(tx, ty, x + uy * w * 0.8 + ux * length * 0.4, y - ux * w * 0.8 + uy * length * 0.4,
                         x + uy * w, y - ux * w)
            ctx.close_path()
            rgb(ctx, color, alpha)
            ctx.fill()
        metal(ctx, [(x - 20, y - 14), (x + 20, y - 14), (x + 26, y + 12), (x - 26, y + 12)], BLACK_LIGHT, SILVER,
              width=4, highlight=False)


BLADE = [[(-14, 0), (0, -200), (14, 0)]]
for _side in (-1, 1):
    BLADE.append([(_side * 8, 4), (_side * 58, -46), (_side * 70, -160), (_side * 40, -64), (_side * 24, 28)])
    BLADE.append([(_side * 22, 30), (_side * 66, 14), (_side * 28, 58)])


def draw_blade(ctx, p, light=0.0):
    """The red lacquer head of the halberd, socket line at (0, 0), pointing up."""
    if light > 0:
        glow(ctx, 0, -90, 120, RED_LIGHT, 0.4 * light)
    metal(ctx, [(-15, 6), (15, 6), (12, 40), (-12, 40)], SILVER, SILVER_LIGHT, width=4)
    for pts in BLADE:
        poly(ctx, pts)
        xs = [q[0] for q in pts]
        g = cairo.LinearGradient(min(xs), 0, max(xs), 0)
        g.add_color_stop_rgb(0, *mix(RED_LIGHT, WHITE, 0.4 * light))
        g.add_color_stop_rgb(0.5, *mix(RED, RED_LIGHT, 0.6 * light))
        g.add_color_stop_rgb(1, *mix(RED, (0, 0, 0), 0.4))
        ctx.set_source(g)
        ctx.fill_preserve()
        rgb(ctx, INK)
        ctx.set_line_width(5)
        ctx.set_line_join(cairo.LINE_JOIN_MITER)
        ctx.stroke()
        ctx.set_line_join(cairo.LINE_JOIN_ROUND)
    ctx.move_to(-4, -20)
    ctx.line_to(0, -180)
    rgb(ctx, WHITE, 0.6)
    ctx.set_line_width(3)
    ctx.stroke()


def halberd(ctx, gx, gy, angle, p, scale=1.0):
    """11: the halberd, longer than the suit: a straight dark shaft and a red head on a separate socket, which
    comes off (p.blade) and flies on its own."""
    ctx.save()
    ctx.translate(gx, gy)
    ctx.rotate(math.radians(angle))
    ctx.scale(scale, scale)
    ctx.translate(0, p.w_grip)
    top, bottom = -560, 352
    metal(ctx, [(-7, top), (7, top), (7, bottom), (-7, bottom)], BLACK, BLACK_LIGHT, width=5, highlight=False)
    for y in (-40, 40):
        metal(ctx, [(-11, y - 8), (11, y - 8), (11, y + 8), (-11, y + 8)], BLACK_LIGHT, SILVER, width=4, highlight=False)
    if p.blade is None:
        ctx.save()
        ctx.translate(0, top)
        draw_blade(ctx, p, p.w_glow)
        ctx.restore()
    else:
        metal(ctx, [(-10, top - 30), (10, top - 30), (10, top), (-10, top)], SILVER, SILVER_LIGHT, width=4,
              highlight=False)
    poly(ctx, [(-11, bottom), (11, bottom), (0, bottom + 34)])
    rgb(ctx, RED)
    ctx.fill_preserve()
    rgb(ctx, INK)
    ctx.set_line_width(4)
    ctx.stroke()
    ctx.restore()


def draw_long(ctx, p):
    r = rig(p)
    ctx.save()
    ctx.translate(CX + p.x, GROUND - p.lift)
    s = BODY * p.scale
    ctx.scale(p.face * s / max(p.squash, 0.2) ** 0.45, s * p.squash)
    if p.spin:
        ctx.translate(0, -440)
        ctx.rotate(math.radians(p.spin))
        ctx.translate(0, 440)
    for prop in p.back:
        prop(ctx, p)
    draw_jets(ctx, p, r)
    draw_tendrils(ctx, p, r)
    draw_crystals(ctx, p, r)
    draw_backpack(ctx, p, r)
    if p.weapon == 'free' and p.w_pos:
        halberd(ctx, p.w_pos[0], p.w_pos[1], p.w_angle, p)
    if p.weapon == 'held' and p.w_behind:
        halberd(ctx, r['hands'][1][0], r['hands'][1][1] + 16, p.w_angle, p)
    for i in (0, 1):
        draw_leg(ctx, p, r, i)
    draw_skirt(ctx, p, r)
    draw_torso(ctx, p, r)
    draw_arm(ctx, p, r, 0)
    draw_hand(ctx, p, r, 0)
    draw_shoulder(ctx, p, r, 0)
    draw_head(ctx, p, r)
    draw_arm(ctx, p, r, 1)
    if p.weapon == 'held' and not p.w_behind:
        halberd(ctx, r['hands'][1][0], r['hands'][1][1] + 16, p.w_angle, p)
    draw_hand(ctx, p, r, 1)
    draw_shoulder(ctx, p, r, 1)
    if p.reach >= 0.5:
        draw_tendrils(ctx, p, r, 'front')
    if p.blade is not None:
        bx, by, ba = p.blade[:3]
        ctx.save()
        ctx.translate(bx, by)
        ctx.rotate(math.radians(ba))
        draw_blade(ctx, p, p.blade[3] if len(p.blade) > 3 else 0.0)
        ctx.restore()
    for prop in p.front:
        prop(ctx, p)
    ctx.restore()


def render(p):
    surface = cairo.ImageSurface(cairo.FORMAT_ARGB32, SIZE, SIZE)
    ctx = cairo.Context(surface)
    ctx.set_line_join(cairo.LINE_JOIN_ROUND)
    for prop in p.under:
        prop(ctx, p)
    if p.alpha > 0 and p.scale > 0.01:
        if p.shadow:
            k = 1 / (1 + p.lift / 220)
            ellipse(ctx, CX + p.x, GROUND + 2, 120 * p.scale * k, 15 * k)
            rgb(ctx, (0, 0, 0), 0.18 * k * p.alpha)
            ctx.fill()
        # Afterimages: violet silhouettes of the suit where it just was.
        for ghost in p.ghosts:
            dx, a = ghost[0], ghost[1]
            dy = ghost[2] if len(ghost) > 2 else 0
            ctx.push_group()
            draw_long(ctx, replace(p, x=p.x + dx, lift=p.lift + dy, front=[], back=[], blade=None))
            pattern = ctx.pop_group()
            ctx.set_source_rgba(*VIOLET_LIGHT, a * p.alpha)
            ctx.mask(pattern)
        ctx.push_group()
        draw_long(ctx, p)
        ctx.pop_group_to_source()
        ctx.paint_with_alpha(p.alpha)
    for prop in p.over:
        prop(ctx, p)
    return surface


# ---------------------------------------------------------------- effects (props): body space unless noted

HOLO = (0.80, 0.66, 1.0)        # hologram light
HOT = (1.0, 0.88, 0.62)         # sparks: white-hot light, not the armour's gold
STEAM = (0.93, 0.93, 0.97)


def hologram(t, size=1.0, kind='map', at=(-340, -800)):
    """Thinking: a strategy table projected from the open left palm, a disc of rings with nodes that light in turn
    and links that draw themselves; 'plan' moves a red marker along the links."""
    def draw(ctx, p):
        if size <= 0.02:
            return
        r = rig(p)
        hx, hy = r['hands'][0]
        cx, cy = at
        R = 170 * size
        ctx.move_to(hx, hy - 10)
        ctx.line_to(cx - R * 0.9, cy + 6)
        ctx.line_to(cx + R * 0.9, cy + 6)
        ctx.close_path()
        g = cairo.LinearGradient(0, hy, 0, cy)
        g.add_color_stop_rgba(0, *HOLO, 0.45)
        g.add_color_stop_rgba(1, *HOLO, 0.06)
        ctx.set_source(g)
        ctx.fill()
        ellipse(ctx, cx, cy, R, R * 0.34)
        rgb(ctx, HOLO, 0.16)
        ctx.fill_preserve()
        rgb(ctx, HOLO, 0.9)
        ctx.set_line_width(3)
        ctx.stroke()
        for k in (0.33, 0.66):
            ellipse(ctx, cx, cy, R * k, R * k * 0.34)
            rgb(ctx, HOLO, 0.4)
            ctx.set_line_width(2)
            ctx.stroke()
        a = t * 2 * math.pi
        ctx.move_to(cx, cy)
        ctx.line_to(cx + math.cos(a) * R, cy + math.sin(a) * R * 0.34)
        rgb(ctx, WHITE, 0.6)
        ctx.set_line_width(2.5)
        ctx.stroke()
        nodes = []
        rnd = random.Random(4)
        for i in range(6):
            ang = i / 6 * 2 * math.pi + rnd.uniform(-0.3, 0.3)
            rad = rnd.uniform(0.35, 0.85)
            nodes.append((cx + math.cos(ang) * R * rad, cy + math.sin(ang) * R * rad * 0.34, rnd.uniform(30, 70) * size))
        order = (0, 2, 4, 1, 5, 3)
        lit = t * len(order) * 1.2
        for j in range(len(order) - 1):
            u = max(0.0, min(1.0, lit - j - 1))
            if u <= 0:
                continue
            (x0, y0, h0), (x1, y1, h1) = nodes[order[j]], nodes[order[j + 1]]
            ctx.move_to(x0, y0 - h0)
            ctx.line_to(lerp(x0, x1, u), lerp(y0 - h0, y1 - h1, u))
            rgb(ctx, RED_LIGHT, 0.8)
            ctx.set_line_width(3)
            ctx.stroke()
        for j, idx in enumerate(order):
            x, y, h = nodes[idx]
            on = lit > j
            ctx.move_to(x, y)
            ctx.line_to(x, y - h)
            rgb(ctx, HOLO, 0.7)
            ctx.set_line_width(2)
            ctx.stroke()
            if on:
                glow(ctx, x, y - h, 22, RED_LIGHT, 0.6)
            poly(ctx, [(x, y - h - 9), (x + 8, y - h), (x, y - h + 9), (x - 8, y - h)])
            rgb(ctx, RED_LIGHT if on else HOLO, 0.95)
            ctx.fill()
        if kind == 'plan':
            u = (t * 2) % 1
            seg = int(u * 5)
            w = ease(u * 5 - seg)
            (x0, y0, h0), (x1, y1, h1) = nodes[order[seg]], nodes[order[seg + 1]]
            x, y = lerp(x0, x1, w), lerp(y0 - h0, y1 - h1, w) - 18 * math.sin(math.pi * w)
            glow(ctx, x, y, 34, RED_LIGHT, 0.7)
            star(ctx, x, y, 16, RED_LIGHT, points=4, inner=0.4)
    return draw


def data_panel(t, open_=1.0, at=(330, -830), beam=None, flick=0.0):
    """Reading: a floating translucent sheet of text lines; `beam` (0..1, top to bottom) is the scan line the eyes
    cast on it; `flick` slides in the next page."""
    def draw(ctx, p):
        if open_ <= 0.02:
            return
        cx, cy = at
        W, H = 230, 300 * open_
        for page, dx, alpha in ((0, -flick * 260, 1 - flick), (1, (1 - flick) * 260, flick)):
            if alpha <= 0.02:
                continue
            x0 = cx - W / 2 + dx
            ctx.save()
            poly(ctx, [(x0, cy - H / 2), (x0 + W, cy - H / 2 - 20), (x0 + W, cy + H / 2 - 20), (x0, cy + H / 2)])
            rgb(ctx, HOLO, 0.16 * alpha)
            ctx.fill_preserve()
            rgb(ctx, HOLO, 0.9 * alpha)
            ctx.set_line_width(3)
            ctx.stroke_preserve()
            ctx.clip()
            rnd = random.Random(10 + page)
            scroll = (t * 2 % 1) * 40
            for k in range(-1, 9):
                y = cy - H / 2 + 24 + k * 40 - scroll
                w = rnd.uniform(0.4, 0.9) * (W - 40)
                ctx.move_to(x0 + 20, y)
                ctx.line_to(x0 + 20 + w, y - 20 * w / W)
                rgb(ctx, WHITE if k % 3 else RED_LIGHT, 0.6 * alpha)
                ctx.set_line_width(7)
                ctx.stroke()
            ctx.restore()
        if beam is not None and open_ > 0.9:
            r = rig(p)
            hx, hy = r['head']
            y = cy - H / 2 + 10 + beam * (H - 20)
            ctx.move_to(hx + 20, hy)
            ctx.line_to(cx - W / 2, y)
            ctx.line_to(cx + W / 2, y - 20)
            ctx.close_path()
            rgb(ctx, EYE, 0.16)
            ctx.fill()
            ctx.move_to(cx - W / 2, y)
            ctx.line_to(cx + W / 2, y - 20)
            rgb(ctx, EYE, 0.9)
            ctx.set_line_width(4)
            ctx.stroke()
            glow(ctx, cx, y - 10, 70, EYE, 0.3)
    return draw


def sparks(t, x, y, seed=1, count=10, spread=1.0, speed=1.0, color=HOT, up=1.0):
    """Hot sparks thrown out of a point; each one has its own phase so the burst runs as a loop."""
    def draw(ctx, p):
        rnd = random.Random(seed)
        ctx.set_line_cap(cairo.LINE_CAP_ROUND)
        for i in range(count):
            ang = rnd.uniform(-math.pi, 0) * spread - math.pi / 2 * (1 - spread)
            v = rnd.uniform(220, 420) * speed
            local = (t + rnd.uniform(0, 1)) % 1
            vx, vy = math.cos(ang) * v, math.sin(ang) * v * up
            px, py = x + vx * local, y + vy * local + 600 * local * local
            tx, ty = vx, vy + 1200 * local
            n = math.hypot(tx, ty) or 1
            L = 26 * (1 - local) + 6
            ctx.move_to(px, py)
            ctx.line_to(px - tx / n * L, py - ty / n * L)
            rgb(ctx, color, 1 - local)
            ctx.set_line_width(5 * (1 - local) + 2)
            ctx.stroke()
    return draw


def burst(t, x, y, seed=2, count=14, radius=260, color=HOT):
    """One explosive spray from a point, t 0..1."""
    def draw(ctx, p):
        if t <= 0 or t >= 1:
            return
        rnd = random.Random(seed)
        ctx.set_line_cap(cairo.LINE_CAP_ROUND)
        glow(ctx, x, y, radius * 0.6 * (1 - t), WHITE, 0.6 * (1 - t))
        for i in range(count):
            ang = rnd.uniform(0, 2 * math.pi)
            d = radius * rnd.uniform(0.5, 1.0) * ease_out(t)
            px, py = x + math.cos(ang) * d, y + math.sin(ang) * d
            L = 40 * (1 - t) + 4
            ctx.move_to(px, py)
            ctx.line_to(px - math.cos(ang) * L, py - math.sin(ang) * L)
            rgb(ctx, color, 1 - t)
            ctx.set_line_width(6 * (1 - t) + 2)
            ctx.stroke()
    return draw


def shockwave(t, x=0, y=0, size=1.0, color=WHITE, dust=True):
    """Landing or a slam: a flat ring racing out over the ground, dust kicked to both sides."""
    def draw(ctx, p):
        if t <= 0 or t >= 1:
            return
        R = (90 + 520 * ease_out(t)) * size
        ellipse(ctx, x, y, R, R * 0.16)
        rgb(ctx, color, 0.85 * (1 - t))
        ctx.set_line_width(22 * (1 - t) + 3)
        ctx.stroke()
        ellipse(ctx, x, y, R * 0.7, R * 0.7 * 0.16)
        rgb(ctx, VIOLET_LIGHT, 0.5 * (1 - t))
        ctx.set_line_width(10 * (1 - t) + 2)
        ctx.stroke()
        if dust:
            for side in (-1, 1):
                for k in range(3):
                    d = (130 + 300 * ease_out(t) * (0.6 + 0.25 * k)) * size
                    puff(ctx, x + side * d, y - 30 - 70 * t * (1 + 0.4 * k), (28 + 26 * t) * (1 - 0.2 * k) * size,
                         (1 - t) * 0.9)
    return draw


def cracks(t, x=0, y=0):
    def draw(ctx, p):
        if t <= 0:
            return
        rnd = random.Random(9)
        a = 1 - max(0.0, t - 0.6) / 0.4
        for i in range(7):
            ang = rnd.uniform(0, math.pi) if i % 2 else rnd.uniform(math.pi, 2 * math.pi)
            px, py = x, y
            ctx.move_to(px, py)
            for k in range(3):
                ang += rnd.uniform(-0.5, 0.5)
                d = rnd.uniform(40, 80) * min(1.0, t * 3)
                px += math.cos(ang) * d
                py += math.sin(ang) * d * 0.18
                ctx.line_to(px, py)
            rgb(ctx, INK, 0.7 * a)
            ctx.set_line_width(5)
            ctx.stroke()
    return draw


def beacon(t, light=1.0):
    """Needs input: a red signal diamond over the crest with an exclamation mark, pulse rings going out."""
    def draw(ctx, p):
        if light <= 0.02:
            return
        r = rig(p)
        x, y = r['head'][0], r['head'][1] - 290
        for k in range(2):
            u = (t + k * 0.5) % 1
            ellipse(ctx, x, y, 60 + 140 * u, (60 + 140 * u) * 0.9)
            rgb(ctx, RED_LIGHT, 0.6 * (1 - u) * light)
            ctx.set_line_width(8 * (1 - u) + 2)
            ctx.stroke()
        s = 62 * light * (1 + 0.06 * math.sin(t * 4 * math.pi))
        glow(ctx, x, y, s * 2.2, RED_LIGHT, 0.45 * light)
        poly(ctx, [(x, y - s), (x + s * 0.85, y), (x, y + s), (x - s * 0.85, y)])
        rgb(ctx, RED)
        ctx.fill_preserve()
        rgb(ctx, INK)
        ctx.set_line_width(6)
        ctx.stroke()
        poly(ctx, [(x - 7, y - s * 0.55), (x + 7, y - s * 0.55), (x + 4, y + s * 0.15), (x - 4, y + s * 0.15)])
        rgb(ctx, WHITE)
        ctx.fill()
        ellipse(ctx, x, y + s * 0.38, 7, 7)
        ctx.fill()
    return draw


def bolts(t, seed=1, amount=1.0):
    """Short circuit: jagged arcs crawling over the torso and shoulders."""
    def draw(ctx, p):
        if amount <= 0:
            return
        rnd = random.Random(seed * 97 + int(t * 40))
        r = rig(p)
        cx, cy = r['chest'][0], r['chest'][1] + 120
        ctx.set_line_cap(cairo.LINE_CAP_ROUND)
        ctx.set_line_join(cairo.LINE_JOIN_MITER)
        for i in range(int(4 * amount) + 1):
            x, y = cx + rnd.uniform(-160, 160), cy + rnd.uniform(-200, 120)
            pts = [(x, y)]
            ang = rnd.uniform(0, 2 * math.pi)
            for k in range(5):
                ang += rnd.uniform(-1.2, 1.2)
                x += math.cos(ang) * 34
                y += math.sin(ang) * 34
                pts.append((x, y))
            for width, color, alpha in ((14, VIOLET_LIGHT, 0.5), (5, WHITE, 1.0)):
                ctx.move_to(*pts[0])
                for q in pts[1:]:
                    ctx.line_to(*q)
                rgb(ctx, color, alpha * amount)
                ctx.set_line_width(width)
                ctx.stroke()
        ctx.set_line_join(cairo.LINE_JOIN_ROUND)
    return draw


def smoke_up(t, x=0, y=-700, amount=3, seed=4, size=1.0):
    def draw(ctx, p):
        rnd = random.Random(seed)
        for i in range(amount):
            local = (t + i / amount) % 1
            puff(ctx, x + rnd.uniform(-40, 40) + 30 * math.sin(local * 4 + i), y - local * 260,
                 (24 + 40 * local) * size, 0.85 * (1 - local))
    return draw


def vents(t, strength=1.0):
    """Steam from the shoulder vents, rising and curling outwards."""
    def draw(ctx, p):
        if strength <= 0:
            return
        r = rig(p)
        for side, (sx, sy) in zip((-1, 1), r['shoulders']):
            for k in range(3):
                local = (t + k / 3) % 1
                puff(ctx, sx + side * (30 + 120 * local), sy - 60 - 200 * local, (22 + 36 * local) * strength,
                     0.9 * (1 - local) * strength)
    return draw


def fireworks(t, seed=3, bursts=4):
    """Canvas space: shells rise and burst in violet, red and white rings."""
    colors = (VIOLET_LIGHT, RED_LIGHT, WHITE, mix(VIOLET_LIGHT, WHITE, 0.5))

    def draw(ctx, p):
        rnd = random.Random(seed)
        ctx.set_line_cap(cairo.LINE_CAP_ROUND)
        for b in range(bursts):
            x = CX + p.x + rnd.uniform(-330, 330)
            y = rnd.uniform(120, 360)
            start = b / bursts * 0.55
            local = (t - start) / 0.45
            color = colors[b % len(colors)]
            if 0 < local < 0.3:
                u = local / 0.3
                sy = lerp(GROUND - 300, y, ease_out(u))
                ctx.move_to(x, sy)
                ctx.line_to(x, sy + 40)
                rgb(ctx, color, 0.9)
                ctx.set_line_width(5)
                ctx.stroke()
            elif 0.3 <= local < 1:
                u = (local - 0.3) / 0.7
                R = 120 * ease_out(u)
                glow(ctx, x, y, R * 0.8, color, 0.4 * (1 - u))
                for k in range(14):
                    a = k / 14 * 2 * math.pi
                    px, py = x + math.cos(a) * R, y + math.sin(a) * R + 40 * u * u
                    ctx.move_to(px, py)
                    ctx.line_to(px - math.cos(a) * 22 * (1 - u), py - math.sin(a) * 22 * (1 - u))
                    rgb(ctx, color, 1 - u)
                    ctx.set_line_width(6 * (1 - u) + 2)
                    ctx.stroke()
    return draw


def spin_smear(a0, a1, at, radius=760, width=120, color=RED_LIGHT):
    """A translucent swoosh where the halberd head swept from a0 to a1 (degrees from straight up) round `at`."""
    def draw(ctx, p):
        x, y = at
        lo, hi = sorted((math.radians(a0 - 90), math.radians(a1 - 90)))
        if hi - lo < 0.05:
            return
        g = cairo.RadialGradient(x, y, radius - width, x, y, radius)
        g.add_color_stop_rgba(0, *color, 0)
        g.add_color_stop_rgba(0.75, *color, 0.35)
        g.add_color_stop_rgba(1, *mix(color, WHITE, 0.6), 0.7)
        ctx.new_path()
        ctx.arc(x, y, radius, lo, hi)
        ctx.arc_negative(x, y, radius - width, hi, lo)
        ctx.close_path()
        ctx.set_source(g)
        ctx.fill()
    return draw


def speed_lines(strength, y0=-900, y1=-100, seed=7):
    def draw(ctx, p):
        rnd = random.Random(seed)
        ctx.set_line_cap(cairo.LINE_CAP_ROUND)
        for i in range(9):
            y = rnd.uniform(y0, y1)
            x = -rnd.uniform(220, 380)
            L = rnd.uniform(140, 300) * strength
            ctx.move_to(x, y)
            ctx.line_to(x - L, y)
            rgb(ctx, VIOLET_LIGHT, 0.55 * strength)
            ctx.set_line_width(rnd.uniform(5, 10))
            ctx.stroke()
    return draw


def heart_path(ctx, x, y, s):
    ctx.move_to(x, y + s * 0.9)
    ctx.curve_to(x - s * 1.4, y - s * 0.1, x - s * 0.8, y - s * 1.2, x, y - s * 0.45)
    ctx.curve_to(x + s * 0.8, y - s * 1.2, x + s * 1.4, y - s * 0.1, x, y + s * 0.9)
    ctx.close_path()


def holo_heart(t, size=1.0, at_hand=1):
    def draw(ctx, p):
        if size <= 0.02:
            return
        r = rig(p)
        px, py = r['hands'][0] if at_hand else (r['chest'][0], r['chest'][1] + 50)
        x, y = px - 40 * at_hand, py - 230 - 50 * t
        s = 80 * size * (1 + 0.08 * math.sin(t * 4 * math.pi))
        glow(ctx, x, y, s * 2.0, RED_LIGHT, 0.35 * size)
        ctx.move_to(px, py - 10)
        ctx.line_to(x - s, y)
        ctx.line_to(x + s, y)
        ctx.close_path()
        rgb(ctx, RED_LIGHT, 0.12 * size)
        ctx.fill()
        heart_path(ctx, x, y, s)
        rgb(ctx, RED_LIGHT, 0.4 * size)
        ctx.fill()
        heart_path(ctx, x, y, s)
        rgb(ctx, mix(RED_LIGHT, WHITE, 0.4), 0.9 * size)
        ctx.set_line_width(5)
        ctx.stroke()
        ctx.save()
        heart_path(ctx, x, y, s)
        ctx.clip()
        for k in range(-6, 7):
            yy = y + k * 16 + (t * 64) % 16
            ctx.move_to(x - s * 1.5, yy)
            ctx.line_to(x + s * 1.5, yy)
            rgb(ctx, WHITE, 0.35)
            ctx.set_line_width(3)
            ctx.stroke()
        ctx.restore()
    return draw


def energy_cell(x, y, angle=0.0, light=1.0, size=1.0):
    def draw(ctx, p):
        if size <= 0.02:
            return
        ctx.save()
        ctx.translate(x, y)
        ctx.rotate(math.radians(angle))
        ctx.scale(size, size)
        glow(ctx, 0, 0, 90, VIOLET_LIGHT, 0.5 * light)
        metal(ctx, [(-20, -46), (20, -46), (20, 46), (-20, 46)], mix(VIOLET_LIGHT, WHITE, 0.3 * light), WHITE, width=6)
        for yy in (-46, 46):
            metal(ctx, [(-24, yy - 10), (24, yy - 10), (24, yy + 10), (-24, yy + 10)], BLACK_LIGHT, SILVER, width=5,
                  highlight=False)
        ctx.restore()
    return draw


def standby(t):
    """Sleeping: a slow red standby blip and small Z marks drifting from the helm."""
    def draw(ctx, p):
        r = rig(p)
        hx, hy = r['head']
        for i in range(3):
            local = (t + i / 3) % 1
            x = hx + 90 + local * 80 + math.sin(local * 5) * 10
            y = hy - 80 - local * 240
            s = 12 + local * 20
            a = 1 - ease(max(0, local - 0.6) / 0.4)
            rgb(ctx, mix(VIOLET_LIGHT, WHITE, 0.3), a)
            ctx.set_line_width(6 + local * 3)
            ctx.set_line_join(cairo.LINE_JOIN_MITER)
            ctx.move_to(x - s, y - s)
            ctx.line_to(x + s, y - s)
            ctx.line_to(x - s, y + s)
            ctx.line_to(x + s, y + s)
            ctx.stroke()
            ctx.set_line_join(cairo.LINE_JOIN_ROUND)
    return draw


def low_power(blink):
    def draw(ctx, p):
        r = rig(p)
        x, y = r['head'][0] + 150, r['head'][1] - 150
        poly(ctx, [(x - 60, y - 28), (x + 56, y - 28), (x + 56, y + 28), (x - 60, y + 28)])
        rgb(ctx, BLACK)
        ctx.fill_preserve()
        rgb(ctx, SILVER_LIGHT)
        ctx.set_line_width(7)
        ctx.stroke()
        poly(ctx, [(x + 58, y - 12), (x + 72, y - 12), (x + 72, y + 12), (x + 58, y + 12)])
        rgb(ctx, SILVER_LIGHT)
        ctx.fill()
        if blink:
            glow(ctx, x - 38, y, 40, RED_LIGHT, 0.5)
            poly(ctx, [(x - 50, y - 16), (x - 28, y - 16), (x - 28, y + 16), (x - 50, y + 16)])
            rgb(ctx, RED_LIGHT)
            ctx.fill()
    return draw


def confetti_burst(t, seed=5):
    colors = (VIOLET_LIGHT, RED_LIGHT, WHITE, mix(VIOLET, WHITE, 0.4))

    def draw(ctx, p):
        rnd = random.Random(seed)
        for i in range(30):
            ang = rnd.uniform(-math.pi * 0.9, -math.pi * 0.1)
            v = rnd.uniform(500, 900)
            x = rnd.choice((-1, 1)) * 130 + math.cos(ang) * v * t
            y = -650 + math.sin(ang) * v * t + 900 * t * t
            if y > -20:
                continue
            ctx.save()
            ctx.translate(x, y)
            ctx.rotate(rnd.uniform(-8, 8) * t + i)
            ctx.rectangle(-12, -6, 24, 12)
            rgb(ctx, colors[i % 4], 1 - ease(max(0.0, t - 0.7) / 0.3))
            ctx.fill()
            ctx.restore()
    return draw


def dust(t, x=0, y=0, side=-1, amount=3):
    def draw(ctx, p):
        for k in range(amount):
            local = (t + k / amount) % 1
            puff(ctx, x + side * 120 * local, y - 20 - 50 * local, 20 + 24 * local, 0.8 * (1 - local))
    return draw


# ---------------------------------------------------------------- sequences: lists of (Pose, duration_ms)

LH, RH = (-150, -378), (150, -392)
BASE = Pose()
WORK = (-300, -720)                         # where the plumes forge the blade
GUARD = dict(crouch=0.28, feet=((-120, 0), (96, 0)), lean=3, hands=((-120, -470), (176, -470)), fist=(1, 1),
             w_angle=28, eyes='angry')


def breathe(t, amount=0.008):
    return 1 + amount * math.sin(2 * math.pi * t)


def shaft_top(p):
    """Where the halberd head sits on the shaft (body space), from the right hand, angle and grip."""
    hx, hy = p.hands[1]
    th = math.radians(p.w_angle)
    d = -560 + p.w_grip
    return (hx - d * math.sin(th), hy + 16 + d * math.cos(th))


def fx(seq, fn):
    """Add per-frame effects: fn(pose, t, i) for t in 0..1 over the whole sequence."""
    n = len(seq)
    return [(fn(p, i / max(1, n - 1), i), ms) for i, (p, ms) in enumerate(seq)]


def mirrored(seq):
    return [(replace(p, face=-p.face, x=-p.x, ghosts=tuple((-g[0],) + tuple(g[1:]) for g in p.ghosts)), ms)
            for p, ms in seq]


def idle(kind='plain'):
    """Idle loops: breathing, the plumes rippling one after another, crystal and eye light pulsing."""
    out = []
    n = 20
    for i in range(n):
        t = i / n
        p = replace(BASE, squash=breathe(t), flutter=t, wind=0.15 + 0.06 * osc(t), crystal=0.5 + 0.2 * osc(t, 1, 0.3),
                    glow=0.9 + 0.1 * osc(t, 0.5), tilt=1.2 * math.sin(2 * math.pi * (t - 0.15)),
                    hands=(LH, (RH[0], RH[1] + 4 * osc(t, 1, -0.2))))
        if kind == 'scan':
            plan = [0, 0, -0.6, -1, -1, -1, -0.4, 0.3, 1, 1, 1, 1, 0.5, 0, 0, 0, 0, 0, 0, 0]
            p = replace(p, eyes='scan' if 2 <= i < 15 else 'open', look=plan[i], tilt=plan[i] * 7)
        elif kind == 'plume':
            s = hop(min(1.0, t * 1.3))
            p = replace(p, spread=0.9 * s, flutter=t * 2, crystal=0.5 + 0.5 * s, wind=0.15 * (1 - s), lean=-2 * s,
                        tilt=-4 * s, eyes='happy' if 0.35 < t < 0.65 else 'open')
        elif kind == 'shift':
            # Lift the halberd a hand's width, tilt it, plant it again: a small dust puff and a jolt.
            k = [0, 0, 0.3, 0.7, 1, 1, 1, 0.8, 0.4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0][i]
            p = replace(p, hands=(LH, (RH[0] + 10 * k, RH[1] - 40 * k)), w_angle=-6 - 10 * k, x=6 * k,
                        lean=-2 * k, squash=0.97 if i == 9 else p.squash,
                        front=[dust(0.2 + (i - 9) / 6, x=RH[0] + 70, y=0, side=1, amount=2)] if 9 <= i < 14 else [])
        elif kind == 'happy':
            p = replace(p, eyes='happy' if 4 <= i < 16 else 'open', spread=0.35 + 0.25 * osc(t, 0.5), flutter=t * 2,
                        lift=10 * max(0.0, osc(t, 0.5)), crystal=0.8, glow=1)
        elif kind == 'poor':
            flick = i in (6, 7, 15)
            p = replace(p, droop=0.7, wind=0.0, glow=0.15 if flick else 0.45, crystal=0.1, crouch=0.15, lean=5,
                        tilt=10, squash=breathe(t, 0.005), hands=((-138, -340), (156, -372)), w_angle=2)
        out.append((p, 130))
    if kind in ('plain', 'shift', 'happy'):
        at = 13
        p = out[at][0]
        out[at:at + 1] = [(replace(p, glow=0.25), 60), (replace(p, glow=0.05, eyes='closed'), 70), (replace(p, glow=0.6), 60)]
    return out


# Thinking: the strategy hologram.
PALM_UP = (-200, -560)


def think(phase, kind='map'):
    if phase == 'start':
        out = []
        for i, u in enumerate((0.2, 0.45, 0.7, 0.9, 1.0, 1.0, 1.0)):
            h = ease(u)
            size = back_out(max(0.0, (i - 2) / 4)) if i >= 2 else 0
            out.append((replace(BASE, hands=(lerp_pt(LH, PALM_UP, h), RH), fist=(0, 1), eyes='scan' if i > 2 else 'open',
                                look=-0.6 * h, tilt=-5 * h, flutter=i / 7,
                                front=[hologram(i / 7, size=size, kind=kind)]), (60, 60, 70, 70, 80, 90, 100)[i]))
        return out
    if phase == 'loop':
        out = []
        n = 16
        for i in range(n):
            t = i / n
            out.append((replace(BASE, hands=((PALM_UP[0], PALM_UP[1] + 6 * osc(t)), RH), fist=(0, 1), eyes='scan',
                                look=-0.6 + 0.5 * math.sin(2 * math.pi * t), tilt=-5 + 2 * osc(t), flutter=t,
                                crystal=0.55 + 0.25 * osc(t, 0.5), squash=breathe(t, 0.006),
                                front=[hologram(t, kind=kind)]), 110))
        return out
    out = []
    for i, u in enumerate((1.0, 0.6, 0.25, 0.0)):
        h = ease(u)
        out.append((replace(BASE, hands=(lerp_pt(LH, PALM_UP, h), RH), fist=(0, 1),
                            eyes='scan' if u > 0.5 else 'open', look=-0.6 * h, tilt=-5 * h, flutter=i / 4,
                            front=[hologram(0.9, size=1.08 if i == 0 else u * 0.9)]), (70, 60, 60, 90)[i]))
    out.append((BASE, 100))
    return out


# Reading: the scan panel.
def scan(phase, flicking=False):
    if phase == 'start':
        out = []
        for i, u in enumerate((0.15, 0.4, 0.75, 1.0, 1.06, 1.0)):
            out.append((replace(BASE, eyes='scan' if i > 0 else 'wide', look=0.7 * min(1.0, u), tilt=5 * min(1.0, u),
                                flutter=i / 6, front=[data_panel(0, open_=min(1.08, u))]), (60, 60, 70, 70, 70, 100)[i]))
        return out
    if phase == 'loop':
        out = []
        n = 16
        for i in range(n):
            t = i / n
            if flicking:
                f = ease(max(0.0, min(1.0, ((t * 2) % 1 - 0.55) / 0.3)))
                props = [data_panel(t * 0.5, flick=f, beam=None if f > 0 else (t * 2 % 1) / 0.55)]
                look = 0.7 - 0.6 * f
            else:
                u = (t * 2) % 1
                props = [data_panel(t, beam=ease(u))]
                look = 0.7
            out.append((replace(BASE, eyes='scan', look=look, tilt=5 + 1.5 * osc(t), flutter=t, squash=breathe(t, 0.006),
                                crystal=0.5 + 0.2 * osc(t, 0.5), front=props), 110))
        return out
    out = []
    for i, u in enumerate((1.0, 0.6, 0.2, 0.0)):
        out.append((replace(BASE, eyes='scan' if u > 0.5 else 'open', look=0.7 * u, tilt=5 * u, flutter=i / 4,
                            front=[data_panel(0, open_=u)]), (60, 60, 60, 90)[i]))
    out.append((BASE, 100))
    return out


# Working: the plumes forge the halberd's head.
def forge_pose(t, weld=False):
    flash = weld and (int(t * 12) % 3 == 0)
    hot = 0.6 + 0.4 * osc(t * 3)
    return replace(BASE, reach=1, reach_to=(WORK[0] + 30, WORK[1] + 10), tip_glow=hot, flutter=t,
                   blade=(WORK[0], WORK[1], 90 + 4 * osc(t), 0.4 + 0.5 * hot), eyes='scan',
                   look=-0.8 + 0.2 * osc(t * 2), tilt=-6, hands=((-230, -520), RH), fist=(0, 1), crystal=0.6,
                   front=[sparks(t, WORK[0] + 40, WORK[1] + 10, seed=3, count=18 if weld else 10, speed=1.2 if weld else 1)]
                   + ([burst(0.25, WORK[0] + 40, WORK[1] + 10, seed=int(t * 50), radius=200, color=WHITE)] if flash else []))


def forge(phase, weld=False):
    if phase == 'start':
        out = []
        top = shaft_top(BASE)
        for i in range(8):
            t = (i + 1) / 8
            u = ease(t)
            path = (lerp(top[0], WORK[0], u), lerp(top[1], WORK[1], u) - 120 * math.sin(math.pi * u))
            out.append((replace(BASE, blade=(path[0], path[1], lerp(-6, 90, u), 0.3 * t), reach=ease(min(1, t * 1.3)),
                                reach_to=(WORK[0] + 30, WORK[1] + 10), eyes='scan' if t > 0.3 else 'open', look=-0.8 * u,
                                tilt=-6 * u, hands=(lerp_pt(LH, (-230, -520), u), RH), fist=(0, 1), flutter=t,
                                front=[burst(t * 2.5, *top, seed=4, count=8, radius=120)] if t < 0.4 else []),
                        (50, 50, 60, 60, 70, 70, 80, 100)[i]))
        return out
    if phase == 'loop':
        n = 12
        return [(forge_pose(i / n, weld), 90) for i in range(n)]
    out = []
    top = shaft_top(BASE)
    for i in range(7):
        t = (i + 1) / 7
        u = ease(t)
        path = (lerp(WORK[0], top[0], u), lerp(WORK[1], top[1], u) - 120 * math.sin(math.pi * u))
        last = i == 6
        out.append((replace(BASE, blade=None if last else (path[0], path[1], lerp(90, -6, u), 0.5 * (1 - t)),
                            reach=1 - ease(t), reach_to=(WORK[0] + 30, WORK[1] + 10), eyes='open' if t > 0.5 else 'scan',
                            look=-0.8 * (1 - u), tilt=-6 * (1 - u), hands=(lerp_pt((-230, -520), LH, u), RH), fist=(0, 1),
                            flutter=t, w_glow=0.8 if last else 0,
                            front=[burst(0.3, *top, seed=6, count=10, radius=140)] if last else []),
                    (60, 60, 60, 70, 70, 80, 70)[i]))
    out.append((replace(BASE, w_glow=0.3), 120))
    out.append((BASE, 100))
    return out


def scan_to_forge():
    """Reading to working without a reset: the sheet folds while the head already lifts off the shaft."""
    out = []
    top = shaft_top(BASE)
    for i in range(10):
        t = (i + 1) / 10
        panel = max(0.0, 1 - t * 2.2)
        u = ease(max(0.0, min(1.0, (t - 0.25) / 0.75)))
        path = (lerp(top[0], WORK[0], u), lerp(top[1], WORK[1], u) - 120 * math.sin(math.pi * u))
        out.append((replace(BASE, eyes='scan', look=lerp(0.7, -0.8, ease(t)), tilt=lerp(5, -6, ease(t)),
                            blade=(path[0], path[1], lerp(-6, 90, u), 0.3 * u) if t > 0.25 else None,
                            reach=ease(max(0.0, min(1.0, (t - 0.3) / 0.6))), reach_to=(WORK[0] + 30, WORK[1] + 10),
                            hands=(lerp_pt(LH, (-230, -520), u), RH), fist=(0, 1), flutter=t,
                            front=[data_panel(0, open_=panel)] if panel > 0 else []), 80 if i < 9 else 110))
    return out


def forge_to_scan():
    out = []
    top = shaft_top(BASE)
    for i in range(10):
        t = (i + 1) / 10
        u = ease(min(1.0, t * 1.4))
        panel = ease(max(0.0, min(1.0, (t - 0.5) / 0.5)))
        path = (lerp(WORK[0], top[0], u), lerp(WORK[1], top[1], u) - 120 * math.sin(math.pi * u))
        out.append((replace(BASE, eyes='scan', look=lerp(-0.8, 0.7, ease(t)), tilt=lerp(-6, 5, ease(t)),
                            blade=None if u >= 1 else (path[0], path[1], lerp(90, -6, u), 0.3 * (1 - u)),
                            reach=1 - u, reach_to=(WORK[0] + 30, WORK[1] + 10), hands=(lerp_pt((-230, -520), LH, u), RH),
                            fist=(0, 1), flutter=t,
                            front=[data_panel(0, open_=panel)] if panel > 0 else []), 80 if i < 9 else 110))
    return out


# Needs input: the halberd goes up as a signal, a red beacon over the crest.
RAISED = dict(hands=(LH, (226, -760)), w_grip=250, w_angle=0, w_glow=1, spread=0.6, eyes='wide', crystal=0.8)


def signal(phase):
    if phase == 'start':
        seq = tween(BASE, [
            (2, 60, ease, dict(crouch=0.14, squash=0.97, hands=(LH, (156, -360)), eyes='open')),
            (4, [50, 50, 60, 80], ease_out, dict(crouch=0, squash=1.02, **RAISED)),
            (1, 90, linear, dict(squash=1.0)),
        ])
        return fx(seq, lambda p, t, i: replace(p, flutter=t, front=[beacon(t, light=ease(max(0.0, t * 1.6 - 0.6)))]))
    if phase == 'loop':
        out = []
        n = 10
        for i in range(n):
            t = i / n
            out.append((replace(BASE, **{**RAISED, 'w_glow': 0.7 + 0.3 * osc(t * 2), 'spread': 0.55 + 0.15 * osc(t),
                                         'eyes': 'wide' if i % 5 else 'open'},
                                flutter=t * 2, tilt=-3 * osc(t), front=[beacon(t)]), 90))
        return out
    seq = tween(replace(BASE, **RAISED), [
        (3, [60, 60, 70], ease, dict(hands=(LH, RH), w_grip=0, w_angle=-6, w_glow=0, spread=0, eyes='open',
                                    crystal=0.5)),
        (1, 100, linear, dict(squash=0.98)),
        (1, 90, linear, dict(squash=1.0)),
    ])
    return fx(seq, lambda p, t, i: replace(p, flutter=t, front=[beacon(0.5, light=max(0.0, 1 - t * 2))]))


def short_circuit():
    """Tool error: the core flares, arcs crawl over the suit, the plumes spasm and the light dies; a slump held,
    then a reboot with the eyes flickering back."""
    out = [(replace(BASE, glow=1.4, eyes='wide', front=[burst(0.15, 0, -560, seed=1, radius=200)]), 70)]
    for k in range(5):
        j = (-1) ** k
        out.append((replace(BASE, x=7 * j, lean=4 * j, tilt=-8 * j, eyes='x', glow=1.0 if k % 2 else 0.4,
                            spread=0.4 + 0.3 * (k % 2), flutter=k * 0.37, w_angle=-6 - 5 * k, squash=1.02,
                            front=[bolts(k / 5, seed=k + 2), burst(0.2 + k * 0.15, 0, -560, seed=k, radius=260)]), 55))
    slump = replace(BASE, crouch=0.35, lean=8, tilt=16, eyes='closed', glow=0.05, droop=0.8, wind=0, w_angle=-34,
                    hands=((-140, -330), (168, -330)), crystal=0.0)
    for k in range(3):
        out.append((replace(slump, squash=(0.94, 0.97, 0.96)[k], front=[smoke_up(k / 6, x=0, y=-700, size=1.1)]),
                    (80, 160, 260)[k]))
    for k, (g, e) in enumerate(((0.6, 'open'), (0.0, 'closed'), (1.0, 'wide'), (0.3, 'open'), (1.0, 'open'))):
        u = ease((k + 1) / 5)
        out.append((replace(slump, glow=g, eyes=e, crouch=lerp(0.35, 0, u), lean=lerp(8, 0, u), tilt=lerp(16, 0, u),
                            droop=lerp(0.8, 0, u), wind=0.15 * u, w_angle=lerp(-34, -6, u), crystal=0.5 * u,
                            hands=(lerp_pt((-140, -330), LH, u), lerp_pt((168, -330), RH, u)),
                            front=[smoke_up(0.5 + k / 10, x=0, y=-700, size=1.1)]), (80, 70, 90, 90, 160)[k]))
    return out


KNEEL = dict(crouch=1.0, feet=((-150, 0), (130, 0)), lean=9, tilt=18, hands=((-100, -300), (176, -330)), w_angle=6,
             droop=1.0, wind=0, crystal=0.05, fist=(0, 1))


def power_down():
    """Out of quota: down on one knee, leaning on the halberd, the plumes trailing on the floor; a red cell blinks."""
    out = []
    n = 14
    for i in range(n):
        t = i / n
        out.append((replace(BASE, **KNEEL, squash=breathe(t, 0.006), glow=0.2 + 0.08 * osc(t) + (0.4 if i == 9 else 0),
                            eyes='open' if i == 9 else 'closed', flutter=t * 0.5,
                            front=[low_power(i % 7 < 4)]), 140))
    return out


def victory():
    """Turn finished: anticipation, two twirls of the halberd in front (eased, with a swoosh), the blade raised
    to the sky with fireworks, then the butt slammed down: shockwave, cracks, plumes flared; settle."""
    out = []
    pivot_hand = (40, -620)
    out += tween(BASE, [
        (2, 60, ease, dict(crouch=0.18, squash=0.97, hands=(LH, (110, -520)), eyes='angry')),
        (2, 60, ease, dict(hands=((-60, -560), pivot_hand), w_grip=200, w_angle=-20, fist=(1, 1), spread=0.3)),
    ])
    spin = []
    n = 12
    prev = -20
    for i in range(n):
        u = ease((i + 1) / n)
        ang = lerp(-20, 720, u)
        spin.append((replace(out[-1][0], w_angle=ang, eyes='angry', spread=0.3 + 0.4 * u, flutter=i / 6,
                             wind=0.4, crouch=0.15, lean=2 * math.sin(i),
                             back=[spin_smear(prev, ang, (pivot_hand[0], pivot_hand[1] + 16), radius=560, width=180)]),
                     50 if 2 < i < 10 else 70))
        prev = ang
    out += spin
    raised = replace(BASE, **RAISED)
    out += tween(spin[-1][0], [
        (3, [60, 70, 80], ease_out, {**RAISED, 'back': [], 'crouch': 0, 'lean': 0, 'eyes': 'happy', 'spread': 1.0,
                                     'crystal': 1.0, 'w_angle': 720, 'fist': (0, 1)}),
    ])
    for k in range(6):
        t = k / 6
        out.append((replace(raised, eyes='happy', spread=1.0, crystal=1.0, flutter=t,
                            over=[fireworks(t * 0.9, seed=11)]), 110))
    slam_from = replace(raised, eyes='angry', spread=1.0, crystal=1.0)
    out.append((replace(slam_from, hands=(LH, (170, -720)), w_grip=250, crouch=0.05, lift=20, over=[fireworks(0.92, seed=11)]),
                60))
    hit = replace(BASE, crouch=0.35, squash=0.92, eyes='angry', spread=1.0, crystal=1.0, hands=((-170, -360), (168, -360)),
                  w_angle=-6, glow=1.3)
    for k in range(6):
        t = (k + 1) / 7
        out.append((replace(hit, crouch=lerp(0.35, 0.0, ease(t)), squash=lerp(0.92, 1.0, ease(t)),
                            spread=lerp(1.0, 0.1, ease(t)), crystal=lerp(1.0, 0.6, t), drag=-0.5 * (1 - t),
                            eyes='angry' if k < 3 else 'happy', flutter=t,
                            back=[shockwave(t, x=180, y=0)], front=[cracks(t * 2, x=180, y=0)]), (50, 60, 70, 80, 100, 120)[k]))
    out.append((replace(BASE, eyes='happy'), 200))
    out.append((BASE, 100))
    return out


def standby_mode(phase):
    if phase == 'start':
        seq = tween(BASE, [
            (3, [90, 90, 110], ease, dict(glow=0.3, crouch=0.15, lean=4, tilt=10, droop=0.5, wind=0, crystal=0.2)),
            (1, 80, linear, dict(glow=0.6)),
            (3, [100, 120, 160], ease_in, dict(glow=0.0, eyes='closed', droop=0.85, crystal=0.05, crouch=0.2, tilt=14,
                                              hands=((-140, -350), (156, -372)))),
        ])
        return fx(seq, lambda p, t, i: replace(p, flutter=t * 0.5))
    if phase == 'loop':
        out = []
        n = 16
        for i in range(n):
            t = i / n
            out.append((replace(BASE, glow=0.0, eyes='closed', droop=0.85 + 0.05 * osc(t), wind=0, crystal=0.05 + 0.1 * osc(t),
                                crouch=0.2, lean=4, tilt=14 + osc(t), squash=breathe(t, 0.01), flutter=t * 0.4,
                                hands=((-140, -350), (156, -372)), front=[standby(t)]), 150))
        return out
    seq = tween(replace(BASE, glow=0.0, eyes='closed', droop=0.85, wind=0, crystal=0.05, crouch=0.2, lean=4, tilt=14,
                        hands=((-140, -350), (156, -372))), [
        (1, 90, linear, dict(eyes='wide', glow=1.4)),
        (1, 60, linear, dict(glow=0.2, eyes='open')),
        (1, 70, linear, dict(glow=1.2, eyes='wide', crystal=1.0)),
        (3, [60, 70, 90], back_out, dict(eyes='open', glow=1, droop=-0.4, spread=0.4, crouch=0, lean=0, tilt=0,
                                         crystal=0.7, hands=(LH, RH), lift=16, squash=1.04, wind=0.15)),
        (2, [80, 120], ease, dict(droop=0, spread=0, lift=0, squash=1.0, crystal=0.5)),
    ])
    return fx(seq, lambda p, t, i: replace(p, flutter=t))


def landing():
    """Starting: drops in on violet jets with a trail of afterimages, brakes, lands in a crouch with a shockwave,
    rises; the plumes float up on the way down (follow-through) and settle."""
    out = []
    for k, lift in enumerate((1050, 820, 600, 400, 240, 130, 60)):
        out.append((replace(BASE, lift=lift, jet=0.6 + 0.4 * (k > 3), eyes='angry', crouch=0.1, lean=0,
                            feet=((-50, -10), (60, -20)), shadow=lift < 700, flicker=k / 3, wind=0,
                            ghosts=((0, 0.25, 160), (0, 0.15, 320)) if k < 5 else ((0, 0.2, 80),)), 50))
    out.append((replace(BASE, lift=20, jet=1.2, eyes='angry', crouch=0.3, flicker=0.6, wind=0), 60))
    land = replace(BASE, crouch=0.75, squash=0.9, eyes='angry', jet=0, spread=0.6, lean=6,
                   hands=((-190, -270), (176, -300)), w_angle=10, crystal=1.0)
    for k in range(5):
        t = (k + 1) / 6
        out.append((replace(land, front=[shockwave(t, size=1.2)], squash=lerp(0.9, 0.98, t), flutter=t, glow=1.3 - 0.3 * t),
                    (60, 70, 80, 90, 110)[k]))
    out += tween(land, [
        (4, [70, 70, 80, 100], ease, dict(crouch=0, squash=1.0, lean=0, hands=(LH, RH), w_angle=-6, spread=0,
                                         eyes='open', crystal=0.5)),
        (1, 140, linear, dict(eyes='happy')),
        (1, 100, linear, dict(eyes='open')),
    ])
    return follow_through(out)


def depart():
    """Closing: a crouch, the jets light, a heavy lift-off that speeds up and leaves the frame upwards."""
    out = tween(BASE, [
        (2, 80, ease, dict(crouch=0.4, squash=0.95, eyes='angry', spread=0.3, hands=((-180, -300), (170, -330)))),
        (2, 70, linear, dict(jet=0.7, flicker=0.6, crystal=1.0)),
    ])
    for k, lift in enumerate((30, 90, 200, 380, 620, 900, 1250)):
        out.append((replace(out[-1][0], lift=lift, crouch=0.05, squash=1.06, jet=1.2, flicker=k / 3, wind=0,
                            feet=((-50, -10), (60, -20)), hands=(LH, RH),
                            ghosts=((0, 0.25, -120 - 30 * k), (0, 0.12, -240 - 60 * k)) if k > 1 else (),
                            front=[dust(k / 4, x=-60, side=-1), dust(k / 4, x=60, side=1)] if k < 3 else []),
                    (70, 60, 55, 50, 50, 50, 60)[k]))
    out.append((replace(out[-1][0], lift=1500, alpha=0), 100))
    return follow_through(out)


def rage(leave='dash'):
    """Angry: plumes flare, crystals blaze, vents steam, a stomp; then either a dash off-screen with afterimages
    or a straight jet-blast upwards."""
    out = tween(BASE, [
        (3, [70, 70, 90], ease, dict(**GUARD, spread=1.0, crystal=1.0, glow=1.3, w_glow=1)),
    ])
    out = fx(out, lambda p, t, i: replace(p, front=[vents(t)], flutter=t))
    for k in range(6):
        j = (-1) ** k
        out.append((replace(out[-1][0], x=5 * j, flutter=k / 3 + 0.1, spread=1.0 + 0.1 * j, front=[vents(0.4 + k / 8),
                            sparks(k / 6, 0, -560, seed=2, count=8)]), 60))
    stomp = replace(out[-1][0], x=0, crouch=0.4, squash=0.94, feet=((-150, 0), (96, 0)))
    for k in range(4):
        t = (k + 1) / 5
        out.append((replace(stomp, front=[shockwave(t, x=-150, size=0.9), vents(0.9)], flutter=t), (60, 70, 80, 120)[k]))
    if leave == 'dash':
        for k, x in enumerate((0, 60, 220, 460, 760, 1100)):
            out.append((replace(stomp, x=x, crouch=0.35, lean=18, squash=1.0, wind=1.0, flutter=k / 3, feet=((-150, -10), (40, 0)),
                                ghosts=tuple((-d, a) for d, a in ((120, 0.4), (240, 0.25), (360, 0.12))) if k > 1 else (),
                                front=[dust(k / 6, x=-200, side=-1)]), 50))
    else:
        for k, lift in enumerate((40, 140, 330, 620, 1000, 1400)):
            out.append((replace(stomp, lift=lift, crouch=0.05, squash=1.08, jet=1.4, flicker=k / 3, wind=0,
                                feet=((-60, -10), (60, -20)),
                                ghosts=((0, 0.35, -140), (0, 0.18, -280)) if k > 0 else ()), 50))
    out.append((replace(out[-1][0], alpha=0), 100))
    return follow_through(out)


def held(phase):
    dangle = dict(feet=((-40, 30), (56, 50)), lift=60, eyes='wide', shadow=False, w_angle=12, wind=0,
                  hands=((-160, -360), (150, -380)))
    if phase == 'start':
        seq = tween(BASE, [(4, [50, 50, 60, 70], ease_out, dict(**dangle, squash=1.05, drag=-0.7))])
        return fx(seq, lambda p, t, i: replace(p, flutter=t))
    if phase == 'loop':
        out = []
        n = 12
        for i in range(n):
            t = i / n
            out.append((replace(BASE, **{**dangle, 'feet': ((-40 + 24 * osc(t, 0.5), 30), (56 - 24 * osc(t, 0.5), 50)),
                                         'eyes': 'wide' if i % 6 else 'x'},
                                lean=7 * osc(t), squash=1.04, flutter=t * 2, drag=0.3 * osc(t, 1, 0.25),
                                jet=0.25 if i % 4 == 0 else 0, flicker=t * 3), 80))
        return out
    return [(replace(BASE, lift=20, eyes='wide', feet=((-50, 10), (60, 20)), drag=0.6), 60),
            (replace(BASE, crouch=0.4, squash=0.9, eyes='angry', front=[shockwave(0.3, size=0.6)]), 80),
            (replace(BASE, crouch=0.1, squash=1.03, front=[shockwave(0.6, size=0.6)]), 70),
            (replace(BASE), 100)]


def pat(phase):
    if phase == 'start':
        seq = tween(BASE, [(4, 60, ease, dict(crouch=0.1, tilt=7, eyes='happy', spread=0.3, glow=1.1))])
        return fx(seq, lambda p, t, i: replace(p, flutter=t))
    if phase == 'loop':
        out = []
        n = 10
        for i in range(n):
            t = i / n
            out.append((replace(BASE, crouch=0.1 + 0.03 * osc(t, 0.5), tilt=7 * osc(t), eyes='happy', glow=1.1,
                                spread=0.3 + 0.35 * max(0.0, osc(t, 0.5)), flutter=t * 2, crystal=0.8,
                                front=[holo_heart(t, size=0.45)]), 90))
        return out
    return [(replace(BASE, crouch=0.05, eyes='happy', spread=0.2, squash=1.03), 70),
            (replace(BASE, eyes='happy', squash=0.99), 70), (BASE, 90)]


def poke(phase):
    """Touching the chest: the core flickers, the suit twitches as if ticklish; the plumes jitter."""
    if phase == 'start':
        return [(replace(BASE, glow=1.5, eyes='wide', squash=0.96, lean=-4, front=[burst(0.2, 0, -560, radius=120, count=8)]),
                 60),
                (replace(BASE, glow=0.6, eyes='x', squash=1.04, lean=3, spread=0.4), 60),
                (replace(BASE, glow=1.2, eyes='happy', squash=0.98, lean=-2, spread=0.2), 70)]
    if phase == 'loop':
        out = []
        n = 10
        for i in range(n):
            t = i / n
            out.append((replace(BASE, squash=1 + 0.03 * osc(t, 0.25), lean=4 * osc(t, 0.5), x=4 * osc(t, 0.25),
                                eyes='happy' if i % 4 else 'x', glow=0.8 + 0.6 * (i % 2), spread=0.2 + 0.3 * (i % 3 == 0),
                                flutter=t * 3, hands=((-120, -470 + 10 * osc(t, 0.25)), RH), fist=(1, 1),
                                front=[sparks(t, 0, -560, seed=9, count=5, speed=0.6)]), 75))
        return out
    return [(replace(BASE, squash=1.03, eyes='happy'), 70), (replace(BASE, squash=0.99, eyes='happy'), 70), (BASE, 90)]


def tumble(phase):
    """Thrown (faces right): a fast spin about the waist with afterimages, the jets catch him and he hovers
    tilted, then a three-point landing."""
    if phase == 'start':
        out = []
        for k, ang in enumerate((-60, -140, -220, -300, -360)):
            out.append((replace(BASE, spin=ang, lift=60, eyes='x',
                                shadow=False, wind=0.9, flutter=k / 2, crouch=0.3, jet=0.5 if k == 4 else 0,
                                feet=((-50, -20), (60, -40)),
                                ghosts=((-60, 0.3), (-120, 0.15)) if k < 4 else ()), 55))
        return out
    if phase == 'loop':
        out = []
        n = 10
        for i in range(n):
            t = i / n
            out.append((replace(BASE, lift=60 + 8 * osc(t, 0.5), lean=-14 + 6 * osc(t), jet=0.9, flicker=t * 3,
                                eyes='wide', shadow=False, wind=1.0, flutter=t * 3, crouch=0.2,
                                feet=((-50, -20), (60, -40)), hands=((-200, -440), (176, -420)), w_angle=40,
                                back=[speed_lines(0.7)]), 70))
        return out
    return [(replace(BASE, lift=20, jet=0.4, eyes='wide', crouch=0.2, w_angle=30), 60),
            (replace(BASE, crouch=0.8, squash=0.9, eyes='angry', hands=((-170, -150), (176, -300)), w_angle=20, fist=(0, 1),
                     lean=10, front=[shockwave(0.3, size=0.7)]), 90),
            (replace(BASE, crouch=0.8, squash=0.95, eyes='angry', hands=((-170, -150), (176, -300)), w_angle=20,
                     fist=(0, 1), lean=10, front=[shockwave(0.6, size=0.7)]), 160),
            (replace(BASE, crouch=0.2, eyes='open', squash=1.02), 80),
            (BASE, 110)]


def peek(phase):
    """Hiding at the screen edge (faces right): leans out from behind the edge, scans, ducks back."""
    pose = dict(x=-80, lean=12, tilt=10, eyes='scan', w_angle=-20, hands=(LH, (130, -400)), wind=0.4)
    if phase == 'start':
        return [(replace(BASE, **{**pose, 'lean': 12 * t, 'tilt': 10 * t, 'x': -80 * t}, flutter=t), ms)
                for t, ms in ((0.4, 60), (0.8, 70), (1.0, 100))]
    if phase == 'loop':
        plan = [(1.0, -1, 220), (1.0, 0, 160), (1.0, 1, 260), (1.0, 1, 200), (0.3, 0, 90), (0.0, 0, 200), (0.6, 0, 90),
                (1.0, -1, 300)]
        out = []
        for i, (out_amt, look, ms) in enumerate(plan):
            out.append((replace(BASE, **{**pose, 'lean': 12 * out_amt, 'tilt': 10 * out_amt, 'x': -80 - 60 * (1 - out_amt),
                                         'eyes': 'scan' if out_amt > 0.5 else 'open'},
                                look=look, flutter=i / 8), ms))
        return out
    return [(replace(BASE, **{**pose, 'lean': 12 * (1 - t), 'tilt': 10 * (1 - t), 'x': -80 * (1 - t), 'eyes': 'open'},
                     flutter=t), ms) for t, ms in ((0.4, 70), (0.8, 70), (1.0, 100))]


def look_around():
    plan = [(0, 0, 140), (-1, -7, 260), (-1, -7, 260), (0, 0, 120), (1, 7, 260), (1, 7, 300), (0, 0, 140)]
    return [(replace(BASE, eyes='scan', look=look, tilt=tilt, flutter=i / len(plan)), ms)
            for i, (look, tilt, ms) in enumerate(plan)]


def kata():
    """Halberd form: guard, a wide sweep with a swoosh, a lunging side thrust held, a twirl, back to plant."""
    out = tween(BASE, [
        (3, [70, 70, 90], ease, dict(**GUARD)),
        (1, 120, linear, dict(squash=0.98)),
    ])
    sweep = []
    prev = 28
    for i, u in enumerate((0.1, 0.35, 0.7, 0.92, 1.0)):
        ang = lerp(28, -110, ease(u))
        sweep.append((replace(out[-1][0], w_angle=ang, hands=((-150, -470), (lerp(176, -60, u), -500)), lean=lerp(3, -8, u),
                              squash=1.0, flutter=u, back=[spin_smear(prev, ang, (lerp(176, -60, u), -484), radius=760,
                                                                       width=170)]), 45))
        prev = ang
    out += sweep
    thrust = replace(out[-1][0], back=[], w_angle=90, w_grip=300, hands=((70, -500), (210, -510)),
                     feet=((-150, 0), (150, 0)), crouch=0.45, lean=14, eyes='angry', fist=(1, 1), spread=0.5, wind=0.7)
    out += tween(replace(out[-1][0], back=[]), [
        (4, [50, 50, 50, 60], back_out, {k: getattr(thrust, k) for k in
                                         ('w_angle', 'w_grip', 'hands', 'feet', 'crouch', 'lean', 'spread', 'wind')})])
    out.append((replace(thrust, front=[burst(0.3, 210 + 460, -526, seed=3, radius=160, count=10)]), 260))
    twirl_hand = (60, -600)
    base = replace(thrust, hands=((-60, -560), twirl_hand), w_grip=200, feet=GUARD['feet'], crouch=0.2, lean=0)
    prev = 90
    for i in range(8):
        u = ease((i + 1) / 8)
        ang = lerp(90, 90 + 540, u)
        out.append((replace(base, w_angle=ang, flutter=i / 4, back=[spin_smear(prev, ang, (twirl_hand[0], twirl_hand[1] + 16),
                                                                                  radius=500, width=140)]), 50))
        prev = ang
    out += tween(replace(out[-1][0], back=[], w_angle=(90 + 540) % 360), [
        (3, [70, 80, 90], ease, dict(hands=(LH, RH), w_grip=0, w_angle=-6, feet=BASE.feet, crouch=0.2, spread=0.2,
                                    eyes='open', fist=(0, 1), wind=0.3)),
    ])
    out.append((replace(BASE, crouch=0.2, squash=0.97, eyes='closed', front=[shockwave(0.4, x=150, size=0.5, dust=False)]),
                220))
    out.append((BASE, 110))
    return out


def blade_check():
    """The head comes off the shaft into the left hand; turned in front of the visor, scanned, a puff of dust
    blown off, clicked back on with a spark."""
    top = shaft_top(BASE)
    look_at = (-200, -840)
    out = []
    for i in range(6):
        t = (i + 1) / 6
        u = ease(t)
        pos = (lerp(top[0], look_at[0], u), lerp(top[1], look_at[1], u) - 80 * math.sin(math.pi * u))
        out.append((replace(BASE, blade=(pos[0], pos[1], lerp(-6, -40, u)), hands=(lerp_pt(LH, (look_at[0], look_at[1] + 120), u), RH),
                            fist=(0, 1), eyes='open', tilt=-6 * u, flutter=t), 70))
    for i in range(10):
        t = i / 10
        ang = -40 + 70 * math.sin(t * math.pi)
        out.append((replace(BASE, blade=(look_at[0], look_at[1] + 6 * osc(t), ang, 0.3 if 4 <= i < 7 else 0),
                            hands=((look_at[0], look_at[1] + 120), RH), fist=(0, 1), eyes='scan', look=-0.7 + 0.4 * osc(t),
                            tilt=-6, flutter=t,
                            front=[smoke_up(t, x=look_at[0] - 40, y=look_at[1] - 120, amount=2, size=0.6)] if i >= 7 else []),
                    110))
    for i in range(6):
        t = (i + 1) / 6
        u = ease(t)
        pos = (lerp(look_at[0], top[0], u), lerp(look_at[1], top[1], u) - 80 * math.sin(math.pi * u))
        last = i == 5
        out.append((replace(BASE, blade=None if last else (pos[0], pos[1], lerp(-40, -6, u)),
                            hands=(lerp_pt((look_at[0], look_at[1] + 120), LH, u), RH), fist=(0, 1), tilt=-6 * (1 - u),
                            eyes='open', w_glow=0.9 if last else 0, flutter=t,
                            front=[burst(0.25, *top, seed=7, radius=150, count=10)] if last else []), 70))
    out.append((replace(BASE, eyes='happy', w_glow=0.3), 200))
    out.append((BASE, 100))
    return out


def preen():
    """The plume display: the four plumes rise and fan out like a pheasant's, ripple twice, crystals shimmer;
    then fold back with a shake."""
    out = []
    n = 20
    for i in range(n):
        t = i / (n - 1)
        s = ease(min(1.0, t * 3)) * (1 - ease(max(0.0, (t - 0.75) / 0.25)))
        out.append((replace(BASE, spread=1.1 * s, drag=0.5 * s, flutter=t * 3, crystal=0.5 + 0.5 * s, lean=-3 * s,
                            tilt=-6 * s, eyes='happy' if 0.3 < t < 0.7 else 'open', squash=1 + 0.02 * s,
                            front=[burst((t - 0.3) * 2, -380 + 760 * (i % 2), -980, seed=i, count=6, radius=90,
                                         color=VIOLET_LIGHT)] if 0.3 < t < 0.75 else []), 100 if 5 < i < 15 else 80))
    for k in range(3):
        out.append((replace(BASE, x=4 * (-1) ** k, flutter=k / 2 + 0.2, spread=0.15 * (-1) ** k), 60))
    out.append((BASE, 100))
    return out


def vent():
    """A sigh: shoulders up, a pause, steam blows out of the shoulder vents and the suit sinks."""
    out = tween(BASE, [
        (3, [90, 90, 110], ease, dict(lift=10, squash=1.04, eyes='closed', tilt=-6, spread=0.2)),
        (1, 260, linear, dict(squash=1.045)),
        (4, [70, 80, 90, 110], ease_out, dict(lift=0, squash=0.96, crouch=0.12, tilt=6, spread=0, droop=0.3)),
        (2, [160, 120], ease, dict(squash=1.0, crouch=0, tilt=0, droop=0, eyes='open')),
    ])
    return fx(out, lambda p, t, i: replace(p, flutter=t, front=[vents((i - 4) / 6, 1.0 - max(0, i - 8) * 0.4)] if i >= 4 else []))


def startled():
    """Danger: a jump back into guard, plumes snapping up, the visor wide, a beacon flash."""
    out = [(replace(BASE, eyes='wide', squash=1.06, lift=40, x=-30, spread=1.0, drag=-0.8, front=[beacon(0.1, 0.8)]), 60),
           (replace(BASE, eyes='wide', lift=60, x=-50, spread=1.1, drag=-0.6, front=[beacon(0.3, 1.0)]), 70)]
    out += tween(replace(BASE, x=-50, lift=60, eyes='wide', spread=1.1), [
        (2, [60, 80], ease_in, dict(lift=0, **GUARD, spread=0.9)),
        (1, 90, linear, dict(squash=0.94)),
        (1, 500, linear, dict(squash=1.0, eyes='angry')),
        (3, [100, 100, 120], ease, dict(x=0, crouch=0, feet=BASE.feet, lean=0, hands=(LH, RH), fist=(0, 1), w_angle=-6,
                                       spread=0, eyes='open')),
    ])
    return fx(out, lambda p, t, i: replace(p, flutter=t * 2))


def love():
    out = []
    n = 16
    for i in range(n):
        t = i / (n - 1)
        size = back_out(min(1.0, t * 2.5)) * (1 - ease(max(0.0, (t - 0.8) / 0.2)))
        out.append((replace(BASE, eyes='happy', glow=1.2, spread=0.3 + 0.2 * osc(t, 0.5), flutter=t * 2, crystal=0.8,
                            hands=((-170, -520), RH), fist=(0, 1), tilt=4 * osc(t),
                            front=[holo_heart(t, size=size)]), 100))
    return out


def birthday():
    out = tween(BASE, [
        (2, 70, ease, dict(crouch=0.15, squash=0.97, eyes='happy')),
        (3, [60, 60, 90], ease_out, dict(crouch=0, squash=1.0, **{**RAISED, 'eyes': 'happy'})),
    ])
    n = 16
    for i in range(n):
        t = i / (n - 1)
        out.append((replace(BASE, **{**RAISED, 'eyes': 'happy', 'spread': 0.8 + 0.2 * osc(t, 0.5)}, flutter=t * 2,
                            lift=8 * max(0.0, osc(t, 0.5)),
                            front=[confetti_burst(min(1.0, t * 1.1))], over=[fireworks(t, seed=21, bursts=5)]), 110))
    out += tween(out[-1][0], [(3, [70, 80, 100], ease, dict(hands=(LH, RH), w_grip=0, w_angle=-6, w_glow=0, spread=0,
                                                           crystal=0.5, lift=0, front=[], over=[]))])
    return out


def refuel():
    """Snack: a violet energy cell from the left hand pressed into the chest core; the core and crystals light."""
    out = []
    start = (-300, -260)
    core = (0, -566)
    for i in range(6):
        t = (i + 1) / 6
        u = ease(t)
        pos = (lerp(start[0], core[0] - 60, u), lerp(start[1], core[1], u) - 80 * math.sin(math.pi * u))
        out.append((replace(BASE, hands=((pos[0] - 10, pos[1] + 50), RH), fist=(0, 1), eyes='happy' if t > 0.6 else 'open',
                            tilt=4 * u, flutter=t, front=[energy_cell(pos[0], pos[1], -30 + 30 * u, size=min(1.0, t * 2))]), 80))
    for i in range(4):
        t = (i + 1) / 4
        out.append((replace(BASE, hands=((core[0] - 60 + 50 * t, core[1] + 50), RH), fist=(0, 1), eyes='happy', tilt=4,
                            front=[energy_cell(core[0] - 60 + 60 * t, core[1], 0, size=1 - t * 0.9)]), 70))
    for i in range(8):
        t = i / 7
        out.append((replace(BASE, glow=1.5 - 0.5 * t, crystal=1.0 - 0.5 * t, eyes='happy', squash=1 + 0.03 * math.sin(t * 6),
                            spread=0.5 * (1 - t), flutter=t * 2, hands=(lerp_pt((core[0] - 10, core[1] + 50), LH, ease(t)), RH),
                            fist=(0, 1), front=[burst(t, 0, -566, seed=4, radius=240, count=12, color=VIOLET_LIGHT)]), 90))
    out.append((BASE, 100))
    return out


def coolant():
    """Water: coolant mist blasts from the shoulder vents, the suit relaxes with closed eyes."""
    out = []
    n = 14
    for i in range(n):
        t = i / (n - 1)
        out.append((replace(BASE, eyes='closed' if 2 < i < 12 else 'open', tilt=-5 * hop(t), lift=6 * hop(t),
                            squash=1 + 0.02 * hop(t), flutter=t, droop=0.2 * hop(t),
                            front=[vents(t * 2 % 1, 1.0 if i < 11 else 0.5), vents((t * 2 + 0.5) % 1, 0.8)]), 110))
    return out


def shy():
    """The plumes curl forward over the face like a fan; the visor peeks between them once."""
    out = []
    cover = (0, -760)
    plan = [(0.3, 'open'), (0.7, 'happy'), (1.0, 'happy'), (1.0, 'happy'), (1.0, 'closed'), (0.85, 'happy'),
            (0.85, 'happy'), (1.0, 'closed'), (1.0, 'closed'), (0.6, 'happy'), (0.2, 'happy'), (0.0, 'open')]
    for i, (r, e) in enumerate(plan):
        out.append((replace(BASE, reach=r, reach_to=cover, eyes=e, tilt=8 * r, crouch=0.08 * r, flutter=i / 6,
                            hands=((-120, -470 * r - 378 * (1 - r)), RH), fist=(0, 1)), 120 if 2 < i < 9 else 80))
    return out


def calibrate():
    """Eye break: a joint calibration routine: the left arm sweeps up in an arc, the waist leans each way, the
    head scans, and every joint ticks into place."""
    out = tween(BASE, [
        (4, 90, ease, dict(hands=((-330, -760), RH), fist=(0, 1), lean=-6, eyes='scan', look=-1)),
        (1, 200, linear, dict(squash=1.01)),
        (4, 90, ease, dict(hands=((-60, -980), RH), lean=6, look=1, tilt=8)),
        (1, 200, linear, dict(squash=1.0)),
        (4, 90, ease, dict(hands=(LH, RH), lean=0, look=0, tilt=0, eyes='open')),
        (1, 120, linear, dict(squash=0.98, eyes='happy')),
        (1, 120, linear, dict(squash=1.0)),
    ])
    return fx(out, lambda p, t, i: replace(p, flutter=t, crystal=0.5 + 0.4 * (i in (4, 9, 15))))


def war_drum():
    """Friday evening: the butt of the halberd beats a slow-slow-quick-quick rhythm, a ring on every hit."""
    beats = (0, 4, 8, 10, 12)
    out = []
    n = 16
    for i in range(n):
        t = i / n
        hit = i in beats
        up = (i + 1) in beats or (i + 1) % n in beats
        rh = (RH[0], RH[1] - (36 if up else 0))
        out.append((replace(BASE, hands=(LH, rh), crouch=0.08 if hit else 0, squash=0.98 if hit else 1.0,
                            spread=0.4 if hit else 0.15, eyes='happy' if 4 <= i < 12 else 'open', flutter=t * 2,
                            tilt=4 * osc(t, 0.5), crystal=0.9 if hit else 0.5,
                            front=[shockwave(0.25, x=176, size=0.4, dust=False)] if hit else []), 100))
    return out


# Moves: flying on the crystal jets, and a ground dash.
def fly(phase, angle=0.0):
    """Faces right. Mount: crouch, jets light, lift; loop tilted by `angle` (degrees, + climbs); land."""
    forward = dict(hands=((-100, -520), (230, -470)), w_angle=70, w_grip=300, fist=(1, 1))
    if phase == 'start':
        out = tween(BASE, [
            (2, 70, ease, dict(crouch=0.3, squash=0.95, eyes='angry', jet=0.3, flicker=0.3)),
            (3, [60, 60, 80], ease_out, dict(crouch=0.05, squash=1.03, lift=60, jet=1.0, lean=12, wind=0.8,
                                            feet=((-80, -10), (40, -30)), flicker=1.0, **forward)),
        ])
        return follow_through(out)
    if phase == 'loop':
        out = []
        n = 8
        for i in range(n):
            t = i / n
            out.append((replace(BASE, lift=60 + 10 * osc(t), lean=12 - angle * 0.4, tilt=-angle * 0.3, jet=1.0 + 0.3 * (angle > 0),
                                flicker=t * 2, eyes='angry', wind=1.0, flutter=t * 2, feet=((-80, -10), (40, -30)),
                                back=[speed_lines(0.7 + 0.3 * osc(t))], **forward), 70))
        return out
    out = tween(replace(BASE, lift=60, lean=12, jet=1.0, eyes='angry', wind=1.0, feet=((-80, -10), (40, -30)), **forward), [
        (2, [60, 60], ease, dict(lean=-6, lift=40, jet=1.2)),
        (1, 70, linear, dict(lift=0, jet=0, crouch=0.4, squash=0.92, lean=0, feet=BASE.feet, wind=0.4)),
        (3, [70, 80, 100], ease, dict(crouch=0, squash=1.0, hands=(LH, RH), w_angle=-6, w_grip=0, fist=(0, 1), eyes='open',
                                     wind=0.15)),
    ])
    out = fx(out, lambda p, t, i: replace(p, front=[shockwave(0.2 + (i - 3) * 0.2, size=0.6)] if 3 <= i < 7 else []))
    return follow_through(out)


def dash(phase):
    """Faces right: low skim over the ground with violet afterimages and dust."""
    low = dict(crouch=0.45, lean=20, feet=((-150, -10), (40, 0)), hands=((-200, -420), (190, -440)), w_angle=74, w_grip=300,
               fist=(1, 1), eyes='angry', wind=1.0)
    if phase == 'start':
        return [(replace(BASE, crouch=0.3, squash=0.95, eyes='angry', lean=6), 70),
                (replace(BASE, **low, squash=0.97), 60),
                (replace(BASE, **low, ghosts=((-90, 0.3),)), 60)]
    if phase == 'loop':
        out = []
        n = 6
        for i in range(n):
            t = i / n
            out.append((replace(BASE, **low, lift=8 + 4 * osc(t, 0.5), flutter=t * 3,
                                ghosts=((-110, 0.4), (-220, 0.22), (-330, 0.1)),
                                front=[dust(t, x=-160, side=-1)], back=[speed_lines(0.9)]), 60))
        return out
    return [(replace(BASE, **{**low, 'lean': -6}, front=[dust(0.1, x=40, side=1)]), 70),
            (replace(BASE, **{**low, 'lean': -10, 'crouch': 0.5}, front=[dust(0.4, x=60, side=1)]), 80),
            (replace(BASE, crouch=0.2, lean=0, eyes='open', front=[dust(0.7, x=60, side=1)]), 90),
            (BASE, 110)]


SEQUENCES = {
    'idle': lambda: idle(),
    'idle_scan': lambda: idle('scan'),
    'idle_plume': lambda: idle('plume'),
    'idle_shift': lambda: idle('shift'),
    'idle_happy': lambda: idle('happy'),
    'idle_poor': lambda: idle('poor'),
    'holo/start': lambda: think('start'),
    'holo/loop': lambda: think('loop'),
    'holo/plan': lambda: think('loop', 'plan'),
    'holo/end': lambda: think('end'),
    'scan/start': lambda: scan('start'),
    'scan/loop': lambda: scan('loop'),
    'scan/flick': lambda: scan('loop', flicking=True),
    'scan/end': lambda: scan('end'),
    'scan/to_forge': scan_to_forge,
    'forge/start': lambda: forge('start'),
    'forge/loop': lambda: forge('loop'),
    'forge/weld': lambda: forge('loop', weld=True),
    'forge/end': lambda: forge('end'),
    'forge/to_scan': forge_to_scan,
    'signal/start': lambda: signal('start'),
    'signal/loop': lambda: signal('loop'),
    'signal/end': lambda: signal('end'),
    'short_circuit': short_circuit,
    'power_down': power_down,
    'victory': victory,
    'standby/start': lambda: standby_mode('start'),
    'standby/loop': lambda: standby_mode('loop'),
    'standby/end': lambda: standby_mode('end'),
    'landing': landing,
    'depart': depart,
    'rage': rage,
    'rage_blast': lambda: rage('blast'),
    'held/start': lambda: held('start'),
    'held/loop': lambda: held('loop'),
    'held/end': lambda: held('end'),
    'pat/start': lambda: pat('start'),
    'pat/loop': lambda: pat('loop'),
    'pat/end': lambda: pat('end'),
    'poke/start': lambda: poke('start'),
    'poke/loop': lambda: poke('loop'),
    'poke/end': lambda: poke('end'),
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
    'look': look_around,
    'kata': kata,
    'blade_check': blade_check,
    'preen': preen,
    'vent': vent,
    'startled': startled,
    'love': love,
    'birthday': birthday,
    'refuel': refuel,
    'coolant': coolant,
    'shy': shy,
    'calibrate': calibrate,
    'war_drum': war_drum,
    'fly/mount_right': lambda: fly('start'),
    'fly/right': lambda: fly('loop'),
    'fly/rise_right': lambda: fly('loop', angle=20),
    'fly/dive_right': lambda: fly('loop', angle=-20),
    'fly/land_right': lambda: fly('end'),
    'fly/mount_left': lambda: mirrored(fly('start')),
    'fly/left': lambda: mirrored(fly('loop')),
    'fly/rise_left': lambda: mirrored(fly('loop', angle=20)),
    'fly/dive_left': lambda: mirrored(fly('loop', angle=-20)),
    'fly/land_left': lambda: mirrored(fly('end')),
    'dash/start_right': lambda: dash('start'),
    'dash/right': lambda: dash('loop'),
    'dash/end_right': lambda: dash('end'),
    'dash/start_left': lambda: mirrored(dash('start')),
    'dash/left': lambda: mirrored(dash('loop')),
    'dash/end_left': lambda: mirrored(dash('end')),
}

if __name__ == '__main__':
    run(SEQUENCES, render, __doc__)
