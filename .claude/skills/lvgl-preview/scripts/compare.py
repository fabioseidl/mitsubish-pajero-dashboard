#!/usr/bin/env python3
"""Compare an HTML prototype render with an LVGL simulator render.

Writes one image: HTML | LVGL | diff. In the diff panel, red is ink only the
HTML has (LVGL is missing it or drew it elsewhere), cyan is ink only LVGL has,
yellow is the same brightness in a different colour, and grey is agreement.

Prints a similarity score, the diff bounding box and each side's ink bounding
box, so offsets can be read as numbers rather than eyeballed.

Usage: compare.py html.png lvgl.png out.png [--crop x,y,w,h] [--zoom N] [--tol N]
"""
import argparse
import sys

from PIL import Image, ImageChops, ImageDraw

LABEL_H = 18


def luma(px):
    """Rec. 601 luma of an RGB tuple."""
    r, g, b = px
    return (299 * r + 587 * g + 114 * b) // 1000


def ink_bbox(img, bg):
    """Bounding box of every pixel that differs from the background colour."""
    solid = Image.new("RGB", img.size, bg)
    return ImageChops.difference(img, solid).convert("L").point(lambda v: 255 if v > 24 else 0).getbbox()


def diff_panel(a, b, tol):
    """Builds the colour-coded diff image and returns it with the mismatch count."""
    out = Image.new("RGB", a.size)
    pa, pb, po = a.load(), b.load(), out.load()
    bad = 0
    for y in range(a.size[1]):
        for x in range(a.size[0]):
            ca, cb = pa[x, y], pb[x, y]
            la, lb = luma(ca), luma(cb)
            if max(abs(ca[i] - cb[i]) for i in range(3)) <= tol:
                g = 40 + lb // 3
                po[x, y] = (g, g, g)
                continue
            bad += 1
            if la - lb > tol:
                po[x, y] = (255, 40, 40)
            elif lb - la > tol:
                po[x, y] = (40, 220, 255)
            else:
                po[x, y] = (255, 220, 0)
    return out, bad


def labelled(img, text):
    """Adds a caption strip above an image."""
    canvas = Image.new("RGB", (img.size[0], img.size[1] + LABEL_H), (32, 32, 32))
    canvas.paste(img, (0, LABEL_H))
    ImageDraw.Draw(canvas).text((4, 3), text, fill=(230, 230, 230))
    return canvas


def main():
    """Parses arguments, compares the two renders and writes the report image."""
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("html")
    ap.add_argument("lvgl")
    ap.add_argument("out")
    ap.add_argument("--crop", help="x,y,w,h region to inspect, in panel pixels")
    ap.add_argument("--zoom", type=int, default=1, help="nearest-neighbour scale of the output")
    ap.add_argument("--tol", type=int, default=48, help="per-channel tolerance before a pixel counts as different")
    args = ap.parse_args()

    a = Image.open(args.html).convert("RGB")
    b = Image.open(args.lvgl).convert("RGB")
    if a.size != b.size:
        sys.exit(f"size mismatch: html {a.size} vs lvgl {b.size}")

    if args.crop:
        x, y, w, h = (int(v) for v in args.crop.split(","))
        a, b = a.crop((x, y, x + w, y + h)), b.crop((x, y, x + w, y + h))

    d, bad = diff_panel(a, b, args.tol)
    total = a.size[0] * a.size[1]
    bg = b.getpixel((0, 0))

    print(f"similarity   {100 * (1 - bad / total):.2f}%  ({bad} of {total} px differ, tol {args.tol})")
    print(f"diff bbox    {ImageChops.difference(a, b).convert('L').point(lambda v: 255 if v > args.tol else 0).getbbox()}")
    print(f"html ink     {ink_bbox(a, bg)}")
    print(f"lvgl ink     {ink_bbox(b, bg)}")

    if args.zoom > 1:
        size = (a.size[0] * args.zoom, a.size[1] * args.zoom)
        a, b, d = (im.resize(size, Image.NEAREST) for im in (a, b, d))

    panels = [labelled(a, "HTML"), labelled(b, "LVGL"), labelled(d, "diff: red=HTML only  cyan=LVGL only")]
    gap = 6
    sheet = Image.new("RGB", (sum(p.size[0] for p in panels) + gap * 2, panels[0].size[1]), (90, 90, 90))
    x = 0
    for p in panels:
        sheet.paste(p, (x, 0))
        x += p.size[0] + gap
    sheet.save(args.out)
    print(f"report       {args.out}")


if __name__ == "__main__":
    main()
