"""Cut Phù Đồ's drawings off the design sheet (design-sheet.png) into this folder.

    pip install torch "rembg[cpu]" opencv-python-headless scipy pillow
    curl -LO https://github.com/xinntao/Real-ESRGAN/releases/download/v0.2.2.4/RealESRGAN_x4plus_anime_6B.pth
    python3 scripts/phudo_art/extract.py RealESRGAN_x4plus_anime_6B.pth [names...]

For each asset:
1. Inside the given label and dimension boxes, inpaint the white text and lines (the drawing keeps its colour).
2. Upscale 4x (Real-ESRGAN anime).
3. Matte (isnet-anime), harden the alpha so the sheet's haze drops out, keep the main body and what touches it.
4. Erase any extra regions that belong to a neighbouring figure.
5. Regrade (grade.py): off the blueprint's blue haze, deeper blacks, the palette's saturation, local contrast, sharper
   ink. Saved at the upscaled 4x resolution, cropped to the drawing on a grid of 4/3 so the landmarks in
   draw_phudo.py (measured at 3x) line up."""
import sys
from pathlib import Path

import cv2
import numpy as np
import torch
from PIL import Image
from scipy import ndimage
from rembg import remove, new_session
from esrgan import load, upscale
from grade import grade

HERE = Path(__file__).resolve().parent

ASSETS = {
    'form1': dict(box=(28, 60, 242, 305), text=[(218, 82, 320, 112), (203, 100, 228, 120), (22, 92, 60, 304),
                                                 (22, 97, 72, 105), (22, 141, 72, 149), (22, 181, 72, 189)]),
    'form2': dict(box=(268, 55, 553, 305), text=[(438, 76, 542, 127), (420, 92, 444, 122), (272, 241, 370, 272),
                                                  (358, 225, 384, 254)],
                  erase=[(540, 170, 553, 305)]),
    'form3': dict(box=(505, 52, 772, 305), text=[(692, 84, 712, 106), (645, 284, 760, 300)],
                  label=[(708, 76, 776, 104)], erase=[(505, 52, 551, 174)], bgkey=[(596, 52, 690, 120)]),
    'form4': dict(box=(768, 58, 1012, 300), text=[(926, 76, 992, 106), (768, 118, 818, 147), (800, 140, 828, 160),
                                                   (766, 236, 808, 263), (936, 258, 990, 290), (800, 250, 830, 270)]),
    'greatsword': dict(box=(545, 532, 798, 594), text=[]),
}


def clean_text(rgb, boxes, ox, oy, labels=()):
    """Inpaint white text and lines in `boxes`; in `labels` (text with a drop shadow over the drawing) the
    shadow's near-black pixels go too."""
    img = (rgb * 255).astype(np.uint8)
    hsv = cv2.cvtColor(img, cv2.COLOR_RGB2HSV)
    white = (hsv[..., 2] > 150) & (hsv[..., 1] < 110)
    dark = hsv[..., 2] < 40
    mask = np.zeros(white.shape, bool)
    for group, pick in ((boxes, white), (labels, white | dark)):
        for x0, y0, x1, y1 in group:
            m = np.zeros_like(mask)
            m[max(0, y0 - oy):max(0, y1 - oy), max(0, x0 - ox):max(0, x1 - ox)] = True
            mask |= m & pick
    mask = ndimage.binary_dilation(mask, iterations=1)
    out = cv2.inpaint(cv2.cvtColor(img, cv2.COLOR_RGB2BGR), mask.astype(np.uint8) * 255, 3, cv2.INPAINT_TELEA)
    return cv2.cvtColor(out, cv2.COLOR_BGR2RGB).astype(np.float32) / 255


def main(model, names):
    torch.set_num_threads(2)
    net = load(model)
    session = new_session('isnet-anime')
    sheet = np.asarray(Image.open(HERE / 'design-sheet.png').convert('RGB'), dtype=np.float32) / 255
    for name in names:
        spec = ASSETS[name]
        x0, y0, x1, y1 = spec['box']
        crop = clean_text(sheet[y0:y1, x0:x1], spec['text'], x0, y0, spec.get('label', ()))
        up = upscale(net, crop)
        img = Image.fromarray((up * 255 + 0.5).astype(np.uint8))
        a = np.asarray(remove(img, session=session, only_mask=True), dtype=np.float32) / 255
        hard = spec.get('hard', 0.35)
        a = np.clip((a - hard) / 0.25, 0, 1)
        for ex0, ey0, ex1, ey1 in spec.get('erase', []):
            a[(ey0 - y0) * 4:(ey1 - y0) * 4, (ex0 - x0) * 4:(ex1 - x0) * 4] = 0
        # Enclosed background the matte kept (inside a loop of tendrils): blueprint-coloured pixels in the boxes.
        rgbu = np.asarray(img, dtype=np.float32)
        near_bg = np.linalg.norm(rgbu - np.array([25, 57, 86], np.float32), axis=-1) < 34
        for bx0, by0, bx1, by1 in spec.get('bgkey', []):
            sl = np.s_[(by0 - y0) * 4:(by1 - y0) * 4, (bx0 - x0) * 4:(bx1 - x0) * 4]
            a[sl] = np.where(ndimage.binary_opening(near_bg[sl], iterations=2), 0, a[sl])
        solid = a > 0.5
        lab, n = ndimage.label(ndimage.binary_dilation(solid, iterations=3))
        if n:
            sizes = ndimage.sum(solid, lab, range(1, n + 1))
            keep = np.isin(lab, [i + 1 for i, s in enumerate(sizes) if s > 0.015 * sizes.max()])
            a = a * keep
        a = np.where(a < 0.04, 0, a)
        out = grade(np.dstack([np.asarray(img), (a * 255 + 0.5).astype(np.uint8)]))
        # Crop to the drawing on the 3x grid the landmarks were measured on, scaled to 4x.
        ys, xs = np.nonzero(a > 0)
        top, left = max(0, ys.min() * 3 // 4 - 4) * 4 // 3, max(0, xs.min() * 3 // 4 - 4) * 4 // 3
        out = out[top:ys.max() + 7, left:xs.max() + 7]
        Image.fromarray(out).save(HERE / f'{name}.png', optimize=True)
        print(name, img.size, flush=True)


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2:] or list(ASSETS))
