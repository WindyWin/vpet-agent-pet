"""Cut Phù Đồ's drawings off the design sheet (design-sheet.png) into this folder.

    pip install torch "rembg[cpu]" opencv-python-headless scipy pillow
    curl -LO https://github.com/xinntao/Real-ESRGAN/releases/download/v0.2.2.4/RealESRGAN_x4plus_anime_6B.pth
    python3 scripts/phudo_art/extract.py RealESRGAN_x4plus_anime_6B.pth [names...]

For each asset:
1. Inside the given label and dimension boxes, inpaint the white text and lines (the drawing keeps its colour).
2. Upscale 4x (Real-ESRGAN anime).
3. Matte (isnet-anime), harden the alpha so the sheet's haze drops out, keep the main body and what touches it.
4. Erase any extra regions that belong to a neighbouring figure."""
import sys
from pathlib import Path

import cv2
import numpy as np
import torch
from PIL import Image
from scipy import ndimage
from rembg import remove, new_session
from esrgan import load, upscale

HERE = Path(__file__).resolve().parent

ASSETS = {
    'form1': dict(box=(28, 60, 242, 305), text=[(218, 82, 320, 112), (203, 100, 228, 120), (22, 92, 60, 304),
                                                 (22, 97, 72, 105), (22, 141, 72, 149), (22, 181, 72, 189)]),
    'form2': dict(box=(268, 55, 553, 305), text=[(438, 76, 542, 127), (420, 92, 444, 122), (272, 241, 370, 272),
                                                  (358, 225, 384, 254)],
                  erase=[(540, 170, 553, 305)]),
    'form3': dict(box=(505, 52, 772, 305), text=[(688, 74, 764, 102), (700, 98, 724, 116), (645, 284, 760, 300)],
                  erase=[(505, 52, 551, 174), (712, 70, 772, 102)], bgkey=[(596, 52, 690, 120)]),
    'form4': dict(box=(768, 58, 1012, 300), text=[(926, 76, 992, 106), (768, 118, 818, 147), (800, 140, 828, 160),
                                                   (766, 236, 808, 263), (936, 258, 990, 290), (800, 250, 830, 270)]),
    'phase1': dict(box=(28, 398, 182, 582), text=[(140, 398, 222, 438), (120, 410, 150, 440)], hard=0.6),
    'phase2': dict(box=(180, 398, 352, 582), text=[(300, 404, 400, 434), (290, 420, 312, 444)], hard=0.6),
    'phase3': dict(box=(355, 398, 512, 582), text=[], hard=0.6),
    'greatsword': dict(box=(545, 532, 798, 594), text=[]),
}


def clean_text(rgb, boxes, ox, oy):
    img = (rgb * 255).astype(np.uint8)
    hsv = cv2.cvtColor(img, cv2.COLOR_RGB2HSV)
    white = (hsv[..., 2] > 150) & (hsv[..., 1] < 110)
    mask = np.zeros(white.shape, bool)
    for x0, y0, x1, y1 in boxes:
        m = np.zeros_like(mask)
        m[max(0, y0 - oy):max(0, y1 - oy), max(0, x0 - ox):max(0, x1 - ox)] = True
        mask |= m & white
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
        crop = clean_text(sheet[y0:y1, x0:x1], spec['text'], x0, y0)
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
        out = np.dstack([np.asarray(img, dtype=np.float32) / 255, a])
        # Down to 3x the sheet, resampled premultiplied so edges stay clean, and cropped to the drawing.
        pm = Image.fromarray((np.dstack([out[..., :3] * out[..., 3:4], out[..., 3:4]]) * 255 + 0.5).astype(np.uint8))
        W, H = pm.size
        small = np.asarray(pm.resize((W * 3 // 4, H * 3 // 4), Image.LANCZOS), dtype=np.float32) / 255
        sa = small[..., 3:4]
        out = np.dstack([np.clip(small[..., :3] / np.maximum(sa, 1e-3), 0, 1), sa])
        out[..., 3] = np.where(out[..., 3] < 0.04, 0, out[..., 3])
        ys, xs = np.nonzero(out[..., 3] > 0)
        out = out[max(0, ys.min() - 4):ys.max() + 5, max(0, xs.min() - 4):xs.max() + 5]
        Image.fromarray((out * 255 + 0.5).astype(np.uint8)).save(HERE / f'{name}.png', optimize=True)
        print(name, img.size, flush=True)


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2:] or list(ASSETS))
