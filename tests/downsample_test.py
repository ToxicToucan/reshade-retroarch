"""Checks the Downsample mode's averaging (capture.slang with rra_average, downsample.h)
on generated frames, offline through tools/render_png.

    python tests/downsample_test.py [--build out/build] [--work out/downsample] [--apis d3d11,d3d12,d3d9]
                                    [--librashader <librashader.dll>]
                                    [--dxc <folder with dxcompiler.dll and dxil.dll>]

Each case makes a frame (a picture with hard-edged shapes and noise, like 3D drawn without
anti-aliasing), runs render_png with --grid down:WxH (or down-stretch:WxH, and --crop),
and compares with a reference computed here with numpy: every small pixel the average of
the frame pixels its cell covers, each weighted by the part of its area inside the cell.

  - Direct3D 11: the small picture itself (render_png --native) must be within 1/255 of
    the reference in every channel (8-bit rounding).
  - Direct3D 12 and Direct3D 9 (render_png cannot save the small picture there): the
    output frame, rendered with tests/presets/nearest.slangp (an exact nearest-neighbour
    enlargement that inverts the colours), is compared with that enlargement of the
    reference, within 1/255. On Direct3D 9 the capture (capture_sm3.slang) is exact only
    while cells span at most 16 frame pixels, which all cases here do; the case that
    enlarges is not checked there (see below). On both, the pixels outside the rectangle
    must be untouched.

Also checks the grid render_png prints (size and rectangle) against downsample.h's rules.
Needs a GPU, and librashader.dll in the work folder or given with --librashader.
Exit code 0 when every case passes.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys

import numpy as np
from PIL import Image

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(__file__))
import e2e  # noqa: E402  (newest_dxc)

# (name, frame width, height, --grid, --crop or None, expected small size and rectangle
# (native_w, native_h, rect_x, rect_y, rect_w, rect_h)).
CASES = [
    ("16x9-1080p-320x240", 1920, 1080, "down:320x240", None, (427, 240, 0, 0, 1920, 1080)),
    ("16x9-4k-320x240", 3840, 2160, "down:320x240", None, (427, 240, 0, 0, 3840, 2160)),
    ("4x3-1440x1080-256x224", 1440, 1080, "down:256x224", None, (256, 224, 0, 0, 1440, 1080)),
    ("odd-1366x768-320x224", 1366, 768, "down:320x224", None, (427, 224, 0, 0, 1366, 768)),
    ("stretch-1920x1080-320x240", 1920, 1080, "down-stretch:320x240", None, (320, 240, 0, 0, 1920, 1080)),
    ("crop43-1920x1080-320x240", 1920, 1080, "down:320x240", "12.5,0,12.5,0", (320, 240, 240, 0, 1440, 1080)),
    ("crop-uneven-1600x900-384x224", 1600, 900, "down:384x224", "5,10,2.5,0", (526, 224, 80, 90, 1480, 810)),
    ("up-200x150-320x240", 200, 150, "down:320x240", None, (320, 240, 0, 0, 200, 150)),
]


def make_frame(w, h, seed):
    """A w x h RGB picture: a gradient, hard-edged random triangles and some noise."""
    rng = np.random.default_rng(seed)
    y, x = np.mgrid[0:h, 0:w]
    img = np.stack([x * 255 // max(1, w - 1), y * 255 // max(1, h - 1), (x + y) * 127 // max(1, w + h)], -1)
    img = img.astype(np.int32)
    for _ in range(40):
        pts = rng.uniform([0, 0], [w, h], (3, 2))
        colour = rng.integers(0, 256, 3)
        (x0, y0), (x1, y1), (x2, y2) = pts
        d = (y1 - y2) * (x0 - x2) + (x2 - x1) * (y0 - y2)
        if abs(d) < 1e-6:
            continue
        a = ((y1 - y2) * (x + 0.5 - x2) + (x2 - x1) * (y + 0.5 - y2)) / d
        b = ((y2 - y0) * (x + 0.5 - x2) + (x0 - x2) * (y + 0.5 - y2)) / d
        inside = (a >= 0) & (b >= 0) & (a + b <= 1)
        img[inside] = colour
    img += rng.integers(-12, 13, img.shape)
    return np.clip(img, 0, 255).astype(np.uint8)


def weights(extent, count):
    """count x extent matrix: row i holds how much of each pixel 0..extent-1 lies in cell
    i, when `count` cells of extent / count pixels cover the axis."""
    edges = np.arange(count + 1) * extent / count
    px = np.arange(extent)
    lo = np.maximum(px[None, :], edges[:-1, None])
    hi = np.minimum(px[None, :] + 1, edges[1:, None])
    return np.clip(hi - lo, 0, None)


def area_average(img, rect, nw, nh):
    """The reference: img's rectangle rect = (x, y, w, h) averaged down to nw x nh, as
    floats in 0..255."""
    x, y, w, h = rect
    part = img[y:y + h, x:x + w].astype(np.float64)
    wx, wy = weights(w, nw), weights(h, nh)
    out = np.stack([wy @ part[:, :, c] @ wx.T for c in range(3)], -1)
    return out / (wx.sum(1)[None, :, None] * wy.sum(1)[:, None, None])


def nearest_inverted(frame, small, rect):
    """What tests/presets/nearest.slangp makes of `small` over `rect` of `frame`: the
    rectangle replaced by the exact nearest-neighbour enlargement of 255 - small."""
    x, y, w, h = rect
    nh, nw = small.shape[:2]
    out = frame.astype(np.float64).copy()
    rows = (np.arange(h) * nh) // h
    cols = (np.arange(w) * nw) // w
    out[y:y + h, x:x + w] = 255.0 - small[rows][:, cols]
    return out


def load(path):
    return np.asarray(Image.open(path).convert("RGB")).astype(np.float64)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--build", default=os.path.join(REPO, "out", "build"))
    ap.add_argument("--work", default=os.path.join(REPO, "out", "downsample"))
    ap.add_argument("--apis", default="d3d11,d3d12,d3d9")
    ap.add_argument("--librashader", help="librashader.dll to copy next to render_png.exe")
    ap.add_argument("--dxc", default=e2e.newest_dxc(),
                    help="folder with dxcompiler.dll and dxil.dll, for Direct3D 12 (default: the Windows SDK)")
    args = ap.parse_args()

    work = os.path.abspath(args.work)
    os.makedirs(work, exist_ok=True)
    render = os.path.join(work, "render_png.exe")
    shutil.copy(os.path.join(args.build, "render_png.exe"), render)
    if args.librashader:
        shutil.copy(args.librashader, os.path.join(work, "librashader.dll"))
    if not os.path.exists(os.path.join(work, "librashader.dll")):
        sys.exit("librashader.dll missing: pass --librashader")
    preset = {api: os.path.join(REPO, "tests", "presets", "nearest_sm3.slangp" if api == "d3d9" else "nearest.slangp")
              for api in ["d3d11", "d3d12", "d3d9"]}

    results = []
    for seed, (name, w, h, grid, crop, want) in enumerate(CASES):
        frame = make_frame(w, h, seed)
        src = os.path.join(work, name + ".png")
        Image.fromarray(frame).save(src)
        nw, nh, rx, ry, rw, rh = want
        reference = area_average(frame, (rx, ry, rw, rh), nw, nh)
        for api in args.apis.split(","):
            label = f"{api:5} {name}"
            if api == "d3d9" and nw > rw:
                # nearest_sm3.slang computes floor(x * in / out) in floating point, which
                # can land one pixel low where the exact value is a whole number; that
                # happens all over when it shrinks (here 320 -> 200), so this case cannot
                # be checked through it.
                results.append((None, label, "not checked: the Direct3D 9 test preset is not exact when shrinking"))
                continue
            out = os.path.join(work, f"{name}-{api}-out.png")
            native = os.path.join(work, f"{name}-{api}-native.png")
            cmd = [render, preset[api], src, out, "--frames", "1", "--grid", grid]
            if crop:
                cmd += ["--crop", crop]
            if api == "d3d11":
                cmd += ["--native", native]
            else:
                cmd += ["--api", api]
                if api == "d3d12" and args.dxc:
                    cmd += ["--dxc", args.dxc]
            r = subprocess.run(cmd, capture_output=True, text=True, cwd=work)
            if r.returncode != 0:
                results.append((False, label, f"render_png failed: {(r.stdout + r.stderr).strip()[:300]}"))
                continue
            m = re.search(r"grid: (\d+)x(\d+) in (\d+)x(\d+) at \((\d+),(\d+)\), averaged", r.stdout)
            got = (int(m[1]), int(m[2]), int(m[5]), int(m[6]), int(m[3]), int(m[4])) if m else None
            if got != want:
                results.append((False, label, f"grid {got}, expected {want}"))
                continue
            if api == "d3d11":
                d = np.abs(load(native) - reference)
                ok = d.max() <= 1.0
                results.append((ok, label, f"small picture vs reference: max {d.max():.2f}, mean {d.mean():.3f}"))
                continue
            expected = nearest_inverted(frame, np.round(reference), (rx, ry, rw, rh))
            shot = load(out)
            d = np.abs(shot - expected)
            outside = np.ones(frame.shape[:2], bool)
            outside[ry:ry + rh, rx:rx + rw] = False
            untouched = bool((shot[outside] == frame[outside]).all())
            ok = d.max() <= 1.0 and untouched
            results.append((ok, label, f"output vs reference: max {d.max():.0f}, mean {d.mean():.3f}"
                                       f"{'' if untouched else ', pixels outside the rectangle changed'}"))

    for ok, label, detail in results:
        print(f"{'n/a ' if ok is None else 'PASS' if ok else 'FAIL'}  {label} ({detail})")
    failed = sum(1 for ok, _, _ in results if ok is False)
    passed = sum(1 for ok, _, _ in results if ok)
    print(f"\n{passed} passed, {failed} failed, {len(results) - passed - failed} n/a")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
