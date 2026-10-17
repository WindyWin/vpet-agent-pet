#!/usr/bin/env python3
"""Derive transparent landing-page media from the repository's docs/media.

The README's GIF/PNG previews were exported with an opaque blue backdrop baked
into every frame, so they show a light box on the dark landing page. The pet art
itself (assets/vpet/**.png) is properly transparent; only these previews are not.

Two details make a naive colour-key fail:

* The backdrop is a soft gradient, so a key against one sampled colour leaves a
  halo wherever the gradient drifts.
* Frames after the first are small patches over the previous frame, so any
  backdrop left behind in frame 0 stays on screen for the whole animation.

So each frame is coalesced to a full image and flood-filled from its whole
border: the fill follows the gradient in from the edges but cannot cross the
pet's dark outline, which keeps enclosed light shapes (silver hair, the cream
recap bubble, white socks) intact. The filled frames are written back as full
frames with Background disposal rather than optimised to patches, because the
optimiser emits `dispose: previous` frames that some viewers combine badly with
transparency and smear.

The fuzz is deliberately conservative. Much above ~32% the fill starts leaking
through anti-aliased gaps in the outline and eats the pet's hair.

Requires ImageMagick 7 (`magick`). Output is committed, so the site build never
needs ImageMagick.

    python3 site/scripts/derive_transparent_media.py [--fuzz 24]
"""

from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "docs" / "media"
DEST = ROOT / "site" / "src" / "assets" / "pet"

# Border sampling step, in pixels: every step-th pixel of all four edges seeds a
# flood fill with its own colour.
STEP = 4

GIFS = [
    # Display states
    "thinking",
    "reading",
    "working",
    "needs-input",
    "tool-error",
    "turn-finished",
    "sleeping",
    "startled",
    # Play
    "touch-head",
    "dance",
    "fidget-yawn",
    "snack-hungry",
]

PNGS = ["eye-break", "recap"]

# docs/media/break-buddy.gif cannot be used: its rope frames are stored with
# `dispose: none`, so a spec-compliant viewer leaves every loop the rope makes
# on screen and the rope becomes an unreadable scribble. Rebuild it from the
# pet's own source frames instead — they are already transparent and each holds
# a single rope position. The trailing number in each file name is the frame's
# duration in milliseconds.
ROPE = ROOT / "assets" / "vpet" / "pet" / "vup" / "WORK" / "RopeSkipping" / "Happy"
ROPE_PARTS = ["A", "B/1", "C"]  # start, loop, end
ROPE_HEIGHT = 260

PIXEL = re.compile(r"srgba?\(([^)]+)\)")
BBOX = re.compile(r"(\d+)x(\d+)([+-]\d+)([+-]\d+)")
ROPE_NAME = re.compile(r"_(\d+)_(\d+)\.png$")


def rope_frame_index(path: Path) -> int:
    match = ROPE_NAME.search(path.name)
    if not match:
        raise ValueError(f"unexpected rope frame name: {path.name}")
    return int(match.group(1))


def rope_frame_duration(path: Path) -> int:
    match = ROPE_NAME.search(path.name)
    if not match:
        raise ValueError(f"unexpected rope frame name: {path.name}")
    return int(match.group(2))


def magick(*args: str) -> str:
    result = subprocess.run(
        ["magick", *args], check=True, text=True, capture_output=True
    )
    return result.stdout


def content_box(frames: list[Path]) -> str | None:
    """Union of the non-transparent bounds of every frame, as a crop geometry.

    The previews are exported on a fixed canvas much larger than the pet, and
    the pet sits differently in each one, so the cards would otherwise show it
    at wildly different sizes.
    """
    boxes = []
    for frame in frames:
        match = BBOX.match(magick("identify", "-format", "%@", str(frame)).strip())
        if match:
            width, height, x, y = (int(v) for v in match.groups())
            boxes.append((x, y, width, height))
    if not boxes:
        return None
    x0 = min(x for x, _, _, _ in boxes)
    y0 = min(y for _, y, _, _ in boxes)
    x1 = max(x + w for x, _, w, _ in boxes)
    y1 = max(y + h for _, y, _, h in boxes)
    return f"{x1 - x0}x{y1 - y0}+{x0}+{y0}"


def border_points(width: int, height: int) -> list[tuple[int, int]]:
    points: list[tuple[int, int]] = []
    for x in range(0, width, STEP):
        points += [(x, 0), (x, height - 1)]
    for y in range(0, height, STEP):
        points += [(0, y), (width - 1, y)]
    # A dict preserves order while dropping duplicates.
    return list(dict.fromkeys(points))


def seed_colors(path: Path, points: list[tuple[int, int]]) -> list[str]:
    fmt = "|".join(f"%[pixel:p{{{x},{y}}}]" for x, y in points)
    raw = magick("identify", "-format", fmt, str(path))
    colors = []
    for chunk in raw.split("|"):
        match = PIXEL.match(chunk.strip())
        if not match:
            raise ValueError(f"unexpected pixel format: {chunk!r}")
        r, g, b = (int(float(v)) for v in match.group(1).split(",")[:3])
        colors.append(f"srgb({r},{g},{b})")
    return colors


def clear_backdrop(path: Path, fuzz: str) -> None:
    width, height = (int(v) for v in magick("identify", "-format", "%w %h", str(path)).split())
    points = border_points(width, height)
    args = ["-fuzz", f"{fuzz}%", "-fill", "none"]
    for (x, y), color in zip(points, seed_colors(path, points)):
        args += ["-floodfill", f"+{x}+{y}", color]
    magick(str(path), *args, str(path))


def derive_gif(name: str, fuzz: str) -> Path:
    src = SOURCE / f"{name}.gif"
    out = DEST / f"{name}.gif"
    if not src.exists():
        raise FileNotFoundError(src)

    with tempfile.TemporaryDirectory() as tmp:
        magick(str(src), "-coalesce", str(Path(tmp) / "f-%04d.png"))
        frames = sorted(Path(tmp).glob("f-*.png"))
        if not frames:
            raise ValueError(f"no frames decoded from {src}")
        for frame in frames:
            clear_backdrop(frame, fuzz)
        crop = content_box(frames)
        paths = [str(f) for f in frames]
        # Full frames with Background disposal rather than `-layers Optimize`:
        # the optimiser emits `dispose: previous` patches, which some viewers
        # combine badly with transparency and smear. The cost is ~0.9 MB across
        # the whole set.
        extra = ["-crop", crop, "+repage"] if crop else []
        magick(
            "-dispose",
            "Background",
            "-loop",
            "0",
            *paths,
            *extra,
            str(out),
        )
    return out


def derive_png(name: str, fuzz: str) -> Path:
    src = SOURCE / f"{name}.png"
    out = DEST / f"{name}.png"
    if not src.exists():
        raise FileNotFoundError(src)
    with tempfile.TemporaryDirectory() as tmp:
        staged = Path(tmp) / f"{name}.png"
        shutil.copy2(src, staged)
        clear_backdrop(staged, fuzz)
        shutil.copy2(staged, out)
    return out


def derive_break_buddy() -> Path:
    """Rebuild the jump-rope animation from the pet's source frames."""
    out = DEST / "break-buddy.gif"
    sources = [
        path
        for part in ROPE_PARTS
        for path in sorted((ROPE / part).glob("*.png"), key=rope_frame_index)
    ]
    if not sources:
        raise FileNotFoundError(ROPE)

    with tempfile.TemporaryDirectory() as tmp:
        frames = []
        delays = []
        for i, src in enumerate(sources):
            staged = Path(tmp) / f"f-{i:04d}.png"
            shutil.copy2(src, staged)
            frames.append(staged)
            # GIF delays are centiseconds.
            delays.append(max(1, round(rope_frame_duration(src) / 10)))

        crop = content_box(frames)
        args = ["-dispose", "Background", "-loop", "0"]
        for delay, frame in zip(delays, frames):
            args += ["-delay", str(delay), str(frame)]
        if crop:
            args += ["-crop", crop, "+repage"]
        args += ["-resize", f"x{ROPE_HEIGHT}", str(out)]
        magick(*args)
    return out


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--fuzz",
        type=int,
        default=24,
        help="flood-fill tolerance in percent (default: 24)",
    )
    args = parser.parse_args()

    if not shutil.which("magick"):
        print("ImageMagick 7 (`magick`) is required", file=sys.stderr)
        return 1

    DEST.mkdir(parents=True, exist_ok=True)
    out = derive_break_buddy()
    print(f"gif  {'break-buddy':16} -> {out.relative_to(ROOT)}")
    for name in GIFS:
        out = derive_gif(name, args.fuzz)
        print(f"gif  {name:16} -> {out.relative_to(ROOT)}")
    for name in PNGS:
        out = derive_png(name, args.fuzz)
        print(f"png  {name:16} -> {out.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
