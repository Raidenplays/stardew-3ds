#!/usr/bin/env python3
"""
Builds a small PLACEHOLDER world shaped like an unpacked Content folder:
Farm, FarmHouse, BusStop, Town, SeedShop, with simple original art.
Used for tests and for engine screenshots when the real game files
aren't around. It is not Stardew's art or maps.

    python3 tests/make_fake_content.py fake
    python3 tools/convert.py "fake/Content (unpacked)" out
"""
import json
import os
import random
import sys

from PIL import Image, ImageDraw

OUT = sys.argv[1] if len(sys.argv) > 1 else "fake"
C = os.path.join(OUT, "Content (unpacked)")
R = random.Random(7)


def save(img, rel):
    p = os.path.join(C, rel + ".png")
    os.makedirs(os.path.dirname(p), exist_ok=True)
    img.save(p)


def shade(c, k):
    return tuple(max(0, min(255, int(v * k))) for v in c[:3]) + ((c[3],) if len(c) > 3 else (255,))


# ------------------------------------------------------------------ outdoor sheet
# tile ids (25 columns)
GRASS, WATER, WATER2, PATH, GRASS2, FENCE, TRUNK, CANOPY, ROOF, WALL, DOOR, WINDOW, FLOWERS, \
    PLAZA, SIGN, BUSH, ROOF_EDGE, CLIFF = range(18)

SEASON_GRASS = {"spring": (96, 168, 72), "summer": (78, 150, 60), "fall": (176, 140, 66), "winter": (226, 232, 240)}
SEASON_LEAF = {"spring": (54, 120, 52), "summer": (40, 104, 44), "fall": (190, 96, 40), "winter": (200, 210, 220)}


def outdoor_sheet(season):
    img = Image.new("RGBA", (400, 1264), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    g = SEASON_GRASS[season]
    leaf = SEASON_LEAF[season]

    def cell(i):
        return (i % 25) * 16, (i // 25) * 16

    def grass(i, base, specks=True):
        x, y = cell(i)
        d.rectangle([x, y, x + 15, y + 15], fill=base)
        if specks:
            for _ in range(10):
                px, py = R.randrange(16), R.randrange(15)
                d.line([x + px, y + py, x + px, y + py + 1], fill=shade(base, 0.82))
            for _ in range(4):
                px, py = R.randrange(16), R.randrange(16)
                d.point((x + px, y + py), fill=shade(base, 1.15))

    grass(GRASS, g)
    grass(GRASS2, shade(g, 0.9))
    for i, off in ((WATER, 0), (WATER2, 4)):
        x, y = cell(i)
        d.rectangle([x, y, x + 15, y + 15], fill=(62, 128, 196))
        for k in range(3):
            yy = y + 3 + k * 5
            d.line([x + (off + k * 3) % 12, yy, x + (off + k * 3) % 12 + 4, yy], fill=(150, 200, 240))
    x, y = cell(PATH)
    d.rectangle([x, y, x + 15, y + 15], fill=(196, 160, 110))
    for _ in range(6):
        px, py = R.randrange(15), R.randrange(15)
        d.rectangle([x + px, y + py, x + px + 1, y + py], fill=(160, 126, 84))
    x, y = cell(FENCE)
    d.rectangle([x + 2, y + 2, x + 4, y + 15], fill=(140, 92, 50))
    d.rectangle([x + 11, y + 2, x + 13, y + 15], fill=(140, 92, 50))
    d.rectangle([x, y + 5, x + 15, y + 7], fill=(170, 116, 64))
    d.rectangle([x, y + 10, x + 15, y + 12], fill=(170, 116, 64))
    x, y = cell(TRUNK)
    grass(TRUNK, g)
    d.rectangle([x + 6, y, x + 9, y + 13], fill=(110, 72, 40))
    d.ellipse([x + 3, y + 11, x + 12, y + 15], fill=shade(g, 0.7))
    x, y = cell(CANOPY)
    d.ellipse([x - 1, y + 1, x + 16, y + 17], fill=leaf, outline=shade(leaf, 0.7))
    d.ellipse([x + 3, y + 3, x + 8, y + 8], fill=shade(leaf, 1.2))
    x, y = cell(ROOF)
    d.rectangle([x, y, x + 15, y + 15], fill=(176, 64, 52))
    for k in range(0, 16, 4):
        d.line([x, y + k, x + 15, y + k], fill=(132, 44, 36))
    x, y = cell(ROOF_EDGE)
    d.rectangle([x, y, x + 15, y + 15], fill=(176, 64, 52))
    d.rectangle([x, y + 12, x + 15, y + 15], fill=(110, 40, 32))
    x, y = cell(WALL)
    d.rectangle([x, y, x + 15, y + 15], fill=(226, 196, 140))
    for k in range(0, 16, 5):
        d.line([x, y + k, x + 15, y + k], fill=(196, 160, 106))
    x, y = cell(DOOR)
    d.rectangle([x, y, x + 15, y + 15], fill=(226, 196, 140))
    d.rectangle([x + 3, y + 1, x + 12, y + 15], fill=(120, 72, 36), outline=(80, 46, 22))
    d.point((x + 10, y + 9), fill=(240, 210, 90))
    x, y = cell(WINDOW)
    d.rectangle([x, y, x + 15, y + 15], fill=(226, 196, 140))
    d.rectangle([x + 3, y + 3, x + 12, y + 11], fill=(120, 180, 220), outline=(90, 60, 30))
    d.line([x + 7, y + 3, x + 7, y + 11], fill=(90, 60, 30))
    grass(FLOWERS, g)
    x, y = cell(FLOWERS)
    for (px, py, col) in ((3, 4, (240, 90, 120)), (10, 3, (250, 220, 80)), (6, 10, (180, 120, 240)), (12, 11, (250, 250, 250))):
        d.rectangle([x + px, y + py, x + px + 1, y + py + 1], fill=col)
    x, y = cell(PLAZA)
    d.rectangle([x, y, x + 15, y + 15], fill=(176, 172, 160))
    d.line([x, y + 7, x + 15, y + 7], fill=(150, 146, 136))
    d.line([x + 7, y, x + 7, y + 7], fill=(150, 146, 136))
    d.line([x + 3, y + 8, x + 3, y + 15], fill=(150, 146, 136))
    d.line([x + 11, y + 8, x + 11, y + 15], fill=(150, 146, 136))
    x, y = cell(SIGN)
    d.rectangle([x + 2, y + 2, x + 13, y + 9], fill=(196, 150, 90), outline=(110, 72, 36))
    d.rectangle([x + 7, y + 10, x + 8, y + 15], fill=(110, 72, 36))
    x, y = cell(BUSH)
    d.ellipse([x, y + 2, x + 15, y + 16], fill=shade(leaf, 0.95), outline=shade(leaf, 0.6))
    d.ellipse([x + 3, y + 4, x + 7, y + 8], fill=shade(leaf, 1.25))
    x, y = cell(CLIFF)
    d.rectangle([x, y, x + 15, y + 15], fill=(130, 110, 90))
    d.line([x, y + 5, x + 15, y + 5], fill=(100, 84, 68))
    d.line([x, y + 11, x + 15, y + 11], fill=(100, 84, 68))
    return img


# ------------------------------------------------------------------ interior sheet
FLOOR, IWALL, IWALL_TOP, COUNTER, SHELF, RUG, MAT, BED_T, BED_T2, BED_M, BED_M2, BED_B, BED_B2, ITABLE, IWINDOW = range(15)


def interior_sheet():
    img = Image.new("RGBA", (256, 256), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    def cell(i):
        return (i % 16) * 16, (i // 16) * 16

    x, y = cell(FLOOR)
    d.rectangle([x, y, x + 15, y + 15], fill=(186, 128, 76))
    for k in (0, 5, 10, 15):
        d.line([x, y + k, x + 15, y + k], fill=(150, 98, 54))
    d.line([x + 8, y, x + 8, y + 4], fill=(150, 98, 54))
    d.line([x + 3, y + 6, x + 3, y + 9], fill=(150, 98, 54))
    x, y = cell(IWALL)
    d.rectangle([x, y, x + 15, y + 15], fill=(236, 214, 170))
    d.rectangle([x, y + 13, x + 15, y + 15], fill=(150, 98, 54))
    x, y = cell(IWALL_TOP)
    d.rectangle([x, y, x + 15, y + 15], fill=(92, 60, 40))
    x, y = cell(COUNTER)
    d.rectangle([x, y, x + 15, y + 15], fill=(132, 84, 46))
    d.rectangle([x, y, x + 15, y + 4], fill=(196, 140, 84))
    x, y = cell(SHELF)
    d.rectangle([x, y, x + 15, y + 15], fill=(120, 76, 42))
    for k, col in enumerate(((220, 80, 60), (90, 160, 80), (240, 200, 80), (90, 120, 200))):
        d.rectangle([x + 1 + k * 4, y + 3, x + 3 + k * 4, y + 6], fill=col)
        d.rectangle([x + 1 + ((k + 2) % 4) * 4, y + 10, x + 3 + ((k + 2) % 4) * 4, y + 13], fill=col)
    x, y = cell(RUG)
    d.rectangle([x, y, x + 15, y + 15], fill=(176, 60, 56))
    d.rectangle([x + 2, y + 2, x + 13, y + 13], outline=(230, 190, 110))
    x, y = cell(MAT)
    d.rectangle([x, y, x + 15, y + 15], fill=(186, 128, 76))
    d.rectangle([x + 1, y + 4, x + 14, y + 12], fill=(110, 140, 80))
    for i, (top, side) in enumerate(((BED_T, 0), (BED_T2, 1), (BED_M, 0), (BED_M2, 1), (BED_B, 0), (BED_B2, 1))):
        x, y = cell(top)
        row = i // 2
        d.rectangle([x, y, x + 15, y + 15], fill=(110, 70, 40))
        if row == 0:
            d.rectangle([x + (2 if side == 0 else 0), y + 4, x + (15 if side == 0 else 13), y + 15], fill=(250, 250, 240))
        else:
            d.rectangle([x + (2 if side == 0 else 0), y, x + (15 if side == 0 else 13), y + (15 if row == 1 else 11)],
                        fill=(70, 110, 190))
    x, y = cell(ITABLE)
    d.rectangle([x + 1, y + 3, x + 14, y + 10], fill=(160, 104, 60), outline=(110, 70, 40))
    d.rectangle([x + 2, y + 10, x + 3, y + 15], fill=(110, 70, 40))
    d.rectangle([x + 12, y + 10, x + 13, y + 15], fill=(110, 70, 40))
    x, y = cell(IWINDOW)
    d.rectangle([x, y, x + 15, y + 15], fill=(236, 214, 170))
    d.rectangle([x + 3, y + 2, x + 12, y + 11], fill=(150, 200, 240), outline=(120, 80, 40))
    return img


# ------------------------------------------------------------------ TMX writer
def write_tmx(name, w, h, tilesets, layers, props, objects):
    """tilesets: list of (firstgid, image, cols, count, {tid: {prop: val}}, {tid: [(frame,ms)]})
    layers: dict name -> list of gids; objects: list of (layer, x, y, w, h, {props})"""
    out = ['<?xml version="1.0" encoding="UTF-8"?>',
           f'<map version="1.10" orientation="orthogonal" renderorder="right-down" width="{w}" height="{h}" tilewidth="16" tileheight="16" infinite="0">']
    if props:
        out.append(" <properties>")
        for k, v in props.items():
            out.append(f'  <property name="{k}" value="{v}"/>')
        out.append(" </properties>")
    for (first, image, cols, count, tprops, anims) in tilesets:
        rows = (count + cols - 1) // cols
        out.append(f' <tileset firstgid="{first}" name="{image}" tilewidth="16" tileheight="16" tilecount="{count}" columns="{cols}">')
        out.append(f'  <image source="{image}.png" width="{cols * 16}" height="{rows * 16}"/>')
        for tid in sorted(set(tprops) | set(anims)):
            out.append(f'  <tile id="{tid}">')
            if tid in tprops:
                out.append("   <properties>")
                for k, v in tprops[tid].items():
                    out.append(f'    <property name="{k}" value="{v}"/>')
                out.append("   </properties>")
            if tid in anims:
                out.append("   <animation>")
                for fr, ms in anims[tid]:
                    out.append(f'    <frame tileid="{fr}" duration="{ms}"/>')
                out.append("   </animation>")
            out.append("  </tile>")
        out.append(" </tileset>")
    for lname in ("Back", "Buildings", "Paths", "Front", "AlwaysFront"):
        data = layers.get(lname, [0] * (w * h))
        rows = [",".join(str(v) for v in data[r * w:(r + 1) * w]) for r in range(h)]
        out.append(f' <layer name="{lname}" width="{w}" height="{h}"><data encoding="csv">')
        out.append(",\n".join(rows))
        out.append(" </data></layer>")
    groups = {}
    for o in objects:
        groups.setdefault(o[0], []).append(o)
    oid = 1
    for g, objs in groups.items():
        out.append(f' <objectgroup name="{g}">')
        for (_, x, y, ow, oh, pr) in objs:
            out.append(f'  <object id="{oid}" name="TileData" x="{x * 16}" y="{y * 16}" width="{ow * 16}" height="{oh * 16}">')
            out.append("   <properties>")
            for k, v in pr.items():
                out.append(f'    <property name="{k}" value="{v}"/>')
            out.append("   </properties>")
            out.append("  </object>")
            oid += 1
        out.append(" </objectgroup>")
    out.append("</map>")
    p = os.path.join(C, "Maps", name + ".tmx")
    os.makedirs(os.path.dirname(p), exist_ok=True)
    with open(p, "w") as f:
        f.write("\n".join(out))


OUT_TS = (1, "spring_outdoorsTileSheet", 25, 25 * 79,
          {GRASS: {"Diggable": "T", "Spawnable": "T", "Type": "Grass"}, GRASS2: {"Diggable": "T", "Type": "Grass"},
           FLOWERS: {"Diggable": "T"}, PATH: {"Type": "Dirt"},
           WATER: {"Water": "T"}, WATER2: {"Water": "T"}},
          {WATER: [(WATER, 400), (WATER2, 400)]})
IN_TS = (1, "townInterior", 16, 256, {}, {})


class Grid:
    def __init__(self, w, h):
        self.w, self.h = w, h
        self.L = {k: [0] * (w * h) for k in ("Back", "Buildings", "Paths", "Front", "AlwaysFront")}

    def set(self, layer, x, y, tid):
        if 0 <= x < self.w and 0 <= y < self.h:
            self.L[layer][y * self.w + x] = tid + 1   # gid, firstgid 1

    def fill(self, layer, x0, y0, x1, y1, tid):
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                self.set(layer, x, y, tid)

    def tree(self, x, y):
        self.set("Buildings", x, y, TRUNK)
        self.set("Front", x, y - 1, CANOPY)


def make_maps():
    # ---- Farm 80x65
    f = Grid(80, 65)
    for y in range(65):
        for x in range(80):
            f.set("Back", x, y, GRASS if R.random() > 0.12 else GRASS2)
    for x in range(80):
        if x % 2 == 0:
            f.tree(x, 1)
            f.tree(x, 64)
    for y in range(2, 64):
        if y % 2 == 0:
            f.tree(0, y)
            if not 13 <= y <= 19:
                f.tree(79, y)
        if not 13 <= y <= 19:
            f.set("Buildings", 79, y, BUSH)
    f.fill("Back", 20, 40, 28, 46, WATER)
    f.fill("Back", 64, 18, 64, 24, PATH)
    f.fill("Back", 64, 15, 79, 17, PATH)
    for (x, y) in ((40, 10), (45, 30), (12, 20), (30, 55), (55, 50)):
        f.set("Back", x, y, FLOWERS)
    for x in range(50, 58):
        f.set("Buildings", x, 27, FENCE)
    warps = " ".join(f"80 {y} BusStop 1 {y + 6}" for y in range(14, 19))
    write_tmx("Farm", 80, 65, [OUT_TS], f.L, {"Outdoors": "T", "Warp": warps}, [])

    # ---- FarmHouse 12x12
    h = Grid(12, 12)
    h.fill("Back", 0, 0, 11, 11, FLOOR)
    h.fill("Buildings", 0, 0, 11, 1, IWALL_TOP)
    h.fill("Buildings", 0, 2, 11, 2, IWALL)
    h.set("Buildings", 4, 2, IWINDOW)
    h.fill("Buildings", 0, 0, 0, 11, IWALL_TOP)
    h.fill("Buildings", 11, 0, 11, 11, IWALL_TOP)
    for i, t in enumerate((BED_T, BED_T2, BED_M, BED_M2, BED_B, BED_B2)):
        h.set("Buildings", 9 + i % 2, 4 + i // 2, t)
    h.fill("Back", 4, 6, 7, 8, RUG)
    h.set("Buildings", 6, 4, ITABLE)
    h.set("Back", 3, 11, MAT)
    write_tmx("FarmHouse", 12, 12, [IN_TS], h.L, {"Warp": "3 12 Farm 64 18"}, [])

    # ---- BusStop 30x30
    b = Grid(30, 30)
    b.fill("Back", 0, 0, 29, 29, GRASS)
    b.fill("Back", 0, 20, 29, 24, PATH)
    for x in range(0, 30, 2):
        b.tree(x, 2)
        b.tree(x, 29)
    b.fill("Buildings", 0, 3, 29, 17, CLIFF)
    b.set("Buildings", 14, 18, SIGN)
    warps = " ".join(f"-1 {y} Farm 78 {y - 6}" for y in range(20, 25)) + " " + \
        " ".join(f"30 {y} Town 1 {y + 8}" for y in range(20, 25))
    write_tmx("BusStop", 30, 30, [OUT_TS], b.L, {"Outdoors": "T", "Warp": warps}, [])

    # ---- Town 60x45
    t = Grid(60, 45)
    t.fill("Back", 0, 0, 59, 44, GRASS)
    t.fill("Back", 0, 28, 59, 32, PATH)
    t.fill("Back", 20, 22, 44, 27, PLAZA)
    t.fill("Back", 38, 33, 43, 38, WATER)
    t.fill("Back", 28, 22, 28, 27, PATH)
    for x in range(0, 60, 2):
        t.tree(x, 2)
    # Pierre's
    t.fill("Buildings", 23, 12, 33, 15, ROOF)
    t.fill("Buildings", 23, 16, 33, 16, ROOF_EDGE)
    t.fill("Buildings", 23, 17, 33, 21, WALL)
    for wx in (25, 31):
        t.set("Buildings", wx, 19, WINDOW)
    t.set("Buildings", 28, 21, DOOR)
    t.set("Front", 28, 18, SIGN)
    # a couple of houses
    for hx in (5, 45):
        t.fill("Buildings", hx, 15, hx + 6, 17, ROOF)
        t.fill("Buildings", hx, 18, hx + 6, 18, ROOF_EDGE)
        t.fill("Buildings", hx, 19, hx + 6, 21, WALL)
        t.set("Buildings", hx + 3, 21, DOOR)
        t.set("Buildings", hx + 1, 20, WINDOW)
        t.set("Buildings", hx + 5, 20, WINDOW)
    for (x, y) in ((12, 35), (18, 40), (50, 38), (34, 40)):
        t.tree(x, y)
    for (x, y) in ((21, 26), (44, 23), (10, 26)):
        t.set("Back", x, y, FLOWERS)
    warps = " ".join(f"-1 {y} BusStop 28 {y - 8}" for y in range(28, 33))
    objs = [("Buildings", 28, 21, 1, 1, {"Action": "LockedDoorWarp 6 15 SeedShop 900 2100"})]
    write_tmx("Town", 60, 45, [OUT_TS], t.L, {"Outdoors": "T", "Warp": warps}, objs)

    # ---- SeedShop 14x17
    s = Grid(14, 17)
    s.fill("Back", 0, 0, 13, 16, FLOOR)
    s.fill("Buildings", 0, 0, 13, 1, IWALL_TOP)
    s.fill("Buildings", 0, 2, 13, 2, IWALL)
    s.fill("Buildings", 0, 0, 0, 16, IWALL_TOP)
    s.fill("Buildings", 13, 0, 13, 16, IWALL_TOP)
    s.fill("Buildings", 1, 3, 12, 3, SHELF)
    s.fill("Buildings", 2, 8, 7, 8, COUNTER)
    s.fill("Buildings", 9, 11, 12, 11, SHELF)
    s.fill("Back", 5, 12, 8, 14, RUG)
    s.set("Back", 6, 16, MAT)
    objs = [("Buildings", 4, 8, 1, 1, {"Action": "OpenShop SeedShop down"})]
    write_tmx("SeedShop", 14, 17, [IN_TS], s.L, {"Warp": "6 17 Town 28 22 7 17 Town 28 22"}, objs)


# ------------------------------------------------------------------ sprites
def objects_sheet():
    img = Image.new("RGBA", (384, 33 * 16), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    def at(i):
        return (i % 24) * 16, (i // 24) * 16

    def seedbag(i, col):
        x, y = at(i)
        d.polygon([(x + 3, y + 5), (x + 12, y + 5), (x + 14, y + 14), (x + 1, y + 14)], fill=(214, 182, 126), outline=(122, 85, 48))
        d.rectangle([x + 5, y + 7, x + 10, y + 11], fill=col)
        d.line([x + 4, y + 4, x + 11, y + 4], fill=(122, 85, 48))

    for i, col in ((472, (240, 230, 190)), (473, (120, 190, 90)), (474, (240, 240, 230)), (475, (200, 160, 100)), (477, (80, 150, 70))):
        seedbag(i, col)
    x, y = at(24)   # parsnip
    d.polygon([(x + 5, y + 6), (x + 10, y + 6), (x + 8, y + 15)], fill=(242, 230, 194), outline=(156, 133, 80))
    d.line([x + 7, y + 6, x + 4, y + 1], fill=(78, 154, 58), width=2)
    d.line([x + 8, y + 6, x + 11, y + 1], fill=(78, 154, 58), width=2)
    x, y = at(190)   # cauliflower
    d.ellipse([x + 2, y + 4, x + 13, y + 13], fill=(244, 240, 220), outline=(170, 160, 120))
    d.arc([x, y + 6, x + 15, y + 15], 0, 180, fill=(80, 150, 70), width=2)
    x, y = at(192)   # potato
    d.ellipse([x + 3, y + 5, x + 13, y + 13], fill=(196, 150, 96), outline=(130, 90, 50))
    x, y = at(250)   # kale
    d.polygon([(x + 8, y + 1), (x + 14, y + 8), (x + 8, y + 15), (x + 2, y + 8)], fill=(70, 140, 70), outline=(40, 90, 40))
    x, y = at(188)   # bean
    d.arc([x + 2, y + 2, x + 14, y + 14], 200, 340, fill=(90, 170, 70), width=3)
    x, y = at(388)   # wood
    d.rectangle([x + 2, y + 6, x + 13, y + 11], fill=(154, 98, 52), outline=(92, 54, 24))
    d.ellipse([x + 11, y + 5, x + 15, y + 12], fill=(217, 168, 106), outline=(92, 54, 24))
    x, y = at(390)   # stone
    d.polygon([(x + 2, y + 13), (x + 5, y + 5), (x + 11, y + 4), (x + 14, y + 13)], fill=(150, 152, 158), outline=(74, 79, 85))
    x, y = at(771)   # fiber
    for k in range(3):
        d.line([x + 5 + k * 3, y + 14, x + 4 + k * 4, y + 2], fill=(94, 154, 58))
    for i, col in ((378, (200, 120, 70)), (334, (220, 140, 80)), (382, (40, 40, 44)), (398, (120, 60, 160))):
        x, y = at(i)
        d.ellipse([x + 3, y + 4, x + 12, y + 13], fill=col, outline=(30, 30, 30))
    x, y = at(145)   # perch
    d.ellipse([x + 2, y + 5, x + 12, y + 11], fill=(110, 150, 90), outline=(40, 70, 40))
    d.polygon([(x + 12, y + 8), (x + 15, y + 5), (x + 15, y + 11)], fill=(110, 150, 90))
    x, y = at(168)   # trash
    d.rectangle([x + 4, y + 4, x + 11, y + 13], fill=(130, 130, 120), outline=(60, 60, 50))
    x, y = at(344)   # jelly
    d.rectangle([x + 4, y + 4, x + 11, y + 14], fill=(150, 60, 120), outline=(60, 30, 50))
    x, y = at(599)   # sprinkler
    d.ellipse([x + 3, y + 8, x + 12, y + 14], fill=(150, 150, 160), outline=(60, 60, 70))
    d.line([x + 7, y + 8, x + 7, y + 3], fill=(60, 60, 70), width=2)
    for i in (674, 675):   # weeds
        x, y = at(i)
        for k in range(4):
            d.line([x + 3 + k * 3, y + 15, x + 2 + k * 3 + (i - 674) * 2, y + 4 + (k % 2) * 3], fill=(60, 130, 50), width=2)
    for i in (343, 450):   # stones on the ground
        x, y = at(i)
        d.ellipse([x + 1, y + 5, x + 14, y + 15], fill=(140, 142, 148), outline=(70, 72, 78))
        d.ellipse([x + 4, y + 7, x + 7, y + 9], fill=(180, 182, 188))
    for i in (294, 295):   # twigs
        x, y = at(i)
        d.line([x + 2, y + 12, x + 14, y + 6], fill=(110, 70, 36), width=2)
        d.line([x + 8, y + 9, x + 10, y + 4], fill=(110, 70, 36), width=1)
    return img


def craftables_sheet():
    img = Image.new("RGBA", (128, 64), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    # 0 chest
    x = 0
    d.rectangle([x + 1, 14, x + 14, 30], fill=(160, 104, 52), outline=(84, 50, 22))
    d.rectangle([x + 1, 14, x + 14, 19], fill=(186, 128, 66), outline=(84, 50, 22))
    d.rectangle([x + 7, 18, x + 8, 22], fill=(240, 200, 80))
    # 1 furnace
    x = 16
    d.polygon([(x + 2, 31), (x + 3, 10), (x + 12, 10), (x + 13, 31)], fill=(120, 116, 112), outline=(60, 58, 56))
    d.rectangle([x + 5, 20, x + 10, 28], fill=(40, 20, 12))
    d.rectangle([x + 6, 23, x + 9, 27], fill=(240, 120, 40))
    # 2 jar
    x = 32
    d.rectangle([x + 3, 12, x + 12, 30], fill=(200, 220, 230), outline=(90, 110, 120))
    d.rectangle([x + 4, 9, x + 11, 12], fill=(170, 120, 70), outline=(90, 60, 30))
    d.rectangle([x + 5, 18, x + 10, 28], fill=(150, 60, 120))
    return img


def tree_sheet():
    """Original placeholder tree sheet in the 144x160 layout the engine reads."""
    img = Image.new("RGBA", (144, 160), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.rectangle([20, 60, 27, 95], fill=(120, 80, 44), outline=(70, 44, 20))      # trunk
    d.ellipse([2, 4, 45, 70], fill=(70, 140, 60), outline=(36, 84, 32))          # canopy
    d.ellipse([10, 14, 26, 30], fill=(96, 170, 80))
    d.rectangle([35, 114, 44, 127], fill=(120, 80, 44), outline=(70, 44, 20))    # stump
    d.ellipse([35, 110, 44, 117], fill=(200, 160, 110), outline=(70, 44, 20))
    d.rectangle([6, 104, 9, 127], fill=(120, 80, 44))                            # sapling
    d.ellipse([1, 96, 14, 112], fill=(70, 140, 60))
    d.ellipse([36, 134, 43, 140], fill=(150, 100, 50))                           # seed
    d.line([7, 142, 7, 134], fill=(70, 140, 60), width=2)                        # sprout
    d.ellipse([18, 132, 29, 142], fill=(70, 140, 60))                            # small
    return img


def crops_sheet():
    img = Image.new("RGBA", (256, 3 * 32), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    produce = [(242, 230, 194), (244, 240, 220), (196, 150, 96), (70, 140, 70), (90, 170, 70)]
    for idx, col in enumerate(produce):
        bx, by = (idx % 2) * 128, (idx // 2) * 32
        for f in range(8):
            x, y = bx + f * 16, by
            if f <= 1:
                for k in range(3):
                    d.point((x + 5 + k * 3, y + 27 - (k + f) % 2), fill=(110, 80, 50))
                continue
            hgt = min(f, 5) * 3
            d.line([x + 8, y + 30, x + 8, y + 30 - hgt], fill=(70, 150, 60), width=2)
            d.ellipse([x + 3, y + 30 - hgt, x + 8, y + 33 - hgt], fill=(80, 170, 70))
            d.ellipse([x + 8, y + 28 - hgt, x + 13, y + 31 - hgt], fill=(80, 170, 70))
            if f >= 5:
                d.ellipse([x + 4, y + 22, x + 12, y + 30], fill=col, outline=shade(col, 0.6))
    return img


def tools_sheet():
    img = Image.new("RGBA", (336, 64), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    # hoe 0, can 1, axe 2, pickaxe 3, scythe 4
    x = 0
    d.line([x + 3, x + 14, x + 12, 4], fill=(139, 90, 43), width=2)
    d.polygon([(x + 10, 1), (x + 15, 1), (x + 15, 5), (x + 12, 5)], fill=(154, 164, 174), outline=(74, 79, 85))
    x = 16
    d.rectangle([x + 3, 6, x + 11, 14], fill=(62, 143, 176), outline=(31, 78, 99))
    d.line([x + 11, 9, x + 15, 5], fill=(31, 78, 99), width=2)
    d.arc([x + 4, 2, x + 10, 10], 180, 360, fill=(31, 78, 99))
    x = 32
    d.line([x + 4, 15, x + 11, 2], fill=(139, 90, 43), width=2)
    d.polygon([(x + 9, 1), (x + 15, 3), (x + 13, 8), (x + 9, 6)], fill=(154, 164, 174), outline=(74, 79, 85))
    x = 48
    d.line([x + 5, 15, x + 10, 4], fill=(139, 90, 43), width=2)
    d.arc([x + 1, 1, x + 15, 12], 190, 350, fill=(110, 119, 129), width=2)
    x = 64
    d.line([x + 6, 15, x + 9, 3], fill=(139, 90, 43), width=2)
    d.polygon([(x + 9, 3), (x + 15, 5), (x + 14, 8), (x + 9, 5)], fill=(185, 194, 202), outline=(74, 79, 85))
    x = 112  # rod
    d.line([x + 2, 15, x + 14, 1], fill=(139, 90, 43), width=2)
    d.line([x + 14, 1, x + 14, 10], fill=(220, 220, 220))
    x = 80   # copper hoe
    d.line([x + 3, 14, x + 12, 4], fill=(139, 90, 43), width=2)
    d.polygon([(x + 10, 1), (x + 15, 1), (x + 15, 5), (x + 12, 5)], fill=(214, 134, 74), outline=(120, 66, 30))
    x = 96   # copper can
    d.rectangle([x + 3, 6, x + 11, 14], fill=(214, 134, 74), outline=(120, 66, 30))
    d.line([x + 11, 9, x + 15, 5], fill=(120, 66, 30), width=2)
    d.arc([x + 4, 2, x + 10, 10], 180, 360, fill=(120, 66, 30))
    return img


def farmer_sheets():
    base = Image.new("RGBA", (96, 96), (0, 0, 0, 0))
    d = ImageDraw.Draw(base)
    skin, shirt, pants, shoe = (246, 196, 150), (70, 120, 190), (90, 70, 50), (60, 40, 30)
    for row in range(3):
        for col in range(6):
            x, y = col * 16, row * 32
            step = (0, 1, -1, 0, 0, 0)[col] if col < 3 else 0
            d.ellipse([x + 4, y + 6, x + 11, y + 14], fill=skin, outline=(160, 110, 80))
            d.rectangle([x + 4, y + 15, x + 11, y + 22], fill=shirt, outline=(40, 70, 120))
            d.rectangle([x + 2, y + 16, x + 3, y + 21], fill=skin)
            d.rectangle([x + 12, y + 16, x + 13, y + 21], fill=skin)
            d.rectangle([x + 4, y + 23, x + 7, y + 28 + step], fill=pants)
            d.rectangle([x + 8, y + 23, x + 11, y + 28 - step], fill=pants)
            d.rectangle([x + 4, y + 29 + step, x + 7, y + 30 + step], fill=shoe)
            d.rectangle([x + 8, y + 29 - step, x + 11, y + 30 - step], fill=shoe)
            if row == 0:
                d.point((x + 6, y + 10), fill=(40, 30, 30))
                d.point((x + 9, y + 10), fill=(40, 30, 30))
            elif row == 1:
                d.point((x + 10, y + 10), fill=(40, 30, 30))
    hair = Image.new("RGBA", (128, 96), (0, 0, 0, 0))
    d = ImageDraw.Draw(hair)
    for row in range(3):
        x, y = 0, row * 32
        d.chord([x + 3, y + 4, x + 12, y + 12], 180, 360, fill=(200, 200, 200))
        d.rectangle([x + 3, y + 7, x + 4, y + 11], fill=(200, 200, 200))
        if row != 0:
            d.rectangle([x + (3 if row == 1 else 3), y + 7, x + (6 if row == 1 else 12), y + 12], fill=(200, 200, 200))
        else:
            d.rectangle([x + 11, y + 7, x + 12, y + 11], fill=(200, 200, 200))
    return base, hair


def buildings():
    house = Image.new("RGBA", (160, 144), (0, 0, 0, 0))
    d = ImageDraw.Draw(house)
    d.polygon([(4, 64), (80, 6), (156, 64)], fill=(176, 64, 52), outline=(110, 40, 32))
    for k in range(4):
        d.line([(4 + k * 19, 64 - k * 14), (156 - k * 19, 64 - k * 14)], fill=(132, 44, 36))
    d.rectangle([14, 62, 146, 143], fill=(226, 196, 140), outline=(130, 90, 50))
    for k in range(66, 144, 7):
        d.line([15, k, 145, k], fill=(200, 166, 112))
    d.rectangle([88, 112, 103, 143], fill=(120, 72, 36), outline=(80, 46, 22))
    d.point((100, 128), fill=(240, 210, 90))
    for wx in (30, 120):
        d.rectangle([wx, 84, wx + 18, 102], fill=(120, 180, 220), outline=(90, 60, 30))
        d.line([wx + 9, 84, wx + 9, 102], fill=(90, 60, 30))
    d.rectangle([110, 20, 124, 44], fill=(150, 80, 60), outline=(90, 50, 40))
    bin_ = Image.new("RGBA", (32, 32), (0, 0, 0, 0))
    d = ImageDraw.Draw(bin_)
    d.rectangle([1, 10, 30, 31], fill=(150, 98, 52), outline=(92, 54, 24))
    d.rectangle([0, 6, 31, 12], fill=(176, 120, 66), outline=(92, 54, 24))
    for k in (8, 16, 24):
        d.line([k, 13, k, 30], fill=(110, 70, 36))
    return house, bin_


def hoe_dirt():
    img = Image.new("RGBA", (128, 64), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    for half, base in ((0, (150, 104, 62)), (1, (104, 70, 42))):
        for i in range(16):
            x, y = half * 64 + (i % 4) * 16, (i // 4) * 16
            d.rectangle([x, y, x + 15, y + 15], fill=base)
            for k in range(4):
                d.line([x + 1, y + 3 + k * 4, x + 14, y + 3 + k * 4], fill=shade(base, 0.85))
            d.rectangle([x, y, x + 15, y + 15], outline=shade(base, 0.7))
    return img


# ------------------------------------------------------------------ villagers (original)
VILLAGERS = {
    # name: (shirt, hair, skin)
    "Rowan": ((70, 140, 90), (60, 40, 30), (240, 200, 160)),
    "Juniper": ((170, 80, 150), (230, 180, 60), (200, 150, 110)),
}


def villager_sprites(shirt, hair, skin):
    img = Image.new("RGBA", (64, 128), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    for row in range(4):           # down, right, up, left
        for col in range(4):
            x, y = col * 16, row * 32
            step = (0, 1, 0, -1)[col]
            d.ellipse([x + 4, y + 5, x + 11, y + 13], fill=skin, outline=shade(skin, 0.7))
            d.chord([x + 3, y + 3, x + 12, y + 11], 180, 360, fill=hair)
            d.rectangle([x + 4, y + 14, x + 11, y + 23], fill=shirt, outline=shade(shirt, 0.6))
            d.rectangle([x + 4, y + 24, x + 7, y + 29 + step], fill=(70, 60, 80))
            d.rectangle([x + 8, y + 24, x + 11, y + 29 - step], fill=(70, 60, 80))
            if row == 0:
                d.point((x + 6, y + 9), fill=(30, 30, 30))
                d.point((x + 9, y + 9), fill=(30, 30, 30))
            elif row in (1, 3):
                d.point((x + (10 if row == 1 else 5), y + 9), fill=(30, 30, 30))
            else:
                d.rectangle([x + 4, y + 6, x + 11, y + 11], fill=hair)
    return img


def villager_portraits(shirt, hair, skin):
    img = Image.new("RGBA", (128, 192), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    for i in range(6):             # neutral happy sad unique love angry
        x, y = (i % 2) * 64, (i // 2) * 64
        d.rectangle([x + 14, y + 46, x + 50, y + 63], fill=shirt)
        d.ellipse([x + 16, y + 10, x + 48, y + 48], fill=skin, outline=shade(skin, 0.7))
        d.chord([x + 13, y + 4, x + 51, y + 36], 180, 360, fill=hair)
        ey = y + 27
        if i == 4:
            for ex in (24, 37):
                d.polygon([(x + ex, ey), (x + ex + 3, ey - 2), (x + ex + 6, ey)], fill=(220, 60, 90))
        else:
            d.rectangle([x + 24, ey - 2, x + 27, ey + 1], fill=(30, 30, 30))
            d.rectangle([x + 37, ey - 2, x + 40, ey + 1], fill=(30, 30, 30))
        if i == 5:
            d.line([x + 22, ey - 6, x + 28, ey - 4], fill=(30, 30, 30), width=2)
            d.line([x + 42, ey - 6, x + 36, ey - 4], fill=(30, 30, 30), width=2)
        mouth = {0: (26, 38, 0), 1: (25, 39, 1), 2: (26, 38, -1), 3: (28, 36, 0), 4: (25, 39, 1), 5: (26, 38, -1)}[i]
        my = y + 38
        if mouth[2] > 0:
            d.arc([x + mouth[0], my - 4, x + mouth[1], my + 3], 20, 160, fill=(120, 40, 40), width=2)
        elif mouth[2] < 0:
            d.arc([x + mouth[0], my, x + mouth[1], my + 6], 200, 340, fill=(120, 40, 40), width=2)
        else:
            d.line([x + mouth[0], my, x + mouth[1], my], fill=(120, 40, 40), width=2)
        if i == 3:
            d.ellipse([x + 44, y + 8, x + 52, y + 16], fill=(250, 230, 120))
    return img


def villager_data():
    chars = {
        "Rowan": {"DisplayName": "Rowan", "BirthSeason": "Spring", "BirthDay": 1, "CanSocialize": "TRUE",
                  "Home": [{"Id": "Default", "Location": "SeedShop", "Tile": {"X": 4, "Y": 7}, "Direction": "down"}]},
        "Juniper": {"DisplayName": "Juniper", "BirthSeason": "Summer", "BirthDay": 9,
                    "Home": [{"Id": "Default", "Location": "Town", "Tile": {"X": 8, "Y": 23}, "Direction": "down"}]},
    }
    schedules = {
        "Rowan": {"spring": "900 SeedShop 4 7 2/1700 SeedShop 9 13 2/2100 SeedShop 4 7 2"},
        "Juniper": {"spring": "800 Town 20 30 2/1000 BusStop 15 22 2/1300 Town 40 26 1/1800 Town 8 23 2",
                    "rain": "900 Town 30 23 2/1700 Town 8 23 2"},
    }
    dialogue = {
        "Rowan": {
            "Introduction": "Oh! You must be the new farmer.#$b#I'm Rowan. I run the seed counter here.$h",
            "Mon": "Mondays are slow. Good day to restock the shelves.",
            "spring_Tue": "$q 101 none#Planted anything yet?#$r 101 20 rowan_yes#Parsnips, of course!#$r 102 0 rowan_no#Not yet.",
            "rowan_yes": "Good choice. They grow fast.$h",
            "rowan_no": "No rush. The season's young.",
        },
        "Juniper": {
            "Introduction": "Hey there. I'm Juniper.#$b#I paint the river most mornings. Say hi sometime!$h",
            "Mon": "The light on the water is perfect today.",
        },
    }
    tastes = {
        "Rowan": "Ah, I really love these!/24 190/Thanks, that's nice./192/Oh... okay./388/Ugh, no./771/Thanks./",
        "Juniper": "You remembered! I love it./250/That's sweet of you./24/Hm./390/Not my thing./771/Thank you./",
        "Universal_Love": "", "Universal_Like": "", "Universal_Dislike": "", "Universal_Hate": "", "Universal_Neutral": "",
    }
    os.makedirs(os.path.join(C, "Characters", "schedules"), exist_ok=True)
    os.makedirs(os.path.join(C, "Characters", "Dialogue"), exist_ok=True)
    json.dump(chars, open(os.path.join(C, "Data/Characters.json"), "w"), indent=1)
    json.dump(tastes, open(os.path.join(C, "Data/NPCGiftTastes.json"), "w"), indent=1)
    for n, sch in schedules.items():
        json.dump(sch, open(os.path.join(C, "Characters", "schedules", n + ".json"), "w"), indent=1)
    for n, dia in dialogue.items():
        json.dump(dia, open(os.path.join(C, "Characters", "Dialogue", n + ".json"), "w"), indent=1)
    # an original placeholder cutscene: needs the "juniperInvite" mail, so it
    # only fires when a test asks for it
    os.makedirs(os.path.join(C, "Data", "Events"), exist_ok=True)
    events = {
        "900/n juniperInvite/t 600 2600":
            'none/20 30/farmer 17 30 1 Juniper 22 30 3/skippable/pause 300/'
            'speak Juniper "Oh! You startled me.$s"/emote farmer 16/move Juniper -2 0 3/'
            'faceDirection farmer 1/'
            'speak Juniper "I paint the river most mornings.#$b#This one might be my best yet.$h"/'
            'question fork1 "What do you think?#It\'s beautiful.#Needs more ducks."/fork juniper_ducks/'
            'friendship Juniper 50/speak Juniper "You\'re sweet.$l"/end',
        "juniper_ducks": 'jump Juniper/speak Juniper "Ducks?! ...okay, maybe a few.$h"/friendship Juniper 20/end',
    }
    json.dump(events, open(os.path.join(C, "Data", "Events", "Town.json"), "w"), indent=1)
    # placeholder arrival (own wording): the bus stop event for "day 0"
    arrival = {
        "900001/u 0":
            'none/10 10/farmer 10 12 0 Rowan 12 10 2/skippable/pause 200/'
            'speak Rowan "Welcome! The farm is just down this road."/shake Rowan 300/'
            'advancedMove Rowan false 0 2 2 0/proceedPosition Rowan/halt/'
            'changeLocation Farm/warp farmer 64 18/pause 200/'
            'speak Rowan "Make yourself at home."/end beginGame',
    }
    json.dump(arrival, open(os.path.join(C, "Data", "Events", "BusStop.json"), "w"), indent=1)

    for n, (shirt, hair, skin) in VILLAGERS.items():
        save(villager_sprites(shirt, hair, skin), f"Characters/{n}")
        save(villager_portraits(shirt, hair, skin), f"Portraits/{n}")


def data_files():
    os.makedirs(os.path.join(C, "Data"), exist_ok=True)
    crops = {
        "472": {"Seasons": ["Spring"], "DaysInPhase": [1, 1, 1, 1], "RegrowDays": -1, "HarvestItemId": "24", "SpriteIndex": 0},
        "474": {"Seasons": ["Spring"], "DaysInPhase": [1, 2, 4, 4, 1], "RegrowDays": -1, "HarvestItemId": "190", "SpriteIndex": 1},
        "475": {"Seasons": ["Spring"], "DaysInPhase": [1, 1, 1, 2, 1], "RegrowDays": -1, "HarvestItemId": "192", "SpriteIndex": 2},
        "477": {"Seasons": ["Spring"], "DaysInPhase": [1, 2, 2, 1], "RegrowDays": -1, "HarvestItemId": "250", "SpriteIndex": 3},
        "473": {"Seasons": ["Spring"], "DaysInPhase": [1, 1, 1, 3, 4], "RegrowDays": 3, "IsRaised": True, "HarvestItemId": "188", "SpriteIndex": 4},
    }
    objs = {
        "24": ("Parsnip", 35, 24, -75, 10), "188": ("Green Bean", 40, 188, -75, 10),
        "190": ("Cauliflower", 175, 190, -75, 30), "192": ("Potato", 80, 192, -75, 10),
        "250": ("Kale", 110, 250, -75, 20), "388": ("Wood", 2, 388, -16, -300),
        "390": ("Stone", 2, 390, -16, -300), "771": ("Fiber", 1, 771, -16, -300),
        "472": ("Parsnip Seeds", 10, 472, -74, -300), "473": ("Bean Starter", 30, 473, -74, -300),
        "474": ("Cauliflower Seeds", 40, 474, -74, -300), "475": ("Potato Seeds", 25, 475, -74, -300),
        "477": ("Kale Seeds", 35, 477, -74, -300),
        "378": ("Copper Ore", 5, 378, -15, -300), "334": ("Copper Bar", 60, 334, -15, -300),
        "382": ("Coal", 15, 382, -15, -300), "398": ("Test Berry", 50, 398, -79, 15),
        "344": ("Jelly", 160, 344, -26, 20), "599": ("Sprinkler", 100, 599, -8, -300),
        "309": ("Test Acorn", 20, 309, -74, -300), "92": ("Sap", 2, 92, -81, -1),
        "145": ("Test Perch", 30, 145, -4, 5), "168": ("Trash", 0, 168, -20, -300),
    }
    tools = {
        "Hoe": {"ClassName": "Hoe", "Name": "Hoe", "SpriteIndex": 0, "MenuSpriteIndex": -1, "UpgradeLevel": 0},
        "WateringCan": {"ClassName": "WateringCan", "Name": "Watering Can", "SpriteIndex": 1, "UpgradeLevel": 0},
        "Axe": {"ClassName": "Axe", "Name": "Axe", "SpriteIndex": 2, "UpgradeLevel": 0},
        "Pickaxe": {"ClassName": "Pickaxe", "Name": "Pickaxe", "SpriteIndex": 3, "UpgradeLevel": 0},
        "Scythe": {"ClassName": "MeleeWeapon", "Name": "Scythe", "SpriteIndex": 4, "UpgradeLevel": 0},
        "TrainingRod": {"ClassName": "FishingRod", "Name": "Training Rod", "SpriteIndex": 7, "UpgradeLevel": 0},
        "CopperHoe": {"ClassName": "Hoe", "Name": "Copper Hoe", "SpriteIndex": 5, "UpgradeLevel": 1,
                      "SalePrice": 2000, "ConventionalUpgradeFrom": "(T)Hoe"},
        "CopperWateringCan": {"ClassName": "WateringCan", "Name": "Copper Watering Can", "SpriteIndex": 6,
                              "UpgradeLevel": 1, "SalePrice": 2000, "ConventionalUpgradeFrom": "(T)WateringCan"},
    }
    for t in tools.values():
        t["Texture"] = "TileSheets\\tools"
    shops = {"SeedShop": {"Items": [
        {"ItemId": "(O)472", "Price": -1, "Condition": "SEASON Spring"},
        {"ItemId": "(O)473", "Price": -1, "Condition": "SEASON Spring"},
        {"ItemId": "(O)474", "Price": -1, "Condition": "SEASON Spring"},
        {"ItemId": "(O)475", "Price": -1, "Condition": "SEASON Spring"},
        {"ItemId": "(O)477", "Price": -1, "Condition": "SEASON Spring"},
        {"ItemId": "(O)388", "Price": -1, "Condition": "PLAYER_HAS_MAIL Current someLetter"},
    ]}}
    json.dump(crops, open(os.path.join(C, "Data/Crops.json"), "w"), indent=1)
    json.dump({k: {"Name": v[0], "Price": v[1], "SpriteIndex": v[2], "Category": v[3], "Edibility": v[4]}
               for k, v in objs.items()}, open(os.path.join(C, "Data/Objects.json"), "w"), indent=1)
    json.dump(tools, open(os.path.join(C, "Data/Tools.json"), "w"), indent=1)
    # crafting: placeholder recipes in the 1.6 formats
    # placeholder letters (own wording) + a trigger, in the 1.6 formats
    json.dump({
        "spring_2_1": "Dear @,^Some wood for your new place.^- The Test Town Council%item object 388 10 %%[#]Welcome Wood",
        "testQuestLetter": "Hi @,^Could you take a walk into town?%item quest 7 %%[#]A Walk",
    }, open(os.path.join(C, "Data/mail.json"), "w"), indent=1)
    json.dump([{"Id": "Test_Day3", "Trigger": "DayStarted", "Condition": "DAY_OF_MONTH 3",
                "Action": "AddMail Current testQuestLetter", "MarkActionApplied": True}],
              open(os.path.join(C, "Data/TriggerActions.json"), "w"), indent=1)
    # placeholder quests (own wording) in the 1.6 Data/Quests format
    json.dump({
        "6": "ItemHarvest/First Crop/Grow something./Harvest a parsnip./(O)24/h7/100/-1/true",
        "7": "Location/Into Town/Take a walk./Visit the town./Town/-1/50/-1/true",
        "9": "Social/Say Hello/Meet the neighbours./Talk to everyone./null/-1/0/-1/true",
        "8": "Crafting/Box It Up/Make storage./Craft a chest./(BC)130/-1/25/-1/true",
    }, open(os.path.join(C, "Data/Quests.json"), "w"), indent=1)
    json.dump({"145": "Test Perch/30/mixed/4/12/600 2600/spring summer fall winter/both/690 .4/2/.3/.5/0/false"},
              open(os.path.join(C, "Data/Fish.json"), "w"), indent=1)
    json.dump({"1": {"Textures": [{"Season": None, "Texture": "TerrainFeatures\\tree1_spring"}],
                     "SeedItemId": "(O)309", "GrowthChance": 1.0}},
              open(os.path.join(C, "Data/WildTrees.json"), "w"), indent=1)
    json.dump({k: {"Name": n, "Price": p, "SpriteIndex": i} for k, (n, p, i) in {
        "130": ("Chest", 0, 0), "13": ("Furnace", 0, 1), "15": ("Preserves Jar", 0, 2)}.items()},
        open(os.path.join(C, "Data/BigCraftables.json"), "w"), indent=1)
    json.dump({
        "Chest": "388 10/Home/130/true/default/",
        "Furnace": "378 5 390 5/Home/13/true/default/",
        "Preserves Jar": "388 5 390 5 382 1/Home/15/true/s Farming 1/",
        "Sprinkler": "334 1 390 2/Home/599/false/default/",
    }, open(os.path.join(C, "Data/CraftingRecipes.json"), "w"), indent=1)

    def rule(rid, req, tags, count, out, minutes):
        return {"Id": rid, "Triggers": [{"Trigger": "ItemPlacedInMachine", "RequiredItemId": req,
                                          "RequiredTags": tags, "RequiredCount": count}],
                "OutputItem": [{"ItemId": out}], "MinutesUntilReady": minutes, "DaysUntilReady": -1}
    json.dump({
        "(BC)13": {"AdditionalConsumedItems": [{"ItemId": "(O)382", "RequiredCount": 1}],
                   "OutputRules": [rule("Bar", "(O)378", None, 5, "(O)334", 30)]},
        "(BC)15": {"OutputRules": [rule("Jelly", None, ["category_fruits"], 1, "FLAVORED_ITEM Jelly DROP_IN_ID", 120)]},
    }, open(os.path.join(C, "Data/Machines.json"), "w"), indent=1)
    json.dump(shops, open(os.path.join(C, "Data/Shops.json"), "w"), indent=1)
    json.dump({"Town": {"DisplayName": "Pelican Town (placeholder)", "MinDailyForageSpawn": 4, "MaxDailyForageSpawn": 4,
                        "MaxSpawnedForageAtOnce": 6,
                        "Forage": [{"ItemId": "(O)24", "Season": "Spring", "Chance": 1.0}],
                        "Fish": [{"ItemId": "(O)145", "Chance": 1.0}]},
               "Farm": {"Fish": [{"ItemId": "(O)145", "Chance": 1.0}]},
               "Default": {"Fish": [{"RandomItemId": ["(O)168"], "Chance": 1.0, "Precedence": 2000}]}},
              open(os.path.join(C, "Data/Locations.json"), "w"))


def test_mod():
    """An original Content Patcher pack for testing tools/cp.py: adds a meadow
    east of town, edits the town map, adds an item, overlays an icon."""
    pack = os.path.join(OUT, "mods", "[CP] Test Meadow")
    os.makedirs(os.path.join(pack, "assets"), exist_ok=True)
    os.makedirs(os.path.join(pack, "i18n"), exist_ok=True)
    with open(os.path.join(pack, "manifest.json"), "w") as f:
        f.write("""{
  // comments and trailing commas, like real packs
  "Name": "Test Meadow", "Author": "stardew3ds tests", "Version": "1.0.0",
  "UniqueID": "stardew3ds.TestMeadow",
  "ContentPackFor": { "UniqueID": "Pathoschild.ContentPatcher", },
}""")
    with open(os.path.join(pack, "i18n", "default.json"), "w") as f:
        f.write('{ "item.name": "Meadow Tea", "meadow.name": "Quiet Meadow", }')
    # the meadow map: grass with a pond and a path back west
    g = Grid(20, 15)
    g.fill("Back", 0, 0, 19, 14, GRASS)
    g.fill("Back", 0, 6, 19, 8, PATH)
    g.fill("Back", 12, 2, 15, 4, WATER)
    write_tmx("_tmp_meadow", 20, 15, [OUT_TS], g.L, {"Outdoors": "T", "Warp": "-1 7 Town 58 30"}, [])
    os.replace(os.path.join(C, "Maps", "_tmp_meadow.tmx"), os.path.join(pack, "assets", "Meadow.tmx"))
    # a 3x3 patch for the town: a pond corner (water tiles)
    p = Grid(3, 3)
    p.fill("Back", 0, 0, 2, 2, WATER)
    write_tmx("_tmp_patch", 3, 3, [OUT_TS], p.L, {}, [])
    os.replace(os.path.join(C, "Maps", "_tmp_patch.tmx"), os.path.join(pack, "assets", "pond.tmx"))
    icon = Image.new("RGBA", (16, 16), (0, 0, 0, 0))
    ImageDraw.Draw(icon).ellipse([3, 3, 12, 12], fill=(120, 200, 140), outline=(40, 90, 50))
    icon.save(os.path.join(pack, "assets", "tea.png"))
    with open(os.path.join(pack, "content.json"), "w") as f:
        f.write("""{
  "Format": "2.0.0",
  "ConfigSchema": { "EnablePond": { "AllowValues": "true, false", "Default": "true" } },
  "DynamicTokens": [ { "Name": "Mood", "Value": "calm" }, { "Name": "Mood", "Value": "stormy", "When": { "Weather": "Rain" } } ],
  "Changes": [
    { "Action": "Load", "Target": "Maps/Custom_TestMeadow", "FromFile": "assets/Meadow.tmx" },
    { "Action": "EditData", "Target": "Data/Locations",
      "Entries": { "Custom_TestMeadow": { "DisplayName": "{{i18n:meadow.name}}", "CreateOnLoad": { "MapPath": "Maps/Custom_TestMeadow" } } } },
    { "Action": "EditMap", "Target": "Maps/Town",
      "TextOperations": [ { "Operation": "Append", "Target": ["MapProperties", "Warp"], "Value": "60 30 Custom_TestMeadow 1 7" } ],
      "MapTiles": [ { "Position": { "X": 30, "Y": 22 }, "Layer": "Back", "SetProperties": { "Action": "Message \\"Hi\\"" } } ], },
    { "Action": "EditMap", "Target": "Maps/Town", "FromFile": "assets/pond.tmx",
      "ToArea": { "X": 50, "Y": 36, "Width": 3, "Height": 3 }, "When": { "EnablePond": true } },
    { "Action": "EditData", "Target": "Data/Objects",
      "Entries": { "9001": { "Name": "{{i18n:item.name}}", "Price": .5e2, "SpriteIndex": 9001, "Category": -26, "Edibility": 25, "Texture": null, }, }, },
    { "Action": "EditData", "Target": "Data/Objects", "Fields": { "24": { "Price": 99 } } },
    { "Action": "EditData", "Target": "Data/Objects", "When": { "Season": "winter" },
      "Entries": { "24": null } },
    { "Action": "EditMap", "Target": "Maps/Custom_TestMeadow", "FromFile": "assets/pond.tmx",
      "ToArea": { "X": 2, "Y": 2, "Width": 3, "Height": 3 }, "When": { "Season": "summer" } },
    { "Action": "EditImage", "Target": "Maps/springobjects", "FromFile": "assets/tea.png",
      "ToArea": { "X": 0, "Y": 0, "Width": 16, "Height": 16 }, "PatchMode": "Overlay" },
  ],
}""")


def test_ftm():
    """An original Farm Type Manager pack: copper ore on the bus stop road."""
    pack = os.path.join(OUT, "mods", "[FTM] Test Ore")
    os.makedirs(pack, exist_ok=True)
    with open(os.path.join(pack, "manifest.json"), "w") as f:
        f.write('{ "Name": "Test Ore", "UniqueID": "stardew3ds.TestOre", "Version": "1.0.0",'
                ' "ContentPackFor": { "UniqueID": "Esca.FarmTypeManager" }, }')
    with open(os.path.join(pack, "content.json"), "w") as f:
        json.dump({"ForageSpawnEnabled": False, "OreSpawnEnabled": True,
                   "Ore_Spawn_Settings": {"Areas": [{
                       "UniqueAreaID": "bus stop copper", "MapName": "BusStop",
                       "MinimumSpawnsPerDay": 5, "MaximumSpawnsPerDay": 5,
                       "IncludeTerrainTypes": ["Dirt"], "IncludeCoordinates": ["2,20;10,24"],
                       "ExcludeCoordinates": [], "StartingSpawnChance": {"Copper": 100},
                       "ExtraConditions": {"Seasons": []}}]}}, f, indent=1)


def main():
    for s in ("spring", "summer", "fall", "winter"):
        save(outdoor_sheet(s), f"Maps/{s}_outdoorsTileSheet")
    save(interior_sheet(), "Maps/townInterior")
    make_maps()
    save(objects_sheet(), "Maps/springobjects")
    save(crops_sheet(), "TileSheets/crops")
    save(craftables_sheet(), "TileSheets/Craftables")
    save(tree_sheet(), "TerrainFeatures/tree1_spring")
    save(tools_sheet(), "TileSheets/tools")
    save(hoe_dirt(), "TerrainFeatures/hoeDirt")
    base, hair = farmer_sheets()
    save(base, "Characters/Farmer/farmer_base")
    save(hair, "Characters/Farmer/hairstyles")
    house, bin_ = buildings()
    save(house, "Buildings/houses")
    save(bin_, "Buildings/Shipping Bin")
    data_files()
    villager_data()
    test_mod()
    test_ftm()
    print(f"placeholder content written to {C}")


if __name__ == "__main__":
    main()
