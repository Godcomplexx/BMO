"""Convert faces_ref2/rostoNN.png (Figma "2nd Palette" exports) into faces_data.c/.h.

Per face it emits a 160x128 RGB565 bitmap (byte-swapped for SPI), the bounding
boxes of the two eyes (for runtime blinking; w=0 means "no blink") and a flag
for wide faces (smaller animation amplitude so they do not clip).

Usage: python tools/gen_faces.py     (writes faces_data.c/.h and faces_ref2/_eyes.png)
"""
import os
from collections import deque
from PIL import Image, ImageDraw

W, H = 160, 128
FACES = list(range(1, 26))            # Rosto-01 .. Rosto-25
BASE_SCALE = W / 260.0                # tile (320x180) -> panel
here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(here)


def bbox_of_ink(im, bg):
    px = im.load()
    x0, y0, x1, y1 = im.width, im.height, -1, -1
    for y in range(im.height):
        for x in range(im.width):
            r, g, b = px[x, y]
            if abs(r - bg[0]) + abs(g - bg[1]) + abs(b - bg[2]) > 36:
                x0, y0 = min(x0, x), min(y0, y)
                x1, y1 = max(x1, x), max(y1, y)
    return x0, y0, x1, y1


def find_eyes(img):
    """Return [(x, y, w, h), (x, y, w, h)] for the two eyes, or None."""
    px = img.load()
    dark = [[sum(px[x, y]) < 210 for x in range(W)] for y in range(H)]
    seen = [[False] * W for _ in range(H)]
    comps = []
    for y in range(H):
        for x in range(W):
            if not dark[y][x] or seen[y][x]:
                continue
            q = deque([(x, y)])
            seen[y][x] = True
            pts = []
            while q:
                cx, cy = q.popleft()
                pts.append((cx, cy))
                for dx in (-1, 0, 1):
                    for dy in (-1, 0, 1):
                        nx, ny = cx + dx, cy + dy
                        if 0 <= nx < W and 0 <= ny < H and dark[ny][nx] and not seen[ny][nx]:
                            seen[ny][nx] = True
                            q.append((nx, ny))
            xs = [p[0] for p in pts]
            ys = [p[1] for p in pts]
            bx, by = min(xs), min(ys)
            bw, bh = max(xs) - bx + 1, max(ys) - by + 1
            comps.append(dict(x=bx, y=by, w=bw, h=bh, area=len(pts),
                              cx=bx + bw / 2, cy=by + bh / 2))
    cand = []
    for c in comps:
        fill = c["area"] / (c["w"] * c["h"])
        asp = c["w"] / c["h"]
        if (c["area"] >= 25 and 5 <= c["w"] <= 34 and 5 <= c["h"] <= 34
                and 0.3 <= asp <= 3.0 and fill >= 0.3 and c["cy"] < H * 0.62):
            cand.append(c)
    best, best_score = None, 0
    for i, a in enumerate(cand):
        for b in cand[i + 1:]:
            l, r = (a, b) if a["cx"] < b["cx"] else (b, a)
            if not (l["cx"] < W / 2 < r["cx"]):
                continue
            if abs(l["cy"] - r["cy"]) > 14:
                continue
            ratio = l["area"] / r["area"]
            if not 0.5 <= ratio <= 2.0:
                continue
            score = l["area"] + r["area"]
            if score > best_score:
                best, best_score = (l, r), score
    if not best:
        return None
    return [(c["x"], c["y"], c["w"], c["h"]) for c in best]


def convert(path):
    im = Image.open(path).convert("RGB")      # 320x180 tile
    bg = im.getpixel((2, 2))
    x0, y0, x1, y1 = bbox_of_ink(im, bg)
    cx, cy = im.width / 2, im.height / 2
    half_w = max(abs(x0 - cx), abs(x1 - cx), 1)
    half_h = max(abs(y0 - cy), abs(y1 - cy), 1)
    scale = min(BASE_SCALE, (W / 2 - 4) / half_w, (H / 2 - 4) / half_h)
    nw, nh = round(im.width * scale), round(im.height * scale)
    im = im.resize((nw, nh), Image.LANCZOS)
    out = Image.new("RGB", (W, H), bg)
    out.paste(im, (W // 2 - nw // 2, H // 2 - nh // 2))
    return bg, out, scale


def to_words(img):
    px = img.load()
    data = []
    for y in range(H):
        for x in range(W):
            r, g, b = px[x, y]
            v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
            data.append(((v & 0xFF) << 8) | (v >> 8))
    return data


# Faces whose eyes must not blink (hearts, stars, spirals...) and hand-made boxes.
NO_BLINK = {21, 22, 23, 24, 25}
EYE_OVERRIDE = {13: [(25, 39, 30, 30), (105, 39, 30, 30)]}

bg_color = None
eyes_all, wide_all, previews = [], [], []
with open(os.path.join(root, "faces_data.c"), "w") as f:
    f.write('#include "faces_data.h"\n\n')
    for i, n in enumerate(FACES):
        path = os.path.join(root, "faces_ref2", f"rosto{n:02d}.png")
        bg, img, scale = convert(path)
        bg_color = bg_color or bg
        eyes = EYE_OVERRIDE[n] if n in EYE_OVERRIDE else (None if n in NO_BLINK else find_eyes(img))
        eyes_all.append(eyes)
        wide_all.append(1 if scale < 0.55 else 0)
        print(f"rosto{n:02d}: scale {scale:.3f} eyes {eyes}")
        prev = img.copy()
        d = ImageDraw.Draw(prev)
        for e in eyes or []:
            d.rectangle([e[0], e[1], e[0] + e[2] - 1, e[1] + e[3] - 1], outline=(255, 0, 0))
        previews.append(prev)
        words = to_words(img)
        f.write(f"/* rosto{n:02d}.png */\nconst uint16_t face_bitmap_{i}[{W * H}] = {{\n")
        for k in range(0, len(words), 16):
            f.write(",".join(f"0x{v:04X}" for v in words[k:k + 16]) + ",\n")
        f.write("};\n\n")
    f.write("const uint16_t *const face_bitmaps[FACE_BITMAP_COUNT] = {\n")
    f.write(",\n".join(f"face_bitmap_{i}" for i in range(len(FACES))) + "\n};\n\n")
    f.write("/* x, y, w, h of the left and right eye; w == 0: no blinking */\n")
    f.write("const uint8_t face_eyes[FACE_BITMAP_COUNT][8] = {\n")
    for e in eyes_all:
        if e:
            f.write("    {" + ",".join(str(v) for v in e[0] + e[1]) + "},\n")
        else:
            f.write("    {0,0,0,0, 0,0,0,0},\n")
    f.write("};\n\n")
    f.write("const uint8_t face_wide[FACE_BITMAP_COUNT] = {" + ",".join(map(str, wide_all)) + "};\n")

with open(os.path.join(root, "faces_data.h"), "w") as f:
    f.write("#pragma once\n#include <stdint.h>\n\n")
    f.write(f"#define FACE_BITMAP_COUNT {len(FACES)}\n")
    f.write(f"#define FACE_BG_R {bg_color[0]}\n#define FACE_BG_G {bg_color[1]}\n#define FACE_BG_B {bg_color[2]}\n")
    f.write("extern const uint16_t *const face_bitmaps[FACE_BITMAP_COUNT];\n")
    f.write("extern const uint8_t face_eyes[FACE_BITMAP_COUNT][8];\n")
    f.write("extern const uint8_t face_wide[FACE_BITMAP_COUNT];\n")

cols = 5
sheet = Image.new("RGB", (cols * W // 2, ((len(previews) + cols - 1) // cols) * H // 2), "white")
for i, p in enumerate(previews):
    sheet.paste(p.resize((W // 2, H // 2)), ((i % cols) * W // 2, (i // cols) * H // 2))
sheet.save(os.path.join(root, "faces_ref2", "_eyes.png"))
print(len(FACES), "faces,", sum(1 for e in eyes_all if e), "with blinking eyes")
