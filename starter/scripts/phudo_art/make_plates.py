"""Split each form into armour plates along its ink lines, for the piece-by-piece transformations.

    pip install scikit-image scipy pillow
    python3 scripts/phudo_art/make_plates.py         # writes scripts/phudo_art/plates/<form>.png

A watershed from the middle of every inked panel, walled by the ink lines, gives one region per plate; slivers
merge into the neighbour they share most border with. The result is an 8-bit label map (0 = empty)."""
from pathlib import Path

import numpy as np
from PIL import Image
from scipy import ndimage
from skimage.segmentation import watershed
from skimage.feature import peak_local_max


def plates(rgba, target=90, min_area=None):
    a = rgba[..., 3].astype(np.float32) / 255
    rgb = rgba[..., :3].astype(np.float32) / 255
    solid = a > 0.5
    lum = rgb @ np.array([0.3, 0.55, 0.15], np.float32)
    ink = (lum < 0.13) & solid
    ink = ndimage.binary_dilation(ink, iterations=1)
    inner = solid & ~ink
    dist = ndimage.distance_transform_edt(inner)
    area = solid.sum()
    min_area = min_area or area / (target * 2.5)
    spacing = int(max(8, np.sqrt(area / target) * 0.55))
    peaks = peak_local_max(ndimage.gaussian_filter(dist, 2), min_distance=spacing, labels=ndimage.label(inner)[0],
                           exclude_border=False)
    markers = np.zeros(a.shape, np.int32)
    for i, (y, x) in enumerate(peaks, 1):
        markers[y, x] = i
    # Every piece of plate with no peak (thin parts) still gets a marker.
    lab, n = ndimage.label(inner)
    nxt = markers.max() + 1
    for i, sl in enumerate(ndimage.find_objects(lab), 1):
        region = lab[sl] == i
        if not (markers[sl][region] > 0).any() and region.sum() > 20:
            ys, xs = np.nonzero(region)
            markers[sl[0].start + ys[len(ys) // 2], sl[1].start + xs[len(xs) // 2]] = nxt
            nxt += 1
    elev = ndimage.gaussian_filter(ink.astype(np.float32), 1.0) - dist / (dist.max() + 1e-3) * 0.3
    seg = watershed(elev, markers, mask=solid)
    # Merge small plates into the neighbour they share most border with.
    for _ in range(6):
        ids, counts = np.unique(seg[seg > 0], return_counts=True)
        small = ids[counts < min_area]
        if not len(small):
            break
        for sid in small:
            m = seg == sid
            if not m.any():
                continue
            ring = ndimage.binary_dilation(m, iterations=2) & ~m & (seg > 0)
            if ring.any():
                nb, c = np.unique(seg[ring], return_counts=True)
                seg[m] = nb[np.argmax(c)]
    # Relabel 1..N.
    ids = np.unique(seg[seg > 0])
    remap = np.zeros(seg.max() + 1, np.int32)
    remap[ids] = np.arange(1, len(ids) + 1)
    return remap[seg]


HERE = Path(__file__).resolve().parent
TARGETS = {'form1': 90, 'form2': 70, 'form3': 100, 'form4': 90}

if __name__ == '__main__':
    (HERE / 'plates').mkdir(exist_ok=True)
    for name, target in TARGETS.items():
        seg = plates(np.asarray(Image.open(HERE / f'{name}.png').convert('RGBA')), target)
        assert seg.max() < 256
        Image.fromarray(seg.astype(np.uint8), 'L').save(HERE / 'plates' / f'{name}.png', optimize=True)
        print(name, seg.max(), 'plates')
