"""Regrade a cut-out off the blueprint sheet: lift it off the blueprint's blue haze, deepen the blacks, restore the palette's saturation,
add local contrast, and sharpen the ink."""
import numpy as np
import cv2
from PIL import Image


TINT = [0.86, 0.80, 1.22]


def grade(rgba, haze=0.8, sat=1.45, clahe=2.6, sharp=1.0, gamma=1.2):
    rgb = rgba[..., :3].astype(np.float32) / 255
    a = rgba[..., 3:4].astype(np.float32) / 255
    # The sheet's navy wash: pull every pixel away from the blueprint colour in proportion to how close it is.
    bp = np.array([25, 57, 86], np.float32) / 255
    d = np.linalg.norm(rgb - bp, axis=-1, keepdims=True)
    w = np.clip(1 - d / 0.45, 0, 1) * haze
    lum = rgb.mean(-1, keepdims=True)
    rgb = rgb + (lum * np.array(TINT) - rgb) * w        # deep indigo steel instead of blueprint blue
    # Local contrast on lightness.
    lab = cv2.cvtColor((np.clip(rgb, 0, 1) * 255).astype(np.uint8), cv2.COLOR_RGB2LAB)
    l = cv2.createCLAHE(clipLimit=clahe, tileGridSize=(8, 8)).apply(lab[..., 0])
    lab[..., 0] = l
    rgb = cv2.cvtColor(lab, cv2.COLOR_LAB2RGB).astype(np.float32) / 255
    # Saturation, protecting the near-greys.
    hsv = cv2.cvtColor((rgb * 255).astype(np.uint8), cv2.COLOR_RGB2HSV).astype(np.float32)
    hsv[..., 1] = np.clip(hsv[..., 1] * (1 + (sat - 1) * np.clip(hsv[..., 1] / 90, 0, 1)), 0, 255)
    rgb = cv2.cvtColor(hsv.astype(np.uint8), cv2.COLOR_HSV2RGB).astype(np.float32) / 255
    # Blacks: a gentle S-curve.
    rgb = np.clip(rgb, 0, 1)
    rgb = rgb ** gamma
    # Unsharp mask.
    blur = cv2.GaussianBlur(rgb, (0, 0), 1.6)
    rgb = np.clip(rgb + (rgb - blur) * sharp, 0, 1)
    out = np.dstack([rgb, a])
    return (out * 255 + 0.5).astype(np.uint8)
