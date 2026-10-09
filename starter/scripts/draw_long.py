#!/usr/bin/env python3
"""Draw Long, the chibi warlord mecha pet, frame by frame, from WindyWin's own design sheet.

    pip install pycairo pillow numpy scipy
    python3 scripts/draw_long.py OUT              # every sequence, as OUT/<sequence>/_NNN_<ms>.png
    python3 scripts/draw_long.py OUT idle         # only these sequences

The art is WindyWin's painted design sheet (scripts/long_art/design-sheet.png): four poses, standing with the
spear, a lunge, a fist at the chin and a cheer with the spear raised. The poses were matted with rembg's
isnet-general-use model and split by long_art/split_poses.py (pose1.png .. pose4.png).

The standing pose is a cutout puppet, the way Spine or Live2D rig a character. long_art/make_rig.py cuts it into
the designer's own pixels per part: head, both arms, the spear, thighs, shins, boots, the two clusters of red
tendrils and the body. It inpaints what each part hid, and records the joints in long_art/rig/rig.json. Here
every part turns about its joint:
- the head about the neck, the arms about the shoulders, the spear about (and sliding through) the fist;
- the upper body about the hips;
- the legs by two-bone IK from the hips to wherever the feet are put, with the boots kept flat;
- the tendrils sway in a travelling wave and fan out.
The other three poses are used whole for the moments the sheet draws (the lunge, the hand at the chin, the shout).
A swap between drawings happens on a squash, so it reads as a snap rather than a cut.

On top of either, this script moves the figure: offsets, rotation, squash and stretch, and afterimages. It also
relights the drawing's own cyan gems and eyes and its violet crystals (dimmed, turned red, or blooming), and adds
effects in the drawing's palette (holograms, a scan sheet, sparks, shockwaves, a beacon, fireworks and the rest).
It never repaints the suit. Everything is deterministic, so the sheet, the rig and this script are the art's
source.
"""
import json
import math
import random
from dataclasses import dataclass, field, replace
from pathlib import Path

import cairo
import numpy as np
from PIL import Image
from scipy import ndimage

from pet_art import (CX, GROUND, INK, SIZE, WHITE, back_out, ease, ease_in, ease_out, ellipse, hop, lerp,
                     linear, mix, osc, puff, rgb, run, star, tween)

BLACK = (0.17, 0.17, 0.19)
BLACK_LIGHT = (0.30, 0.30, 0.34)
SILVER = (0.66, 0.67, 0.70)
SILVER_LIGHT = (0.86, 0.87, 0.89)
GOLD = (0.74, 0.60, 0.32)
GOLD_LIGHT = (0.95, 0.84, 0.55)
VIOLET = (0.45, 0.30, 0.70)
VIOLET_LIGHT = (0.70, 0.55, 0.96)
RED = (0.62, 0.08, 0.14)
RED_LIGHT = (0.90, 0.24, 0.30)
CYAN = (0.25, 0.88, 0.96)
EYE = (0.40, 0.95, 1.0)
LINE = 9

ART = Path(__file__).resolve().parent / 'long_art'
S = 1.3                     # drawing pixels to canvas pixels
OLD = 0.62                  # effect units to canvas pixels (the effects below are laid out in these units)
PAD = 48                    # transparent margin round each pose, so a bloom is not cut off

# Each pose: file, anchor (the point between the feet, in the drawing's pixels) and landmarks relative to it
# (x right, y up negative), measured on the drawing: the eyes' row, the head, chest gem, shoulders, fists and
# the spear's tip and butt.
POSES = {
    1: dict(file='pose1.png', anchor=(157, 579), eyes=(-17, -352, 18), head=(3, -350), chest=(-12, -284),
            shoulders=((-61, -302), (60, -303)), hands=((-114, -271), (86, -211)), tip=(-117, -575), butt=(-112, -25)),
    2: dict(file='pose2.png', anchor=(200, 448), eyes=(-23, -345, 16), head=(-10, -350), chest=(0, -274),
            shoulders=((-71, -300), (29, -286)), hands=((-87, -203), (63, -213)), tip=(146, -33), butt=(-192, -293)),
    3: dict(file='pose3.png', anchor=(100, 450), eyes=(-6, -346, 22), head=(-10, -348), chest=(-14, -277),
            shoulders=((-56, -297), (51, -296)), hands=((-35, -315), (76, -227)), tip=None, butt=None),
    4: dict(file='pose4.png', anchor=(195, 589), eyes=(1, -369, 18), head=(1, -368), chest=(-15, -299),
            shoulders=((-49, -336), (52, -334)), hands=((-113, -433), (132, -438)), tip=(55, -585), butt=(-182, -365)),
}


@dataclass
class Pose:
    pose: int = 1               # which drawing: 1 stand, 2 lunge, 3 fist at the chin, 4 cheer
    x: float = 0
    lift: float = 0
    rot: float = 0              # degrees, about `pivot`
    pivot: float = 0            # canvas pixels above the feet that the rotation turns about
    squash: float = 1           # vertical scale about the feet; width follows inversely
    scale: float = 1
    face: int = 1
    alpha: float = 1
    shake: float = 0            # sideways jitter, canvas pixels
    gems: float = 1             # light of the cyan gems: 0 dark, 1 as drawn, >1 blooming
    eyes: float = 1             # light of the eyes alone
    eye_red: float = 0          # eyes and gems turned red (anger, faults)
    crystal: float = 0          # violet bloom of the crystals
    dim: float = 0              # the whole suit darkened (power loss)
    blush: float = 0
    jet: float = 0              # violet thrust under the crystals
    flicker: float = 0
    flutter: float = 0          # phase for effects
    drag: float = 0             # set by pet_art.follow_through; unused by the drawing
    ghosts: tuple = ()          # afterimages: (dx, alpha[, dlift])
    # The puppet (pose 1 only), in the drawing's pixels and degrees:
    head_rot: float = 0         # about the neck; + turns clockwise on screen
    head_dx: float = 0
    head_dy: float = 0
    arm_l: float = 0            # the spear arm, about its shoulder (+ raises the fist)
    arm_r: float = 0            # the free arm (- swings the fist out and up)
    spear_rot: float = 0        # the spear turning in the fist (- tips it to the left)
    spear_dy: float = 0         # the spear sliding through the fist along its shaft (- towards the tip)
    body_rot: float = 0         # the upper body leaning about the hips
    hip_dx: float = 0
    hip_dy: float = 0           # + lowers the hips (a crouch: the knees bend)
    feet: tuple = ((0, 0), (0, 0))          # foot offsets (left, right); - lifts
    foot_rot: tuple = (0, 0)
    wave: float = 0.6           # tendril sway
    spread: float = 0           # tendrils lifted and fanned
    shadow: bool = True
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
    g = cairo.LinearGradient(min(xs), min(ys), max(xs), max(ys))
    g.add_color_stop_rgb(0, *light)
    g.add_color_stop_rgb(0.55, *base)
    g.add_color_stop_rgb(1, *mix(base, (0, 0, 0), 0.35))
    ctx.set_source(g)
    ctx.fill_preserve()
    rgb(ctx, INK)
    ctx.set_line_width(width)
    ctx.stroke()


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


# ---------------------------------------------------------------- the drawing, relit

class Art:
    """Pixels and the masks used to relight them: cyan (gems and eyes), the eyes alone, and violet crystal above
    `crystal_below`. Arrays are cropped to the art plus a margin; `offset` is where (0, 0) of the drawing is."""
    def __init__(self, im, eyes=None, crystal_below=None):
        ys, xs = np.nonzero(im[..., 3] > 0)
        y0, y1, x0, x1 = max(0, ys.min() - 2), ys.max() + 3, max(0, xs.min() - 2), xs.max() + 3
        im = np.pad(im[y0:y1, x0:x1], ((PAD, PAD), (PAD, PAD), (0, 0)))
        self.offset = (x0 - PAD, y0 - PAD)          # drawing coordinates of this array's (0, 0)
        self.rgb, self.a = im[..., :3], im[..., 3]
        r, g, b = self.rgb[..., 0], self.rgb[..., 1], self.rgb[..., 2]
        cyan = np.clip((np.minimum(g, b) - r - 0.12) / 0.25, 0, 1) * np.clip((b - 0.45) / 0.2, 0, 1) * self.a
        self.cyan = ndimage.gaussian_filter(cyan, 0.6)
        H, W = self.a.shape
        yy, xx = np.mgrid[0:H, 0:W]
        yy, xx = yy + self.offset[1], xx + self.offset[0]
        if eyes:
            cx, cy, half = eyes
            box = np.clip(1 - np.maximum(np.abs(xx - cx) / 52, np.abs(yy - cy) / half), 0, 1)
            self.eye = self.cyan * np.clip(box * 4, 0, 1)
        else:
            self.eye = np.zeros_like(self.cyan)
        self.gem = np.clip(self.cyan - self.eye, 0, 1)
        violet = np.clip((b - g - 0.08) / 0.2, 0, 1) * np.clip((r - g + 0.02) / 0.15, 0, 1) * self.a
        self.crystal = violet * (yy < crystal_below) if crystal_below is not None else np.zeros_like(violet)
        self.lit = (self.cyan.max() > 0.05) or (self.crystal.max() > 0.05)
        self._cache = {}

    def surface(self, p):
        key = (round(p.gems, 2), round(p.eyes, 2), round(p.eye_red, 2), round(p.crystal, 2), round(p.dim, 2))
        if not self.lit:
            key = (0, 0, 0, 0, key[4])
        if key in self._cache:
            return self._cache[key]
        rgb = self.rgb.copy()
        lum = rgb.mean(-1, keepdims=True)
        for mask, level in ((self.gem, p.gems), (self.eye, p.eyes)):
            m = mask[..., None]
            if level < 1:
                rgb = rgb * (1 - m * (1 - level) * 0.88)
            if p.eye_red > 0:
                red = np.concatenate([np.clip(lum * 1.5, 0, 1), lum * 0.18, lum * 0.24], -1)
                rgb = rgb + (red - rgb) * m * p.eye_red
        if p.dim > 0:
            rgb = rgb * (1 - 0.45 * p.dim)
        a = self.a
        add = np.zeros_like(rgb)
        if self.lit:
            bloom = (max(0.0, p.gems - 1) * 1.2, self.gem), (max(0.0, p.eyes - 1) * 1.4 + 0.25 * (p.eyes >= 1), self.eye)
            for strength, mask in bloom:
                if strength > 0 and mask.max() > 0.05:
                    color = mix(EYE, RED_LIGHT, p.eye_red)
                    halo = ndimage.gaussian_filter(mask, 7) * strength * 2.2
                    add += halo[..., None] * np.array(color)
            if p.crystal > 0 and self.crystal.max() > 0.05:
                halo = ndimage.gaussian_filter(self.crystal, 9) * p.crystal * 1.1
                add += halo[..., None] * np.array(VIOLET_LIGHT)
                rgb = rgb + self.crystal[..., None] * p.crystal * 0.12
        # Light adds over the drawing and spills past its edge as a soft halo.
        glow_a = np.clip(add.max(-1), 0, 1)
        out_a = a + glow_a * (1 - a)
        out_rgb = np.clip((rgb * a[..., None] + add) / np.maximum(out_a[..., None], 1e-4), 0, 1)
        surface = to_surface(out_rgb, out_a)
        if len(self._cache) > 64:
            self._cache.clear()
        self._cache[key] = surface
        return surface

    def silhouette(self):
        if 'sil' not in self._cache:
            self._cache['sil'] = to_surface(np.ones_like(self.rgb), self.a)
        return self._cache['sil']

    def paint(self, ctx, surface):
        ctx.set_source_surface(surface, *self.offset)
        ctx.get_source().set_filter(cairo.FILTER_GOOD)
        ctx.paint()


def to_surface(rgb, a):
    """A cairo surface (premultiplied BGRA) from float RGB and alpha arrays."""
    H, W = a.shape
    pm = np.dstack([rgb[..., 2] * a, rgb[..., 1] * a, rgb[..., 0] * a, a])
    surface = cairo.ImageSurface(cairo.FORMAT_ARGB32, W, H)
    stride = surface.get_stride()
    view = np.ndarray((H, stride // 4, 4), dtype=np.uint8, buffer=surface.get_data())
    view[:, :W] = (pm * 255 + 0.5).astype(np.uint8)
    surface.mark_dirty()
    return surface


def load(path):
    return np.asarray(Image.open(path).convert('RGBA'), dtype=np.float32) / 255


_ART = {}


def pose_art(n):
    """A whole drawing (poses 2 to 4, and pose 1 for afterimages), in drawing coordinates."""
    if n not in _ART:
        spec = POSES[n]
        ax, ay = spec['anchor']
        _ART[n] = Art(load(ART / spec['file']), eyes=(ax + spec['eyes'][0], ay + spec['eyes'][1], spec['eyes'][2]),
                      crystal_below=ay + spec['chest'][1] + 10)
    return _ART[n]


# ---------------------------------------------------------------- the cutout rig of pose 1 (long_art/make_rig.py)

RIG = json.loads((ART / 'rig' / 'rig.json').read_text())
PIV = {k: tuple(v) for k, v in RIG['pivots'].items()}
_PARTS = {}


def part(name):
    if name not in _PARTS:
        spec = POSES[1]
        ax, ay = spec['anchor']
        _PARTS[name] = Art(load(ART / 'rig' / f'{name}.png'),
                           eyes=(ax + spec['eyes'][0], ay + spec['eyes'][1], spec['eyes'][2]) if name == 'head' else None,
                           crystal_below=ay + spec['chest'][1] + 10 if name == 'body' else None)
    return _PARTS[name]


def rotate_about(ctx, pivot, degrees):
    if degrees:
        ctx.translate(*pivot)
        ctx.rotate(math.radians(degrees))
        ctx.translate(-pivot[0], -pivot[1])


def body_matrix(p):
    """Where the upper body sits: moved by the hips and leaned about them."""
    m = cairo.Matrix()
    m.translate(p.hip_dx, p.hip_dy)
    hx, hy = PIV['hips']
    m.translate(hx, hy)
    m.rotate(math.radians(p.body_rot))
    m.translate(-hx, -hy)
    return m


def leg_bones(p, side):
    """Hip, knee and ankle of one leg (drawing coordinates) after the pose: hips follow the body, the ankle the
    foot offset, and the knee comes from two-bone IK, bending outwards like the drawing."""
    s = '_l' if side < 0 else '_r'
    m = body_matrix(p)
    hip = m.transform_point(*PIV['hip' + s])
    foot = p.feet[0 if side < 0 else 1]
    ankle = (PIV['ankle' + s][0] + foot[0], PIV['ankle' + s][1] + foot[1])
    lt = math.dist(PIV['hip' + s], PIV['knee' + s])
    ls = math.dist(PIV['knee' + s], PIV['ankle' + s])
    reach = math.dist(hip, ankle)
    if reach > lt + ls - 0.5:       # a leg cannot stretch: the foot hangs from the straight leg instead
        k = (lt + ls - 0.5) / reach
        ankle = (hip[0] + (ankle[0] - hip[0]) * k, hip[1] + (ankle[1] - hip[1]) * k)
    knee = max((ik(hip, ankle, lt, ls, b) for b in (-1, 1)), key=lambda q: side * q[0])
    return hip, knee, ankle


def ik(a, b, l1, l2, bend_side):
    dx, dy = b[0] - a[0], b[1] - a[1]
    d = max(1e-3, min(math.hypot(dx, dy), l1 + l2 - 1e-3))
    a1 = math.atan2(dy, dx)
    cos_t = (l1 * l1 + d * d - l2 * l2) / (2 * l1 * d)
    t = math.acos(max(-1.0, min(1.0, cos_t)))
    ang = a1 - bend_side * t
    return (a[0] + math.cos(ang) * l1, a[1] + math.sin(ang) * l1)


def bone_delta(rest0, rest1, now0, now1):
    """Degrees a part turns so its rest bone rest0->rest1 lies along now0->now1."""
    return math.degrees(math.atan2(now1[1] - now0[1], now1[0] - now0[0]) - math.atan2(rest1[1] - rest0[1], rest1[0] - rest0[0]))


def tendril_surface(name, p):
    """The tendrils swaying: pixels shift sideways more the further they hang from the root, in a travelling wave."""
    art = part(name)
    if not p.wave and not p.spread:
        return art.surface(p)
    rx, ry = PIV[name]
    side = -1 if name.endswith('_l') else 1
    H, W = art.a.shape
    yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
    gx, gy = xx + art.offset[0], yy + art.offset[1]
    d = np.hypot(gx - rx, gy - ry)
    u = np.clip(d / 170, 0, 1.4)
    phase = p.flutter * 2 * math.pi + d * 0.035
    sx = side * (p.wave * 9 * u ** 1.5 * np.sin(phase) + p.spread * 30 * u ** 1.3)
    sy = -p.spread * 26 * u ** 1.6 + p.wave * 3 * u * np.cos(phase)
    coords = [yy - sy, xx - sx]
    surf = art.surface(p)
    stride = surf.get_stride()
    data = np.ndarray((H, stride // 4, 4), dtype=np.uint8, buffer=surf.get_data())[:, :W].astype(np.float32)
    warped = np.stack([ndimage.map_coordinates(data[..., c], coords, order=1, mode='constant') for c in range(4)], -1)
    out = cairo.ImageSurface(cairo.FORMAT_ARGB32, W, H)
    view = np.ndarray((H, out.get_stride() // 4, 4), dtype=np.uint8, buffer=out.get_data())
    view[:, :W] = np.clip(warped + 0.5, 0, 255).astype(np.uint8)
    out.mark_dirty()
    return out


def draw_rig(ctx, p, silhouette=False):
    """Pose 1 as a cutout puppet, in drawing coordinates."""
    def paint(name, surface=None):
        art = part(name)
        if silhouette:
            ctx.set_source_rgba(*VIOLET_LIGHT, 1)
            ctx.mask_surface(art.silhouette(), *art.offset)
        else:
            art.paint(ctx, surface or art.surface(p))
    upper = body_matrix(p)
    legs = {side: leg_bones(p, side) for side in (-1, 1)}
    for name in RIG['order']:
        ctx.save()
        if name.startswith('tendrils'):
            ctx.transform(upper)
            paint(name, None if silhouette else tendril_surface(name, p))
        elif name[:5] in ('thigh', 'shin_', 'boot_'):
            side = -1 if name.endswith('_l') else 1
            s = '_l' if side < 0 else '_r'
            hip, knee, ankle = legs[side]
            if name.startswith('thigh'):
                rest0, rest1, now0, now1 = PIV['hip' + s], PIV['knee' + s], hip, knee
            elif name.startswith('shin'):
                rest0, rest1, now0, now1 = PIV['knee' + s], PIV['ankle' + s], knee, ankle
            else:
                rest0, rest1, now0, now1 = PIV['ankle' + s], None, ankle, None
            ctx.translate(now0[0] - rest0[0], now0[1] - rest0[1])
            if rest1:
                rotate_about(ctx, rest0, bone_delta(rest0, rest1, now0, now1))
            else:
                rotate_about(ctx, rest0, p.foot_rot[0 if side < 0 else 1])
            paint(name)
        else:
            ctx.transform(upper)
            if name == 'arm_r':
                rotate_about(ctx, PIV['arm_r'], p.arm_r)
            elif name in ('arm_l', 'spear'):
                rotate_about(ctx, PIV['arm_l'], p.arm_l)
                if name == 'spear':
                    rotate_about(ctx, PIV['fist_l'], p.spear_rot)
                    ctx.translate(0, p.spear_dy)
            elif name == 'head':
                ctx.translate(p.head_dx, p.head_dy)
                rotate_about(ctx, PIV['head'], p.head_rot)
            paint(name)
        ctx.restore()


def rig(p):
    """The pose's landmarks in effect units, for effects that sit on the suit (the eyes, chest, fists)."""
    spec = POSES[p.pose]
    k = S / OLD
    ax, ay = spec['anchor']
    pt = lambda q: (q[0] * k, q[1] * k) if q else None
    if p.pose == 1:
        # Follow the puppet: the body, head and fists move with their bones.
        upper = body_matrix(p)
        def moved(q, extra=None):
            x, y = q[0] + ax, q[1] + ay
            if extra:
                x, y = extra.transform_point(x, y)
            x, y = upper.transform_point(x, y)
            return ((x - ax) * k, (y - ay) * k)
        def turn(pivot, deg):
            m = cairo.Matrix()
            m.translate(*pivot)
            m.rotate(math.radians(deg))
            m.translate(-pivot[0], -pivot[1])
            return m
        arm_l, arm_r = turn(PIV['arm_l'], p.arm_l), turn(PIV['arm_r'], p.arm_r)
        slide = cairo.Matrix(y0=p.spear_dy)
        spear = slide.multiply(turn(PIV['fist_l'], p.spear_rot)).multiply(arm_l)
        chest = moved(spec['chest'])
        return dict(head=moved(spec['head']), chest=(chest[0], chest[1] - 66), gem=chest,
                    hands=(moved(spec['hands'][0], arm_l), moved(spec['hands'][1], arm_r)),
                    shoulders=tuple(moved(q) for q in spec['shoulders']), tip=moved(spec['tip'], spear),
                    butt=moved(spec['butt'], spear))
    chest = pt(spec['chest'])
    return dict(head=pt(spec['head']), chest=(chest[0], chest[1] - 66), gem=chest, hands=tuple(map(pt, spec['hands'])),
                shoulders=tuple(map(pt, spec['shoulders'])), tip=pt(spec['tip']), butt=pt(spec['butt']))


def body_transform(ctx, p, dx=0.0, dlift=0.0):
    ctx.translate(CX + p.x + dx + p.shake, GROUND - p.lift - dlift)
    if p.rot:
        ctx.translate(0, -p.pivot)
        ctx.rotate(math.radians(p.rot) * p.face)
        ctx.translate(0, p.pivot)
    s = S * p.scale
    ctx.scale(p.face * s / max(p.squash, 0.2) ** 0.5, s * p.squash)


def draw_figure(ctx, p, silhouette=False):
    ax, ay = POSES[p.pose]['anchor']
    ctx.save()
    ctx.translate(-ax, -ay)
    if p.pose == 1:
        draw_rig(ctx, p, silhouette)
    else:
        art = pose_art(p.pose)
        if silhouette:
            ctx.set_source_rgba(*VIOLET_LIGHT, 1)
            ctx.mask_surface(art.silhouette(), *art.offset)
        else:
            art.paint(ctx, art.surface(p))
    ctx.restore()


def effects(ctx, p, props):
    ctx.save()
    ctx.scale(OLD / S, OLD / S)
    for prop in props:
        prop(ctx, p)
    ctx.restore()


def draw_jets(ctx, p):
    """Violet thrust from behind the shoulders, pointing down the body's axis."""
    if p.jet <= 0:
        return
    r = rig(p)
    for side, (sx, sy) in zip((-1, 1), r['shoulders']):
        x, y = sx + side * 70, sy + 250
        length = 520 * p.jet * (0.85 + 0.15 * math.sin(p.flicker * 2 * math.pi * 3 + side))
        tx, ty = x - side * 30, y + length
        glow(ctx, x, y + length * 0.4, length * 0.6, VIOLET_LIGHT, 0.45)
        for wmul, color, alpha in ((1.0, VIOLET_LIGHT, 0.85), (0.5, mix(VIOLET_LIGHT, WHITE, 0.7), 0.95)):
            w = 34 * wmul
            ctx.move_to(x - w, y)
            ctx.curve_to(x - w * 0.8, y + length * 0.4, tx, ty, tx, ty)
            ctx.curve_to(tx, ty, x + w * 0.8, y + length * 0.4, x + w, y)
            ctx.close_path()
            rgb(ctx, color, alpha)
            ctx.fill()


def render(p):
    surface = cairo.ImageSurface(cairo.FORMAT_ARGB32, SIZE, SIZE)
    ctx = cairo.Context(surface)
    ctx.set_line_join(cairo.LINE_JOIN_ROUND)
    for prop in p.under:
        prop(ctx, p)
    if p.alpha > 0 and p.scale > 0.01:
        if p.shadow:
            k = 1 / (1 + p.lift / 220)
            ellipse(ctx, CX + p.x, GROUND + 4, 150 * p.scale * k, 18 * k)
            rgb(ctx, (0, 0, 0), 0.2 * k * p.alpha)
            ctx.fill()
        for ghost in p.ghosts:
            dx, a = ghost[0], ghost[1]
            dy = ghost[2] if len(ghost) > 2 else 0
            ctx.push_group()
            ctx.save()
            body_transform(ctx, p, dx, dy)
            draw_figure(ctx, p, silhouette=True)
            ctx.restore()
            ctx.pop_group_to_source()
            ctx.paint_with_alpha(a * p.alpha)
        ctx.push_group()
        ctx.save()
        body_transform(ctx, p)
        effects(ctx, p, p.back)
        effects(ctx, p, [lambda c, q: draw_jets(c, q)])
        draw_figure(ctx, p)
        effects(ctx, p, p.front)
        ctx.restore()
        ctx.pop_group_to_source()
        ctx.paint_with_alpha(p.alpha)
    for prop in p.over:
        prop(ctx, p)
    return surface


# ---------------------------------------------------------------- effects (props): body space unless noted

HOLO = (0.80, 0.66, 1.0)        # hologram light
HOT = (1.0, 0.88, 0.62)         # sparks: white-hot light, not the armour's gold
STEAM = (0.93, 0.93, 0.97)


def hologram(t, size=1.0, kind='map', at=(-380, -1040)):
    """Thinking: a strategy table projected from the open left palm, a disc of rings with nodes that light in turn
    and links that draw themselves; 'plan' moves a red marker along the links."""
    def draw(ctx, p):
        if size <= 0.02:
            return
        r = rig(p)
        hx, hy = r['head'][0] - 36, r['head'][1] + 8
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


def data_panel(t, open_=1.0, at=(390, -900), beam=None, flick=0.0):
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
        x, y = r['head'][0], r['head'][1] - 420
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
            x = hx + 160 + local * 80 + math.sin(local * 5) * 10
            y = hy - 120 - local * 240
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
        x, y = r['head'][0] + 240, r['head'][1] - 140
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


# ---------------------------------------------------------------- more effects for the drawn poses

def glint(t, a, b):
    """A star of light running along the spear from a to b (effect units), t 0..1."""
    def draw(ctx, p):
        if not 0 < t < 1.1:
            return
        u = min(1.0, t)
        x, y = lerp(a[0], b[0], ease(u)), lerp(a[1], b[1], ease(u))
        s = 34 + 30 * math.sin(math.pi * min(1.0, t))
        glow(ctx, x, y, s * 2.4, (0.8, 0.98, 1.0), 0.55)
        star(ctx, x, y, s, WHITE, outline=False, points=4, inner=0.18, rot=t * 2)
    return draw


def twinkles(t, points, seed=3, color=WHITE):
    def draw(ctx, p):
        rnd = random.Random(seed)
        for i, (x, y) in enumerate(points):
            local = (t * 1.5 + rnd.uniform(0, 1)) % 1
            s = math.sin(local * math.pi) * rnd.uniform(16, 30)
            if s > 2:
                star(ctx, x + rnd.uniform(-30, 30), y + rnd.uniform(-30, 30), s, color, outline=False, points=4, inner=0.2)
    return draw


def blush(amount):
    def draw(ctx, p):
        r = rig(p)
        hx, hy = r['head']
        for side in (-1, 1):
            glow(ctx, hx + side * 70, hy + 40, 46, (1.0, 0.45, 0.6), 0.55 * amount)
    return draw


def scan_grid(t):
    """A diagnostic sweep: a cyan line runs down the suit with a faint grid behind it."""
    def draw(ctx, p):
        y = lerp(-1000, 0, ease(t))
        for k in range(-8, 9):
            ctx.move_to(k * 60, -1000)
            ctx.line_to(k * 60, 0)
        for k in range(0, 18):
            ctx.move_to(-480, -k * 60)
            ctx.line_to(480, -k * 60)
        rgb(ctx, CYAN, 0.12)
        ctx.set_line_width(2)
        ctx.stroke()
        glow(ctx, 0, y, 260, CYAN, 0.18)
        ctx.move_to(-460, y)
        ctx.line_to(460, y)
        rgb(ctx, mix(CYAN, WHITE, 0.4), 0.9)
        ctx.set_line_width(6)
        ctx.stroke()
    return draw


def question(t):
    def draw(ctx, p):
        r = rig(p)
        x, y = r['head'][0] + 230, r['head'][1] - 230 - 20 * math.sin(t * 2 * math.pi)
        ctx.select_font_face('Sans', cairo.FONT_SLANT_NORMAL, cairo.FONT_WEIGHT_BOLD)
        ctx.set_font_size(150)
        glow(ctx, x + 40, y - 50, 110, CYAN, 0.3)
        ctx.move_to(x, y)
        ctx.text_path('?')
        rgb(ctx, CYAN)
        ctx.fill_preserve()
        rgb(ctx, INK)
        ctx.set_line_width(6)
        ctx.stroke()
    return draw


# ---------------------------------------------------------------- sequences: lists of (Pose, duration_ms)

BASE = Pose()


def P(**kw):
    return replace(BASE, **kw)


def breathe(t, amount=0.010):
    return 1 + amount * math.sin(2 * math.pi * t)


def fx(seq, fn):
    """Add per-frame effects: fn(pose, t, i) for t in 0..1 over the whole sequence."""
    n = len(seq)
    return [(fn(p, i / max(1, n - 1), i), ms) for i, (p, ms) in enumerate(seq)]


def mirrored(seq):
    return [(replace(p, face=-p.face, x=-p.x, ghosts=tuple((-g[0],) + tuple(g[1:]) for g in p.ghosts)), ms)
            for p, ms in seq]


def pop(a, b, ms=(60, 60, 90)):
    """Change drawings on a squash: the old pose dips, the new one springs up past rest and settles."""
    return [(replace(a, squash=a.squash * 0.93), ms[0]), (replace(b, squash=b.squash * 1.05), ms[1]),
            (replace(b, squash=b.squash * 0.99), ms[2])]


def at(p, name):
    return rig(p)[name]


def keys(start, segments):
    """tween() with the flutter phase running on, so the tendrils keep swaying through keyframed moves."""
    out = tween(start, segments)
    n = len(out)
    return [(replace(p, flutter=p.flutter + i / max(1, n) * 1.5), ms) for i, (p, ms) in enumerate(out)]


REST = dict(head_rot=0, head_dx=0, head_dy=0, arm_l=0, arm_r=0, spear_rot=0, spear_dy=0, body_rot=0, hip_dx=0, hip_dy=0,
            feet=((0, 0), (0, 0)), foot_rot=(0, 0), spread=0, wave=0.6)
GUARD = dict(hip_dy=16, body_rot=-4, feet=((-12, 0), (8, 0)), spear_rot=-55, arm_l=10, arm_r=-14, head_rot=-3,
             eyes=1.6, gems=1.2)
CROUCH = dict(hip_dy=26, feet=((-8, 0), (8, 0)), arm_r=14, arm_l=-6, head_dy=3)


# Idle -------------------------------------------------------------------------------------------------------

def idle(kind='plain'):
    """Breathing: the knees give a little, the head and the free arm follow a beat late, the tendrils sway."""
    out = []
    n = 20
    for i in range(n):
        t = i / n
        p = P(hip_dy=1.6 + 1.6 * math.sin(2 * math.pi * t), head_rot=1.4 * math.sin(2 * math.pi * (t - 0.12)),
              head_dy=0.8 * math.sin(2 * math.pi * (t - 0.08)), arm_r=-2.5 * math.sin(2 * math.pi * (t - 0.2)),
              arm_l=1.2 * math.sin(2 * math.pi * (t - 0.15)), spear_rot=-0.8 * math.sin(2 * math.pi * (t - 0.25)),
              gems=1 + 0.12 * osc(t), eyes=1 + 0.15 * osc(t, 0.5), crystal=0.12 + 0.1 * osc(t, 1, 0.3), flutter=t,
              wave=0.7)
        if kind == 'surge':
            s = hop(min(1.0, t * 1.25))
            p = replace(p, crystal=0.15 + 1.0 * s, gems=1 + 0.6 * s, eyes=1 + 0.5 * s, spread=0.8 * s, head_rot=-5 * s,
                        head_dy=-3 * s, arm_r=-30 * s, arm_l=8 * s, hip_dy=-4 * s, lift=8 * s,
                        front=[twinkles(t, [(-200, -800), (200, -800), (-260, -650), (260, -650)],
                                        seed=4, color=mix(VIOLET_LIGHT, WHITE, 0.5))] if 0.2 < t < 0.8 else [])
        elif kind == 'shift':
            # Weight onto one leg, the spear lifted a hand and planted again with a puff of dust.
            k = [0, 0, 0.3, 0.7, 1, 1, 1, 0.8, 0.4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0][i]
            w = ease(min(1.0, i / 6)) * (1 - ease(max(0.0, (i - 12) / 7)))
            p = replace(p, hip_dx=8 * w, body_rot=-2.5 * w, head_rot=p.head_rot + 3 * w, arm_l=10 * k, spear_dy=-14 * k,
                        hip_dy=p.hip_dy + 3 * w,
                        front=[dust((i - 9) / 5, x=at(p, 'butt')[0], y=0, side=-1, amount=2)] if 9 <= i < 14 else [])
        elif kind == 'happy':
            b = max(0.0, math.sin(2 * math.pi * t * 2))
            p = replace(p, hip_dy=8 * b, lift=10 * max(0.0, -math.sin(2 * math.pi * t * 2)), arm_r=-60 - 50 * b,
                        head_rot=4 * math.sin(2 * math.pi * t * 2), gems=1.3, eyes=1.6, crystal=0.4, wave=1.2)
        elif kind == 'poor':
            flick = i in (6, 7, 15)
            p = replace(p, hip_dy=14 + 1.5 * osc(t), body_rot=3.5, head_rot=10 + osc(t), head_dy=7, arm_r=10, arm_l=-4,
                        spear_rot=4, dim=0.35, gems=0.15 if flick else 0.45, eyes=0.1 if flick else 0.5, crystal=0, wave=0.25)
        out.append((p, 130))
    if kind in ('plain', 'shift'):
        p = out[13][0]
        out[13:14] = [(replace(p, eyes=0.3), 60), (replace(p, eyes=0.0), 70), (replace(p, eyes=0.5), 60)]
    return out


def ponder():
    """A fidget: fist to the chin for a moment, a question mark, a small sway; back to standing."""
    out = [(P(head_rot=-4), 200)]
    out += pop(P(head_rot=-4), P(pose=3))
    for i in range(10):
        t = i / 10
        out.append((P(pose=3, rot=1.5 * osc(t), eyes=1.2 + 0.3 * osc(t * 2), front=[question(t)] if 3 <= i < 9 else []), 130))
    out += pop(P(pose=3), P())
    out.append((P(), 120))
    return out


def look_around():
    """The head turns one way, the body follows a little later; then the other way; back."""
    return keys(P(), [
        (3, [80, 80, 100], ease, dict(head_rot=-9, head_dx=-4, body_rot=-1.5, eyes=1.5, arm_r=3)),
        (1, 300, linear, dict(head_rot=-10)),
        (4, [70, 70, 80, 110], ease, dict(head_rot=9, head_dx=4, body_rot=1.5, arm_r=-4)),
        (1, 320, linear, dict(head_rot=10)),
        (3, [80, 90, 120], ease, dict(head_rot=0, head_dx=0, body_rot=0, eyes=1.0, arm_r=0)),
    ])


# Agent states ---------------------------------------------------------------------------------------------

def think(phase, kind='map'):
    if phase == 'start':
        out = pop(P(head_rot=-3), P(pose=3))
        for i, u in enumerate((0.3, 0.6, 0.85, 1.0)):
            out.append((P(pose=3, eyes=1 + 0.4 * u, front=[hologram(i / 4, size=back_out(u), kind=kind)]), (60, 70, 80, 100)[i]))
        return out
    if phase == 'loop':
        out = []
        n = 16
        for i in range(n):
            t = i / n
            out.append((P(pose=3, rot=1.2 * osc(t), squash=breathe(t, 0.008), eyes=1.4 + 0.2 * osc(t * 2),
                          gems=1.1 + 0.15 * osc(t), crystal=0.2 + 0.15 * osc(t, 0.5), flutter=t,
                          front=[hologram(t, kind=kind)]), 110))
        return out
    out = [(P(pose=3, eyes=1.2, front=[hologram(0.9, size=u)]), (60, 60, 70)[i]) for i, u in enumerate((1.08, 0.6, 0.2))]
    out += pop(P(pose=3), P())
    return out


READ = dict(head_rot=6, head_dx=3, arm_r=-48, body_rot=1.5, eyes=1.5)


def scan(phase, flicking=False):
    """Reading: the free hand comes up, a text sheet unfolds beside it, the eyes scan it line by line."""
    if phase == 'start':
        out = []
        for i, u in enumerate((0.15, 0.4, 0.75, 1.0, 1.06, 1.0)):
            e = ease(min(1.0, u))
            out.append((P(head_rot=6 * e, head_dx=3 * e, arm_r=-48 * e, body_rot=1.5 * e, eyes=1 + 0.5 * e, flutter=i / 6,
                          front=[data_panel(0, open_=min(1.08, u))]), (60, 60, 70, 70, 70, 100)[i]))
        return out
    if phase == 'loop':
        out = []
        n = 16
        for i in range(n):
            t = i / n
            if flicking:
                f = ease(max(0.0, min(1.0, ((t * 2) % 1 - 0.55) / 0.3)))
                props = [data_panel(t * 0.5, flick=f, beam=None if f > 0 else (t * 2 % 1) / 0.55)]
                arm = -48 - 18 * math.sin(math.pi * f)
            else:
                u = (t * 2) % 1
                props = [data_panel(t, beam=ease(u))]
                arm = -48
            out.append((P(**{**READ, 'arm_r': arm, 'head_rot': 6 + 2.5 * (ease((t * 2) % 1) - 0.5) if not flicking else 6},
                          hip_dy=1.5 + 1.5 * math.sin(2 * math.pi * t), gems=1.1, flutter=t, front=props), 110))
        return out
    out = []
    for i, u in enumerate((1.0, 0.6, 0.2, 0.0)):
        e = ease(u)
        out.append((P(head_rot=6 * e, head_dx=3 * e, arm_r=-48 * e, body_rot=1.5 * e, eyes=1 + 0.5 * e,
                      front=[data_panel(0, open_=u)] if u else []), (60, 60, 60, 90)[i]))
    out.append((P(), 100))
    return out


def drill(phase, spin=False):
    """Working: spear drills. Guard, then thrusts that slide the spear through the fist with a lunge, a flash at
    the tip and afterimages, each eased back to guard; the variant twirls the spear like a windmill."""
    guard = P(**GUARD)
    if phase == 'start':
        return keys(P(), [(2, [60, 60], ease, dict(hip_dy=22, squash=0.97)),
                          (3, [60, 70, 100], back_out, dict(GUARD, squash=1.0))])
    if phase == 'end':
        return keys(guard, [(1, 70, ease, dict(hip_dy=24)), (3, [70, 80, 110], ease_out, dict(REST, eyes=1.0, gems=1.0))])
    out = []
    if spin:
        n = 16
        prev = -55
        fist = None
        for i in range(n):
            t = i / n
            ang = -55 - 720 * t
            p = replace(guard, spear_rot=ang, arm_l=18 + 6 * math.sin(4 * math.pi * t), body_rot=-3 + 2 * math.sin(4 * math.pi * t),
                        hip_dy=16 + 3 * math.sin(4 * math.pi * t), flutter=t * 2, wave=1.1)
            fist = at(p, 'hands')[0]
            p = replace(p, back=[spin_smear(prev, ang, fist, radius=700, width=200, color=(0.75, 0.95, 1.0))])
            out.append((p, 60))
            prev = ang
        return out
    beats = [  # hip shift, spear slide, body lean, ms
        (0, 0, -4, 110), (6, 14, -2, 70), (-20, -70, -11, 45), (-18, -66, -10, 70), (-8, -30, -7, 70), (0, 0, -4, 90)]
    for rep in range(2):
        for k, (dx, slide, lean, ms) in enumerate(beats):
            p = replace(guard, hip_dx=dx, spear_dy=slide, body_rot=lean, feet=((-12 - (14 if k in (2, 3) else 0), 0), (8, 0)),
                        arm_l=10 + (6 if k in (2, 3) else 0), flutter=(rep * 6 + k) / 12, wave=0.9)
            tip = at(p, 'tip')
            p = replace(p, ghosts=((24, 0.35), (48, 0.18)) if k == 2 else (),
                        front=[burst(0.12 if k == 2 else 0.45, tip[0], tip[1], seed=rep * 7 + k, radius=180)] if k in (2, 3) else [])
            out.append((p, ms))
    return out


def scan_to_drill():
    """Reading to working without stopping: the sheet folds as he sinks into guard."""
    out = []
    for i, u in enumerate((0.7, 0.35, 0.0)):
        e = ease(1 - u)
        q = {k: lerp(READ.get(k, 0), GUARD.get(k, 0), e) for k in ('head_rot', 'arm_r', 'body_rot', 'hip_dy', 'spear_rot', 'arm_l')}
        out.append((P(**q, eyes=1.5, front=[data_panel(0, open_=u)] if u else []), 70))
    out += keys(out[-1][0], [(3, [60, 70, 100], back_out, dict(GUARD, front=[]))])
    return out


def drill_to_scan():
    out = keys(P(**GUARD), [(3, [70, 70, 90], ease, dict(REST, **READ, gems=1.1))])
    for i, u in enumerate((0.4, 0.8, 1.06, 1.0)):
        out.append((P(**READ, front=[data_panel(0, open_=u)]), (60, 60, 70, 100)[i]))
    return out


def signal(phase):
    """Needs input: up into the cheer with the spear raised and a shout, a red beacon over the crest."""
    cheer = P(pose=4, eyes=1.6, gems=1.3)
    if phase == 'start':
        out = keys(P(), [(2, [60, 70], ease, dict(CROUCH, eyes=1.3))])
        out += pop(out[-1][0], cheer)[1:]
        return fx(out + [(cheer, 80)], lambda p, t, i: replace(p, front=[beacon(t, light=ease(t))]))
    if phase == 'loop':
        out = []
        n = 10
        for i in range(n):
            t = i / n
            out.append((replace(cheer, lift=6 * max(0.0, osc(t * 2)), gems=1.2 + 0.4 * osc(t * 2), crystal=0.3 * osc(t),
                                flutter=t, front=[beacon(t)]), 90))
        return out
    out = pop(cheer, P(hip_dy=10))
    out += keys(out[-1][0], [(2, [80, 100], ease, dict(hip_dy=0))])
    return fx(out, lambda p, t, i: replace(p, front=[beacon(0.5, light=max(0.0, 1 - t * 2))]))


def short_circuit():
    """Tool error: the gems flare, arcs crawl over the suit while every joint twitches, red flashes, the light
    dies and he slumps; a beat; a reboot that straightens him up."""
    chest = at(P(), 'gem')
    out = [(P(gems=2.2, eyes=2.0, head_rot=-3, front=[burst(0.15, chest[0], chest[1], seed=1, radius=240)]), 70)]
    rnd = random.Random(8)
    for k in range(6):
        j = (-1) ** k
        out.append((P(shake=6 * j, head_rot=rnd.uniform(-9, 9), arm_r=rnd.uniform(-35, 10), arm_l=rnd.uniform(-8, 14),
                      body_rot=rnd.uniform(-3, 3), spear_rot=rnd.uniform(-8, 8), hip_dy=rnd.uniform(0, 8),
                      eye_red=1.0 if k % 2 else 0.0, gems=1.8 if k % 2 else 0.3, eyes=1.8 if k % 2 else 0.2, wave=2.0,
                      flutter=k * 0.37, front=[bolts(k / 6, seed=k + 2), burst(0.2 + k * 0.12, chest[0], chest[1], seed=k, radius=280)]),
                    55))
    SLUMP = dict(hip_dy=24, body_rot=6, head_rot=16, head_dy=10, arm_r=14, arm_l=-8, spear_rot=-10, dim=0.45, gems=0.1,
                 eyes=0.0, wave=0.1)
    slump = P(**SLUMP)
    out += keys(out[-1][0], [(2, [60, 80], ease_in, dict(SLUMP, shake=0, eye_red=0, front=[]))])
    for k in range(3):
        out.append((replace(slump, hip_dy=24 + (2, 0, 1)[k], front=[smoke_up(k / 6, x=0, y=-760, size=1.1)]), (80, 160, 260)[k]))
    for k, (e, g) in enumerate(((1.2, 0.3), (0.0, 0.1), (1.8, 1.0), (0.4, 0.6), (1.0, 1.0))):
        u = back_out((k + 1) / 5) if k < 4 else 1.0
        q = {key: lerp(getattr(slump, key), 0, u) for key in ('hip_dy', 'body_rot', 'head_rot', 'head_dy', 'arm_r', 'arm_l',
                                                               'spear_rot', 'dim')}
        out.append((P(**q, eyes=e, gems=g, front=[smoke_up(0.5 + k / 10, x=0, y=-760, size=1.1)]), (80, 70, 90, 90, 160)[k]))
    return out


def power_down():
    """Out of quota: sagging onto the spear, knees bent, the head hanging, the suit dark, a red cell blinking."""
    out = []
    n = 14
    for i in range(n):
        t = i / n
        out.append((P(hip_dy=30 + 2 * osc(t), body_rot=-4, head_rot=-13 + osc(t), head_dy=10, arm_r=12, arm_l=-6,
                      spear_rot=5, feet=((-10, 0), (10, 0)), dim=0.45, gems=0.25 + (0.5 if i == 9 else 0),
                      eyes=0.15 + (0.8 if i == 9 else 0), wave=0.15, flutter=t * 0.5, front=[low_power(i % 7 < 4)]), 140))
    return out


def victory():
    """Turn finished: a deep crouch with the arm drawn back, a jump that becomes the cheer at the top, fireworks,
    a heavy landing with a shockwave and cracks, the cheer held, then down to standing."""
    cheer = P(pose=4, eyes=1.8, gems=1.6, crystal=0.7)
    out = keys(P(), [(2, [70, 90], ease, dict(CROUCH, arm_r=24, head_rot=4, eyes=1.4, squash=0.97)),
                     (1, 60, linear, dict(squash=1.0))])
    out.append((P(lift=50, squash=1.08, arm_r=-80, arm_l=20, feet=((0, 14), (0, 14)), spread=0.6, eyes=1.6), 50))
    for k, lift in enumerate((120, 165, 180, 172, 140, 80)):
        out.append((replace(cheer, lift=lift, squash=1.03 if k < 2 else 1.0, flutter=k / 6, over=[fireworks(k / 12, seed=11)]),
                    (50, 60, 90, 90, 60, 50)[k]))
    for k in range(5):
        t = (k + 1) / 6
        out.append((replace(cheer, squash=lerp(0.9, 1.0, ease(t)), flutter=t, over=[fireworks(0.5 + t * 0.4, seed=11)],
                            back=[shockwave(t, x=0, y=0, size=1.1)], front=[cracks(t * 2, x=0, y=0)]), (50, 60, 70, 90, 110)[k]))
    for k in range(4):
        out.append((replace(cheer, gems=1.4 + 0.3 * osc(k / 4), over=[fireworks(0.95 + k * 0.02, seed=11)]), 110))
    out += pop(cheer, P(hip_dy=8, eyes=1.4))
    out += keys(out[-1][0], [(2, [90, 120], ease, dict(hip_dy=0, eyes=1.0))])
    return out


SLEEP = dict(head_rot=12, head_dy=9, hip_dy=8, body_rot=2, arm_r=8, arm_l=-3, eyes=0.0, gems=0.3, dim=0.2, wave=0.2)


def standby_mode(phase):
    """Sleeping on his feet: the light fades out of the eyes, the head nods down onto the chest, the tendrils
    settle; standby letters drift. Waking, the eyes flash, the head snaps up past level and the tendrils flare."""
    if phase == 'start':
        return keys(P(), [(2, [90, 110], ease, dict(eyes=0.4, head_rot=4, head_dy=3)),
                          (1, 120, linear, dict(eyes=0.8, head_rot=2)),     # catches itself once
                          (4, [110, 130, 150, 180], ease_in, dict(SLEEP))])
    if phase == 'loop':
        out = []
        n = 16
        for i in range(n):
            t = i / n
            out.append((P(**{**SLEEP, 'head_rot': 12 + 1.5 * osc(t, 1, 0.1), 'hip_dy': 8 + 1.5 * osc(t),
                             'gems': 0.3 + 0.1 * osc(t)}, flutter=t * 0.5, front=[standby(t)]), 150))
        return out
    out = [(P(**{**SLEEP, 'eyes': 2.0, 'gems': 1.5}), 90), (P(**{**SLEEP, 'eyes': 0.3}), 60)]
    out += keys(P(**{**SLEEP, 'eyes': 1.8, 'gems': 1.4}), [
        (2, [50, 60], back_out, dict(REST, head_rot=-6, head_dy=-3, spread=0.7, eyes=1.6, gems=1.2, dim=0, lift=12)),
        (3, [70, 90, 120], ease, dict(REST, eyes=1.0, gems=1.0, lift=0))])
    return out


def landing():
    """Starting: dropping in on violet jets with knees tucked and afterimages trailing up; braking; landing in the
    lunge with a shockwave; then standing up."""
    out = []
    for k, lift in enumerate((1150, 900, 660, 450, 280, 150, 60)):
        out.append((P(lift=lift, jet=0.7 + 0.3 * (k > 3), eyes=1.6, flicker=k / 3, shadow=lift < 700, feet=((4, -24), (-4, -18)),
                      hip_dy=6, arm_r=-30, arm_l=12, spread=0.8, wave=1.4, flutter=k / 4,
                      ghosts=((0, 0.25, 180), (0, 0.12, 360)) if k < 5 else ((0, 0.2, 90),)), 50))
    out.append((P(lift=16, jet=1.2, squash=0.95, eyes=1.6, flicker=0.6, feet=((0, -8), (0, -6)), spread=0.5), 50))
    land = P(pose=2, eyes=1.8, gems=1.6, crystal=0.6)
    for k in range(5):
        t = (k + 1) / 6
        out.append((replace(land, squash=lerp(0.9, 1.0, ease(t)), front=[shockwave(t, size=1.2)]), (60, 70, 80, 100, 120)[k]))
    out.append((replace(land, gems=1.2, eyes=1.4, crystal=0.2), 200))
    out += pop(land, P(**CROUCH))
    out += keys(out[-1][0], [(3, [80, 90, 120], ease, dict(REST))])
    return out


def depart():
    """Closing: a deep crouch, the jets light, a heavy launch that speeds up and leaves the frame upwards."""
    out = keys(P(), [(2, [80, 90], ease, dict(CROUCH, arm_r=20, eyes=1.5)), (1, 70, linear, dict(jet=0.6, flicker=0.6))])
    for k, lift in enumerate((30, 100, 220, 400, 650, 950, 1300)):
        out.append((P(lift=lift, squash=1.06, jet=1.2, flicker=k / 3, eyes=1.6, feet=((0, 10), (0, 10)), arm_r=-20, arm_l=6,
                      spread=0.6, wave=1.6, flutter=k / 4,
                      ghosts=((0, 0.25, -120 - 40 * k), (0, 0.12, -240 - 70 * k)) if k > 1 else (),
                      front=[dust(k / 4, x=-120, side=-1), dust(k / 4, x=120, side=1)] if k < 3 else []), (70, 60, 55, 50, 50, 50, 60)[k]))
    out.append((P(lift=1500, alpha=0), 100))
    return out


def rage(leave='dash'):
    """Angry: eyes and gems go red, the tendrils flare, the fist shakes, steam, a stomp; then a dash off-screen
    with afterimages, or a jet blast straight up."""
    out = keys(P(), [(3, [70, 70, 90], ease, dict(eye_red=1, eyes=1.8, gems=1.6, crystal=1.0, spread=1.0, hip_dy=12,
                                                    arm_r=-40, head_rot=-6, head_dy=4))])
    angry = out[-1][0]
    for k in range(6):
        j = (-1) ** k
        out.append((replace(angry, shake=5 * j, arm_r=-40 + 8 * j, head_rot=-6 + 2 * j, wave=2.0, flutter=k * 0.3,
                            front=[vents(0.3 + k / 8), sparks(k / 6, 0, -560, seed=2, count=8)]), 60))
    out += keys(angry, [(2, [70, 60], ease_out, dict(feet=((0, 0), (6, -36)), hip_dx=-6, body_rot=-3))])
    for k in range(4):
        t = (k + 1) / 5
        out.append((replace(angry, squash=lerp(0.92, 1.0, t), front=[shockwave(t, x=70, size=0.9), vents(0.9)]), (60, 70, 80, 120)[k]))
    if leave == 'dash':
        dash_pose = P(pose=2, eye_red=1, eyes=1.8, gems=1.6, crystal=1.0)
        for k, x in enumerate((0, 60, 220, 460, 760, 1100)):
            out.append((replace(dash_pose, x=x, rot=6, pivot=200, ghosts=((-120, 0.4), (-240, 0.25), (-360, 0.12)) if k > 1 else (),
                                front=[dust(k / 6, x=-200, side=-1)]), 50))
    else:
        for k, lift in enumerate((40, 140, 330, 620, 1000, 1400)):
            out.append((replace(angry, lift=lift, squash=1.08, jet=1.4, flicker=k / 3, feet=((0, 10), (0, 10)),
                                ghosts=((0, 0.35, -140), (0, 0.18, -280)) if k > 0 else ()), 50))
    out.append((replace(out[-1][0], alpha=0), 100))
    return out


def held(phase):
    """Dragging: lifted by the head and swinging like a pendulum; the legs dangle and kick out of step, the arms
    hang, the jets sputter as if to fly off."""
    top = 640
    hang = dict(feet=((4, 16), (-4, 16)), arm_r=10, arm_l=-6, shadow=False)
    if phase == 'start':
        return keys(P(), [(4, [50, 50, 60, 70], ease_out, dict(hang, lift=48, squash=1.05, eyes=1.6, rot=6, pivot=top, spread=0.5))])
    if phase == 'loop':
        out = []
        n = 12
        for i in range(n):
            t = i / n
            sw = math.sin(2 * math.pi * t)
            out.append((P(**{**hang, 'feet': ((4 + 10 * math.sin(2 * math.pi * (t * 2)), 16 - 8 * max(0.0, math.sin(2 * math.pi * t * 2))),
                                              (-4 - 10 * math.sin(2 * math.pi * (t * 2 + 0.5)), 16 - 8 * max(0.0, -math.sin(2 * math.pi * t * 2)))),
                             'arm_r': 10 + 8 * sw, 'arm_l': -6 - 5 * sw},
                          lift=48, squash=1.05, rot=8 * sw, pivot=top, head_rot=-4 * math.sin(2 * math.pi * (t - 0.1)), eyes=1.6 if i % 6 else 0.3, wave=1.6, flutter=t * 2,
                          jet=0.35 if i % 4 == 0 else 0, flicker=t * 3), 80))
        return out
    return [(P(lift=20, eyes=1.6, squash=1.04, feet=((0, 8), (0, 8))), 60),
            (P(**CROUCH, squash=0.92, eyes=1.8, front=[shockwave(0.3, size=0.6)]), 80),
            (P(hip_dy=10, squash=1.02, front=[shockwave(0.6, size=0.6)]), 70),
            (P(hip_dy=3), 90), (P(), 100)]


def pat(phase):
    """Patted on the crest: the head tips into the hand, the knees dip, the eyes brighten, a little heart."""
    if phase == 'start':
        return keys(P(), [(3, 60, ease, dict(hip_dy=8, head_rot=-7, head_dy=5, eyes=1.7, gems=1.3, arm_r=-10))])
    if phase == 'loop':
        out = []
        n = 10
        for i in range(n):
            t = i / n
            out.append((P(hip_dy=8 + 3 * osc(t, 0.5), head_rot=-7 + 4 * math.sin(2 * math.pi * t), head_dy=5, eyes=1.7,
                          gems=1.3, arm_r=-10 - 6 * osc(t), crystal=0.3 + 0.2 * osc(t), wave=1.0, flutter=t,
                          front=[holo_heart(t, size=0.5, at_hand=0), blush(0.8)]), 90))
        return out
    return keys(P(hip_dy=8, head_rot=-7, head_dy=5, eyes=1.7), [(1, 70, back_out, dict(hip_dy=-3, head_rot=2, head_dy=-2)),
                                                                 (2, [70, 90], ease, dict(REST, eyes=1.0))])


def poke(phase):
    """Poked in the chest: the gem flashes, the body jerks back, the free arm flails; ticklish twitches."""
    chest = at(P(), 'gem')
    if phase == 'start':
        return [(P(gems=2.2, eyes=1.8, body_rot=4, head_rot=6, arm_r=-40, hip_dx=4,
                   front=[burst(0.2, chest[0], chest[1], radius=140, count=8)]), 60),
                (P(gems=0.6, body_rot=-2, arm_r=-10, head_rot=-3), 60), (P(gems=1.6, eyes=1.6, body_rot=1, arm_r=-25), 70)]
    if phase == 'loop':
        out = []
        n = 10
        for i in range(n):
            t = i / n
            out.append((P(body_rot=3 * math.sin(2 * math.pi * t * 2), head_rot=-5 * math.sin(2 * math.pi * (t * 2 - 0.1)),
                          arm_r=-25 - 20 * math.sin(2 * math.pi * t * 4), hip_dy=3 + 3 * osc(t, 0.25), gems=1.0 + 0.8 * (i % 2),
                          eyes=1.7, wave=1.5, flutter=t * 2,
                          front=[sparks(t, chest[0], chest[1], seed=9, count=5, speed=0.6), blush(0.6)]), 75))
        return out
    return keys(P(arm_r=-25, eyes=1.5), [(3, [70, 70, 90], ease, dict(REST, eyes=1.0))])


def tumble(phase):
    """Thrown (faces right): a spin about the middle with limbs flung out and afterimages; the jets catch him in
    the lunge; a heavy landing."""
    mid = 300
    if phase == 'start':
        return [(P(rot=ang, pivot=mid, lift=60, eyes=1.8, shadow=False, arm_r=-90, arm_l=30, spear_rot=-30, feet=((-20, -30), (20, -40)),
                   spread=1.0, wave=2.0, flutter=k / 3, ghosts=((-60, 0.3), (-120, 0.15)) if k < 4 else ()), 55)
                for k, ang in enumerate((60, 140, 220, 300, 360))]
    if phase == 'loop':
        out = []
        n = 10
        for i in range(n):
            t = i / n
            out.append((P(pose=2, lift=70 + 8 * osc(t, 0.5), rot=-6 + 4 * osc(t), pivot=mid, jet=0.9, flicker=t * 3,
                          eyes=1.6, shadow=False, back=[speed_lines(0.7)]), 70))
        return out
    out = [(P(pose=2, lift=24, jet=0.4, eyes=1.6), 60),
           (P(pose=2, squash=0.9, eyes=1.8, gems=1.4, front=[shockwave(0.3, size=0.7)]), 90),
           (P(pose=2, squash=0.97, eyes=1.6, front=[shockwave(0.6, size=0.7)]), 160)]
    out += pop(P(pose=2), P(**CROUCH))
    return out + keys(out[-1][0], [(2, [90, 110], ease, dict(REST))])


def peek(phase):
    """Hiding at the screen edge (faces right): leans out from the edge, fist at the chin, and ducks back."""
    def pose(out_amt, **kw):
        return P(pose=3, rot=10 * out_amt, x=-70 - 70 * (1 - out_amt), eyes=1 + 0.6 * out_amt, **kw)
    if phase == 'start':
        return [(pose(t), ms) for t, ms in ((0.4, 60), (0.8, 70), (1.0, 100))]
    if phase == 'loop':
        plan = [(1.0, 220), (1.0, 160), (1.0, 260), (0.3, 90), (0.0, 220), (0.6, 90), (1.0, 300), (1.0, 200)]
        return [(pose(a, flutter=i / 8, front=[question(i / 8)] if i in (1, 2) else []), ms) for i, (a, ms) in enumerate(plan)]
    return [(pose(1 - t), ms) for t, ms in ((0.4, 70), (0.8, 70), (1.0, 100))]


# Reactions and fidgets -------------------------------------------------------------------------------------

def kata():
    """A spear form: guard; a wide sweep over the head with a swoosh; a lunging thrust with a strike flash, held;
    a full twirl in the fist; the butt planted with a ground ring."""
    out = keys(P(), [(3, [70, 70, 90], ease, dict(GUARD)), (1, 140, linear, dict(hip_dy=18))])
    prev = GUARD['spear_rot']
    g = out[-1][0]
    for k, u in enumerate((0.15, 0.4, 0.7, 0.9, 1.0)):
        ang = lerp(-55, -235, ease(u))
        p = replace(g, spear_rot=ang, arm_l=lerp(10, 30, math.sin(math.pi * u)), body_rot=lerp(-4, 4, u), head_rot=lerp(-3, 4, u),
                    flutter=u, wave=1.2)
        p = replace(p, back=[spin_smear(prev, ang, at(p, 'hands')[0], radius=700, width=200, color=(0.75, 0.95, 1.0))])
        out.append((p, 45))
        prev = ang
    thrust = replace(g, spear_rot=-90, spear_dy=-80, hip_dx=-24, body_rot=-12, hip_dy=22, feet=((-34, 0), (10, 0)), arm_l=16,
                     head_rot=-5, back=[])
    out += keys(replace(out[-1][0], back=[]), [(2, [45, 50], ease_out, {k: getattr(thrust, k) for k in
                                                                       ('spear_rot', 'spear_dy', 'hip_dx', 'body_rot', 'hip_dy', 'feet', 'arm_l', 'head_rot')})])
    tip = at(thrust, 'tip')
    out.append((replace(thrust, ghosts=((30, 0.35), (60, 0.18)), front=[burst(0.15, tip[0], tip[1], seed=3, radius=220, count=12)]), 60))
    out.append((replace(thrust, front=[burst(0.5, tip[0], tip[1], seed=3, radius=220, count=12)]), 300))
    base = replace(thrust, spear_dy=0, hip_dx=0, body_rot=-2, feet=GUARD['feet'], hip_dy=14, arm_l=20)
    prev = -90
    for i in range(8):
        u = ease((i + 1) / 8)
        ang = lerp(-90, -90 - 270, u)
        p = replace(base, spear_rot=ang, flutter=i / 4, wave=1.2)
        p = replace(p, back=[spin_smear(prev, ang, at(p, 'hands')[0], radius=700, width=200, color=(0.75, 0.95, 1.0))])
        out.append((p, 50))
        prev = ang
    out += keys(replace(out[-1][0], back=[], spear_rot=0), [(2, [70, 80], ease_in, dict(REST, hip_dy=20, eyes=1.6))])
    butt = at(P(), 'butt')
    for k in range(4):
        t = (k + 1) / 5
        out.append((P(hip_dy=lerp(20, 0, ease(t)), eyes=1.5, front=[shockwave(t, x=butt[0], size=0.6, dust=False)]), 70))
    out.append((P(), 110))
    return out


def shine():
    """Checking the spear: the fist raises it and tips it towards the visor; a glint runs up the shaft to the tip
    and flares; a satisfied nod as it is planted again."""
    look = dict(arm_l=22, spear_rot=16, head_rot=-8, head_dx=-3, body_rot=-1.5, eyes=1.5)
    out = keys(P(), [(3, [70, 80, 100], ease, look)])
    p = out[-1][0]
    for k in range(1, 9):
        out.append((replace(p, flutter=k / 8, front=[glint(k / 8, at(p, 'butt'), at(p, 'tip'))]), 70))
    tip = at(p, 'tip')
    out += [(replace(p, gems=1.6, front=[burst(0.2 + 0.2 * k, tip[0], tip[1], seed=6, radius=150, count=8,
                                              color=(0.8, 0.98, 1.0))]), 80) for k in range(4)]
    out += keys(p, [(3, [80, 90, 110], ease, dict(REST, head_rot=5, head_dy=3, eyes=1.6)),
                    (2, [90, 120], ease, dict(head_rot=0, head_dy=0, eyes=1.0))])
    return out


def crystal_surge():
    """The crystals blaze: arms open, chest up, the tendrils fan out, twinkles round the shoulders; then a shake
    back to rest."""
    out = []
    n = 18
    for i in range(n):
        t = i / (n - 1)
        s = ease(min(1.0, t * 3)) * (1 - ease(max(0.0, (t - 0.7) / 0.3)))
        out.append((P(crystal=1.3 * s, gems=1 + 0.7 * s, eyes=1 + 0.8 * s, lift=12 * s, spread=1.0 * s, arm_r=-45 * s,
                      arm_l=10 * s, head_rot=-6 * s, head_dy=-3 * s, hip_dy=-2 * s, wave=0.6 + s, flutter=t * 1.5,
                      front=[twinkles(t, [(-230, -820), (230, -820), (-300, -660), (300, -660), (0, -980)], seed=7,
                                      color=mix(VIOLET_LIGHT, WHITE, 0.5))] if 0.15 < t < 0.8 else []), 100))
    out += [(P(shake=4 * (-1) ** k, head_rot=2 * (-1) ** k), 60) for k in range(3)] + [(P(), 100)]
    return out


def vent():
    """A sigh: shoulders up, a held breath, steam blows from the shoulder vents and the whole suit sags."""
    out = keys(P(), [
        (3, [90, 90, 110], ease, dict(hip_dy=-4, head_dy=-4, head_rot=-4, arm_r=-6, eyes=0.7, lift=4)),
        (1, 260, linear, dict(head_rot=-5)),
        (4, [70, 80, 90, 110], ease_out, dict(hip_dy=10, head_dy=6, head_rot=6, arm_r=8, arm_l=-4, eyes=0.4, lift=0, wave=0.2)),
        (2, [160, 120], ease, dict(REST, eyes=1.0)),
    ])
    return fx(out, lambda p, t, i: replace(p, front=[vents((i - 4) / 6, 1.0 - max(0, i - 8) * 0.4)] if i >= 4 else []))


def startled():
    """Danger: a jump back with the arms flung, tendrils snapping up, then down into a guard; it holds, relaxes."""
    out = [(P(eyes=2.0, lift=40, x=-30, arm_r=-70, arm_l=24, spear_rot=-20, feet=((10, -24), (-10, -18)), spread=1.0,
              head_rot=-6, wave=2.0, front=[beacon(0.1, 0.8)]), 60),
           (P(eyes=2.0, lift=60, x=-50, arm_r=-60, arm_l=20, spear_rot=-24, feet=((10, -20), (-10, -14)), spread=1.0,
              wave=2.0, flutter=0.3, front=[beacon(0.3, 1.0)]), 70)]
    out += keys(out[-1][0], [(2, [50, 60], ease_in, dict(lift=0, feet=((0, 0), (0, 0)), arm_r=-20, front=[])),
                             (1, 80, linear, dict(GUARD, x=-50, squash=0.94, spread=0.6)),
                             (1, 80, linear, dict(squash=1.02)),
                             (1, 500, linear, dict(squash=1.0, spread=0.3)),
                             (4, [100, 100, 110, 130], ease, dict(REST, x=0, eyes=1.0, gems=1.0))])
    return out


def love():
    out = []
    n = 16
    for i in range(n):
        t = i / (n - 1)
        size = back_out(min(1.0, t * 2.5)) * (1 - ease(max(0.0, (t - 0.8) / 0.2)))
        out.append((P(pose=3, eyes=1.6, gems=1.3, crystal=0.3, rot=2 * osc(t), front=[holo_heart(t, size=size, at_hand=0),
                                                                                      blush(0.7 * size)]), 100))
    return pop(P(), P(pose=3))[:2] + out + pop(P(pose=3), P())[1:]


def birthday():
    out = keys(P(), [(2, [70, 80], ease, dict(CROUCH, eyes=1.4))])
    out += pop(out[-1][0], P(pose=4, eyes=1.7, gems=1.4))[1:]
    n = 16
    for i in range(n):
        t = i / (n - 1)
        out.append((P(pose=4, eyes=1.7, gems=1.3 + 0.3 * osc(t * 2), crystal=0.5, lift=10 * max(0.0, osc(t, 0.5)),
                      front=[confetti_burst(min(1.0, t * 1.1))], over=[fireworks(t, seed=21, bursts=5)]), 110))
    out += pop(P(pose=4), P(hip_dy=8))
    out += keys(out[-1][0], [(2, [90, 110], ease, dict(hip_dy=0))])
    return out


def refuel():
    """Snack: the head turns to watch a violet energy cell fly in and lock into the chest gem; the gems and
    crystals surge; a happy hop."""
    out = []
    chest = at(P(), 'gem')
    start = (-420, -260)
    for i in range(7):
        t = (i + 1) / 7
        u = ease(t)
        pos = (lerp(start[0], chest[0], u), lerp(start[1], chest[1], u) - 140 * math.sin(math.pi * u))
        out.append((P(eyes=1.2 + 0.4 * u, head_rot=lerp(-8, 8, u), head_dy=4 * u, arm_r=-12 * u, flutter=t,
                      front=[energy_cell(pos[0], pos[1], -40 + 40 * u, size=min(1.0, t * 2) * (1 - 0.8 * (t > 0.9)))]), 80))
    for i in range(8):
        t = i / 7
        hopping = math.sin(t * math.pi)
        out.append((P(gems=2.0 - 0.9 * t, crystal=1.0 - 0.8 * t, eyes=1.8 - 0.6 * t, lift=16 * hopping,
                      feet=((0, -10 * hopping), (0, -10 * hopping)), arm_r=-60 * hopping, head_rot=8 * (1 - t), head_dy=4 * (1 - t),
                      spread=0.6 * hopping, wave=1.0, flutter=t * 2,
                      front=[burst(t, chest[0], chest[1], seed=4, radius=260, count=12, color=VIOLET_LIGHT)]), 90))
    out.append((P(), 100))
    return out


def coolant():
    """Water: coolant mist blasts from the shoulder vents; the head tips back, eyes dim with relief."""
    out = []
    n = 14
    for i in range(n):
        t = i / (n - 1)
        h = hop(t)
        out.append((P(eyes=lerp(1.0, 0.4, h), head_rot=-6 * h, head_dy=-3 * h, arm_r=-16 * h, arm_l=4 * h, hip_dy=-2 * h,
                      lift=4 * h, wave=0.4, flutter=t,
                      front=[vents(t * 2 % 1, 1.0 if i < 11 else 0.5), vents((t * 2 + 0.5) % 1, 0.8)]), 110))
    return out


def shy():
    """Reminder done: fist to the face, a step back, a blush on the mask; peeks; back."""
    out = pop(P(head_rot=-4), P(pose=3, eyes=0.8))
    plan = [(-2, 0.4, 120), (-4, 0.8, 140), (-5, 1.0, 160), (-5, 1.0, 200), (-3, 0.8, 140), (-5, 1.0, 220)]
    out += [(P(pose=3, rot=r, x=-10 * b, eyes=0.7 if i != 4 else 1.6, front=[blush(b)]), ms) for i, (r, b, ms) in enumerate(plan)]
    out += pop(P(pose=3, eyes=0.8), P())
    return out


def calibrate():
    """Eye break: a joint check under a diagnostic sweep: the head left and right, the free arm up and down, the
    spear arm, the knees; each joint ticked off with a flash of the gems."""
    tests = [dict(head_rot=-10), dict(head_rot=10), dict(arm_r=-110), dict(arm_r=20), dict(arm_l=26, spear_rot=-10),
             dict(hip_dy=26, feet=((-10, 0), (10, 0))), dict()]
    out = []
    cur = P(eyes=1.5)
    for k, test in enumerate(tests):
        target = dict(REST, eyes=1.5, **test)
        seg = keys(cur, [(3, [80, 80, 100], ease, target)])
        seg = [(replace(p, front=[scan_grid((k * 3 + j + 1) / (len(tests) * 3))]), ms) for j, (p, ms) in enumerate(seg)]
        out += seg
        cur = seg[-1][0]
        out.append((replace(cur, gems=1.7, front=[]), 140))
    out += keys(replace(cur, front=[]), [(2, [90, 110], ease, dict(REST, eyes=1.0))])
    return out


def war_drum():
    """Friday evening: the spear beats the ground slow, slow, quick-quick; the knees bounce, the head nods and
    the free fist pumps on every hit; a ring each time."""
    beats = (0, 4, 8, 10, 12)
    out = []
    n = 16
    butt = at(P(), 'butt')
    for i in range(n):
        t = i / n
        hit = i in beats
        up = (i + 1) % n in beats
        out.append((P(arm_l=14 if up else 0, spear_dy=-16 if up else 0, hip_dy=10 if hit else (2 if up else 4),
                      head_rot=5 if hit else -2, head_dy=3 if hit else 0, arm_r=-70 if hit else -30,
                      gems=1.6 if hit else 1.0, eyes=1.6 if hit else 1.1, crystal=0.5 if hit else 0.1, wave=1.0, flutter=t,
                      front=[shockwave(0.25, x=butt[0], size=0.4, dust=False)] if hit else []), 100))
    return out


# Moves ------------------------------------------------------------------------------------------------------

FLY = dict(feet=((-16, -22), (-30, -40)), hip_dy=8, arm_r=-22, arm_l=-14, spear_rot=68, body_rot=0, head_rot=-4, eyes=1.6,
           gems=1.2, spread=0.4, wave=1.6)


def fly(phase, angle=0.0):
    """Faces right. Mount: crouch, jets light, rise leaning into the flight with the spear levelled forward and
    the legs trailing; loop tilted by `angle` (+ climbs); land with a shockwave."""
    mid = 300
    if phase == 'start':
        out = keys(P(), [(2, [70, 70], ease, dict(CROUCH, eyes=1.4, jet=0.3, flicker=0.3))])
        out += keys(out[-1][0], [(3, [60, 60, 80], ease_out, dict(REST, **FLY, lift=70, rot=12, pivot=mid, jet=1.0, flicker=1.2))])
        return out
    if phase == 'loop':
        out = []
        n = 8
        for i in range(n):
            t = i / n
            out.append((P(**FLY, lift=70 + 10 * osc(t), rot=12 - angle * 0.6, pivot=mid, jet=1.0 + 0.3 * (angle > 0),
                          flicker=t * 2, flutter=t * 2, ghosts=((-90, 0.22),), back=[speed_lines(0.7 + 0.3 * osc(t))]), 70))
        return out
    out = keys(P(**FLY, lift=70, rot=12, pivot=mid, jet=1.0), [
        (2, [60, 60], ease, dict(rot=-4, lift=30, jet=1.2, feet=((0, -6), (0, -6)))),
        (1, 60, linear, dict(REST, lift=0, rot=0, jet=0, hip_dy=26, squash=0.94, feet=((-8, 0), (8, 0)), eyes=1.6))])
    out = fx(out, lambda p, t, i: replace(p, front=[shockwave(0.3, size=0.6)] if i == len(out) - 1 else []))
    out += keys(out[-1][0], [(3, [80, 90, 110], ease, dict(REST, squash=1.0, eyes=1.0, front=[]))])
    return out


DASH = dict(hip_dy=22, body_rot=10, feet=((-34, 0), (22, 0)), spear_rot=72, arm_l=-10, arm_r=24, head_rot=-6, eyes=1.7,
            gems=1.3, spread=0.5, wave=1.8)


def dash(phase):
    """Faces right: a low skim with the spear forward, violet afterimages and dust; a skidding stop."""
    if phase == 'start':
        return keys(P(), [(1, 70, ease, dict(CROUCH, eyes=1.4)), (2, [60, 60], ease_out, dict(DASH))])
    if phase == 'loop':
        out = []
        n = 6
        for i in range(n):
            t = i / n
            out.append((P(**DASH, lift=4 + 3 * osc(t, 0.5), flutter=t * 2, ghosts=((-110, 0.4), (-220, 0.22), (-330, 0.1)),
                          front=[dust(t, x=-160, side=-1)], back=[speed_lines(0.9, y0=-700, y1=-120)]), 60))
        return out
    out = keys(P(**DASH), [(2, [70, 80], ease_out, dict(body_rot=-8, hip_dx=-6, feet=((-20, 0), (30, 0)))),
                           (3, [80, 90, 110], ease, dict(REST, eyes=1.0, gems=1.0))])
    return fx(out, lambda p, t, i: replace(p, front=[dust(0.1 + t * 0.8, x=80, side=1)] if i < 4 else []))


SEQUENCES = {
    'idle': lambda: idle(),
    'idle_surge': lambda: idle('surge'),
    'idle_shift': lambda: idle('shift'),
    'idle_ponder': ponder,
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
    'scan/to_drill': scan_to_drill,
    'drill/start': lambda: drill('start'),
    'drill/loop': lambda: drill('loop'),
    'drill/spin': lambda: drill('loop', spin=True),
    'drill/end': lambda: drill('end'),
    'drill/to_scan': drill_to_scan,
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
    'shine': shine,
    'crystal_surge': crystal_surge,
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
