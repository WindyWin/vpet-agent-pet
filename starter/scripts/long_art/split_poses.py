"""Split the matted design sheet into its four poses.

    pip install "rembg[cpu]" opencv-python-headless scipy pillow
    python3 -c "from rembg import remove, new_session; from PIL import Image; \
        remove(Image.open('design-sheet.png').convert('RGB'), session=new_session('isnet-general-use')).save('matted.png')"
    python3 split_poses.py matted.png .            # writes pose1.png .. pose4.png

Poses 2 to 4 touch: the lunge's spear tip lies on the thinking pose's boot, and the cheer's spear butt on its
crystal. Each spear goes to its owner, and what it hid on the neighbour is inpainted."""
import sys
import numpy as np
import cv2
from PIL import Image
from scipy import ndimage

src, out = sys.argv[1], sys.argv[2]
rgba = np.array(Image.open(src).convert('RGBA'))
alpha = rgba[:, :, 3]
H, W = alpha.shape

def taper(points, widths):
    """Mask of a polyline whose half-width goes through `widths` at each point."""
    m = np.zeros((H, W), np.uint8)
    for (p0, w0), (p1, w1) in zip(zip(points, widths), zip(points[1:], widths[1:])):
        for t in np.linspace(0, 1, 60):
            x, y = p0[0] + (p1[0] - p0[0]) * t, p0[1] + (p1[1] - p0[1]) * t
            r = w0 + (w1 - w0) * t
            cv2.circle(m, (int(round(x)), int(round(y))), max(1, int(round(r))), 1, -1)
    return m.astype(bool)

spear2 = taper([(520, 495), (585, 545), (637, 587)], [28, 18, 6])
spear4 = taper([(803, 271), (840, 234), (882, 194)], [10, 10, 10])
xs = np.arange(W)[None, :].repeat(H, 0)
solid = alpha > 0
owner = np.zeros((H, W), np.int8)
owner[solid & (xs < 297)] = 1
owner[solid & (xs >= 297) & (xs < 588)] = 2
owner[solid & (xs >= 588) & (xs < 838)] = 3
owner[solid & (xs >= 838)] = 4
owner[solid & spear2] = 2
owner[solid & spear4] = 4
for k in (1, 2, 3, 4):
    m = owner == k
    # Keep the main body and whatever touches it; drop crumbs that belong to a neighbour.
    lab, n = ndimage.label(m)
    if n > 1:
        sizes = ndimage.sum(m, lab, range(1, n + 1))
        m = np.isin(lab, [i + 1 for i, s in enumerate(sizes) if s > 400])
    a = np.where(m, alpha, 0).astype(np.uint8)
    rgb = rgba[:, :, :3].copy()
    if k == 3:
        hidden = (spear2 | spear4) & solid
        body = ndimage.binary_closing(m | hidden, iterations=6) & ndimage.binary_dilation(m, iterations=4)
        fill = hidden & body
        rgb = cv2.inpaint(rgb, fill.astype(np.uint8) * 255, 5, cv2.INPAINT_TELEA)
        a = np.where(fill, 255, a).astype(np.uint8)
    img = np.dstack([rgb, a])
    y0, x0 = np.argwhere(a > 0).min(0)
    y1, x1 = np.argwhere(a > 0).max(0) + 1
    Image.fromarray(img).crop((x0, y0, x1, y1)).save(f'{out}/pose{k}.png')
    print(k, (x0, y0, x1, y1))
