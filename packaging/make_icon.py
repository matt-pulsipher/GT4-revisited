#!/usr/bin/env python3
"""Draws the GT4 Revisited application icon (original artwork, no game assets).

The same family as the shared engine's own icon (a rounded square with a road on it): white,
an S-bend seen from above with navy and blue kerbs, and a 4 in Barlow Condensed SemiBold (the
engine's own UI font). Writes packaging/icon.png (1024x1024) and packaging/icon.ico (Windows).
Needs Pillow:

    python3 packaging/make_icon.py
"""
import math
import os

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
FONT = os.path.join(os.path.dirname(HERE), "engine", "gt3r", "third_party", "fonts", "barlow",
                    "BarlowCondensed-SemiBold.ttf")
S = 1024         # the icon's size; every coordinate below is in this space
SS = 2           # drawn this many times larger, then reduced (smooth edges)
INSET, RADIUS = 32, 200  # the rounded square, as the engine's icon draws it

WHITE = (240, 242, 244, 255)
ASPHALT = (54, 58, 64, 255)
KERB_BLUE = (36, 96, 210, 255)
NAVY = (26, 36, 64, 255)


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


def art():
    """White, an S-bend on the right with kerbs on both apexes, a 4 in the open top left."""
    img = gradient((252, 252, 253), (206, 211, 219))
    d = ImageDraw.Draw(img)
    raw = []
    for i in range(600):
        t = i / 599
        raw.append((585 - 172 * math.sin(2 * math.pi * t) + 40 * (t - 0.5), 1090 - 1160 * t))
    c = resample(raw, 300)
    n = len(c) - 1
    road(d, c, const(116), const(14), const(16), WHITE,
         [(int(n * 0.13), int(n * 0.37), 1), (int(n * 0.63), int(n * 0.87), -1)], const(34), (NAVY, KERB_BLUE))
    font = ImageFont.truetype(FONT, int(400 * SS * 1.36))  # a cap height of about 400
    x, y = 265, 330
    d.text(((x + 8) * SS, (y + 10) * SS), "4", font=font, fill=(180, 186, 198, 255), anchor="mm")  # shadow
    d.text((x * SS, y * SS), "4", font=font, fill=NAVY, anchor="mm")
    return img


def draw(size=S):
    big = S * SS
    mask = Image.new("L", (big, big), 0)
    ImageDraw.Draw(mask).rounded_rectangle((INSET * SS, INSET * SS, big - INSET * SS, big - INSET * SS),
                                           radius=RADIUS * SS, fill=255)
    img = Image.new("RGBA", (big, big), (0, 0, 0, 0))
    img.paste(art(), (0, 0), mask)
    ImageDraw.Draw(img).rounded_rectangle((34 * SS, 34 * SS, big - 34 * SS, big - 34 * SS), radius=198 * SS,
                                          outline=(150, 158, 172, 255), width=10 * SS)
    return img.resize((size, size), Image.LANCZOS)


def main():
    icon = draw()
    icon.save(os.path.join(HERE, "icon.png"))
    icon.save(os.path.join(HERE, "icon.ico"), sizes=[(16, 16), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
    print("icons written")


if __name__ == "__main__":
    main()
