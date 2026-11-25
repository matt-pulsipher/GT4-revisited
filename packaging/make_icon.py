#!/usr/bin/env python3
"""Draws the GT4 Revisited application icon (original artwork, no game assets).

The same family as the shared engine's own icon (a rounded square with a road on it), told
apart by its palette and its road. Writes packaging/icon.png (1024x1024) and
packaging/icon.ico (Windows). Needs Pillow:

    python3 packaging/make_icon.py                                   # the icon
    python3 packaging/make_icon.py --variant hairpin --out <file.png>  # one design to a file

Designs: "esses" (the icon: an S-bend seen from above, on green), "hairpin" (a hairpin seen
from above, on red), "horizon" (a road winding to a low sun).
"""
import argparse
import math
import os

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
S = 1024         # the icon's size; every coordinate below is in this space
SS = 2           # drawn this many times larger, then reduced (smooth edges)
INSET, RADIUS = 32, 200  # the rounded square, as the engine's icon draws it

WHITE = (240, 242, 244, 255)
ASPHALT = (54, 58, 64, 255)
KERB_BLUE = (36, 110, 230, 255)


def gradient(top, bottom):
    """A vertical gradient from top to bottom over the whole canvas."""
    ramp = Image.linear_gradient("L").resize((S * SS, S * SS))
    a = Image.new("RGBA", ramp.size, tuple(top) + (255,))
    b = Image.new("RGBA", ramp.size, tuple(bottom) + (255,))
    return Image.composite(b, a, ramp)


def poly(d, pts, fill):
    d.polygon([(x * SS, y * SS) for x, y in pts], fill=fill)


def resample(pts, n):
    """n+1 points spaced evenly along the polyline pts."""
    dist = [0.0]
    for a, b in zip(pts, pts[1:]):
        dist.append(dist[-1] + math.dist(a, b))
    out, j = [], 0
    for i in range(n + 1):
        s = dist[-1] * i / n
        while j < len(dist) - 2 and dist[j + 1] < s:
            j += 1
        t = (s - dist[j]) / ((dist[j + 1] - dist[j]) or 1)
        out.append((pts[j][0] + (pts[j + 1][0] - pts[j][0]) * t, pts[j][1] + (pts[j + 1][1] - pts[j][1]) * t))
    return out


def offset(pts, off, first=0, last=None):
    """Points first..last of the polyline moved sideways by off(i); positive is to the right
    of the direction of travel."""
    last = len(pts) - 1 if last is None else last
    out = []
    for i in range(first, last + 1):
        a, b = pts[max(i - 1, 0)], pts[min(i + 1, len(pts) - 1)]
        dx, dy = b[0] - a[0], b[1] - a[1]
        ln = math.hypot(dx, dy) or 1
        o = off(i)
        out.append((pts[i][0] - dy / ln * o, pts[i][1] + dx / ln * o))
    return out


def band(d, centre, inner, outer, first, last, fill):
    """The strip between two sideways offsets of centre, over points first..last."""
    poly(d, offset(centre, inner, first, last) + offset(centre, outer, first, last)[::-1], fill)


def road(d, centre, half, edge, dash, dash_color, kerbs=(), kerb=lambda i: 0, kerb_colors=(WHITE, KERB_BLUE),
         dash_every=8, stripe=6):
    """A road along centre: asphalt, white edge lines, a dashed centre line and kerbs. The
    widths are functions of the point index (a road in perspective narrows); kerbs is a list
    of (first, last, side) point ranges, side +1 right of the direction of travel, -1 left."""
    n = len(centre) - 1
    band(d, centre, lambda i: -half(i), half, 0, n, ASPHALT)
    for first, last, side in kerbs:
        for k, i in enumerate(range(first, last, stripe)):
            j = min(i + stripe, last)
            if side > 0:
                band(d, centre, lambda i: half(i) - 1, lambda i: half(i) + kerb(i), i, j, kerb_colors[k % 2])
            else:
                band(d, centre, lambda i: -half(i) - kerb(i), lambda i: -half(i) + 1, i, j, kerb_colors[k % 2])
    for side in (-1, 1):
        band(d, centre, lambda i: side * (half(i) - 2 * edge(i)), lambda i: side * (half(i) - edge(i)), 0, n, WHITE)
    for i in range(0, n, dash_every):
        band(d, centre, lambda i: -dash(i) / 2, lambda i: dash(i) / 2, i, min(i + dash_every // 2, n), dash_color)


def const(v):
    return lambda i: v


def esses():
    """An S-bend seen from above on green, blue and white kerbs on both apexes."""
    img = gradient((28, 124, 86), (6, 42, 32))
    d = ImageDraw.Draw(img)
    raw = []
    for i in range(600):
        t = i / 599
        raw.append((512 - 172 * math.sin(2 * math.pi * t) + 40 * (t - 0.5), 1090 - 1160 * t))
    c = resample(raw, 300)
    n = len(c) - 1
    road(d, c, const(116), const(14), const(16), WHITE,
         [(int(n * 0.13), int(n * 0.37), 1), (int(n * 0.63), int(n * 0.87), -1)], const(34))
    return img, (226, 240, 232, 255)


def hairpin():
    """A hairpin seen from above on red, the kerb on its inside."""
    img = gradient((196, 36, 44), (70, 8, 18))
    d = ImageDraw.Draw(img)
    cx, cy, r = 512, 440, 220
    pts = [(cx - r, 1100), (cx - r, cy)]
    for i in range(1, 121):
        a = math.pi - math.pi * i / 120
        pts.append((cx + r * math.cos(a), cy - r * math.sin(a)))
    pts.append((cx + r, 1100))
    c = resample(pts, 300)
    n = len(c) - 1
    arc0 = int(n * (1100 - cy) / (2 * (1100 - cy) + math.pi * r))
    road(d, c, const(112), const(13), const(15), WHITE, [(arc0 - 6, n - arc0 + 6, 1)], const(34))
    return img, (245, 245, 245, 255)


def horizon():
    """A road winding to a low sun on the horizon."""
    img = gradient((250, 168, 70), (120, 40, 110))
    d = ImageDraw.Draw(img)
    hy = 470
    d.ellipse(((560 - 150) * SS, (hy - 150) * SS, (560 + 150) * SS, (hy + 150) * SS), fill=(255, 226, 150, 255))
    ground = gradient((40, 26, 60), (18, 12, 30))
    img.paste(ground.crop((0, hy * SS, S * SS, S * SS)), (0, hy * SS))
    c = []
    for i in range(301):
        s = i / 300
        p = (1 - s) ** 2
        c.append((560 - 150 * math.sin(4.2 * s) * (1 - s) ** 0.8 - 90 * p, hy + (1090 - hy) * p))
    scale = lambda i: (1 - i / 300) ** 2
    road(d, c, lambda i: 420 * scale(i) + 3, lambda i: 18 * scale(i) + 0.5, lambda i: 22 * scale(i) + 1,
         (255, 214, 90, 255), dash_every=10)
    return img, (255, 200, 110, 255)


VARIANTS = {"esses": esses, "hairpin": hairpin, "horizon": horizon}


def draw(variant="esses", size=S):
    art, rim = VARIANTS[variant]()
    big = S * SS
    mask = Image.new("L", (big, big), 0)
    ImageDraw.Draw(mask).rounded_rectangle((INSET * SS, INSET * SS, big - INSET * SS, big - INSET * SS),
                                           radius=RADIUS * SS, fill=255)
    img = Image.new("RGBA", (big, big), (0, 0, 0, 0))
    img.paste(art, (0, 0), mask)
    ImageDraw.Draw(img).rounded_rectangle((34 * SS, 34 * SS, big - 34 * SS, big - 34 * SS), radius=198 * SS,
                                          outline=rim, width=10 * SS)
    return img.resize((size, size), Image.LANCZOS)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--variant", default="esses", choices=sorted(VARIANTS))
    ap.add_argument("--out", help="write this design to one PNG instead of the packaging icons")
    args = ap.parse_args()
    icon = draw(args.variant)
    if args.out:
        icon.save(args.out)
        return
    icon.save(os.path.join(HERE, "icon.png"))
    icon.save(os.path.join(HERE, "icon.ico"), sizes=[(16, 16), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
    print("icons written")


if __name__ == "__main__":
    main()
