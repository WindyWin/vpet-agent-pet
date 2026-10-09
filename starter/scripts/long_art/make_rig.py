#!/usr/bin/env python3
"""Cut Long's standing pose (pose1.png) into the parts of a cutout rig, the way Spine or Live2D would.

    pip install pillow numpy scipy opencv-python-headless
    python3 scripts/long_art/make_rig.py          # writes scripts/long_art/rig/*.png and rig.json

Every part is the designer's own pixels inside a polygon (drawing coordinates of pose1.png), on a canvas the size
of the pose so all parts share one coordinate system. A pixel goes to the first part in PARTS whose polygon (and,
for the tendrils, colour test) takes it. What a part hid on the body behind it is inpainted on the body, so a
moving arm, head or leg never leaves a hole. The pivots are the joints the rig turns the parts about.
"""
import json
from pathlib import Path

import cv2
import numpy as np
from PIL import Image
from scipy import ndimage

HERE = Path(__file__).resolve().parent

# Draw order is the reverse of claim order: claims run front to back.
PARTS = [
    ('head', [(100, 160), (150, 140), (163, 126), (178, 145), (233, 154), (226, 185), (213, 205), (213, 240),
              (196, 257), (165, 265), (140, 259), (122, 241), (112, 225), (106, 196)]),
    ('arm_l', [(22, 313), (55, 307), (78, 299), (98, 291), (118, 295), (121, 322), (113, 348), (101, 363), (24, 365)]),
    ('spear', [(3, 0), (83, 0), (83, 200), (64, 205), (60, 262), (57, 300), (55, 470), (64, 470), (62, 530), (58, 583),
               (27, 583), (27, 470), (30, 300), (3, 210)]),
    ('arm_r', [(212, 297), (252, 293), (271, 301), (275, 346), (273, 380), (271, 415), (250, 421), (225, 417),
               (219, 380), (215, 340)]),
    ('boot_l', [(60, 504), (149, 504), (147, 574), (58, 574)]),
    ('boot_r', [(173, 507), (252, 507), (258, 583), (177, 583)]),
    ('shin_l', [(92, 429), (155, 427), (151, 470), (149, 504), (88, 504), (62, 524), (55, 498), (65, 448), (79, 424)]),
    ('shin_r', [(163, 437), (229, 433), (237, 418), (263, 468), (284, 547), (262, 547), (247, 520), (241, 507),
                (176, 507), (169, 470)]),
    ('thigh_l', [(96, 365), (155, 365), (155, 431), (96, 433)]),
    ('thigh_r', [(162, 365), (223, 365), (225, 439), (165, 441)]),
    # Tendrils: everything left beside the torso (ink outlines too), and red lacquer only where they cross it.
    ('tendrils_l', [(40, 292), (110, 292), (110, 338), (99, 346), (97, 460), (40, 460)]),
    ('tendrils_l', [(40, 292), (134, 292), (134, 460), (40, 460)]),
    ('tendrils_r', [(250, 302), (295, 302), (295, 482), (250, 482)]),
    ('tendrils_r', [(240, 280), (295, 280), (295, 482), (240, 482)]),
]
PIVOTS = {
    'head': (160, 258), 'arm_l': (108, 300), 'arm_r': (236, 300), 'fist_l': (43, 337),
    'hip_l': (126, 372), 'hip_r': (192, 372), 'knee_l': (124, 432), 'knee_r': (196, 440),
    'ankle_l': (112, 508), 'ankle_r': (212, 511), 'hips': (160, 372),
    'tendrils_l': (114, 300), 'tendrils_r': (250, 292),
}


def main():
    im = np.asarray(Image.open(HERE / 'pose1.png').convert('RGBA'))
    rgb, alpha = im[..., :3].astype(np.float32) / 255, im[..., 3]
    H, W = alpha.shape
    r, g, b = rgb[..., 0], rgb[..., 1], rgb[..., 2]
    # Red lacquer, shadowed red included; gold is too green to pass.
    red = (r > 0.12) & (g < r * 0.62) & (b < r * 0.75)
    red = ndimage.binary_closing(red, iterations=2) | ndimage.binary_dilation(red, iterations=1)
    free = alpha > 0
    owner = {}
    for name, polygon in PARTS:
        mask = np.zeros((H, W), np.uint8)
        cv2.fillPoly(mask, [np.array(polygon, np.int32)], 1)
        take = (mask > 0) & free
        if name.startswith('tendrils') and name in owner:
            take &= red
        if name in ('arm_l', 'arm_r'):
            take &= ~red          # red lacquer there is a tendril passing behind
        if name == 'spear':
            take &= ~(red & (np.arange(W)[None, :] >= 56))      # a shin fin beside the butt
        if name == 'arm_l':
            # The shaft showing just above and below the fist belongs to the spear, which turns in the fist.
            xs, ys = np.arange(W)[None, :], np.arange(H)[:, None]
            take &= ~(((xs >= 33) & (xs <= 55)) & ((ys < 321) | (ys > 357)))
        owner[name] = owner.get(name, np.zeros_like(take)) | take
        free &= ~take
    out = HERE / 'rig'
    out.mkdir(exist_ok=True)
    # Each part also carries a few pixels past its cut, drawn under whatever is in front of it, so joints show no
    # seam when the parts are resampled or turn a little. The head, drawn last, carries only a sliver.
    # The bleed only reaches into the body and, for a leg, the next segment of the same leg: never into the spear,
    # the arms or the tendrils, which move on their own and would drag a stray strip along.
    for name, take in owner.items():
        if name.startswith('tendrils') or name == 'spear':
            pass
        else:
            into = free.copy()
            for seg in ('thigh', 'shin', 'boot'):
                if name[-2:] in ('_l', '_r') and name.startswith(('thigh', 'shin', 'boot')):
                    into |= owner[seg + name[-2:]]
            take = take | (ndimage.binary_dilation(take, iterations=2 if name == 'head' else 6) & into)
        a = np.where(take, alpha, 0).astype(np.uint8)
        Image.fromarray(np.dstack([im[..., :3], a])).save(out / f'{name}.png', optimize=True)
    # The body: what is left, without crumbs of other parts cut off from it, and with the hidden parts painted
    # in where they were enclosed by the body.
    lab, n = ndimage.label(free & (alpha > 24))
    sizes = ndimage.sum(np.ones_like(lab), lab, range(1, n + 1))
    crumbs = np.isin(lab, [i + 1 for i, size in enumerate(sizes) if size < 150])
    free &= ~ndimage.binary_dilation(crumbs, iterations=1)
    body = free
    taken = ~free & (alpha > 0)
    enclosed = ndimage.binary_closing(body, structure=np.ones((3, 3)), iterations=7) & taken
    enclosed = ndimage.binary_opening(enclosed, iterations=1)
    bgr = cv2.cvtColor(im[..., :3], cv2.COLOR_RGB2BGR)
    keep = np.where(body[..., None], bgr, 0).astype(np.uint8)
    filled = cv2.inpaint(keep, (~body).astype(np.uint8) * 255, 4, cv2.INPAINT_TELEA)
    filled = cv2.cvtColor(filled, cv2.COLOR_BGR2RGB)
    a = np.where(body, alpha, 0)
    a = np.where(enclosed, 255, a).astype(np.uint8)
    Image.fromarray(np.dstack([filled, a])).save(out / 'body.png', optimize=True)
    # The shaft behind the fist: the column just above it, repeated down.
    spear = np.asarray(Image.open(out / 'spear.png')).copy()
    row = spear[306, 28:58]
    for y in range(308, 368):
        spear[y, 28:58] = np.where(row[:, 3:4] > 0, row, spear[y, 28:58])
    Image.fromarray(spear).save(out / 'spear.png', optimize=True)
    order = list(dict.fromkeys(name for name, _ in reversed(PARTS)))
    order.insert(order.index('arm_l') + 1, 'body')
    json.dump({'size': [W, H], 'order': order, 'pivots': PIVOTS}, open(out / 'rig.json', 'w'), indent=1)
    print('parts:', ', '.join(f'{n} {int(t.sum())}px' for n, t in owner.items()), '| body', int(body.sum()), 'px, filled',
          int(enclosed.sum()), 'px')


if __name__ == '__main__':
    main()
