#!/usr/bin/env python3
"""Draws the toolbar / Notebookbar / sidebar icons (16 and 26 pixels) with Pillow.
Called by gen_ui.py; every icon is drawn on a 104x104 canvas and scaled down."""
import math
import os

from PIL import Image, ImageDraw

S = 104
BLUE, ORANGE, GREEN, RED, GREY, DARK, GOLD, PURPLE, TEAL = (
    (52, 120, 200), (232, 132, 40), (46, 160, 90), (210, 60, 60), (130, 135, 145), (60, 64, 72), (214, 160, 30), (150, 90, 190),
    (30, 150, 160))


def bars(d, heights, color, x0=14, x1=90, base=92, gap=4):
    n = len(heights)
    w = (x1 - x0 - gap * (n - 1)) / n
    for i, h in enumerate(heights):
        x = x0 + i * (w + gap)
        d.rectangle([x, base - h, x + w, base], fill=color)


def curve(d, f, color, width=7, x0=10, x1=94):
    pts = [(x, f((x - x0) / (x1 - x0))) for x in range(x0, x1 + 1, 2)]
    d.line(pts, fill=color, width=width, joint="curve")


def bell(t):
    return 88 - 70 * math.exp(-((t - 0.5) ** 2) / 0.03)


def dots(d, pts, color, r=6):
    for x, y in pts:
        d.ellipse([x - r, y - r, x + r, y + r], fill=color)


def scatter_pts(sign=-1):
    import random
    rnd = random.Random(4)
    out = []
    for _ in range(14):
        t = rnd.random()
        y = 50 + sign * (t - 0.5) * 60 + rnd.uniform(-12, 12)
        out.append((14 + t * 76, y))
    return out


def draw(name):
    im = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    if name == "wizard":
        d.line([(18, 90), (70, 38)], fill=DARK, width=10)
        for cx, cy, r in ((78, 26, 10), (52, 16, 5), (92, 50, 5), (88, 12, 4)):
            d.polygon([(cx, cy - r * 2), (cx + r * 0.6, cy - r * 0.6), (cx + r * 2, cy), (cx + r * 0.6, cy + r * 0.6), (cx, cy + r * 2),
                       (cx - r * 0.6, cy + r * 0.6), (cx - r * 2, cy), (cx - r * 0.6, cy - r * 0.6)], fill=GOLD)
    elif name == "correlation":
        d.line([(10, 94), (10, 10)], fill=GREY, width=4)
        d.line([(10, 94), (96, 94)], fill=GREY, width=4)
        dots(d, scatter_pts(-1), TEAL)
        dots(d, [(18, 20)], RED, 7)
        dots(d, [(88, 84)], GREEN, 7)
    elif name == "output":
        d.ellipse([12, 12, 92, 92], outline=ORANGE, width=8)
        d.ellipse([34, 34, 70, 70], fill=ORANGE)
    elif name == "register":
        d.polygon([(10, 30), (60, 30), (92, 52), (60, 74), (10, 74)], fill=BLUE)
        d.ellipse([20, 46, 32, 58], fill=(255, 255, 255))
        d.line([(40, 44), (68, 44)], fill=(255, 255, 255), width=5)
        d.line([(40, 60), (62, 60)], fill=(255, 255, 255), width=5)
    elif name == "lock":
        d.arc([28, 8, 76, 60], 180, 360, fill=DARK, width=9)
        d.line([(32, 34), (32, 46)], fill=DARK, width=9)
        d.line([(72, 34), (72, 46)], fill=DARK, width=9)
        d.rounded_rectangle([18, 44, 86, 96], 8, fill=RED)
        d.ellipse([46, 60, 58, 72], fill=(255, 255, 255))
    elif name == "fit":
        bars(d, [14, 34, 58, 48, 26, 12], BLUE + (150,))
        curve(d, bell, ORANGE)
    elif name == "model":
        d.rounded_rectangle([18, 8, 86, 96], 6, outline=DARK, width=6, fill=(255, 255, 255))
        for i, c in enumerate((BLUE, BLUE, GOLD, ORANGE)):
            d.rectangle([30, 22 + i * 18, 74, 32 + i * 18], fill=c)
    elif name == "influence":
        nodes = {"a": (20, 22), "b": (20, 82), "c": (54, 52), "o": (88, 52)}
        for p, q in (("a", "c"), ("b", "c"), ("c", "o")):
            d.line([nodes[p], nodes[q]], fill=GREY, width=5)
        for k, (x, y) in nodes.items():
            c = ORANGE if k == "o" else GOLD if k == "c" else BLUE
            d.ellipse([x - 13, y - 13, x + 13, y + 13], fill=c)
    elif name == "mentor":
        d.rounded_rectangle([8, 12, 96, 74], 14, fill=PURPLE)
        d.polygon([(26, 72), (46, 72), (22, 96)], fill=PURPLE)
        dots(d, [(32, 43), (52, 43), (72, 43)], (255, 255, 255), 6)
    elif name == "context":
        d.rectangle([10, 14, 94, 90], outline=DARK, width=5, fill=(255, 255, 255))
        for i in range(1, 4):
            d.line([(10, 14 + i * 19), (94, 14 + i * 19)], fill=GREY, width=3)
        d.line([(40, 14), (40, 90)], fill=GREY, width=3)
        d.rectangle([10, 14, 94, 33], fill=BLUE)
    elif name == "settings":
        cx, cy = 52, 52
        for k in range(8):
            a = k * math.pi / 4
            d.line([(cx + 26 * math.cos(a), cy + 26 * math.sin(a)), (cx + 44 * math.cos(a), cy + 44 * math.sin(a))], fill=GREY, width=14)
        d.ellipse([cx - 32, cy - 32, cx + 32, cy + 32], fill=GREY)
        d.ellipse([cx - 13, cy - 13, cx + 13, cy + 13], fill=(255, 255, 255, 0))
    elif name == "multiple":
        for i, c in enumerate((GREY, BLUE, ORANGE)):
            o = i * 14
            d.rectangle([12 + o, 12 + o, 62 + o, 62 + o], fill=c, outline=(255, 255, 255), width=3)
    elif name == "start":
        d.polygon([(22, 10), (92, 52), (22, 94)], fill=GREEN)
    elif name == "reset":
        d.line([(20, 20), (84, 84)], fill=RED, width=14)
        d.line([(84, 20), (20, 84)], fill=RED, width=14)
    elif name == "statistics":
        bars(d, [10, 24, 46, 70, 56, 34, 18, 8], BLUE)
        d.line([(58, 6), (58, 96)], fill=(200, 30, 90), width=5)
    elif name == "variant":
        for i, c in enumerate((BLUE, ORANGE, GREEN)):
            bars(d, [20 + 18 * i], c, 14 + 26 * i, 36 + 26 * i)
    elif name == "histogram":
        bars(d, [18, 40, 66, 52, 28, 12], ORANGE)
    elif name == "calccorr":
        for r in range(3):
            for c in range(3):
                col = BLUE if r == c else (TEAL if (r + c) % 2 else GOLD)
                d.rectangle([12 + c * 28, 12 + r * 28, 36 + c * 28, 36 + r * 28], fill=col)
    elif name == "correlogram":
        bars(d, [80, 52, 34, 22, 14, 8], TEAL)
        d.line([(10, 70), (96, 70)], fill=RED, width=3)
    elif name == "iteration":
        d.ellipse([10, 10, 76, 76], outline=BLUE, width=9)
        d.line([(66, 66), (94, 94)], fill=BLUE, width=12)
        d.line([(43, 30), (43, 58)], fill=DARK, width=8)
        dots(d, [(43, 22)], DARK, 5)
    elif name == "expected":
        curve(d, bell, BLUE)
        d.line([(52, 8), (52, 96)], fill=ORANGE, width=6)
    elif name == "preview":
        d.rounded_rectangle([12, 12, 92, 92], 14, fill=(255, 255, 255), outline=DARK, width=6)
        dots(d, [(32, 32), (72, 32), (52, 52), (32, 72), (72, 72)], DARK, 8)
    elif name == "store":
        d.rectangle([10, 12, 94, 92], fill=(255, 255, 255), outline=GREEN, width=6)
        d.rectangle([10, 12, 94, 30], fill=GREEN)
        bars(d, [14, 30, 22, 40], GREEN, 22, 82, 84)
    elif name == "save":
        d.rounded_rectangle([12, 10, 92, 94], 6, fill=GREEN)
        d.rectangle([28, 10, 76, 40], fill=(255, 255, 255))
        d.rectangle([60, 16, 70, 34], fill=GREEN)
        d.rectangle([26, 56, 78, 94], fill=(255, 255, 255))
    elif name in ("sortasc", "sortdesc"):
        hs = [20, 36, 52, 68, 84] if name == "sortasc" else [84, 68, 52, 36, 20]
        for i, h in enumerate(hs):
            d.rectangle([12, 10 + i * 18, 12 + h, 22 + i * 18], fill=BLUE)
    elif name == "check":
        d.rectangle([10, 14, 76, 90], outline=DARK, width=5, fill=(255, 255, 255))
        d.line([(30, 56), (50, 80), (96, 20)], fill=GREEN, width=12)
    elif name == "forecast":
        d.line([(10, 70), (26, 58), (40, 66), (56, 44)], fill=BLUE, width=7)
        d.polygon([(56, 44), (96, 10), (96, 60)], fill=ORANGE + (110,))
        d.line([(56, 44), (96, 34)], fill=ORANGE, width=6)
    elif name == "batch":
        for i in range(3):
            d.rectangle([12 + i * 8, 12 + i * 22, 70 + i * 8, 28 + i * 22], fill=BLUE)
        d.polygon([(66, 66), (96, 82), (66, 98)], fill=GREEN)
    elif name in ("hide", "load"):
        d.line([(14, 30), (30, 30)], fill=DARK, width=6)
        d.line([(14, 50), (30, 50)], fill=DARK, width=6)
        d.ellipse([36, 30, 96, 74], outline=DARK, width=6)
        d.ellipse([56, 42, 76, 62], fill=DARK)
        if name == "hide":
            d.line([(36, 90), (96, 14)], fill=RED, width=8)
        else:
            d.polygon([(66, 78), (96, 78), (81, 98)], fill=GREEN)
    elif name == "variables":
        for i in range(4):
            d.rectangle([12, 12 + i * 22, 24, 24 + i * 22], fill=(BLUE, BLUE, GOLD, ORANGE)[i])
            d.line([(32, 18 + i * 22), (92, 18 + i * 22)], fill=DARK, width=6)
    elif name == "about":
        d.ellipse([8, 8, 96, 96], fill=BLUE)
        d.rectangle([46, 44, 58, 80], fill=(255, 255, 255))
        dots(d, [(52, 28)], (255, 255, 255), 7)
    elif name == "deck":
        bars(d, [16, 34, 58, 72, 50, 24], BLUE)
        curve(d, lambda t: 92 - 82 * t ** 1.4, ORANGE, 7)
    else:
        raise SystemExit("gen_icons.py: no drawing for icon " + name)
    return im


def write_all(folder, names):
    os.makedirs(folder, exist_ok=True)
    for n in names:
        im = draw(n)
        for size in (16, 26):
            im.resize((size, size), Image.LANCZOS).save(os.path.join(folder, "%s_%d.png" % (n, size)), optimize=True)
    print("wrote %d icons to %s" % (len(names) * 2, os.path.relpath(folder)))


if __name__ == "__main__":
    import sys
    write_all(sys.argv[1], sys.argv[2:])
