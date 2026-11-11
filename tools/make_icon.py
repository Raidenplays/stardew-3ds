#!/usr/bin/env python3
"""Draws the app icon (icon.png, 48x48) pixel by pixel: a sprout in tilled
soil under a sunny sky, in a wooden frame. Original art.
Usage: make_icon.py OUT.png"""
import sys

from PIL import Image

OUT = (59, 31, 14)          # dark outline
WOOD = [(122, 70, 32), (184, 118, 60), (224, 160, 96)]   # shadow, base, highlight
SKY = [(96, 168, 236), (124, 192, 244), (156, 212, 248)]
SUN = [(240, 160, 40), (255, 210, 74), (255, 244, 170)]
CLOUD = [(206, 228, 246), (255, 255, 255)]
GRASS = [(58, 122, 36), (92, 168, 52), (140, 210, 80)]
SOIL = [(84, 48, 26), (122, 76, 42), (156, 100, 56)]
LEAF = [(36, 92, 28), (84, 170, 54), (150, 222, 96)]


def main():
    W = 48
    im = Image.new("RGBA", (W, W), (0, 0, 0, 0))
    px = im.load()

    def put(x, y, c):
        if 0 <= x < W and 0 <= y < W:
            px[x, y] = c + (255,) if len(c) == 3 else c

    def inside(x, y, inset):   # rounded square, 3px corner cut
        a, b = inset, W - 1 - inset
        if x < a or x > b or y < a or y > b:
            return False
        cx = min(x - a, b - x)
        cy = min(y - a, b - y)
        return cx + cy >= 3 - (1 if inset else 0)

    # frame: outline, wood (lit top-left, shaded bottom-right), inner outline
    for y in range(W):
        for x in range(W):
            if not inside(x, y, 0):
                continue
            if not inside(x, y, 1):
                put(x, y, OUT)
            elif not inside(x, y, 4):
                tl = x + y < W - 1
                edge = not inside(x, y, 2)
                put(x, y, WOOD[2] if (tl and edge) else WOOD[0] if (not tl and not inside(x, y, 3)) else WOOD[1])
            elif not inside(x, y, 5):
                put(x, y, OUT)
    # wood grain flecks
    for (x, y) in [(10, 2), (11, 2), (30, 3), (31, 3), (2, 20), (2, 21), (45, 30), (45, 31), (20, 45), (21, 45)]:
        put(x, y, WOOD[0])

    # sky
    for y in range(5, 29):
        for x in range(5, 43):
            if inside(x, y, 5):
                c = SKY[0] if y < 12 else SKY[1] if y < 21 else SKY[2]
                if y in (11, 20) and x % 2 == 0:   # dithered band edges
                    c = SKY[1] if y == 11 else SKY[2]
                put(x, y, c)
    # sun (top right), outlined with a lit edge
    sx, sy, r = 38, 10, 3
    for y in range(sy - r - 1, sy + r + 2):
        for x in range(sx - r - 1, sx + r + 2):
            d = (x - sx) ** 2 + (y - sy) ** 2
            if d <= r * r:
                put(x, y, SUN[2] if (x - sx) + (y - sy) < -3 else SUN[1] if d < (r - 1) ** 2 + 2 else SUN[0])
    # a little cloud
    for (x, y, c) in [(10, 10, 1), (11, 10, 1), (12, 10, 1), (9, 11, 1), (10, 11, 1), (11, 11, 1), (12, 11, 1),
                      (13, 11, 1), (14, 11, 1), (8, 12, 0), (9, 12, 1), (10, 12, 1), (11, 12, 1), (12, 12, 1),
                      (13, 12, 1), (14, 12, 1), (15, 12, 0), (9, 13, 0), (10, 13, 0), (11, 13, 0), (12, 13, 0),
                      (13, 13, 0), (14, 13, 0)]:
        put(x, y, CLOUD[c])

    # grass strip
    for x in range(5, 43):
        put(x, 29, GRASS[2] if x % 3 else GRASS[1])
        put(x, 30, GRASS[1])
        put(x, 31, GRASS[0])
    for x in range(6, 42, 4):   # tufts
        put(x, 28, GRASS[1])
    # tilled soil: rows with lit ridges and dark furrows
    for y in range(32, 43):
        for x in range(5, 43):
            if not inside(x, y, 5):
                continue
            r = (y - 32) % 4
            c = SOIL[2] if r == 0 else SOIL[1] if r in (1, 2) else SOIL[0]
            if r == 1 and (x * 7 + y) % 9 == 0:
                c = SOIL[0]   # clods
            put(x, y, c)

    # the sprout: a thick stem with two big leaves growing from its tip
    for y in range(17, 34):
        put(22, y, OUT)
        put(23, y, LEAF[2] if y < 26 else LEAF[1])
        put(24, y, LEAF[1])
        put(25, y, LEAF[0])
        put(26, y, OUT)
    for (x, c) in ((21, SOIL[0]), (27, SOIL[0]), (20, SOIL[1]), (28, SOIL[1])):   # mound around the stem
        put(x, 33, c)
    put(21, 32, SOIL[2]); put(27, 32, SOIL[2])
    LEFT = ["....######.",
            "..#########",
            ".##########",
            "##########.",
            "#########..",
            ".#######...",
            "...###....."]
    def leaf(rows, ox, oy, mirror):
        cells = set()
        for j, row in enumerate(rows):
            for i, ch in enumerate(row):
                if ch == "#":
                    x = ox + (len(row) - 1 - i if mirror else i)
                    cells.add((x, oy + j))
        top = min(y for _, y in cells)
        for (x, y) in cells:   # fill: lit upper half, shaded lower half
            put(x, y, LEAF[2] if y - top <= 1 else LEAF[1] if y - top <= 4 else LEAF[0])
        for (x, y) in list(cells):   # outline
            for nx, ny in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
                if (nx, ny) not in cells and not (22 <= nx <= 26 and ny >= 17):
                    put(nx, ny, OUT)
        # the vein, from the stem out to the tip
        for k in range(6):
            vx = ox + (len(rows[0]) - 1 - k if not mirror else k)
            vy = oy + 4 - k // 2
            if (vx, vy) in cells:
                put(vx, vy, LEAF[0])
    leaf(LEFT, 12, 13, False)   # left leaf, its stem end at the stem tip
    leaf(LEFT, 26, 11, True)    # right leaf, a little higher
    for y in range(16, 19):     # stem tip over the leaf joins
        put(23, y, LEAF[1]); put(24, y, LEAF[1]); put(25, y, LEAF[0])
    put(23, 15, OUT); put(24, 15, OUT); put(25, 15, OUT)
    im.save(sys.argv[1])


if __name__ == "__main__":
    main()
