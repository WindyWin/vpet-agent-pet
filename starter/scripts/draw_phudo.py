#!/usr/bin/env python3
"""Draw Phù Đồ (Chiến Giáp Phá Đá Đế, mecha code Buddha), frame by frame, from WindyWin's own design sheet.

    pip install pycairo pillow numpy scipy
    python3 scripts/draw_phudo.py OUT             # every sequence, as OUT/<sequence>/_NNN_<ms>.png
    python3 scripts/draw_phudo.py OUT idle        # only these sequences

The art is the designer's game-asset sheet, scripts/phudo_art/design-sheet.png: four combat forms (Phong Lôi the
balanced vanguard, Hỏa Dực the aerial assault, Tứ Thủ the four-armed berserker, Hắc Tháp the heavy fortress) and
the weapon arsenal. phudo_art/extract.py cuts them out (labels inpainted, upscaled 4x with Real-ESRGAN's anime
model, matted with isnet-anime, regraded off the blueprint's haze and sharpened); phudo_art/make_plates.py splits
each form into its armour plates along the ink lines. This script never repaints the suit.

It brings the drawings to life: breathing and a lean warp the upper body, the red plumes and tendrils sway in a
travelling wave, and the drawing's own cyan, violet and red lights are relit (dimmed, blooming, overheated) and the
visor flares. The figure moves with offsets, rotation, squash, screen shake and afterimages.

Transformations are piece by piece, in the designer's four phases:
1. Disengage and vent: the locks clack open, plasma vents, and every plate opens out from the body with energy
   light in the seams.
2. Articulation: each plate flies to its place in the new form along an arc round an energy core, spinning and
   flipping over on the way (the old plate turns edge-on, the new one turns face-on); legs first, the head last,
   a spark as each lands. Plates the new form lacks fold into the core; extra ones grow out of it.
3. Armour snap: the new form's plates close onto the body and lock.
4. Ignition: a shock ring, flame on the plume, a flash and a screen shake.

Effects are drawn in the sheet's palette. Everything is deterministic, so the sheet, the cut-outs, the plate maps
and this script are the art's source.
"""
import math
import random
from dataclasses import dataclass, field, replace
from pathlib import Path

import cairo
import numpy as np
from PIL import Image
from scipy import ndimage

from pet_art import (CX, GROUND, INK, SIZE, WHITE, back_out, ease, ease_in, ease_out, ellipse, hop, lerp,
                     mix, osc, puff, rgb, run, star)

BLACK = (0.10, 0.11, 0.18)
BLACK_LIGHT = (0.22, 0.24, 0.36)
SILVER = (0.66, 0.68, 0.76)
SILVER_LIGHT = (0.86, 0.88, 0.94)
GOLD = (0.80, 0.66, 0.36)
GOLD_LIGHT = (0.97, 0.88, 0.60)
NAVY = (0.16, 0.18, 0.40)
VIOLET = (0.50, 0.30, 0.86)
VIOLET_LIGHT = (0.74, 0.56, 1.0)
RED = (0.62, 0.08, 0.14)
RED_LIGHT = (0.95, 0.26, 0.24)
FLAME = (1.0, 0.52, 0.16)
CYAN = (0.30, 0.90, 1.0)
EYE = (1.0, 0.25, 0.30)
LINE = 9

ART = Path(__file__).resolve().parent / 'phudo_art'
BODY_H = 640                # canvas pixels from crest to feet, the same for every form
OLD = 0.62                  # effect units to canvas pixels (the effects are laid out in these units)
PAD = 48
SRC = 4 / 3                 # the cut-outs' pixels per landmark unit (landmarks were measured at 3x the sheet)

# Each drawing: file, anchor (the point between the feet), crest-to-feet height, and landmarks, all in landmark
# units: eyes (x, y, half height of the visor box), head, chest, shoulders, hands, vents (where plasma and smoke
# come out), rigid boxes (weapons the plume sway must not bend), and the waist line the breathing bends about.
FORMS = {
    'phong': dict(file='form1.png', anchor=(375, 705), height=590, eyes=(375, 188, 14), head=(375, 190), chest=(375, 300),
                  shoulders=((250, 205), (500, 205)), hands=((105, 345), (605, 300)), waist=420,
                  vents=((250, 205), (500, 205), (330, 305), (420, 305), (300, 520), (450, 520)),
                  rigid=((0, 400, 270, 712), (430, 0, 560, 140)), tip=(25, 665), butt=(530, 12), plume=(375, 120)),
    'hoa': dict(file='form2.png', anchor=(403, 535), height=640, eyes=(300, 172, 16), head=(300, 175), chest=(400, 250),
                shoulders=((330, 210), (450, 220)), hands=((60, 380), (300, 330)), waist=300,
                vents=((420, 200), (520, 300), (480, 420)), rigid=((0, 260, 460, 450),), tip=(8, 395),
                jets=((505, 268), (540, 318), (505, 455)), plume=(300, 90), flying=True),
    'tu': dict(file='form3.png', anchor=(437, 740), height=575, eyes=(437, 292, 16), head=(437, 292), chest=(437, 360),
               shoulders=((330, 250), (545, 250)), hands=((128, 410), (735, 410)), claws=((150, 130), (690, 170)),
               waist=460, vents=((330, 250), (545, 250), (385, 430), (490, 430)), rigid=((0, 440, 330, 744),),
               tip=(15, 700), plume=(437, 160)),
    'thap': dict(file='form4.png', anchor=(240, 632), height=575, eyes=(240, 142, 14), head=(240, 142), chest=(240, 250),
                 shoulders=((130, 130), (350, 130)), hands=((75, 330), (410, 330)), waist=380,
                 vents=((130, 125), (350, 125), (200, 420), (290, 420)), rigid=((0, 0, 548, 632),),
                 cannons=((85, 28), (395, 38)), shield=(470, 260), spike=(240, 612), plume=(240, 60)),
}


@dataclass
class Pose:
    form: str = 'phong'
    x: float = 0
    lift: float = 0
    rot: float = 0              # degrees, about `pivot`
    pivot: float = 280          # canvas pixels above the feet that the rotation turns about
    squash: float = 1
    scale: float = 1
    face: int = 1
    alpha: float = 1
    shake: float = 0            # sideways jitter (screen shake), canvas pixels
    shake_y: float = 0
    breathe: float = 0          # upper body raised, in the drawing's pixels
    lean: float = 0             # upper body leaning (+ to the right), in the drawing's pixels at the crest
    sway: float = 0.6           # plume and tendril sway
    flutter: float = 0          # phase for sway and effects
    eyes: float = 1             # visor light: 0 dark, 1 as drawn, >1 flaring
    energy: float = 1           # the cyan and violet lights
    heat: float = 0             # overheating: red-orange glow over the energy
    dim: float = 0              # the whole suit darkened (power loss)
    aura: float = 0             # an energy outline round the figure
    aura_color: tuple = CYAN
    gap: float = 0              # the armour plates opened out from the body (unlock / lock), 0..1
    seam: float = 0             # energy light showing between the plates
    morph_to: str = None        # transformation: the form the plates are flying to
    morph: float = 0            # how far the plate-by-plate transformation has gone, 0..1
    ghosts: tuple = ()          # afterimages: (dx, alpha[, dlift])
    shadow: bool = True
    drag: float = 0             # set by pet_art.follow_through; unused
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


def metal(ctx, pts, base, light, width=LINE, highlight=False):
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


def lerp_pt(a, b, t):
    return (lerp(a[0], b[0], t), lerp(a[1], b[1], t))


# ---------------------------------------------------------------- the drawings, alive

def to_surface(rgb_, a):
    """A cairo surface (premultiplied BGRA) from float RGB and alpha arrays."""
    H, W = a.shape
    pm = np.dstack([rgb_[..., 2] * a, rgb_[..., 1] * a, rgb_[..., 0] * a, a])
    surface = cairo.ImageSurface(cairo.FORMAT_ARGB32, W, H)
    view = np.ndarray((H, surface.get_stride() // 4, 4), dtype=np.uint8, buffer=surface.get_data())
    view[:, :W] = (np.clip(pm, 0, 1) * 255 + 0.5).astype(np.uint8)
    surface.mark_dirty()
    return surface


class Art:
    """One drawing, padded, with the masks used to bring it to life: the visor, the energy lights (bright cyan,
    violet and magenta), and the crimson plumes and tendrils that sway (weapons in `rigid` boxes excluded)."""
    def __init__(self, spec):
        im = np.asarray(Image.open(ART / spec['file']).convert('RGBA'), dtype=np.float32) / 255
        im = np.pad(im, ((PAD, PAD), (PAD, PAD), (0, 0)))
        self.spec = spec
        self.pm = np.dstack([im[..., :3] * im[..., 3:4], im[..., 3]])     # premultiplied, for clean resampling
        rgb_, a = im[..., :3], im[..., 3]
        r, g, b = rgb_[..., 0], rgb_[..., 1], rgb_[..., 2]
        mx, mn = rgb_.max(-1), rgb_.min(-1)
        sat = (mx - mn) / np.maximum(mx, 1e-3)
        H, W = a.shape
        self.yy, self.xx = np.mgrid[0:H, 0:W].astype(np.float32)
        gx, gy = (self.xx - PAD) / SRC, (self.yy - PAD) / SRC          # landmark units
        bright = np.clip((mx - 0.45) / 0.3, 0, 1) * np.clip((sat - 0.35) / 0.3, 0, 1) * a
        cyan = bright * (b > r + 0.1) * (g > r)
        violet = bright * (b > g + 0.1) * (r > g)
        self.energy = ndimage.gaussian_filter(np.clip(cyan + violet, 0, 1), 0.9)
        ex, ey, half = spec['eyes']
        box = np.clip(1 - np.maximum(np.abs(gx - ex) / 46, np.abs(gy - ey) / half), 0, 1)
        self.eye = np.clip(box * 3, 0, 1) * a * np.clip((mx - 0.3) / 0.3, 0, 1)
        crimson = (r > 0.22) & (g < r * 0.55) & (b < r * 0.75)
        rigid = np.zeros_like(crimson)
        for x0, y0, x1, y1 in spec['rigid']:
            rigid[int(y0 * SRC) + PAD:int(y1 * SRC) + PAD, int(x0 * SRC) + PAD:int(x1 * SRC) + PAD] = True
        plume = ndimage.binary_dilation(crimson & ~rigid, iterations=3) & (a > 0.05)
        self.plume = ndimage.gaussian_filter(plume.astype(np.float32), 2)
        cx, cy = spec['chest']
        d = np.hypot(gx - cx, gy - cy)
        self.plume_u = np.clip(d / 300, 0, 1.6) * self.plume
        self.ux, self.uy = gx, gy
        top = spec['anchor'][1] - spec['height']
        self.upper = np.clip((spec['waist'] - gy) / max(1, spec['waist'] - top), 0, 1.3)
        self.offset = (spec['anchor'][0] * SRC + PAD, spec['anchor'][1] * SRC + PAD)
        self.k = BODY_H / (spec['height'] * SRC)
        self._sil = None
        self._halo = None
        self._plates = None

    def pixels(self, p, still=False):
        """Warped and relit premultiplied RGBA for this pose (`still`: lit but not warped)."""
        pm = self.pm
        dx = np.zeros_like(self.xx)
        dy = np.zeros_like(self.yy)
        if not still:
            if p.breathe:
                dy -= p.breathe * SRC * self.upper
            if p.lean:
                dx += p.lean * SRC * self.upper ** 1.6
            if p.sway:
                phase = p.flutter * 2 * math.pi
                dx += self.plume_u * p.sway * 14 * SRC * np.sin(phase + self.uy * 0.018 + self.ux * 0.006)
                dy += self.plume_u * p.sway * 6 * SRC * np.cos(phase * 1.3 + self.ux * 0.015)
        warped = bool(dx.any() or dy.any())
        if warped:
            coords = [self.yy - dy, self.xx - dx]
            pm = np.stack([ndimage.map_coordinates(pm[..., c], coords, order=1, mode='constant') for c in range(4)], -1)
        a = pm[..., 3]
        rgb_ = pm[..., :3] / np.maximum(a[..., None], 1e-4)
        energy, eye = self.energy, self.eye
        if warped:
            energy = ndimage.map_coordinates(energy, coords, order=1)
            eye = ndimage.map_coordinates(eye, coords, order=1)
        if p.energy < 1:
            rgb_ = rgb_ * (1 - energy[..., None] * (1 - p.energy) * 0.8)
        if p.eyes < 1:
            rgb_ = rgb_ * (1 - eye[..., None] * (1 - p.eyes) * 0.85)
        if p.heat > 0:
            hot = np.array(FLAME)
            rgb_ = rgb_ + (hot * rgb_.max(-1, keepdims=True) * 1.4 - rgb_) * energy[..., None] * min(1.0, p.heat)
        if p.dim > 0:
            rgb_ = rgb_ * (1 - 0.5 * p.dim)
        add = np.zeros_like(rgb_)
        if p.energy > 1 or p.heat > 0:
            color = np.array(mix(CYAN, FLAME, min(1.0, p.heat)))
            add += ndimage.gaussian_filter(energy, 7)[..., None] * color * (max(0.0, p.energy - 1) * 2.0 + p.heat * 1.4)
        if p.eyes > 0.95:
            add += ndimage.gaussian_filter(eye, 6)[..., None] * np.array(EYE) * (0.5 + max(0.0, p.eyes - 1) * 2.2)
        glow_a = np.clip(add.max(-1), 0, 1)
        out_a = a + glow_a * (1 - a)
        out_rgb = np.clip((rgb_ * a[..., None] + add) / np.maximum(out_a[..., None], 1e-4), 0, 1)
        return out_rgb, out_a

    def surface(self, p):
        return to_surface(*self.pixels(p))

    def silhouette(self):
        if self._sil is None:
            self._sil = to_surface(np.ones_like(self.pm[..., :3]), self.pm[..., 3])
        return self._sil

    def halo(self):
        """A soft glow mask round the figure: its outline spread and blurred, the figure itself kept."""
        if self._halo is None:
            a = self.pm[..., 3]
            spread = np.maximum(ndimage.gaussian_filter(a, 6) * 2.0, ndimage.gaussian_filter(a, 16) * 1.2)
            self._halo = to_surface(np.ones_like(self.pm[..., :3]), np.clip(spread, 0, 1))
        return self._halo

    def plates(self):
        """The armour plates (phudo_art/make_plates.py): each one's pixels, its centre and an ordering key."""
        if self._plates is None:
            labels = np.asarray(Image.open(ART / 'plates' / self.spec['file']))
            labels = np.pad(labels, PAD)
            rgb_, a = self.pixels(replace(Pose(), eyes=1.3, energy=1.15), still=True)
            out = []
            chest = (self.spec['chest'][0] * SRC + PAD, self.spec['chest'][1] * SRC + PAD)
            for i, sl in enumerate(ndimage.find_objects(labels), 1):
                if sl is None:
                    continue
                y0, y1 = max(0, sl[0].start - 2), min(labels.shape[0], sl[0].stop + 2)
                x0, x1 = max(0, sl[1].start - 2), min(labels.shape[1], sl[1].stop + 2)
                m = labels[y0:y1, x0:x1] == i
                # A pixel of overlap so neighbouring plates meet without a hairline.
                m = ndimage.binary_dilation(m, iterations=1) & (a[y0:y1, x0:x1] > 0)
                if m.sum() < 30:
                    continue
                pa = np.where(m, a[y0:y1, x0:x1], 0)
                ys, xs = np.nonzero(m)
                cx, cy = x0 + xs.mean(), y0 + ys.mean()
                out.append(dict(surface=to_surface(rgb_[y0:y1, x0:x1], pa), origin=(x0, y0), centre=(cx, cy),
                                body=((cx - self.offset[0]) * self.k, (cy - self.offset[1]) * self.k),
                                out=(cx - chest[0], cy - chest[1]), area=int(m.sum())))
            self._plates = out
        return self._plates


_ART = {}


def art(form):
    if form not in _ART:
        _ART[form] = Art(FORMS[form])
    return _ART[form]


def rig(p):
    """The form's landmarks in effect units (feet at the origin), for effects that sit on the suit."""
    spec = FORMS[p.form]
    k = BODY_H / spec['height'] / OLD
    ax, ay = spec['anchor']
    def pt(q):
        return ((q[0] - ax) * k, (q[1] - ay) * k) if q else None
    chest = pt(spec['chest'])
    return dict(head=pt(spec['head']), chest=(chest[0], chest[1] - 66), gem=chest, hands=tuple(map(pt, spec['hands'])),
                shoulders=tuple(map(pt, spec['shoulders'])), vents=tuple(map(pt, spec['vents'])),
                tip=pt(spec.get('tip')), butt=pt(spec.get('butt')), plume=pt(spec.get('plume')),
                jets=tuple(map(pt, spec.get('jets', ()))), cannons=tuple(map(pt, spec.get('cannons', ()))),
                claws=tuple(map(pt, spec.get('claws', ()))), shield=pt(spec.get('shield')), spike=pt(spec.get('spike')))


def body_transform(ctx, p, dx=0.0, dlift=0.0):
    ctx.translate(CX + p.x + dx + p.shake, GROUND - p.lift - dlift + p.shake_y)
    if p.rot:
        ctx.translate(0, -p.pivot)
        ctx.rotate(math.radians(p.rot) * p.face)
        ctx.translate(0, p.pivot)
    ctx.scale(p.face * p.scale / max(p.squash, 0.2) ** 0.5, p.scale * p.squash)


def draw_art(ctx, form, surface):
    """Paint a drawing with its feet at the origin, scaled to the common body height."""
    a = art(form)
    ctx.save()
    ctx.scale(a.k, a.k)
    ctx.set_source_surface(surface, -a.offset[0], -a.offset[1])
    ctx.get_source().set_filter(cairo.FILTER_GOOD)
    ctx.paint()
    ctx.restore()


def draw_silhouette(ctx, form, color, alpha):
    a = art(form)
    ctx.save()
    ctx.scale(a.k, a.k)
    ctx.set_source_rgba(*color, alpha)
    ctx.mask_surface(a.silhouette(), -a.offset[0], -a.offset[1])
    ctx.restore()


def draw_halo(ctx, form, color, alpha):
    a = art(form)
    ctx.save()
    ctx.scale(a.k, a.k)
    ctx.set_source_rgba(*color, alpha)
    ctx.mask_surface(a.halo(), -a.offset[0], -a.offset[1])
    ctx.restore()


def effects(ctx, p, props):
    ctx.save()
    ctx.scale(OLD, OLD)
    for prop in props:
        prop(ctx, p)
    ctx.restore()


# ---------------------------------------------------------------- piece-by-piece transformation

def paint_plate(ctx, form, plate, pos, angle=0.0, sx=1.0, s=1.0, alpha=1.0):
    """Draw one armour plate with its centre at `pos` (body space), turned by `angle` degrees, flipped by `sx`."""
    a = art(form)
    ctx.save()
    ctx.translate(*pos)
    if angle:
        ctx.rotate(math.radians(angle))
    ctx.scale(sx * s * a.k, s * a.k)
    ctx.translate(-plate['centre'][0], -plate['centre'][1])
    ctx.set_source_surface(plate['surface'], *plate['origin'])
    ctx.get_source().set_filter(cairo.FILTER_GOOD)
    ctx.paint_with_alpha(alpha)
    ctx.restore()


def opened(form, plate, gap, key):
    """Where a plate sits when the armour is opened by `gap`: pushed out from the chest, turned a little."""
    a = art(form)
    ox, oy = plate['out']
    d = math.hypot(ox, oy) or 1
    push = gap * (8 + d * 0.075) * a.k
    rnd = random.Random(f'{key[0]}:{key[1]}')
    return ((plate['body'][0] + ox / d * push, plate['body'][1] + oy / d * push), gap * rnd.uniform(-6, 6))


def stagger(u, delay, width):
    return max(0.0, min(1.0, (u - delay) / width))


_PAIRS = {}


def pairing(a, b):
    """Which plate of form a becomes which plate of form b: nearest by body position, solved as an assignment;
    leftovers of a fold into the core, extras of b grow out of it."""
    if (a, b) not in _PAIRS:
        from scipy.optimize import linear_sum_assignment
        pa, pb = art(a).plates(), art(b).plates()
        cost = np.array([[math.dist(x['body'], y['body']) for y in pb] for x in pa])
        rows, cols = linear_sum_assignment(cost)
        src = {int(c): int(r) for r, c in zip(rows, cols)}
        gone = sorted(set(range(len(pa))) - set(src.values()))
        _PAIRS[(a, b)] = (src, gone)
    return _PAIRS[(a, b)]


def draw_morph(ctx, p):
    """The plates of p.form fly to their places in p.morph_to: each flips over on its way (showing the old plate,
    then the new one), spins and arcs out from the body; legs first, the head last, a spark as each one lands."""
    a, b = p.form, p.morph_to
    pa, pb = art(a).plates(), art(b).plates()
    src, gone = pairing(a, b)
    core = (0.0, -BODY_H * 0.55)
    flights, sparks_at = [], []
    ys = [q['body'][1] for q in pb]
    lo, hi = min(ys), max(ys)
    for j, plate in enumerate(pb):
        order = (hi - plate['body'][1]) / max(1.0, hi - lo)          # 0 at the feet .. 1 at the crest
        local = ease(stagger(p.morph, order * 0.55, 0.45))
        end, end_rot = opened(b, plate, 1.0, (b, j))
        if j in src:
            i = src[j]
            start, start_rot = opened(a, pa[i], 1.0, (a, i))
            old = pa[i]
        else:
            start, start_rot, old = core, 0.0, None
        mid = ((start[0] + end[0]) / 2, (start[1] + end[1]) / 2)
        out = (mid[0] - core[0], mid[1] - core[1])
        d = math.hypot(*out) or 1
        ctrl = (mid[0] + out[0] / d * 140, mid[1] + out[1] / d * 140 - 60)
        pos = ((1 - local) ** 2 * start[0] + 2 * (1 - local) * local * ctrl[0] + local * local * end[0],
               (1 - local) ** 2 * start[1] + 2 * (1 - local) * local * ctrl[1] + local * local * end[1])
        spin = (1 if j % 2 else -1) * (180 if plate['area'] > 4000 else 360)
        angle = lerp(start_rot, end_rot, local) + spin * local * (1 - local) * 4 * 0.5
        sx = math.cos(math.pi * local)
        s = 1 + 0.18 * math.sin(math.pi * local)
        if old is None:
            s *= max(0.05, local)
        flights.append((local, pos, angle, sx, s, old, plate))
        if 0.82 < local < 0.999:
            sparks_at.append((end, (local - 0.82) / 0.18))
    for i in gone:
        order = 1 - (pa[i]['body'][1] - min(q['body'][1] for q in pa)) / max(1.0, max(q['body'][1] for q in pa) - min(q['body'][1] for q in pa))
        local = ease(stagger(p.morph, order * 0.4, 0.4))
        start, rot = opened(a, pa[i], 1.0, (a, i))
        pos = lerp_pt(start, core, local)
        flights.append((local * 0.999, pos, rot + 300 * local, 1.0, max(0.05, 1 - local), pa[i], None))
    # Energy core and tethers to the plates in flight.
    glow(ctx, *core, 90 + 40 * math.sin(math.pi * p.morph), CYAN, 0.35 * math.sin(math.pi * p.morph))
    glow(ctx, *core, 30 + 14 * math.sin(math.pi * p.morph * 3), WHITE, 0.8 * math.sin(math.pi * p.morph))
    ctx.set_line_cap(cairo.LINE_CAP_ROUND)
    for local, pos, *_ in flights:
        w = math.sin(math.pi * local)
        if w > 0.05:
            ctx.move_to(*core)
            ctx.line_to(*pos)
            rgb(ctx, CYAN, 0.18 * w)
            ctx.set_line_width(2.5)
            ctx.stroke()
    # Plates still waiting or landed underneath, plates in flight on top.
    for local, pos, angle, sx, s, old, plate in sorted(flights, key=lambda f: 0 if f[0] in (0.0, 1.0) else 1):
        if plate is None:                               # an old plate folding into the core
            paint_plate(ctx, a, old, pos, angle, 1.0, s, alpha=max(0.0, 1 - local))
        elif old is not None and local < 0.5:           # the old plate, turning edge-on
            paint_plate(ctx, a, old, pos, angle, max(0.04, sx), s)
        else:                                           # the new plate, turning face-on (or growing from the core)
            paint_plate(ctx, b, plate, pos, angle, max(0.04, abs(sx)) if old is not None else 1.0, s)
        if 0.02 < local < 0.98:
            glow(ctx, *pos, 26 * s, CYAN, 0.25 * math.sin(math.pi * local))
    for (x, y), u in sparks_at:
        glow(ctx, x, y, 60, CYAN, 0.7 * (1 - u))
        star(ctx, x, y, 34 * (1 - u) + 6, WHITE, outline=False, points=4, inner=0.18, rot=u)


def draw_opened(ctx, p):
    """The armour opened by p.gap: each plate pushed out from the chest (outer plates first), light between."""
    form = p.form
    plates = art(form).plates()
    ds = [math.hypot(*q['out']) for q in plates]
    dmax = max(ds) or 1
    for j, plate in enumerate(plates):
        g = max(0.0, min(1.0, p.gap * 1.5 - (1 - ds[j] / dmax) * 0.5))
        pos, rot = opened(form, plate, g, (form, j))
        paint_plate(ctx, form, plate, pos, rot)


def render(p):
    surface = cairo.ImageSurface(cairo.FORMAT_ARGB32, SIZE, SIZE)
    ctx = cairo.Context(surface)
    ctx.set_line_join(cairo.LINE_JOIN_ROUND)
    for prop in p.under:
        prop(ctx, p)
    if p.alpha > 0 and p.scale > 0.01:
        if p.shadow:
            k = 1 / (1 + p.lift / 220)
            ellipse(ctx, CX + p.x, GROUND + 4, 170 * p.scale * k, 20 * k)
            rgb(ctx, (0, 0, 0), 0.22 * k * p.alpha)
            ctx.fill()
        for ghost in p.ghosts:
            dx, a = ghost[0], ghost[1]
            dy = ghost[2] if len(ghost) > 2 else 0
            ctx.save()
            body_transform(ctx, p, dx, dy)
            draw_silhouette(ctx, p.form, VIOLET_LIGHT, a * p.alpha)
            ctx.restore()
        ctx.push_group()
        ctx.save()
        body_transform(ctx, p)
        effects(ctx, p, p.back)
        if p.aura > 0:
            draw_halo(ctx, p.form, p.aura_color, 0.5 * p.aura)
        if p.seam > 0 and not p.morph_to:
            draw_halo(ctx, p.form, CYAN, 0.55 * p.seam)
        if p.morph_to:
            draw_morph(ctx, p)
        elif p.gap > 0:
            draw_opened(ctx, p)
        else:
            draw_art(ctx, p.form, art(p.form).surface(p))
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


# ---------------------------------------------------------------- effects for the forms (effect units, feet at 0)

def plasma_vent(t, points, color=VIOLET_LIGHT, seed=3, power=1.0):
    """Phase 1 venting: a hot jet and a plume of plasma out of each vent, pointing away from the chest."""
    def draw(ctx, p):
        if t <= 0 or t >= 1:
            return
        r = rig(p)
        cx, cy = r['gem']
        rnd = random.Random(seed)
        k = math.sin(math.pi * t) * power
        for (x, y) in points if points else r['vents']:
            ang = math.atan2(y - cy - 120, x - cx) + rnd.uniform(-0.2, 0.2)
            L = (50 + 90 * k) * rnd.uniform(0.8, 1.2)
            ux, uy = math.cos(ang), math.sin(ang)
            glow(ctx, x + ux * L * 0.4, y + uy * L * 0.4, L * 0.7, color, 0.45 * k)
            for w, c, a in ((18, color, 0.5), (7, mix(color, WHITE, 0.7), 0.8)):
                ctx.move_to(x - uy * w, y + ux * w)
                ctx.curve_to(x - uy * w * 0.6 + ux * L * 0.5, y + ux * w * 0.6 + uy * L * 0.5, x + ux * L, y + uy * L,
                             x + ux * L, y + uy * L)
                ctx.curve_to(x + ux * L, y + uy * L, x + uy * w * 0.6 + ux * L * 0.5, y - ux * w * 0.6 + uy * L * 0.5,
                             x + uy * w, y - ux * w)
                ctx.close_path()
                rgb(ctx, c, a * k)
                ctx.fill()
            for j in range(3):
                local = (t * 1.6 + j / 3) % 1
                puff(ctx, x + ux * (L + 60 * local), y + uy * (L + 60 * local) - 50 * local, 22 + 34 * local,
                     0.7 * (1 - local) * k)
    return draw


def clack(t, points, seed=5):
    """Mechanical locks snapping open or shut: a white star and a ring at each joint, one after another."""
    def draw(ctx, p):
        rnd = random.Random(seed)
        for i, (x, y) in enumerate(points):
            local = (t - i / max(1, len(points)) * 0.5) / 0.5
            if 0 < local < 1:
                s = 40 * math.sin(math.pi * local)
                glow(ctx, x, y, s * 2.4, CYAN, 0.6 * (1 - local))
                star(ctx, x, y, s, WHITE, outline=False, points=4, inner=0.16, rot=rnd.uniform(0, 1))
                ellipse(ctx, x, y, 20 + 60 * local, 20 + 60 * local)
                rgb(ctx, CYAN, 0.8 * (1 - local))
                ctx.set_line_width(4)
                ctx.stroke()
    return draw


def flames(t, x, y, size=1.0, seed=7, amount=14, color=FLAME, spread=60):
    """Licking flame: teardrop blobs rising and shrinking from yellow-white through orange to red, then smoke."""
    def draw(ctx, p):
        rnd = random.Random(seed)
        for i in range(amount):
            local = (t * 1.8 + rnd.uniform(0, 1)) % 1
            px = x + rnd.uniform(-spread, spread) * size + math.sin(local * 6 + i) * 14 * size
            py = y - local * 260 * size
            s = (34 - 26 * local) * size * rnd.uniform(0.7, 1.2)
            if local < 0.8:
                c = mix(mix((1.0, 0.95, 0.7), color, min(1.0, local * 2.5)), RED, max(0.0, local - 0.4) * 1.6)
                glow(ctx, px, py, s * 2.2, color, 0.25 * (1 - local))
                ctx.move_to(px, py - s * 1.6)
                ctx.curve_to(px + s, py - s * 0.4, px + s * 0.8, py + s * 0.8, px, py + s)
                ctx.curve_to(px - s * 0.8, py + s * 0.8, px - s, py - s * 0.4, px, py - s * 1.6)
                rgb(ctx, c, 0.9 * (1 - local * 0.6))
                ctx.fill()
            else:
                puff(ctx, px, py - 30, s * 1.5, 0.35 * (1 - local) * 4)
    return draw


def lightning(t, x0, y0, x1, y1, seed=1, width=1.0, color=CYAN):
    """A jagged bolt with branches from (x0, y0) to (x1, y1); t 0..1 flickers it on and fades it."""
    def draw(ctx, p):
        if t <= 0 or t >= 1:
            return
        rnd = random.Random(seed * 31 + int(t * 9))
        alpha = (1 - t) ** 0.6
        def bolt(ax, ay, bx, by, w, depth):
            pts = [(ax, ay)]
            n = 9
            for i in range(1, n):
                u = i / n
                jx = rnd.uniform(-1, 1) * 70 * (1 - abs(u - 0.5) * 1.2) * (0.6 if depth else 1)
                pts.append((lerp(ax, bx, u) + jx, lerp(ay, by, u) + rnd.uniform(-20, 20)))
            pts.append((bx, by))
            for lw, c, a in ((w * 26, color, 0.25), (w * 11, color, 0.6), (w * 4, WHITE, 1.0)):
                ctx.move_to(*pts[0])
                for q in pts[1:]:
                    ctx.line_to(*q)
                rgb(ctx, c, a * alpha)
                ctx.set_line_width(lw)
                ctx.set_line_join(cairo.LINE_JOIN_MITER)
                ctx.stroke()
            ctx.set_line_join(cairo.LINE_JOIN_ROUND)
            if depth < 1:
                for _ in range(2):
                    i = rnd.randint(2, n - 2)
                    sx, sy = pts[i]
                    bolt(sx, sy, sx + rnd.uniform(-220, 220), sy + rnd.uniform(80, 260), w * 0.5, depth + 1)
        bolt(x0, y0, x1, y1, width, 0)
        glow(ctx, x1, y1, 260 * width, color, 0.55 * alpha)
        glow(ctx, x1, y1, 90 * width, WHITE, 0.8 * alpha)
    return draw


def charge(t, x, y, color=CYAN, radius=380, seed=2, count=22):
    """Energy gathering: sparks spiral in to a point while a ball of light swells there."""
    def draw(ctx, p):
        rnd = random.Random(seed)
        ctx.set_line_cap(cairo.LINE_CAP_ROUND)
        for i in range(count):
            local = (t * 2 + rnd.uniform(0, 1)) % 1
            ang = rnd.uniform(0, 2 * math.pi) + local * 1.5
            d = radius * (1 - ease_in(local))
            px, py = x + math.cos(ang) * d, y + math.sin(ang) * d
            tx, ty = x + math.cos(ang - 0.25) * (d + 50), y + math.sin(ang - 0.25) * (d + 50)
            ctx.move_to(px, py)
            ctx.line_to(tx, ty)
            rgb(ctx, mix(color, WHITE, 0.4), 0.85 * local)
            ctx.set_line_width(4)
            ctx.stroke()
        s = 30 + 90 * ease(min(1.0, t))
        glow(ctx, x, y, s * 2.6, color, 0.5)
        glow(ctx, x, y, s, WHITE, 0.9)
    return draw


def beam(t, x, y, angle=180, length=2600, width=1.0, color=CYAN):
    """The railgun firing: a white core in a cyan sheath with rings along it, swelling then thinning out."""
    def draw(ctx, p):
        if t <= 0 or t >= 1:
            return
        w = width * (1 - t) ** 0.5 * (1 + 0.3 * math.sin(t * 40))
        ctx.save()
        ctx.translate(x, y)
        ctx.rotate(math.radians(angle))
        for ww, c, a in ((150 * w, color, 0.25), (80 * w, color, 0.55), (34 * w, WHITE, 1.0)):
            g = cairo.LinearGradient(0, 0, length, 0)
            g.add_color_stop_rgba(0, *c, a)
            g.add_color_stop_rgba(0.85, *c, a * 0.8)
            g.add_color_stop_rgba(1, *c, 0)
            ctx.set_source(g)
            ctx.rectangle(0, -ww / 2, length, ww)
            ctx.fill()
        for k in range(5):
            u = ((t * 3 + k / 5) % 1)
            ellipse(ctx, 120 + u * length * 0.6, 0, 26 * w + 10, 110 * w + 20)
            rgb(ctx, WHITE, 0.7 * (1 - u) * w)
            ctx.set_line_width(5)
            ctx.stroke()
        glow(ctx, 0, 0, 260 * width, color, 0.7 * (1 - t))
        ctx.restore()
    return draw


def shells(t, origins, seed=4, count=6):
    """Artillery: glowing shells fired up out of the cannons, each with a muzzle flash and a trail."""
    def draw(ctx, p):
        rnd = random.Random(seed)
        ctx.set_line_cap(cairo.LINE_CAP_ROUND)
        for i in range(count):
            ox, oy = origins[i % len(origins)]
            start = i / count * 0.6
            local = (t - start) / 0.4
            if local <= 0 or local >= 1.2:
                continue
            if local < 0.25:
                glow(ctx, ox, oy, 120 * (1 - local * 4), FLAME, 0.8)
                star(ctx, ox, oy, 60 * (1 - local * 4), (1, 0.95, 0.75), outline=False, points=6, inner=0.4)
            vx = rnd.uniform(-260, 260)
            sx, sy = ox + vx * local, oy - 1600 * local + 400 * local * local
            ctx.move_to(ox + vx * max(0, local - 0.15), oy - 1600 * max(0, local - 0.15) + 400 * max(0, local - 0.15) ** 2)
            ctx.line_to(sx, sy)
            rgb(ctx, FLAME, 0.6)
            ctx.set_line_width(14)
            ctx.stroke()
            glow(ctx, sx, sy, 50, FLAME, 0.8)
            ellipse(ctx, sx, sy, 12, 12)
            rgb(ctx, WHITE)
            ctx.fill()
    return draw


def explosions(t, spots, seed=6):
    """Shells landing far away: flashes, fireballs and smoke at ground spots (effect units), staggered."""
    def draw(ctx, p):
        rnd = random.Random(seed)
        for i, (x, y) in enumerate(spots):
            local = (t - i * 0.12) / 0.5
            if 0 < local < 1:
                R = 60 + 200 * ease_out(local)
                glow(ctx, x, y - R * 0.4, R * 1.4, FLAME, 0.6 * (1 - local))
                glow(ctx, x, y - R * 0.4, R * 0.6, (1, 0.95, 0.8), 0.9 * (1 - local))
                for k in range(5):
                    puff(ctx, x + rnd.uniform(-1, 1) * R * 0.7, y - R * rnd.uniform(0.2, 1.0), R * 0.35, 0.8 * (1 - local))
    return draw


def shield_dome(t, x=0, y=0, radius=640, color=CYAN, alpha=1.0):
    """Hắc Tháp's barrier: a hexagon-panelled dome over the suit, a ripple running across it."""
    def draw(ctx, p):
        if alpha <= 0:
            return
        R = radius * (0.85 + 0.15 * ease_out(min(1.0, t * 3)))
        ctx.save()
        ctx.new_path()
        ctx.arc(x, y, R, math.pi, 2 * math.pi)
        ctx.close_path()
        ctx.clip_preserve()
        g = cairo.RadialGradient(x, y, R * 0.4, x, y, R)
        g.add_color_stop_rgba(0, *color, 0.05 * alpha)
        g.add_color_stop_rgba(1, *color, 0.35 * alpha)
        ctx.set_source(g)
        ctx.fill()
        s = 70
        for row in range(-1, int(R / (s * 0.86)) + 2):
            for col in range(-int(R / s) - 2, int(R / s) + 3):
                hx = x + col * s * 1.5
                hy = y - row * s * 1.73 - (s * 0.86 if col % 2 else 0)
                for k in range(7):
                    a = math.pi / 3 * k
                    (ctx.move_to if k == 0 else ctx.line_to)(hx + math.cos(a) * s, hy + math.sin(a) * s)
        rgb(ctx, color, 0.35 * alpha)
        ctx.set_line_width(3)
        ctx.stroke()
        ripple = (t * 1.5) % 1
        ellipse(ctx, x, y, R * ripple, R * ripple)
        rgb(ctx, WHITE, 0.4 * (1 - ripple) * alpha)
        ctx.set_line_width(8)
        ctx.stroke()
        ctx.restore()
        ctx.new_path()
        ctx.arc(x, y, R, math.pi, 2 * math.pi)
        rgb(ctx, mix(color, WHITE, 0.4), 0.8 * alpha)
        ctx.set_line_width(6)
        ctx.stroke()
    return draw


def streak(t, x0, y0, x1, y1, bulge=0.18, width=80, color=VIOLET_LIGHT):
    """A slash across the frame from (x0, y0) to (x1, y1): a curved blade of light that is drawn in from x0,
    white along its edge, and fades; thick in the middle and pointed at both ends."""
    def draw(ctx, p):
        if t <= 0 or t >= 1.4:
            return
        head = min(1.0, t / 0.4)
        fade = 1 - max(0.0, (t - 0.4) / 1.0)
        dx, dy = x1 - x0, y1 - y0
        L = math.hypot(dx, dy) or 1
        nx, ny = -dy / L, dx / L
        cx, cy = (x0 + x1) / 2 + nx * L * bulge, (y0 + y1) / 2 + ny * L * bulge
        n = 28
        top, bot = [], []
        for i in range(n + 1):
            u = i / n * ease_out(head)
            px = (1 - u) ** 2 * x0 + 2 * (1 - u) * u * cx + u * u * x1
            py = (1 - u) ** 2 * y0 + 2 * (1 - u) * u * cy + u * u * y1
            w = width * math.sin(math.pi * min(1.0, u / max(ease_out(head), 1e-3))) ** 0.8
            top.append((px + nx * w * 0.2, py + ny * w * 0.2))
            bot.append((px - nx * w, py - ny * w))
        pts = top + bot[::-1]
        glow(ctx, *top[n // 2], width * 3, color, 0.3 * fade)
        ctx.move_to(*pts[0])
        for q in pts[1:]:
            ctx.line_to(*q)
        ctx.close_path()
        g = cairo.LinearGradient(*top[n // 2], *bot[n // 2])
        g.add_color_stop_rgba(0, *mix(color, WHITE, 0.6), 0.95 * fade)
        g.add_color_stop_rgba(0.4, *color, 0.8 * fade)
        g.add_color_stop_rgba(1, *color, 0.0)
        ctx.set_source(g)
        ctx.fill()
        ctx.move_to(*top[0])
        for q in top[1:]:
            ctx.line_to(*q)
        rgb(ctx, WHITE, 0.95 * fade)
        ctx.set_line_width(6)
        ctx.stroke()
    return draw


def slash(t, x, y, radius, a0, a1, width=90, color=VIOLET_LIGHT):
    """A filled crescent slash sweeping from a0 to a1 (degrees, screen), white at the leading edge, fading out."""
    def draw(ctx, p):
        if t <= 0 or t >= 1.4:
            return
        head = min(1.0, t / 0.45)
        fade = 1 - max(0.0, (t - 0.45) / 0.95)
        lo = math.radians(a0)
        hi = math.radians(lerp(a0, a1, ease_out(head)))
        if abs(hi - lo) < 0.02:
            return
        n = 24
        outer, inner = [], []
        for i in range(n + 1):
            u = i / n
            a = lerp(lo, hi, u)
            w = width * math.sin(math.pi * u) ** 0.7 * (0.4 + 0.6 * u)
            outer.append((x + math.cos(a) * radius, y + math.sin(a) * radius))
            inner.append((x + math.cos(a) * (radius - w), y + math.sin(a) * (radius - w)))
        pts = outer + inner[::-1]
        for grow, c, al in ((1.0, color, 0.55), (0.0, mix(color, WHITE, 0.75), 0.95)):
            ctx.move_to(*pts[0])
            for q in pts[1:]:
                ctx.line_to(*q)
            ctx.close_path()
            rgb(ctx, c, al * fade)
            if grow:
                ctx.fill()
            else:
                ctx.set_line_width(4)
                ctx.stroke()
        glow(ctx, *outer[-1], 70, color, 0.6 * fade)
    return draw


def roar(t, x, y, color=RED_LIGHT, seed=3):
    """A roar: shock rings racing out from the head and jagged sound lines."""
    def draw(ctx, p):
        if t <= 0 or t >= 1:
            return
        for k in range(3):
            u = (t * 1.5 - k * 0.2)
            if 0 < u < 1:
                R = 80 + 700 * ease_out(u)
                ellipse(ctx, x, y, R, R * 0.8)
                rgb(ctx, mix(color, WHITE, 0.3), 0.7 * (1 - u))
                ctx.set_line_width(14 * (1 - u) + 2)
                ctx.stroke()
        rnd = random.Random(seed)
        for i in range(10):
            a = rnd.uniform(0, 2 * math.pi)
            d0 = 160 + 300 * t
            ctx.move_to(x + math.cos(a) * d0, y + math.sin(a) * d0 * 0.8)
            ctx.line_to(x + math.cos(a) * (d0 + 120), y + math.sin(a) * (d0 + 120) * 0.8)
            rgb(ctx, WHITE, 0.8 * (1 - t))
            ctx.set_line_width(6)
            ctx.stroke()
    return draw


def debris(t, x, y, seed=8, count=12, spread=1.0):
    """Ground chunks thrown up by an impact, tumbling and falling back."""
    def draw(ctx, p):
        if t <= 0 or t >= 1:
            return
        rnd = random.Random(seed)
        for i in range(count):
            vx = rnd.uniform(-420, 420) * spread
            vy = rnd.uniform(500, 1000)
            px, py = x + vx * t, y - vy * t + 1500 * t * t
            if py > y + 10:
                continue
            s = rnd.uniform(10, 26)
            ctx.save()
            ctx.translate(px, py)
            ctx.rotate(rnd.uniform(0, 6) + t * rnd.uniform(-10, 10))
            poly(ctx, [(-s, -s * 0.6), (s * 0.7, -s), (s, s * 0.5), (-s * 0.4, s)])
            rgb(ctx, (0.35, 0.33, 0.38))
            ctx.fill_preserve()
            rgb(ctx, INK)
            ctx.set_line_width(3)
            ctx.stroke()
            ctx.restore()
    return draw


def focus_lines(strength, seed=9):
    """Canvas space: radial speed lines closing in on the figure, for an ultimate."""
    def draw(ctx, p):
        if strength <= 0:
            return
        rnd = random.Random(seed)
        cx, cy = CX + p.x, GROUND - 330
        ctx.set_line_cap(cairo.LINE_CAP_ROUND)
        for i in range(36):
            a = rnd.uniform(0, 2 * math.pi)
            r0 = rnd.uniform(380, 460)
            r1 = r0 + rnd.uniform(80, 220) * strength
            ctx.move_to(cx + math.cos(a) * r0, cy + math.sin(a) * r0)
            ctx.line_to(cx + math.cos(a) * r1, cy + math.sin(a) * r1)
            rgb(ctx, WHITE, 0.6 * strength)
            ctx.set_line_width(rnd.uniform(3, 8))
            ctx.stroke()
    return draw


def flash(strength, color=WHITE):
    """Canvas space: a radial burst of light on the figure that fades before the frame's edge."""
    def draw(ctx, p):
        if strength <= 0:
            return
        glow(ctx, CX + p.x, GROUND - p.lift - 330, 480, color, 0.85 * strength)
    return draw


_SWORD = None


def greatsword(x, y, angle, materialise=1.0, scale=1.0, light=0.0):
    """The Đại Trảm Đao from the sheet, appearing out of a scan line (materialise 0..1) and glowing."""
    def draw(ctx, p):
        global _SWORD
        if materialise <= 0:
            return
        if _SWORD is None:
            im = np.asarray(Image.open(ART / 'greatsword.png').convert('RGBA'), dtype=np.float32) / 255
            _SWORD = to_surface(im[..., :3], im[..., 3])
        W, H = _SWORD.get_width(), _SWORD.get_height()
        ctx.save()
        ctx.translate(x, y)
        ctx.rotate(math.radians(angle))
        ctx.scale(scale * 1.35, scale * 1.35)
        ctx.translate(-W * 0.88, -H / 2)          # held by the grip, near its right end
        if light > 0:
            glow(ctx, W * 0.45, H / 2, W * 0.5, VIOLET_LIGHT, 0.4 * light)
        ctx.save()
        ctx.rectangle(W * (1 - materialise), -20, W * materialise + 20, H + 40)
        ctx.clip()
        ctx.set_source_surface(_SWORD, 0, 0)
        ctx.paint()
        ctx.restore()
        if materialise < 1:
            xs = W * (1 - materialise)
            ctx.move_to(xs, -10)
            ctx.line_to(xs, H + 10)
            rgb(ctx, WHITE, 0.95)
            ctx.set_line_width(4)
            ctx.stroke()
            glow(ctx, xs, H / 2, 70, CYAN, 0.7)
        ctx.restore()
    return draw


# ---------------------------------------------------------------- sequences: lists of (Pose, duration_ms)

BASE = Pose()


def P(**kw):
    return replace(BASE, **kw)


def fx(seq, fn):
    """Add per-frame effects: fn(pose, t, i) for t in 0..1 over the whole sequence."""
    n = len(seq)
    return [(fn(p, i / max(1, n - 1), i), ms) for i, (p, ms) in enumerate(seq)]


def mirrored(seq):
    return [(replace(p, face=-p.face, x=-p.x, ghosts=tuple((-g[0],) + tuple(g[1:]) for g in p.ghosts)), ms)
            for p, ms in seq]


def at(form, name, i=None):
    q = rig(P(form=form))[name]
    return q[i] if i is not None else q


def living(form, t, **kw):
    """A form at rest: breathing, the plumes swaying, the lights pulsing; t is the loop phase 0..1."""
    base = dict(form=form, breathe=2.5 + 2.5 * math.sin(2 * math.pi * t), sway=0.6, flutter=t,
                energy=1 + 0.12 * osc(t), eyes=1.05 + 0.15 * osc(t, 0.5))
    base.update(kw)
    return P(**base)


def loop(n, ms, make):
    return [(make(i / n, i), ms) for i in range(n)]


# Transformations: the designer's four-phase pipeline --------------------------------------------------------

FORM_COLOR = {'phong': CYAN, 'hoa': CYAN, 'tu': RED_LIGHT, 'thap': VIOLET_LIGHT}


HOVER = 160                 # Hỏa Dực's cruising height above the ground, canvas pixels


def transform(a, b, speed=1.0, lift0=0.0, lift1=None):
    """From form a to form b piece by piece, in the designer's four phases:
    1. Disengage and vent: the stance drops, locks clack open, plasma vents, and every armour plate opens out from
       the body with energy light between them (outer plates first).
    2. Articulation: the plates fly to their places in the new form, each flipping over on the way (the old plate
       turning edge-on, the new one turning face-on), spinning and arcing round an energy core; legs first, the
       head last, a spark as each one lands.
    3. Armour snap: the new form's plates close in onto the body and lock.
    4. Ignition: a lock thud and a ground shock, the visor and lights flare, flame on the plume, a screen shake.
    `speed` > 1 drops frames from every phase; the height eases from lift0 to lift1 (Hỏa Dực hovers)."""
    lift1 = lift0 if lift1 is None else lift1
    def n(v):
        return max(2, round(v / speed))
    out = []
    color = FORM_COLOR.get(b, CYAN)
    va = rig(P(form=a))
    joints = [va['shoulders'][0], va['shoulders'][1], va['gem']]
    # Phase 1: unlock and open.
    m = n(4)
    for i in range(m):
        u = (i + 1) / m
        out.append((P(form=a, squash=1 - 0.04 * ease(u), breathe=-4 * u, eyes=1 + 0.8 * (i % 2), energy=1 + 0.5 * u,
                      shake=(4 if i % 2 else -4), flutter=u * 0.4, sway=1.0,
                      front=[plasma_vent(u * 0.6, None, color=VIOLET_LIGHT if a != 'tu' else FLAME, seed=3),
                             clack(u, joints, seed=4)]), 60))
    m = n(6)
    for i in range(m):
        u = (i + 1) / m
        out.append((P(form=a, gap=back_out(u) if u < 1 else 1.0, seam=u, aura_color=color, squash=0.96,
                      front=[plasma_vent(0.6 + 0.35 * u, None, color=color, seed=5, power=0.7)]), (70, 60, 60, 60, 70, 90)[min(5, round(i * 5 / max(1, m - 1)))]))
    # Phase 2: the plates fly over.
    m = n(18)
    for i in range(m):
        u = (i + 1) / m
        out.append((P(form=a, morph_to=b, morph=u, aura_color=color), 55 if 0.1 < u < 0.9 else 70))
    # Phase 3: lock.
    m = n(6)
    vb_plates = rig(P(form=b))
    locks = [vb_plates['shoulders'][0], vb_plates['shoulders'][1], vb_plates['gem'], vb_plates['head']]
    for i in range(m):
        u = (i + 1) / m
        out.append((P(form=b, gap=1 - ease_in(u), seam=1 - 0.6 * u, aura_color=color, squash=1.0 - 0.03 * u,
                      front=[clack(u, locks, seed=6)]), (60, 50, 45, 45, 60, 80)[min(5, round(i * 5 / max(1, m - 1)))]))
    # Phase 4: ignite.
    vb = rig(P(form=b))
    m = n(9)
    for j in range(m):
        u = j / max(1, m - 1)
        shake = 12 * (1 - u) * (-1) ** j
        props = [shockwave(min(0.95, u * 1.1 + 0.05), size=1.1, color=mix(color, WHITE, 0.5), dust=lift1 < 1)]
        if vb['plume']:
            props.append(flames(u, vb['plume'][0], vb['plume'][1], size=1.0 + 0.4 * (b == 'tu'), seed=9,
                                color=FLAME if b in ('tu', 'phong') else VIOLET_LIGHT))
        if b == 'thap' and vb['spike']:
            props += [cracks(u * 2, vb['spike'][0], 0), debris(u, vb['spike'][0], 0, seed=3)]
        if b == 'hoa':
            props.append(jets(u, power=0.4 + 0.6 * u))
        out.append((P(form=b, squash=(0.94, 1.03, 1.0)[min(2, j)], shake=shake, shake_y=abs(shake) * 0.3,
                      seam=0.3 * (1 - u), aura_color=color, eyes=1.8 - 0.6 * u, energy=1.5 - 0.3 * u,
                      heat=0.3 * (1 - u) if b == 'tu' else 0, flutter=1.6 + u, sway=1.2 - 0.4 * u, front=props,
                      over=[flash(0.3 * (1 - u) ** 2, mix(color, WHITE, 0.5))] if j < 2 else []),
                    (50, 50, 60, 60, 70, 80, 90, 100, 120)[min(8, round(j * 8 / max(1, m - 1)))]))
    total = len(out)
    return [(replace(p, lift=lerp(lift0, lift1, ease(i / max(1, total - 1)))), ms) for i, (p, ms) in enumerate(out)]


# Phong Lôi: idle and the desk states --------------------------------------------------------------------

def idle(kind='plain'):
    out = []
    n = 20
    for i in range(n):
        t = i / n
        p = living('phong', t)
        if kind == 'thunder':
            tip = at('phong', 'tip')
            k = i % 7
            p = replace(p, energy=1.2 + 0.3 * (k < 2), front=[bolts(t, seed=i // 3, amount=0.6)] +
                        ([lightning(0.3 + 0.2 * k, tip[0] + 60, tip[1] - 300, tip[0], tip[1], seed=i, width=0.5)] if k < 2 else []))
        elif kind == 'vent':
            s = hop(min(1.0, t * 1.4))
            p = replace(p, breathe=p.breathe + 4 * s, front=[plasma_vent(min(0.99, t * 1.2), None, color=STEAM, seed=4, power=0.5 * s)])
        elif kind == 'scan':
            look = math.sin(2 * math.pi * t)
            p = replace(p, eyes=1.8, lean=-4 * look, front=[eye_scan(t, look)])
        elif kind == 'happy':
            b = max(0.0, math.sin(2 * math.pi * t * 2))
            p = replace(p, lift=10 * b, squash=1 + 0.02 * b, energy=1.5, eyes=1.6, sway=1.2,
                        front=[twinkles(t, [(-300, -900), (300, -900), (-380, -600), (380, -600)], seed=2, color=CYAN)])
        elif kind == 'poor':
            flick = i in (5, 6, 14)
            p = replace(p, breathe=1 + osc(t), lean=-5, dim=0.35, energy=0.3 if flick else 0.5, eyes=0.2 if flick else 0.5, sway=0.2)
        out.append((p, 130))
    if kind in ('plain', 'vent'):
        p = out[13][0]
        out[13:14] = [(replace(p, eyes=0.3), 60), (replace(p, eyes=0.0), 60), (replace(p, eyes=1.6), 70)]
    return out


def eye_scan(t, look):
    """A red scanning beam swept from the visor across the ground in front."""
    def draw(ctx, p):
        r = rig(p)
        hx, hy = r['head']
        tx = hx + look * 520
        ctx.move_to(hx - 10, hy)
        ctx.line_to(tx - 120, -20)
        ctx.line_to(tx + 120, -20)
        ctx.line_to(hx + 10, hy)
        ctx.close_path()
        rgb(ctx, EYE, 0.12)
        ctx.fill()
        ellipse(ctx, tx, -20, 130, 22)
        rgb(ctx, EYE, 0.6)
        ctx.set_line_width(4)
        ctx.stroke()
    return draw


def look_around():
    return loop(10, 120, lambda t, i: living('phong', t, eyes=1.8, lean=-5 * math.sin(2 * math.pi * t),
                                             front=[eye_scan(t, math.sin(2 * math.pi * t))]))


def think(phase, kind='map'):
    if phase == 'start':
        return [(living('phong', i / 6, eyes=1 + 0.15 * i, front=[hologram(i / 6, size=back_out(i / 5), kind=kind)]),
                 (70, 70, 80, 80, 90, 110)[i]) for i in range(6)]
    if phase == 'loop':
        return loop(16, 110, lambda t, i: living('phong', t, eyes=1.7 + 0.2 * osc(t * 2), energy=1.25 + 0.15 * osc(t),
                                                 lean=-2, front=[hologram(t, kind=kind)]))
    return [(living('phong', i / 4, eyes=1.4 - 0.1 * i, front=[hologram(0.9, size=u)] if u else []), 70)
            for i, u in enumerate((1.08, 0.6, 0.2, 0.0))]


def scan(phase, flicking=False):
    if phase == 'start':
        return [(living('phong', i / 6, eyes=1 + 0.12 * i, lean=2 * min(1.0, u), front=[data_panel(0, open_=min(1.08, u))]),
                 (60, 60, 70, 70, 70, 100)[i]) for i, u in enumerate((0.15, 0.4, 0.75, 1.0, 1.06, 1.0))]
    if phase == 'loop':
        def frame(t, i):
            if flicking:
                f = ease(max(0.0, min(1.0, ((t * 2) % 1 - 0.55) / 0.3)))
                props = [data_panel(t * 0.5, flick=f, beam=None if f > 0 else (t * 2 % 1) / 0.55)]
            else:
                props = [data_panel(t, beam=ease((t * 2) % 1))]
            return living('phong', t, eyes=1.7, lean=2, front=props)
        return loop(16, 110, frame)
    return [(living('phong', i / 4, eyes=1.5 - 0.1 * i, lean=2 * u, front=[data_panel(0, open_=u)] if u else []), 60)
            for i, u in enumerate((1.0, 0.6, 0.2, 0.0))]


def short_circuit():
    """Tool error: the lights flare and overheat, arcs crawl over the suit with the frame shaking, the power dies;
    a slump with smoke, then a reboot of flickering lights."""
    out = [(living('phong', 0, energy=2.4, eyes=2.2, front=[burst(0.15, *at('phong', 'gem'), seed=1, radius=300)]), 70)]
    for k in range(6):
        j = (-1) ** k
        out.append((living('phong', k / 6, shake=10 * j, shake_y=3 * j, heat=1.0 if k % 2 else 0.2, energy=1.8 if k % 2 else 0.3,
                           eyes=2.0 if k % 2 else 0.1, lean=4 * j, sway=2.0,
                           front=[bolts(k / 6, seed=k + 2, amount=1.4), burst(0.2 + k * 0.12, *at('phong', 'gem'), seed=k, radius=320)]), 55))
    for k in range(4):
        out.append((living('phong', 0.3, breathe=-6, squash=0.96, lean=-6, dim=0.5, energy=0.1, eyes=0.0, sway=0.1,
                           front=[smoke_up(k / 6, x=0, y=-900, size=1.4)]), (80, 140, 220, 260)[k]))
    for k, (e, g) in enumerate(((1.5, 0.3), (0.0, 0.1), (2.0, 1.2), (0.4, 0.6), (1.2, 1.0))):
        u = (k + 1) / 5
        out.append((living('phong', 0.3 + u * 0.3, breathe=lerp(-6, 2, u), squash=lerp(0.96, 1.0, u), lean=lerp(-6, 0, u),
                           dim=lerp(0.5, 0, u), energy=g, eyes=e, front=[smoke_up(0.6 + k / 10, x=0, y=-900, size=1.4)]),
                    (80, 70, 90, 90, 160)[k]))
    return out


def standby_mode(phase):
    sleep = dict(eyes=0.0, energy=0.35, dim=0.2, sway=0.2, lean=-3)
    if phase == 'start':
        return [(living('phong', i / 6, eyes=lerp(1, 0, (i + 1) / 6), energy=lerp(1, 0.35, (i + 1) / 6), dim=0.2 * (i + 1) / 6,
                        breathe=2 - i, lean=-0.5 * i), (90, 100, 110, 130, 150, 180)[i]) for i in range(6)]
    if phase == 'loop':
        return loop(16, 150, lambda t, i: living('phong', t * 0.5, breathe=1 + 2 * math.sin(2 * math.pi * t), **sleep,
                                                 front=[standby(t)]))
    out = [(living('phong', 0, **{**sleep, 'eyes': 2.4, 'energy': 1.8}), 90), (living('phong', 0, **sleep), 60)]
    out += [(living('phong', 0.1 * k, eyes=2.0 - 0.2 * k, energy=1.6 - 0.12 * k, lift=10 * hop(k / 4), sway=1.4 - 0.2 * k,
                    front=[plasma_vent(0.3 + k * 0.15, None, color=CYAN, power=0.4)]), 80) for k in range(5)]
    return out


def held(phase):
    top = 640
    if phase == 'start':
        return [(living('phong', k / 4, lift=12 * k, rot=2 * k, pivot=top, squash=1 + 0.012 * k, eyes=1.6, shadow=False, sway=1.4), 60)
                for k in (1, 2, 3, 4)]
    if phase == 'loop':
        return loop(12, 80, lambda t, i: living('phong', t * 2, lift=48, rot=9 * math.sin(2 * math.pi * t), pivot=top, squash=1.05,
                                                eyes=1.8 if i % 6 else 0.2, sway=1.8, shadow=False, lean=-6 * math.sin(2 * math.pi * t)))
    return [(living('phong', 0, lift=20, squash=1.04, eyes=1.8), 60),
            (living('phong', 0.1, squash=0.9, eyes=2.0, front=[shockwave(0.3, size=0.7)]), 80),
            (living('phong', 0.2, squash=1.03, front=[shockwave(0.6, size=0.7)]), 70), (living('phong', 0.3), 100)]


def pat(phase):
    if phase == 'start':
        return [(living('phong', k / 3, squash=1 - 0.02 * k, eyes=1 + 0.3 * k, energy=1 + 0.15 * k), 60) for k in (1, 2, 3)]
    if phase == 'loop':
        return loop(10, 90, lambda t, i: living('phong', t, squash=0.95 + 0.02 * osc(t, 0.5), eyes=1.9, energy=1.5,
                                                lean=4 * math.sin(2 * math.pi * t), front=[holo_heart(t, size=0.55, at_hand=0)]))
    return [(living('phong', 0, squash=1.03, eyes=1.6), 70), (living('phong', 0.2, squash=0.99), 70), (living('phong', 0.4), 90)]


def poke(phase):
    gem = at('phong', 'gem')
    if phase == 'start':
        return [(living('phong', 0, energy=2.2, eyes=2.0, squash=0.96, shake=-8, front=[burst(0.2, *gem, radius=160, count=8)]), 60),
                (living('phong', 0.1, energy=0.6, squash=1.04, shake=6), 60), (living('phong', 0.2, energy=1.6, eyes=1.8), 70)]
    if phase == 'loop':
        return loop(10, 75, lambda t, i: living('phong', t * 2, shake=6 * math.sin(2 * math.pi * t * 4), lean=4 * math.sin(2 * math.pi * t * 2),
                                                energy=1.0 + 0.8 * (i % 2), eyes=1.8, sway=1.6,
                                                front=[sparks(t, *gem, seed=9, count=6, speed=0.7)]))
    return [(living('phong', 0, squash=1.03, eyes=1.6), 70), (living('phong', 0.2, squash=0.99), 70), (living('phong', 0.4), 90)]


def peek(phase):
    def pose(out_amt, i=0):
        return living('phong', i / 8, rot=8 * out_amt, pivot=0, x=-70 - 80 * (1 - out_amt), eyes=1 + 0.8 * out_amt,
                      front=[eye_scan(i / 8, 0.6 * math.sin(i))] if out_amt > 0.9 else [])
    if phase == 'start':
        return [(pose(t), ms) for t, ms in ((0.4, 60), (0.8, 70), (1.0, 100))]
    if phase == 'loop':
        plan = [(1.0, 240), (1.0, 200), (1.0, 260), (0.3, 90), (0.0, 220), (0.6, 90), (1.0, 300), (1.0, 220)]
        return [(pose(a, i), ms) for i, (a, ms) in enumerate(plan)]
    return [(pose(1 - t), ms) for t, ms in ((0.4, 70), (0.8, 70), (1.0, 100))]


# Tứ Thủ: working, anger --------------------------------------------------------------------------------

def berserk(t, **kw):
    return living('tu', t, **{'eyes': 1.7 + 0.2 * osc(t * 2), 'energy': 1.25, 'sway': 1.0, **kw})


def flurry(variant='claws'):
    """Working loops in Tứ Thủ. 'claws': the four arms slash in turn (crescents from each claw, lunges, sparks,
    hit-stop on every hit). 'sword': the Đại Trảm Đao is scanned into being and swung in a huge arc. 'roar':
    the plume flares, a roar ring, flame."""
    out = []
    if variant == 'claws':
        # Four slashes in turn, one per arm, crossing in front of the suit: wind-up, strike with hit-stop, recover.
        cuts = [((-620, -1150), (520, -250), -1), ((620, -1150), (-520, -250), 1), ((-700, -620), (680, -520), -1),
                ((700, -820), (-640, -300), 1)]
        for k, (a0, a1, side) in enumerate(cuts):
            color = mix(VIOLET_LIGHT, RED_LIGHT, 0.25 + 0.2 * (k % 2))
            for j, (ms, hs, lean, shake) in enumerate(((70, 0.0, -5, 0), (40, 0.3, 10, 0), (90, 0.55, 8, 9), (60, 0.85, 3, 0))):
                props = [] if j == 0 else [streak(hs, *a0, *a1, bulge=0.12 * side, width=110, color=color),
                                           sparks(hs, (a0[0] + a1[0]) / 2, (a0[1] + a1[1]) / 2, seed=k * 4 + j, count=12, speed=1.3)]
                out.append((berserk(k / 4 + j / 16, x=side * (12 if j == 1 else 4), lean=side * lean, shake=shake * side,
                                    squash=0.98 if j == 1 else 1.0, front=props), ms))
        return out
    if variant == 'sword':
        hand = at('tu', 'hands', 1)
        grip = (hand[0] - 40, hand[1] - 40)
        for j in range(8):
            u = (j + 1) / 8
            out.append((berserk(u * 0.4, eyes=1.9, front=[greatsword(*grip, 100, materialise=u, light=0.8),
                                                          charge(u, grip[0] + 200, grip[1] - 200, color=VIOLET_LIGHT, radius=320)]), 70))
        swings = ((100, 120, 0), (82, 70, 0), (50, 45, 4), (18, 45, 8), (-12, 50, 12), (-28, 70, 6), (-32, 160, 0))
        for j, (ang, ms, shake) in enumerate(swings):
            props = [greatsword(*grip, ang, light=1.0)]
            if j >= 2:
                props.append(slash(min(1.3, (j - 2) / 3), grip[0], grip[1], 860, 280, 180 + ang, width=180, color=VIOLET_LIGHT))
            if j >= 4:
                props += [shockwave((j - 4) / 3, x=-560, size=0.9, color=VIOLET_LIGHT), debris((j - 4) / 3, -560, 0, seed=2),
                          cracks((j - 4) / 1.5, -560, 0)]
            out.append((berserk(0.4 + j / 15, lean=-8 * math.sin(math.pi * j / 6), shake=shake * (-1) ** j, front=props), ms))
        for j in range(5):
            u = (j + 1) / 5
            out.append((berserk(0.85 + u * 0.15, front=[greatsword(*grip, -32, materialise=1 - u, light=0.6)]), 70))
        return out
    head = at('tu', 'head')
    plume = at('tu', 'plume')
    for j in range(12):
        u = j / 11
        out.append((berserk(u, heat=0.8 * math.sin(math.pi * u), eyes=2.4, energy=1.6, sway=2.0, breathe=6 * math.sin(math.pi * u),
                            shake=8 * math.sin(math.pi * u) * (-1) ** j,
                            front=[roar(u, head[0], head[1]), flames(u, plume[0], plume[1], size=1.5, seed=3)]), 80))
    return out


def work(phase):
    return flurry('claws')

def scan_to_work():
    out = [(living('phong', i / 3, eyes=1.7, front=[data_panel(0, open_=u)] if u else []), 60) for i, u in enumerate((0.7, 0.35, 0.0))]
    return out + transform('phong', 'tu', speed=2.0)

def work_to_scan():
    out = transform('tu', 'phong', speed=2.0)
    return out + [(living('phong', i / 4, eyes=1.6, front=[data_panel(0, open_=u)]), (60, 60, 70, 100)[i])
                  for i, u in enumerate((0.4, 0.8, 1.06, 1.0))]

def rage(leave='dash'):
    """Angry: a fast overheated transformation to Tứ Thủ, a roar in flames, then a dash off-screen with
    afterimages, or a blast straight up in fire."""
    out = transform('phong', 'tu', speed=2.0)
    out += [(replace(p, heat=0.9), ms) for p, ms in flurry('roar')[:9]]
    if leave == 'dash':
        for k, x in enumerate((0, 60, 220, 460, 760, 1100)):
            out.append((berserk(k / 6, x=x, rot=6, heat=0.8, ghosts=((-120, 0.4), (-240, 0.25), (-360, 0.12)) if k > 1 else (),
                                front=[dust(k / 6, x=-200, side=-1)]), 50))
    else:
        for k, lift in enumerate((40, 140, 330, 620, 1000, 1400)):
            out.append((berserk(k / 6, lift=lift, squash=1.08, heat=1.0, ghosts=((0, 0.35, -140), (0, 0.18, -280)) if k else (),
                                front=[flames(k / 6, 0, 80, size=2.0, seed=k, spread=160)]), 50))
    out.append((replace(out[-1][0], alpha=0), 100))
    return out


# Hắc Tháp: attention, quota, defence --------------------------------------------------------------------

def fortress(t, **kw):
    return living('thap', t, **{'eyes': 1.5, 'energy': 1.2, 'sway': 0.3, **kw})


def signal_loop():
    """Needs input, dug in as Hắc Tháp: the cannons charge and pulse, a red beacon overhead."""
    cannons = at('thap', 'cannons')
    return loop(10, 90, lambda t, i: fortress(t, energy=1.3 + 0.5 * osc(t * 2), eyes=1.8,
                                              front=[beacon(t)] + [charge(t, cx, cy - 40, color=VIOLET_LIGHT, radius=200, seed=j, count=10)
                                                                   for j, (cx, cy) in enumerate(cannons)]))

def power_down():
    """Out of quota: Phong Lôi sagging on its feet with the power gone: dark, smoke from a vent, a red cell blinking."""
    vent = at('phong', 'vents', 0)
    return loop(14, 140, lambda t, i: living('phong', t * 0.5, breathe=-3 + osc(t), lean=-5, squash=0.985, dim=0.45,
                                             energy=0.25 + (0.5 if i == 9 else 0), eyes=0.15 + (0.8 if i == 9 else 0), sway=0.15,
                                             front=[low_power(i % 7 < 4), smoke_up(t, x=vent[0], y=vent[1], amount=2, size=0.7)]))
def barrage():
    """Hắc Tháp's artillery: both cannons fire volleys that land far off, the frame kicking with each shot."""
    cannons = at('thap', 'cannons')
    spots = [(-900, 40), (850, 30), (-600, 60), (700, 50), (-1000, 20), (980, 40)]
    out = []
    for j in range(18):
        u = j / 17
        kick = 6 if j % 3 == 0 else 0
        out.append((fortress(u, shake=kick * (-1) ** j, shake_y=kick * 0.5, squash=0.985 if kick else 1.0, eyes=1.9,
                             front=[shells(u, cannons, seed=4, count=8), explosions(max(0.0, u * 1.3 - 0.3), spots, seed=2)]), 70))
    return out

def shield_up():
    """Danger, dug in as Hắc Tháp: the barrier dome is thrown up, holds with a ripple, and drops."""
    out = loop(10, 90, lambda t, i: fortress(t, eyes=2.0, front=[shield_dome(t, alpha=min(1.0, (i + 1) / 3))]))
    return out + [(fortress(0.1 * k, front=[shield_dome(1, alpha=1 - (k + 1) / 4)]), 70) for k in range(4)]

def flight(t, angle=0.0, **kw):
    base = dict(lift=HOVER + 14 * math.sin(2 * math.pi * t), rot=-angle, pivot=300, eyes=1.6, energy=1.4, sway=1.6, flutter=t * 2,
                front=[jets(t)], back=[speed_lines(0.6, y0=-700, y1=-150)])
    base.update(kw)
    return living('hoa', t, **base)

def jets(t, power=1.0):
    """Hỏa Dực's thrusters: cyan exhaust flames streaming back from each nozzle, flickering."""
    def draw(ctx, p):
        r = rig(p)
        for k, (x, y) in enumerate(r['jets']):
            L = (400 + 90 * math.sin(t * 2 * math.pi * 4 + k * 1.7)) * power * (1.2 if k == 2 else 1.0)
            ang = math.radians(12 + 10 * k)
            ux, uy = math.cos(ang), math.sin(ang)
            nx, ny = -uy, ux
            glow(ctx, x + ux * L * 0.4, y + uy * L * 0.4, L * 0.6, CYAN, 0.45 * power)
            for w, c, a, f in ((44, CYAN, 0.6, 1.0), (24, mix(CYAN, WHITE, 0.5), 0.85, 0.75), (10, WHITE, 1.0, 0.45)):
                tx, ty = x + ux * L * f, y + uy * L * f
                ctx.move_to(x + nx * w, y + ny * w)
                ctx.curve_to(x + nx * w * 0.7 + ux * L * 0.5, y + ny * w * 0.7 + uy * L * 0.5, tx, ty, tx, ty)
                ctx.curve_to(tx, ty, x - nx * w * 0.7 + ux * L * 0.5, y - ny * w * 0.7 + uy * L * 0.5, x - nx * w, y - ny * w)
                ctx.close_path()
                rgb(ctx, c, a * power)
                ctx.fill()
    return draw

def fly(angle=0.0):
    """Cruising as Hỏa Dực (facing left, as drawn), tilted by `angle`."""
    return loop(8, 70, lambda t, i: flight(t, angle, ghosts=((120, 0.2),)))

def landing():
    """Starting: Hỏa Dực streaks down from the top with afterimages, brakes on its jets, lands and transforms
    into Phong Lôi."""
    out = []
    for k, (x, lift) in enumerate(((620, 1100), (480, 860), (330, 640), (200, 450), (100, 330), (40, 240), (10, 180))):
        out.append((flight(k / 6, x=x, lift=lift, angle=-25 + 4 * k, ghosts=((90, 0.3, 120), (180, 0.15, 240)) if k < 5 else ()), 50))
    out += [(flight(0.9, lift=HOVER, angle=6), 60), (flight(1.0, lift=HOVER, angle=0), 60)]
    return out + transform('hoa', 'phong', speed=1.4, lift0=HOVER, lift1=0)

def depart():
    out = transform('phong', 'hoa', speed=1.4, lift0=0, lift1=HOVER)
    for k, (x, lift) in enumerate(((0, 200), (-40, 280), (-140, 420), (-300, 600), (-520, 820), (-800, 1080), (-1150, 1400))):
        out.append((flight(k / 6, x=x, lift=lift, angle=-18 - 3 * k, ghosts=((90, 0.3, -100), (180, 0.15, -200)) if k > 1 else ()), 55))
    out.append((replace(out[-1][0], alpha=0), 100))
    return out

def railgun():
    """Đại Pháo Ray, hovering as Hỏa Dực: the arm cannon charges with gathering light, fires a beam across the
    screen with recoil and shake, and cools with smoke."""
    muzzle = at('hoa', 'tip')
    out = []
    for j in range(10):
        u = (j + 1) / 10
        out.append((flight(u * 0.5, angle=0, back=[], eyes=1.6 + 0.6 * u, energy=1.4 + 0.6 * u,
                           front=[jets(u, 0.6), charge(u, muzzle[0] - 30, muzzle[1], color=CYAN, radius=420)]), 70))
    for j in range(8):
        u = j / 7
        out.append((flight(0.5 + u * 0.3, angle=0, back=[], x=(24 if j < 2 else 24 * (1 - u)), shake=(12 * (1 - u)) * (-1) ** j,
                           eyes=2.2, energy=2.0 - u, front=[jets(u, 0.6), beam(u * 0.95 + 0.03, muzzle[0] - 20, muzzle[1], angle=180)],
                           over=[flash(0.5 * (1 - u) ** 2, CYAN)] if j < 2 else []), (40, 40, 50, 60, 70, 80, 90, 110)[j]))
    return out + [(flight(0.8 + 0.05 * k, angle=0, back=[], front=[jets(0.5, 0.6), smoke_up(k / 5, x=muzzle[0] + 40, y=muzzle[1], amount=3, size=0.8)]), 90)
                  for k in range(5)]

def tumble(phase):
    """Thrown: Phong Lôi spins with afterimages and snaps into Hỏa Dực mid-air; it hovers on its jets; it lands
    through the Hỏa Dực-to-Phong Lôi transformation."""
    if phase == 'start':
        out = [(living('phong', k / 5, rot=ang, pivot=330, lift=lerp(60, HOVER, k / 5), eyes=2.0, shadow=False, sway=2.0,
                       ghosts=((-60, 0.3), (-120, 0.15)) if k < 4 else ()), 55) for k, ang in enumerate((60, 140, 220, 300, 360))]
        snap = [(P(form='phong', morph_to='hoa', morph=u, lift=HOVER, shadow=False), 45) for u in (0.2, 0.4, 0.6, 0.8, 1.0)]
        return out + snap
    return loop(10, 70, lambda t, i: flight(t, angle=6 * osc(t), shadow=False))

def dash(phase):
    """Moves (facing right): a low Phong Lôi charge leaning into the run, afterimages, dust, a skidding stop."""
    if phase == 'start':
        return [(living('phong', 0, squash=0.95, breathe=-4, eyes=1.6), 60), (living('phong', 0.1, rot=7, pivot=200, eyes=1.8), 60)]
    if phase == 'loop':
        return loop(6, 60, lambda t, i: living('phong', t * 2, rot=8, pivot=200, lift=4 + 3 * osc(t, 0.5), eyes=1.8, sway=1.8,
                                               ghosts=((-110, 0.4), (-220, 0.22), (-330, 0.1)),
                                               front=[dust(t, x=-200, side=-1)], back=[speed_lines(0.9, y0=-800, y1=-120)]))
    return [(living('phong', 0.1, rot=-6, pivot=200, front=[dust(0.2, x=100, side=1)]), 70),
            (living('phong', 0.2, rot=-3, pivot=200, squash=0.95, front=[dust(0.5, x=120, side=1)]), 80),
            (living('phong', 0.3, squash=1.01), 90), (living('phong', 0.4), 110)]

def thunder_strike():
    """Phong Lôi Kích: wind gathers round the suit, the sky answers with a bolt into the spear's blade, the suit
    lunges with the charged spear and the bolt bursts along the ground; aftershock crackle."""
    tip, gem = at('phong', 'tip'), at('phong', 'gem')
    out = []
    for j in range(8):
        u = (j + 1) / 8
        out.append((living('phong', u * 0.5, breathe=-3 * u, squash=1 - 0.03 * u, eyes=1.2 + u, energy=1 + 0.8 * u, sway=1.6,
                           front=[charge(u, gem[0], gem[1], color=CYAN, radius=560, seed=2), bolts(u, seed=j, amount=0.8 * u)],
                           back=[spin_smear(0, 300 * u, (0, -520), radius=600, width=160, color=CYAN)]), 70))
    for j in range(3):
        out.append((living('phong', 0.6, eyes=2.4, energy=2.4, shake=10 * (-1) ** j, front=[lightning(0.1 + 0.25 * j, tip[0] + 80, -2200, tip[0], tip[1], seed=4, width=1.4)],
                           over=[flash(0.7 - 0.2 * j, CYAN)]), 50))
    for j, (x, lean, ms) in enumerate(((-30, -10, 45), (-70, -14, 50), (-60, -12, 90), (-40, -8, 120))):
        out.append((living('phong', 0.7, x=x, lean=lean, eyes=2.2, energy=2.0 - 0.2 * j, ghosts=((40, 0.35), (80, 0.18)) if j == 0 else (),
                           front=[lightning(0.2 + 0.2 * j, tip[0], tip[1], tip[0] - 1200, tip[1] + 20, seed=7, width=1.2),
                                  shockwave(0.2 + 0.2 * j, x=tip[0], y=0, size=0.9, color=CYAN)]), ms))
    out += [(living('phong', 0.8 + 0.05 * k, x=-40 * (1 - (k + 1) / 5), eyes=1.6, energy=1.4 - 0.08 * k, front=[bolts(k / 5, seed=k + 9, amount=0.6)]), 90)
            for k in range(5)]
    return out


def sword_summon():
    """Đại Trảm Đao in Phong Lôi's hand: scanned into being beside it, swung up over the crest and down in a
    cleave to the left with a shockwave, cracks and debris, then dissolved back into light."""
    hand = at('phong', 'hands', 1)
    grip = (hand[0] - 30, hand[1])
    out = []
    for j in range(8):
        u = (j + 1) / 8
        out.append((living('phong', u * 0.4, eyes=1.4 + 0.5 * u, energy=1.2 + 0.4 * u,
                           front=[greatsword(*grip, 100, materialise=u, light=0.8), charge(u, grip[0] + 200, grip[1] - 200, color=VIOLET_LIGHT, radius=320)]), 70))
    swings = ((100, 140, 0), (82, 70, 0), (50, 45, 4), (18, 45, 8), (-12, 50, 12), (-28, 70, 6), (-32, 160, 0))
    for j, (ang, ms, shake) in enumerate(swings):
        props = [greatsword(*grip, ang, light=1.0)]
        if j >= 2:
            props.append(slash(min(1.3, (j - 2) / 3), grip[0], grip[1], 860, 280, 180 + ang, width=190, color=VIOLET_LIGHT))
        if j >= 4:
            props += [shockwave((j - 4) / 3, x=-560, size=1.0, color=VIOLET_LIGHT), debris((j - 4) / 3, -560, 0, seed=2), cracks((j - 4) / 1.5, -560, 0)]
        out.append((living('phong', 0.4 + j / 15, lean=-10 * math.sin(math.pi * j / 6), breathe=-4 * j / 6, shake=shake * (-1) ** j,
                           eyes=2.0, energy=1.6, front=props), ms))
    out += [(living('phong', 0.85 + u * 0.15, front=[greatsword(*grip, -32, materialise=1 - u, light=0.6)]), 70)
            for u in (0.2, 0.4, 0.6, 0.8, 1.0)]
    return out

def claw_flurry():
    """Tứ Thủ Loạn Trảm: the four-arm slash combo, then a roar in flames."""
    return flurry('claws') + flurry('roar')

def form_cycle():
    """The arsenal at a glance: the plates fly from form to form, Phong Lôi to Hỏa Dực to Tứ Thủ to Hắc Tháp and
    back, each form held for a beat with its own light."""
    out = []
    chain = ('phong', 'hoa', 'tu', 'thap', 'phong')
    for a, b in zip(chain, chain[1:]):
        la = HOVER if a == 'hoa' else 0
        lb = HOVER if b == 'hoa' else 0
        out += transform(a, b, speed=2.2, lift0=la, lift1=lb)
        hold = flight if b == 'hoa' else (berserk if b == 'tu' else (fortress if b == 'thap' else (lambda t, **k: living('phong', t, **k))))
        out += [(hold(j / 3, back=[]) if b == 'hoa' else hold(j / 3), (90, 110, 140)[j]) for j in range(3)]
    return out


def victory():
    """Turn finished: Phong Lôi raises its power, lightning strikes the spear twice, a jump with fireworks, a
    landing shock; the lights flare and settle."""
    tip = at('phong', 'tip')
    out = [(living('phong', 0.1 * j, breathe=-2 * j, squash=1 - 0.012 * j, eyes=1.3 + 0.2 * j, energy=1.2 + 0.2 * j), 70) for j in range(4)]
    for j in range(6):
        out.append((living('phong', 0.5, eyes=2.4, energy=2.2, lift=60 * hop(j / 5), shake=8 * (-1) ** j if j < 3 else 0,
                           front=[lightning(0.1 + 0.3 * (j % 3), tip[0] + 100 - 200 * (j // 3), -2200, tip[0], tip[1], seed=j // 3, width=1.3)],
                           over=[fireworks(j / 12, seed=11), flash(0.5 if j % 3 == 0 else 0, CYAN)]), 60))
    for j in range(6):
        t = (j + 1) / 7
        out.append((living('phong', 0.6 + t * 0.4, squash=lerp(0.92, 1.0, ease(t)), eyes=2.0 - 0.6 * t, energy=1.8 - 0.5 * t,
                           back=[shockwave(t, size=1.2, color=CYAN)], front=[cracks(t * 2)], over=[fireworks(0.5 + t * 0.45, seed=11)]),
                    (50, 60, 70, 90, 110, 160)[j]))
    return out


def vent():
    out = []
    for i in range(12):
        t = i / 11
        h = hop(t)
        out.append((living('phong', t, breathe=4 * h - 2 * max(0.0, t - 0.5), eyes=lerp(1.0, 0.5, h), sway=0.4,
                           front=[plasma_vent(min(0.99, max(0.01, (t - 0.25) * 1.4)), None, color=STEAM, seed=6, power=0.8)]), 110))
    return out


def coolant():
    return loop(14, 110, lambda t, i: living('phong', t, eyes=lerp(1.0, 0.4, hop(t)), breathe=2 * hop(t),
                                             front=[vents(t * 2 % 1, 1.0), vents((t * 2 + 0.5) % 1, 0.8)]))


def refuel():
    out = []
    gem = at('phong', 'gem')
    start = (-520, -260)
    for i in range(7):
        t = (i + 1) / 7
        u = ease(t)
        pos = (lerp(start[0], gem[0], u), lerp(start[1], gem[1], u) - 160 * math.sin(math.pi * u))
        out.append((living('phong', t * 0.3, eyes=1.2 + 0.4 * u, front=[energy_cell(pos[0], pos[1], -40 + 40 * u, size=min(1.0, t * 2) * (1 - 0.8 * (t > 0.9)))]), 80))
    for i in range(8):
        t = i / 7
        out.append((living('phong', 0.3 + t * 0.5, energy=2.2 - t, eyes=1.9 - 0.6 * t, lift=14 * math.sin(t * math.pi), sway=1.4,
                           front=[burst(t, gem[0], gem[1], seed=4, radius=300, count=14, color=CYAN)]), 90))
    return out


def love():
    out = []
    n = 16
    for i in range(n):
        t = i / (n - 1)
        size = back_out(min(1.0, t * 2.5)) * (1 - ease(max(0.0, (t - 0.8) / 0.2)))
        out.append((living('phong', t, eyes=1.8, energy=1.4, front=[holo_heart(t, size=size, at_hand=0)]), 100))
    return out


def birthday():
    """In Tứ Thủ: bouncing with all four arms up, confetti and fireworks."""
    return loop(14, 110, lambda t, i: berserk(t, lift=10 * max(0.0, osc(t, 0.5)), heat=0.3, front=[confetti_burst(min(1.0, t * 1.1))],
                                              over=[fireworks(t, seed=21, bursts=5)]))

def bow():
    """Reminder done: a small bow of the head (the upper body dips), the visor softening, a sparkle."""
    out = []
    for i in range(10):
        t = i / 9
        h = hop(t)
        out.append((living('phong', t, breathe=-5 * h, lean=0, eyes=lerp(1.0, 0.5, h), squash=1 - 0.015 * h,
                           front=[twinkles(t, [(-200, -1000), (220, -980)], seed=5, color=CYAN)] if t > 0.5 else []), 110))
    return out


def calibrate():
    """Eye break: a systems check of every form under a diagnostic sweep: Phong Lôi, Hỏa Dực, Tứ Thủ, Hắc Tháp,
    each scanned and ticked off with a flash, then back."""
    out = []
    order = ('phong', 'hoa', 'tu', 'thap', 'phong')
    for k, form in enumerate(order):
        lift = 180 if form == 'hoa' else 0
        for i in range(4):
            t = (i + 1) / 4
            out.append((living(form, t, lift=lift, eyes=1.7, energy=1.3 + 0.4 * (i == 3), front=[scan_grid(t)] if i < 3 else [],
                               over=[flash(0.25, CYAN)] if i == 0 and k else []), 90 if i < 3 else 160))
    return out


def war_drum():
    """Friday evening, dug in as Hắc Tháp: the anchors pound the ground slow, slow, quick-quick; a shock ring and
    a flash of the lights on every hit."""
    beats = (0, 4, 8, 10, 12)
    spike = at('thap', 'spike')
    out = []
    for i in range(16):
        t = i / 16
        hit = i in beats
        up = (i + 1) % 16 in beats
        out.append((fortress(t, lift=14 if up else 0, squash=0.95 if hit else 1.0, energy=1.8 if hit else 1.1, eyes=1.9 if hit else 1.3,
                             shake=5 if hit else 0, front=[shockwave(0.25, x=spike[0], size=0.6, dust=False), cracks(0.4, spike[0], 0)] if hit else []), 100))
    return out

def ponder():
    """A fidget: the suit settles its weight, the visor scans the ground, a question mark."""
    return loop(12, 120, lambda t, i: living('phong', t, eyes=1.6, lean=-4 * math.sin(2 * math.pi * t),
                                             front=[eye_scan(t, math.sin(2 * math.pi * t)), question(t)] if 2 <= i < 10 else []))


SEQUENCES = {
    # Phong Lôi, the standing form.
    'idle': lambda: idle(),
    'idle_thunder': lambda: idle('thunder'),
    'idle_vent': lambda: idle('vent'),
    'idle_scan': lambda: idle('scan'),
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
    'scan/to_work': scan_to_work,
    'short_circuit': short_circuit,
    'victory': victory,
    'standby/start': lambda: standby_mode('start'),
    'standby/loop': lambda: standby_mode('loop'),
    'standby/end': lambda: standby_mode('end'),
    'held/start': lambda: held('start'),
    'held/loop': lambda: held('loop'),
    'held/end': lambda: held('end'),
    'pat/start': lambda: pat('start'),
    'pat/loop': lambda: pat('loop'),
    'pat/end': lambda: pat('end'),
    'poke/start': lambda: poke('start'),
    'poke/loop': lambda: poke('loop'),
    'poke/end': lambda: poke('end'),
    'peek/start_left': lambda: peek('start'),
    'peek/left': lambda: peek('loop'),
    'peek/end_left': lambda: peek('end'),
    'peek/start_right': lambda: mirrored(peek('start')),
    'peek/right': lambda: mirrored(peek('loop')),
    'peek/end_right': lambda: mirrored(peek('end')),
    'look': look_around,
    'ponder': ponder,
    'thunder_strike': thunder_strike,
    'sword_summon': sword_summon,
    'vent': vent,
    'coolant': coolant,
    'refuel': refuel,
    'love': love,
    'bow': bow,
    'calibrate': calibrate,
    'form_cycle': form_cycle,
    'dash/start_right': lambda: dash('start'),
    'dash/right': lambda: dash('loop'),
    'dash/end_right': lambda: dash('end'),
    'dash/start_left': lambda: mirrored(dash('start')),
    'dash/left': lambda: mirrored(dash('loop')),
    'dash/end_left': lambda: mirrored(dash('end')),
    # The transformations, shared by every state that changes form.
    'tf/phong_tu': lambda: transform('phong', 'tu'),
    'tf/tu_phong': lambda: transform('tu', 'phong'),
    'tf/phong_thap': lambda: transform('phong', 'thap'),
    'tf/thap_phong': lambda: transform('thap', 'phong'),
    'tf/phong_hoa_left': lambda: transform('phong', 'hoa', lift0=0, lift1=HOVER),
    'tf/hoa_phong_left': lambda: transform('hoa', 'phong', lift0=HOVER, lift1=0),
    'tf/phong_hoa_right': lambda: mirrored(transform('phong', 'hoa', lift0=0, lift1=HOVER)),
    'tf/hoa_phong_right': lambda: mirrored(transform('hoa', 'phong', lift0=HOVER, lift1=0)),
    # Tứ Thủ.
    'tu/claws': lambda: flurry('claws'),
    'tu/sword': lambda: flurry('sword'),
    'tu/roar': lambda: flurry('roar'),
    'tu/flurry': claw_flurry,
    'tu/party': birthday,
    'work/to_scan': work_to_scan,
    'rage': rage,
    'rage_blast': lambda: rage('blast'),
    # Hắc Tháp.
    'thap/signal': signal_loop,
    'thap/barrage': barrage,
    'thap/shield': shield_up,
    'thap/drum': war_drum,
    'power_down': power_down,
    # Hỏa Dực: drawn flying to the left, so the left moves are the drawing and the right ones mirror it.
    'hoa/railgun': railgun,
    'landing': landing,
    'depart': depart,
    'fly/left': lambda: fly(),
    'fly/rise_left': lambda: fly(-14),
    'fly/dive_left': lambda: fly(14),
    'fly/right': lambda: mirrored(fly()),
    'fly/rise_right': lambda: mirrored(fly(-14)),
    'fly/dive_right': lambda: mirrored(fly(14)),
    'tumble/start_right': lambda: tumble('start'),
    'tumble/right': lambda: tumble('loop'),
    'tumble/start_left': lambda: mirrored(tumble('start')),
    'tumble/left': lambda: mirrored(tumble('loop')),
}

if __name__ == '__main__':
    run(SEQUENCES, render, __doc__)
