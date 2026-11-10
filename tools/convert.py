#!/usr/bin/env python3
"""
stardew3ds asset converter
==========================

Turns YOUR OWN copy of Stardew Valley's content (unpacked with
StardewXnbHack) into the files the 3DS engine reads from the SD card.

Nothing from the game is included in this project. This script only
reads files you already have.

Usage:
    python3 convert.py "<Stardew Valley>/Content (unpacked)" out/

Then copy   out/3ds/stardew3ds/   to the root of your SD card so you get
    sdmc:/3ds/stardew3ds/tex/...
    sdmc:/3ds/stardew3ds/maps/...
    sdmc:/3ds/stardew3ds/data/...

Requires: Python 3.8+, Pillow, numpy   (pip install pillow numpy)

Output formats (all little-endian):

  .s3t texture
    char[4] "S3T1"; u16 width; u16 height; u16 pageCount; u16 reserved
    per page: u16 y0; u16 h; u16 pw; u16 ph; then pw*ph*4 bytes
    Each page is a horizontal strip of the source image (max 1024 tall),
    padded to power-of-two and pre-swizzled into the 3DS GPU's
    8x8 Morton-tiled RGBA8 layout (byte order A,B,G,R, rows flipped).

  .smap map    see write_smap()
  data/*.dat   tab-separated text, see write_data()
"""
import json
import os
import re
import struct
import sys
import time
import base64
import zlib
import gzip
import shutil
import xml.etree.ElementTree as ET

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

try:
    import numpy as np
    from PIL import Image
except ImportError:
    sys.exit("Needs Pillow and numpy:  pip install pillow numpy")

SEASONS = ["spring", "summer", "fall", "winter"]
MAX_PAGE = 1024

# Textures the engine needs besides map tilesheets.
# (content path without extension, required?)
EXTRA_TEXTURES = [
    ("Maps/springobjects", True),
    ("TileSheets/crops", True),
    ("TileSheets/tools", True),
    ("TerrainFeatures/hoeDirt", True),
    ("Characters/Farmer/farmer_base", True),
    ("Characters/Farmer/hairstyles", True),
    ("Characters/Farmer/pants", False),
    ("TileSheets/furniture", False),
    ("TileSheets/Craftables", False),
    ("Characters/Farmer/shirts", False),
    ("Buildings/houses", False),
    ("Buildings/Shipping Bin", False),
    ("LooseSprites/Cursors", False),
    ("LooseSprites/emotes", False),
    ("LooseSprites/font_bold", False),
    ("LooseSprites/letterBG", False),
    ("Minigames/TitleButtons", False),
]

# Maps are discovered from Maps/*.tmx; these must exist.
REQUIRED_MAPS = ["Farm"]

# Layers drawn by the engine, in this fixed order.
LAYERS = ["Back", "Buildings", "Paths", "Front", "AlwaysFront"]

# cell flags
F_DIGGABLE = 1
F_WATER = 2
F_BLOCKED = 4
F_NOSPAWN = 8
F_PASSABLE_OVERRIDE = 16
F_GRASS = 32
F_DIRT = 64
F_SPAWNABLE = 128


def tex_name(content_path):
    """'Maps/spring_outdoorsTileSheet' -> 'Maps_spring_outdoorsTileSheet'"""
    return content_path.replace("\\", "/").replace("/", "_").replace(" ", "_")


# --------------------------------------------------------------------------
# Textures
# --------------------------------------------------------------------------

def _next_pow2(v):
    p = 8
    while p < v:
        p <<= 1
    return p


def _morton_table(pw, ph):
    """For every (y, x) in a pw*ph texture return the swizzled pixel index.
    y here is GPU row (0 = bottom)."""
    ys, xs = np.mgrid[0:ph, 0:pw]
    tile = (ys >> 3) * (pw >> 3) + (xs >> 3)
    x, y = xs & 7, ys & 7
    morton = ((x & 1) | ((y & 1) << 1) | ((x & 2) << 1) |
              ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3))
    return (tile << 6) + morton


def swizzle(rgba):
    """rgba: HxWx4 uint8 (top row first) already padded to pow2.
    Returns bytes in 3DS GPU RGBA8 tiled layout."""
    ph, pw = rgba.shape[:2]
    flipped = rgba[::-1]                      # GPU row 0 is image bottom
    idx = _morton_table(pw, ph).ravel()
    out = np.zeros((pw * ph, 4), dtype=np.uint8)
    # GPU_RGBA8 little-endian word 0xRRGGBBAA -> bytes A B G R
    out[idx] = flipped.reshape(-1, 4)[:, ::-1]
    return out.tobytes()


def convert_texture(src_png, dst_s3t):
    """Split into pages of at most 1024x1024 (the 3DS GPU limit), each padded
    to power-of-two and swizzled. Writes S3T2 (pages carry x0 and y0)."""
    img = Image.open(src_png).convert("RGBA")
    w, h = img.size
    arr = np.asarray(img)
    pages = []
    for y in range(0, h, MAX_PAGE):
        for x in range(0, w, MAX_PAGE):
            pw_real, ph_real = min(MAX_PAGE, w - x), min(MAX_PAGE, h - y)
            pw, ph = _next_pow2(pw_real), _next_pow2(ph_real)
            page = np.zeros((ph, pw, 4), dtype=np.uint8)
            page[:ph_real, :pw_real] = arr[y:y + ph_real, x:x + pw_real]
            pages.append((x, y, pw_real, ph_real, pw, ph, swizzle(page)))
    os.makedirs(os.path.dirname(dst_s3t), exist_ok=True)
    with open(dst_s3t, "wb") as f:
        f.write(b"S3T2")
        f.write(struct.pack("<HHHH", w, h, len(pages), 0))
        for (x0, y0, ww, hh, pw, ph, data) in pages:
            f.write(struct.pack("<HHHHHH", x0, y0, ww, hh, pw, ph))
            f.write(data)
    return w, h


def farmer_bob(content, out_dir):
    """Per-frame vertical bob of the farmer body (topmost opaque row vs the
    row's standing frame), so shirt and hair move with the body."""
    p = find_png(content, "Characters/Farmer/farmer_base")
    if not p:
        return
    a = np.asarray(Image.open(p).convert("RGBA"))[:, :, 3]
    rows = a.shape[0] // 32
    with open(os.path.join(out_dir, "farmer_bob.dat"), "w") as f:
        for r in range(rows):
            tops = []
            for c in range(6):
                cell = a[r * 32:(r + 1) * 32, c * 16:(c + 1) * 16]
                ys = np.nonzero(cell.max(axis=1))[0]
                tops.append(int(ys[0]) if len(ys) else 0)
            for c in range(6):
                f.write(f"{r * 6 + c}\t{tops[c] - tops[0]}\n")


def write_furniture(content, out_dir):
    furn = _load_json(content, "Data/Furniture.json")
    n = 0
    with open(os.path.join(out_dir, "furniture.dat"), "w", encoding="utf-8") as f:
        for fid, line in furn.items():
            if not str(fid).isdigit():
                continue
            parts = str(line).split("/")
            if len(parts) < 4:
                continue
            def wh(t, dflt):
                try:
                    a, b = t.split()
                    return int(a), int(b)
                except ValueError:
                    return dflt
            sw, sh = wh(parts[2], (1, 2))
            bw, bh = wh(parts[3], (1, 1))
            f.write(f"{fid}\t{_clean(parts[0])}\t{_clean(parts[1])}\t{sw}\t{sh}\t{bw}\t{bh}\n")
            n += 1
    print(f"  furniture.dat: {n} pieces")


def find_png(content, content_path):
    p = os.path.join(content, *content_path.replace("\\", "/").split("/")) + ".png"
    return p if os.path.isfile(p) else None


# --------------------------------------------------------------------------
# Maps (.tmx from StardewXnbHack)
# --------------------------------------------------------------------------

def _props(elem):
    out = {}
    if elem is None:
        return out
    pe = elem.find("properties")
    if pe is None:
        return out
    for p in pe.findall("property"):
        v = p.get("value")
        if v is None:
            v = p.text or ""
        out[p.get("name")] = v
    return out


def _decode_layer_data(data_el, w, h):
    enc = data_el.get("encoding")
    comp = data_el.get("compression")
    if enc == "csv":
        vals = [int(v) for v in re.split(r"[,\s]+", data_el.text.strip()) if v]
    elif enc == "base64":
        raw = base64.b64decode(data_el.text.strip())
        if comp == "zlib":
            raw = zlib.decompress(raw)
        elif comp == "gzip":
            raw = gzip.decompress(raw)
        elif comp:
            raise ValueError(f"unsupported layer compression {comp}")
        vals = list(struct.unpack(f"<{len(raw) // 4}I", raw))
    elif enc is None:
        vals = [int(t.get("gid", 0)) for t in data_el.findall("tile")]
    else:
        raise ValueError(f"unsupported layer encoding {enc}")
    if data_el.find("chunk") is not None:
        raise ValueError("infinite maps not supported")
    return vals


def _load_tileset(ts_el, map_dir):
    first = int(ts_el.get("firstgid"))
    if ts_el.get("source"):                      # external .tsx
        tsx = os.path.join(map_dir, ts_el.get("source"))
        ts_el = ET.parse(tsx).getroot()
        map_dir = os.path.dirname(tsx)
    img = ts_el.find("image")
    src = img.get("source")
    tw = int(ts_el.get("tilewidth", 16))
    iw = int(img.get("width", 0))
    ih = int(img.get("height", 0))
    cols = int(ts_el.get("columns", iw // tw if tw else 0))
    count = int(ts_el.get("tilecount", (iw // tw) * (ih // tw) if tw else 0))
    tiles = {}
    anims = {}
    for t in ts_el.findall("tile"):
        tid = int(t.get("id"))
        tiles[tid] = _props(t)
        an = t.find("animation")
        if an is not None:
            fr = [(int(f.get("tileid")), int(f.get("duration"))) for f in an.findall("frame")]
            if fr:
                anims[tid] = fr
    return {
        "firstgid": first, "image": src, "image_dir": map_dir,
        "cols": cols, "count": count, "tiles": tiles, "anims": anims,
        "tilewidth": tw,
    }


def _image_content_path(content, image_src, image_dir):
    """Resolve tileset image to a content path like 'Maps/spring_town'."""
    image_src = image_src.replace("\\", "/")
    full = os.path.normpath(os.path.join(image_dir, image_src))
    if not full.lower().endswith(".png"):
        full += ".png"
    if not os.path.isfile(full):
        # XnbHack usually writes bare names relative to Maps/
        alt = os.path.join(content, "Maps", os.path.basename(full))
        if os.path.isfile(alt):
            full = alt
        else:
            return None
    rel = os.path.relpath(full, content).replace("\\", "/")
    return rel[:-4]


def convert_map(content, name, out_dir, textures_needed, tmx=None):
    tmx = tmx or os.path.join(content, "Maps", name + ".tmx")
    if not os.path.isfile(tmx):
        print(f"  ! map {name}.tmx not found, skipped")
        return None
    root = ET.parse(tmx).getroot()
    w, h = int(root.get("width")), int(root.get("height"))
    tilesets = [_load_tileset(t, os.path.dirname(tmx)) for t in root.findall("tileset")]
    tilesets.sort(key=lambda t: t["firstgid"])

    # Work out texture names, with {S} for seasonal sheets.
    for ts in tilesets:
        cp = _image_content_path(content, ts["image"], ts["image_dir"])
        if cp is None:
            print(f"  ! tilesheet image {ts['image']} not found")
            ts["tex"] = ""
            continue
        folder, base = os.path.split(cp)
        m = re.match(r"^(spring|summer|fall|winter)_(.*)$", base)
        if m:
            variants = [f"{folder}/{s}_{m.group(2)}" for s in SEASONS]
            have = [v for v in variants if find_png(content, v)]
            for v in have:
                textures_needed.add(v)
            if len(have) == 4:
                ts["tex"] = tex_name(f"{folder}/{{S}}_{m.group(2)}")
            else:
                ts["tex"] = tex_name(cp)
        else:
            textures_needed.add(cp)
            ts["tex"] = tex_name(cp)

    def gid_lookup(gid):
        gid &= 0x1FFFFFFF  # drop flip bits
        if gid == 0:
            return None
        for i in range(len(tilesets) - 1, -1, -1):
            if gid >= tilesets[i]["firstgid"]:
                return i, gid - tilesets[i]["firstgid"]
        return None

    layers = {}
    for lay in root.findall("layer"):
        lname = lay.get("name")
        if lname in LAYERS:
            lw = int(lay.get("width", w))
            lh = int(lay.get("height", h))
            raw = _decode_layer_data(lay.find("data"), lw, lh)
            # some maps have layers smaller than the map: pad / crop to fit
            vals = [0] * (w * h)
            for yy in range(min(h, lh)):
                for xx in range(min(w, lw)):
                    k = yy * lw + xx
                    if k < len(raw):
                        vals[yy * w + xx] = raw[k]
            layers[lname] = vals

    flags = bytearray(w * h)
    actions = {}          # cell -> action string
    ts_arr = {L: bytearray(w * h) for L in LAYERS}
    tile_arr = {L: [0xFFFF] * (w * h) for L in LAYERS}
    for L in LAYERS:
        data = layers.get(L)
        if not data:
            continue
        for i, gid in enumerate(data):
            r = gid_lookup(gid)
            if r is None:
                continue
            tsi, tid = r
            ts_arr[L][i] = tsi
            tile_arr[L][i] = tid
            props = tilesets[tsi]["tiles"].get(tid, {})
            if L == "Back":
                if props.get("Diggable", "").upper().startswith("T"):
                    flags[i] |= F_DIGGABLE
                if props.get("Water", "").upper().startswith("T"):
                    flags[i] |= F_WATER | F_BLOCKED
                if props.get("Passable", "").upper().startswith("F"):
                    flags[i] |= F_BLOCKED
                if "NoSpawn" in props:
                    flags[i] |= F_NOSPAWN
                ttype = props.get("Type", "").lower()
                if ttype == "grass":
                    flags[i] |= F_GRASS
                elif ttype == "dirt":
                    flags[i] |= F_DIRT
                if props.get("Spawnable", "").upper().startswith("T"):
                    flags[i] |= F_SPAWNABLE
            if props.get("Action") and L in ("Buildings", "Front", "Back"):
                actions.setdefault(i, props["Action"])
            if L == "Buildings":
                if "Passable" in props:
                    flags[i] |= F_PASSABLE_OVERRIDE
                else:
                    flags[i] |= F_BLOCKED

    # Per-cell properties (Tiled object layers with TileData / tile objects)
    for og in root.findall("objectgroup"):
        lname = og.get("name", "")
        for ob in og.findall("object"):
            pr = _props(ob)
            if not pr:
                continue
            x = int(float(ob.get("x", 0)) // 16)
            y = int(float(ob.get("y", 0)) // 16)
            if not (0 <= x < w and 0 <= y < h):
                continue
            i = y * w + x
            if "Diggable" in pr:
                flags[i] |= F_DIGGABLE
            if "Water" in pr:
                flags[i] |= F_WATER | F_BLOCKED
            if "NoSpawn" in pr:
                flags[i] |= F_NOSPAWN
            if "Passable" in pr and lname.startswith("Buildings"):
                flags[i] |= F_PASSABLE_OVERRIDE
                flags[i] &= ~F_BLOCKED & 0xFF
            if pr.get("Action"):
                # an object can cover several tiles (wide doors, counters)
                ow = max(1, int(float(ob.get("width", 16)) // 16))
                oh = max(1, int(float(ob.get("height", 16)) // 16))
                for yy in range(y, min(h, y + oh)):
                    for xx in range(x, min(w, x + ow)):
                        actions[yy * w + xx] = pr["Action"]

    mprops = _props(root)
    outdoors = mprops.get("Outdoors", "F").upper().startswith("T") or name == "Farm"
    warps = []
    toks = mprops.get("Warp", "").split()
    for k in range(0, len(toks) - 4, 5):
        try:
            warps.append((int(toks[k]), int(toks[k + 1]), toks[k + 2],
                          int(toks[k + 3]), int(toks[k + 4])))
        except ValueError:
            pass

    os.makedirs(out_dir, exist_ok=True)
    path = os.path.join(out_dir, name + ".smap")
    with open(path, "wb") as f:
        f.write(b"SMP2")
        f.write(struct.pack("<HHB", w, h, len(tilesets)))
        for ts in tilesets:
            nb = ts["tex"].encode()
            f.write(struct.pack("<B", len(nb)))
            f.write(nb)
            f.write(struct.pack("<HHH", ts["cols"], ts["count"], len(ts["anims"])))
            for tid, frames in sorted(ts["anims"].items()):
                frames = frames[:255]
                f.write(struct.pack("<HBH", tid, len(frames), frames[0][1]))
                f.write(struct.pack(f"<{len(frames)}H", *[fr[0] for fr in frames]))
        f.write(struct.pack("<B", len(LAYERS)))
        for L in LAYERS:
            f.write(struct.pack(f"<{w * h}H", *tile_arr[L]))
            f.write(bytes(ts_arr[L]))
        f.write(bytes(flags))
        f.write(struct.pack("<BH", 1 if outdoors else 0, len(warps)))
        for (x, y, tgt, tx, ty) in warps:
            tb = tgt.encode()[:255]
            f.write(struct.pack("<hhB", x, y, len(tb)))
            f.write(tb)
            f.write(struct.pack("<hh", tx, ty))
        acts = sorted(actions.items())[:65535]
        f.write(struct.pack("<H", len(acts)))
        for cell, a in acts:
            ab = a.encode()[:65535]
            f.write(struct.pack("<HHH", cell & 0xFFFF, cell >> 16, len(ab)))
            f.write(ab)
    nd = sum(1 for b in flags if b & F_DIGGABLE)
    print(f"  map {name}: {w}x{h}, {len(tilesets)} sheets, {len(warps)} warps, "
          f"{len(actions)} actions, {nd} diggable")
    edges = [(x, y, tgt, tx, ty) for (x, y, tgt, tx, ty) in warps]
    for cell, a in actions.items():
        parts = a.split()
        if parts and parts[0] in ("Warp", "LockedDoorWarp", "MagicWarp") and len(parts) >= 4:
            try:
                edges.append((cell % w, cell // w, parts[3], int(parts[1]), int(parts[2])))
            except ValueError:
                pass
    return {"w": w, "h": h, "edges": edges}


# --------------------------------------------------------------------------
# Data
# --------------------------------------------------------------------------

def _load_json(content, rel):
    p = os.path.join(content, *rel.split("/"))
    if not os.path.isfile(p):
        print(f"  ! {rel} not found")
        return {}
    with open(p, encoding="utf-8-sig") as f:
        return json.load(f)


def _clean(s):
    return str(s).replace("\t", " ").replace("\n", " ")


class _ItemIds:
    """Item ids are strings in 1.6 (mods use text ids). The engine uses ints:
    numeric ids stay as they are, the rest get stable numbers from 100000."""

    def __init__(self):
        self.map = {}

    def build(self, objs):
        self.map = {}
        texty = []
        for k in objs:
            k = str(k)
            if k.isdigit():
                self.map[k] = int(k)
            else:
                texty.append(k)
        for i, k in enumerate(sorted(texty)):
            self.map[k] = 100000 + i
        # names too, so "Ridge Cherry" style references resolve
        self.names = {}
        for k, o in objs.items():
            nm = str((o or {}).get("Name", "")).lower()
            if nm and nm not in self.names:
                self.names[nm] = self.map[str(k)]

    def get(self, ref):
        r = str(ref).strip()
        if r.startswith("(O)"):
            r = r[3:]
        if r in self.map:
            return self.map[r]
        if r.lstrip("-").isdigit():
            return int(r) if int(r) < 0 else None   # negative = category
        return getattr(self, "names", {}).get(r.lower())


ITEM_IDS = _ItemIds()
MODS_DIR = [None]
BASE_CONTENT = [None]
TEXTURES_FROM_DATA = set()


def write_data(content, out_dir):
    os.makedirs(out_dir, exist_ok=True)
    season_bits = {"spring": 1, "summer": 2, "fall": 4, "winter": 8}

    objs = _load_json(content, "Data/Objects.json")
    ITEM_IDS.build(objs)
    with open(os.path.join(out_dir, "item_ids.dat"), "w", encoding="utf-8") as f:
        for sid, iid in sorted(ITEM_IDS.map.items(), key=lambda kv: kv[1]):
            if not sid.isdigit():
                f.write(f"{_clean(sid)}\t{iid}\n")

    crops = _load_json(content, "Data/Crops.json")
    n = 0
    with open(os.path.join(out_dir, "crops.dat"), "w", encoding="utf-8") as f:
        for seed, c in crops.items():
            sid = ITEM_IDS.get(seed)
            hid = ITEM_IDS.get(c.get("HarvestItemId", ""))
            if sid is None or hid is None:
                continue
            mask = 0
            for ss in c.get("Seasons", []) or []:
                mask |= season_bits.get(str(ss).lower(), 0)
            phases = ",".join(str(int(d)) for d in c.get("DaysInPhase", []) if int(d) < 99999)
            tex = c.get("Texture") or "TileSheets/crops"
            tex_n = tex_name(str(tex).replace("\\", "/"))
            TEXTURES_FROM_DATA.add(str(tex).replace("\\", "/"))
            f.write("\t".join(_clean(x) for x in [
                sid, mask, phases or "1", int(c.get("RegrowDays", -1)), hid,
                int(c.get("SpriteIndex", 0)), 1 if c.get("IsRaised") else 0,
                int(c.get("HarvestMinStack", 1)), tex_n,
            ]) + "\n")
            n += 1
    print(f"  crops.dat: {n} crops")

    n = 0
    with open(os.path.join(out_dir, "objects.dat"), "w", encoding="utf-8") as f:
        for oid, o in objs.items():
            iid = ITEM_IDS.get(oid)
            if iid is None:
                continue
            tex = o.get("Texture") or "Maps/springobjects"
            tex = str(tex).replace("\\", "/")
            TEXTURES_FROM_DATA.add(tex)
            try:
                price = int(float(o.get("Price", 0) or 0))
            except (TypeError, ValueError):
                price = 0
            f.write("\t".join(_clean(x) for x in [
                iid, o.get("Name", oid), price,
                int(o.get("SpriteIndex", 0) or 0), int(o.get("Category", 0) or 0),
                int(o.get("Edibility", -300) if o.get("Edibility") is not None else -300), tex_name(tex), oid,
            ]) + "\n")
            n += 1
    print(f"  objects.dat: {n} objects")

    write_shops(content, out_dir, season_bits)
    write_location_names(content, out_dir)
    write_spawns(content, out_dir, MODS_DIR[0])
    farmer_bob(content, out_dir)
    write_furniture(content, out_dir)
    write_crafting(content, out_dir, objs)
    write_trees(content, out_dir)
    write_fish(content, out_dir)
    write_quests(content, out_dir)
    write_mail(content, out_dir)
    write_professions(content, out_dir)
    write_farm_types(content, out_dir)

    tools = _load_json(content, "Data/Tools.json")
    n = 0
    with open(os.path.join(out_dir, "tools.dat"), "w", encoding="utf-8") as f:
        for key, t in tools.items():
            idx = int(t.get("MenuSpriteIndex", -1))
            if idx < 0:
                idx = int(t.get("SpriteIndex", 0))
            texp = t.get("Texture") or "TileSheets\\tools"
            f.write("\t".join(_clean(x) for x in [
                key, t.get("ClassName", key), t.get("Name", key),
                int(t.get("UpgradeLevel", 0)), idx, tex_name(texp.replace("\\", "/")),
                int(t.get("SalePrice", -1) if t.get("SalePrice") is not None else -1),
                str(t.get("ConventionalUpgradeFrom") or "").replace("(T)", ""),
            ]) + "\n")
            n += 1
    print(f"  tools.dat: {n} tools")


def _parse_condition(cond, season_bits):
    """Returns (ok, seasonMask, minYear). ok=False for conditions we can't
    evaluate yet, so the item is left out rather than shown too early."""
    mask, min_year = 0, 1
    if not cond:
        return True, 0, 1
    for clause in [c.strip() for c in cond.split(",") if c.strip()]:
        parts = clause.split()
        neg = parts[0].startswith("!")
        q = parts[0].lstrip("!").upper()
        args = parts[1:]
        if q in ("SEASON", "LOCATION_SEASON"):
            if q == "LOCATION_SEASON":
                args = args[1:]   # first arg is the location
            m = 0
            for a in args:
                m |= season_bits.get(a.lower(), 0)
            if neg:
                m = 0xF & ~m
            mask = (mask & m) if mask else m
        elif q == "YEAR" and args and not neg:
            try:
                min_year = max(min_year, int(args[0]))
            except ValueError:
                return False, 0, 1
        else:
            return False, 0, 1
    return True, mask, min_year


ORE_NODES = {   # Farm Type Manager ore names -> node object ids (weights come from the area)
    "stone": 343, "geode": 75, "frozengeode": 76, "magmageode": 77, "copper": 751, "iron": 290,
    "gold": 764, "iridium": 765, "mystic": 46, "radioactive": 95,
}


def _rects(lst):
    out = []
    for r in lst or []:
        nums = [int(float(x)) for x in re.split(r"[,;/]", str(r)) if x.strip().lstrip("-").replace(".", "").isdigit()]
        if len(nums) == 4:
            x1, y1, x2, y2 = nums
            out.append(f"{min(x1, x2)} {min(y1, y2)} {max(x1, x2)} {max(y1, y2)}")
    return ",".join(out)


def write_spawns(content, out_dir, mods_dir=None):
    """Daily forage/ore spawns: vanilla Data/Locations Forage + Farm Type Manager packs."""
    season_bits = {"spring": 1, "summer": 2, "fall": 4, "winter": 8}
    lines = []
    locs = _load_json(content, "Data/Locations.json") if os.path.isfile(
        os.path.join(content, "Data", "Locations.json")) else {}
    for name, d in (locs or {}).items():
        if not d or name == "Farm":
            continue
        forage = d.get("Forage") or []
        if not forage:
            continue
        lines.append(f"L\t{name}\t{int(d.get('MinDailyForageSpawn', 1) or 1)}\t"
                     f"{int(d.get('MaxDailyForageSpawn', 4) or 4)}\t{int(d.get('MaxSpawnedForageAtOnce', 6) or 6)}")
        for fo in forage:
            iid = ITEM_IDS.get(fo.get("ItemId") or fo.get("Id") or "")
            if iid is None or iid < 0:
                continue
            mask = season_bits.get(str(fo.get("Season") or "").lower(), 15)
            lines.append(f"F\t{name}\t{mask}\t{iid}\t{float(fo.get('Chance', 1) or 1)}")
    n_areas = 0
    if mods_dir:
        import sjson
        for dirpath, dirs, files in os.walk(mods_dir):
            if "manifest.json" not in files or "content.json" not in files:
                continue
            try:
                man = sjson.load(os.path.join(dirpath, "manifest.json"))
                if "farmtypemanager" not in str(man.get("ContentPackFor", {}).get("UniqueID", "")).lower():
                    continue
                ftm = sjson.load(os.path.join(dirpath, "content.json"))
            except ValueError:
                continue
            for kind, key in (("forage", "Forage_Spawn_Settings"), ("ore", "Ore_Spawn_Settings")):
                if ftm.get(kind.capitalize() + "SpawnEnabled") is False:
                    continue
                sett = ftm.get(key) or {}
                for a in sett.get("Areas") or []:
                    seasons = [str(x).lower() for x in ((a.get("ExtraConditions") or {}).get("Seasons") or [])]
                    mask = 0
                    for ss in seasons:
                        mask |= season_bits.get(ss, 0)
                    mask = mask or 15
                    if kind == "forage":
                        per = []
                        for sk in ("SpringItemIndex", "SummerItemIndex", "FallItemIndex", "WinterItemIndex"):
                            items = a.get(sk)
                            if items is None:
                                items = sett.get(sk) or []
                            ids = []
                            for it in items or []:
                                if isinstance(it, (int, str)):
                                    v = ITEM_IDS.get(it)
                                    if v is not None and v >= 0:
                                        ids.append(f"{v}:1")
                            per.append(",".join(ids))
                        items_s = "|".join(per)
                    else:
                        ch = a.get("StartingSpawnChance") or sett.get("StartingSpawnChance") or {"Stone": 70, "Copper": 30}
                        ids = [f"{ORE_NODES[k.lower()]}:{int(v)}" for k, v in ch.items()
                               if k.lower() in ORE_NODES and v]
                        items_s = "|".join([",".join(ids)] * 4)
                    if not items_s.replace("|", ""):
                        continue
                    terrain = ",".join(str(t).lower() for t in a.get("IncludeTerrainTypes") or []) or "all"
                    lines.append("\t".join(["A", _clean(a.get("UniqueAreaID", "")), _clean(a.get("MapName", "")), kind,
                                             str(int(a.get("MinimumSpawnsPerDay", 0) or 0)),
                                             str(int(a.get("MaximumSpawnsPerDay", 0) or 0)), str(mask), terrain,
                                             _rects(a.get("IncludeCoordinates")), _rects(a.get("ExcludeCoordinates")),
                                             items_s]))
                    n_areas += 1
    with open(os.path.join(out_dir, "spawns.dat"), "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    print(f"  spawns.dat: {sum(1 for l in lines if l[0] == 'L')} vanilla forage locations, {n_areas} mod spawn areas")


def write_location_names(content, out_dir):
    """Data/Locations DisplayName -> data/location_names.dat (mod places get real names)."""
    p = os.path.join(content, "Data", "Locations.json")
    if not os.path.isfile(p):
        return
    try:
        data = json.load(open(p, encoding="utf-8-sig"))
    except ValueError:
        return
    n = 0
    with open(os.path.join(out_dir, "location_names.dat"), "w", encoding="utf-8") as f:
        for name, d in data.items():
            dn = str((d or {}).get("DisplayName") or "")
            if not dn or dn.startswith("["):
                continue
            f.write(f"{_clean(name)}\t{_clean(dn)}\n")
            n += 1
    print(f"  location_names.dat: {n} names")


def write_trees(content, out_dir):
    """data/trees.dat: type, seed item, texture per season, growth chance."""
    trees = _load_json(content, "Data/WildTrees.json")
    n = 0
    with open(os.path.join(out_dir, "trees.dat"), "w", encoding="utf-8") as f:
        for tid, t in trees.items():
            if not str(tid).isdigit() or not t:
                continue
            tex = {}
            for e in t.get("Textures") or []:
                if e.get("Condition"):
                    continue
                path = str(e.get("Texture", "")).replace("\\", "/")
                if not path:
                    continue
                TEXTURES_FROM_DATA.add(path)
                tex[str(e.get("Season") or "").lower() or "any"] = tex_name(path)
            if not tex:
                continue
            default = tex.get("any") or tex.get("spring") or next(iter(tex.values()))
            seasons = [tex.get(s, default) for s in ("spring", "summer", "fall", "winter")]
            seed = ITEM_IDS.get(t.get("SeedItemId") or "")
            f.write("\t".join(_clean(x) for x in [
                tid, seed if seed is not None else -1, *seasons, float(t.get("GrowthChance", 0.2) or 0.2)]) + "\n")
            n += 1
    print(f"  trees.dat: {n} tree types")


def write_fish(content, out_dir):
    """data/fish.dat (how each fish behaves) and data/fishspots.dat (what bites where)."""
    fish = _load_json(content, "Data/Fish.json")
    sb = {"spring": 1, "summer": 2, "fall": 4, "winter": 8}
    n = 0
    with open(os.path.join(out_dir, "fish.dat"), "w", encoding="utf-8") as f:
        for fid, raw in fish.items():
            p = str(raw).split("/")
            if len(p) < 13 or p[1] == "trap":
                continue
            iid = ITEM_IDS.get(fid)
            if iid is None:
                continue
            mask = 0
            for ss in p[6].split():
                mask |= sb.get(ss.lower(), 0)
            weather = {"sunny": 1, "rainy": 2}.get(p[7].strip().lower(), 0)
            f.write("\t".join(_clean(x) for x in [
                iid, int(p[1]), p[2], int(p[3]), int(p[4]), p[5].replace(" ", ","), mask or 15, weather,
                int(p[12]) if p[12].strip().lstrip("-").isdigit() else 0]) + "\n")
            n += 1
    print(f"  fish.dat: {n} fish")

    locs = _load_json(content, "Data/Locations.json")
    n = 0
    with open(os.path.join(out_dir, "fishspots.dat"), "w", encoding="utf-8") as f:
        for loc, d in locs.items():
            for e in (d or {}).get("Fish") or []:
                if e.get("Condition") or e.get("IsBossFish") or e.get("RequireMagicBait") or e.get("FishAreaId"):
                    continue
                ids = e.get("RandomItemId") or [e.get("ItemId")]
                ids = [ITEM_IDS.get(i) for i in ids if i and str(i).startswith("(O)")]
                ids = [i for i in ids if i is not None]
                if not ids:
                    continue
                mask = 0
                if e.get("Season"):
                    mask = sb.get(str(e["Season"]).lower(), 0)
                f.write("\t".join(_clean(x) for x in [
                    loc, ",".join(map(str, ids)), float(e.get("Chance", 1.0) or 1.0), mask or 15,
                    int(e.get("MinFishingLevel", 0) or 0), int(e.get("Precedence", 0) or 0),
                    1 if e.get("IgnoreFishDataRequirements") else 0]) + "\n")
                n += 1
    print(f"  fishspots.dat: {n} entries")


def write_quests(content, out_dir):
    """data/quests.dat: id, type, title, description, objective, target, next, reward."""
    quests = _load_json(content, "Data/Quests.json")
    n = 0
    with open(os.path.join(out_dir, "quests.dat"), "w", encoding="utf-8") as f:
        for qid, raw in quests.items():
            p = str(raw).split("/")
            if len(p) < 8:
                continue
            target = p[4].strip()
            # item targets become engine ids ("(O)24" -> 24, "Name 24 1" keeps the name)
            parts = target.split()
            conv = []
            for t in parts:
                if t.startswith("(BC)") and BIG_IDS.get(t[4:]) is not None:
                    conv.append(f"B{BIG_IDS[t[4:]]}")
                    continue
                iid = ITEM_IDS.get(t) if (t.startswith("(O)") or t.isdigit()) else None
                conv.append(str(iid) if iid is not None else t)
            nxt = " ".join(x.lstrip("h") for x in p[5].split()) if p[5].strip() else "-1"
            f.write("\t".join(_clean(x) for x in [
                qid, p[0], p[1], p[2], p[3], " ".join(conv) or "-", nxt,
                int(p[6]) if p[6].strip().lstrip("-").isdigit() else 0]) + "\n")
            n += 1
    print(f"  quests.dat: {n} quests")


def write_mail(content, out_dir):
    """data/mail.dat (letters; item ids converted) and data/mailtriggers.dat (when they arrive)."""
    mail = _load_json(content, "Data/mail.json")

    def fix_items(text):
        def conv(m):
            kind, rest = m.group(1), m.group(2).split()
            if kind in ("object", "id") and rest:
                out, i = [], 0
                while i < len(rest):
                    ref = rest[i]
                    iid = ITEM_IDS.get(ref) if not ref.startswith("(") or ref.startswith("(O)") else None
                    cnt = rest[i + 1] if i + 1 < len(rest) and rest[i + 1].isdigit() else "1"
                    if iid is not None:
                        out += [str(iid), cnt]
                    i += 2
                return f"%item object {' '.join(out)} %%" if out else ""
            return m.group(0)
        return re.sub(r"%item (\w+)([^%]*)%%", conv, text)

    n = 0
    with open(os.path.join(out_dir, "mail.dat"), "w", encoding="utf-8") as f:
        for key, text in mail.items():
            f.write(f"{_clean(key)}\t{_clean(fix_items(str(text)))}\n")
            n += 1
    trig = _load_json(content, "Data/TriggerActions.json") or []
    m = 0
    with open(os.path.join(out_dir, "mailtriggers.dat"), "w", encoding="utf-8") as f:
        for t in trig:
            acts = t.get("Actions") or ([t["Action"]] if t.get("Action") else [])
            if not acts:
                continue
            f.write("\t".join(_clean(x) for x in [
                t.get("Id", ""), t.get("Trigger", ""), t.get("Condition") or "-", "|".join(acts)]) + "\n")
            m += 1
    print(f"  mail.dat: {n} letters, mailtriggers.dat: {m}")


# profession ids by skill (the game's numbering): level 5 pair, then the two
# level 10 pairs that follow each level 5 choice
PROFESSIONS = [
    ("Farming", ["Rancher", "Tiller", "Coopmaster", "Shepherd", "Artisan", "Agriculturist"]),
    ("Fishing", ["Fisher", "Trapper", "Angler", "Pirate", "Mariner", "Luremaster"]),
    ("Foraging", ["Forester", "Gatherer", "Lumberjack", "Tapper", "Botanist", "Tracker"]),
    ("Mining", ["Miner", "Geologist", "Blacksmith", "Prospector", "Excavator", "Gemologist"]),
    ("Combat", ["Fighter", "Scout", "Brute", "Defender", "Acrobat", "Desperado"]),
]


def write_professions(content, out_dir):
    """data/professions.dat: id, skill, level, parent, name, description (text from your Strings/UI)."""
    ui = _load_json(content, "Strings/UI.json")
    with open(os.path.join(out_dir, "professions.dat"), "w", encoding="utf-8") as f:
        for si, (skill, names) in enumerate(PROFESSIONS):
            for k, key in enumerate(names):
                pid = si * 6 + k
                level = 5 if k < 2 else 10
                parent = -1 if k < 2 else si * 6 + (0 if k < 4 else 1)
                name = ui.get(f"LevelUp_ProfessionName_{key}", key)
                desc = ui.get(f"LevelUp_ProfessionDescription_{key}", "")
                f.write("\t".join(_clean(x) for x in [pid, si, level, parent, key, name, desc]) + "\n")
    print("  professions.dat: 30")


# farm types: the game's built-in ones by map, plus Data/AdditionalFarms
FARM_TYPES = [("Standard", "Standard", "Farm"), ("Fishing", "Riverland", "Farm_Fishing"),
              ("Foraging", "Forest", "Farm_Foraging"), ("Mining", "Hill-top", "Farm_Mining"),
              ("Combat", "Wilderness", "Farm_Combat"), ("FourCorners", "Four Corners", "Farm_FourCorners"),
              ("Beach", "Beach", "Farm_Island")]


def write_farm_types(content, out_dir):
    maps = os.path.join(content, "Maps")
    rows = [(k, n, m) for k, n, m in FARM_TYPES if os.path.isfile(os.path.join(maps, m + ".tmx"))]
    for extra in _load_json(content, "Data/AdditionalFarms.json") or []:
        mp = extra.get("MapName")
        if not mp or not os.path.isfile(os.path.join(maps, mp + ".tmx")):
            continue
        key = str(extra.get("Id", mp))
        name = key.replace("Farm", " Farm").strip() if key.endswith("Farm") else key
        name = name.split(".")[-1].replace("_", " ")
        rows.append((key, name, mp))
    with open(os.path.join(out_dir, "farmtypes.dat"), "w", encoding="utf-8") as f:
        for k, n, m in rows:
            f.write(f"{_clean(k)}\t{_clean(n)}\t{_clean(m)}\n")
    print(f"  farmtypes.dat: {len(rows)} farm types")


# ---------------------------------------------------------------- crafting / machines
BIG_IDS = {}
CATEGORY_TAGS = {
    -2: "gem", -4: "fish", -5: "egg", -6: "milk", -7: "cooking", -8: "crafting",
    -12: "minerals", -14: "meat", -15: "metal_resources", -16: "building_resources",
    -17: "sell_at_pierres", -18: "sell_at_pierres_and_marnies", -19: "fertilizer",
    -20: "junk", -21: "bait", -22: "tackle", -23: "sell_at_fish_shop", -25: "cooking",
    -26: "artisan_goods", -27: "syrup", -28: "monster_loot", -74: "seeds",
    -75: "vegetable", -79: "fruits", -80: "flowers", -81: "greens",
}
# FLAVORED_ITEM <type> -> the object it makes (looked up in your Data/Objects)
FLAVOR_OUTPUT = {"Wine": "348", "Juice": "350", "Jelly": "344", "Pickle": "342",
                 "AgedRoe": "447", "Roe": "812", "Honey": "340", "SmokedFish": "SmokedFish",
                 "DriedFruit": "DriedFruit", "DriedMushroom": "DriedMushrooms", "Bait": "SpecificBait"}
AUTO_TRIGGERS = {"DayUpdate", "MachinePutDown", "OutputCollected"}


def _item_tags(oid, o):
    tags = {str(t).lower() for t in (o.get("ContextTags") or [])}
    cat = int(o.get("Category", 0) or 0)
    if cat in CATEGORY_TAGS:
        tags.add("category_" + CATEGORY_TAGS[cat])
    tags.add("id_o_" + str(oid).lower())
    return tags


def _big_ref(ref):
    r = str(ref).strip()
    if r.startswith("(BC)"):
        return BIG_IDS.get(r[4:])
    return None


def _ing_list(spec):
    """'388 30 (BC)13 2 -5 1' -> [(id, count, isBig)]"""
    parts = spec.split()
    out = []
    for i in range(0, len(parts) - 1, 2):
        ref, cnt = parts[i], parts[i + 1]
        b = _big_ref(ref)
        if b is not None:
            out.append((b, int(cnt), 1))
            continue
        iid = ITEM_IDS.get(ref)
        if iid is None:
            return None
        out.append((iid, int(cnt), 0))
    return out


def write_crafting(content, out_dir, objs):
    bigs = _load_json(content, "Data/BigCraftables.json")
    BIG_IDS.clear()
    texty = sorted(k for k in bigs if not str(k).isdigit())
    for k in bigs:
        if str(k).isdigit():
            BIG_IDS[str(k)] = int(k)
    for i, k in enumerate(texty):
        BIG_IDS[k] = 200000 + i
    with open(os.path.join(out_dir, "bigcraftables.dat"), "w", encoding="utf-8") as f:
        for k, b in bigs.items():
            tex = str(b.get("Texture") or "TileSheets/Craftables").replace("\\", "/")
            TEXTURES_FROM_DATA.add(tex)
            f.write("\t".join(_clean(x) for x in [
                BIG_IDS[str(k)], b.get("Name", k), int(b.get("Price", 0) or 0),
                int(b.get("SpriteIndex", 0) or 0), tex_name(tex), k]) + "\n")
    print(f"  bigcraftables.dat: {len(bigs)}")

    recipes = _load_json(content, "Data/CraftingRecipes.json")
    n = 0
    with open(os.path.join(out_dir, "crafting.dat"), "w", encoding="utf-8") as f:
        for name, raw in recipes.items():
            fs = str(raw).split("/")
            if len(fs) < 5:
                continue
            ing = _ing_list(fs[0])
            out = fs[2].split()
            is_big = fs[3].strip().lower() == "true"
            oid = (BIG_IDS.get(out[0].replace("(BC)", "")) if is_big else ITEM_IDS.get(out[0]))
            if ing is None or oid is None:
                continue
            cnt = int(out[1]) if len(out) > 1 and out[1].isdigit() else 1
            f.write("\t".join(_clean(x) for x in [
                name, ",".join(f"{i}:{c}:{b}" for i, c, b in ing), 1 if is_big else 0, oid, cnt,
                fs[4].strip() or "null"]) + "\n")
            n += 1
    print(f"  crafting.dat: {n} recipes")

    # machines: flatten rules into explicit input lists
    tags = {ITEM_IDS.get(k): _item_tags(k, o or {}) for k, o in objs.items()}
    tags.pop(None, None)

    def matches(req_id, req_tags):
        if req_id:
            iid = ITEM_IDS.get(req_id)
            cand = [iid] if iid is not None else []
        else:
            cand = list(tags)
        res = []
        for c in cand:
            t = tags.get(c, set())
            ok = True
            for rt in req_tags or []:
                for alt in str(rt).split(","):
                    alt = alt.strip().lower()
                    neg = alt.startswith("!")
                    has = alt.lstrip("!") in t
                    if has == neg:
                        ok = False
            if ok:
                res.append(c)
        return res

    machines = _load_json(content, "Data/Machines.json")
    n = 0
    with open(os.path.join(out_dir, "machines.dat"), "w", encoding="utf-8") as f:
        for key, m in machines.items():
            mid = _big_ref(key)
            if mid is None or not m:
                continue
            fuel, fuel_n = -1, 0
            for extra in m.get("AdditionalConsumedItems") or []:
                fi = ITEM_IDS.get(extra.get("ItemId", ""))
                if fi is not None:
                    fuel, fuel_n = fi, int(extra.get("RequiredCount", 1))
                break
            for rule in m.get("OutputRules") or []:
                outs = [o for o in (rule.get("OutputItem") or [])
                        if not o.get("Condition") and not o.get("OutputMethod") and o.get("ItemId")]
                if not outs:
                    continue
                o = outs[0]
                item = str(o["ItemId"]).split()
                flavor, copy = "", 0
                if item[0] == "FLAVORED_ITEM" and len(item) > 1:
                    flavor = item[1]
                    out_id = ITEM_IDS.get(FLAVOR_OUTPUT.get(flavor, ""))
                    if "DROP_IN_ID" not in item:
                        flavor = ""
                elif item[0] == "DROP_IN_ID":
                    out_id, copy = 0, 1
                else:
                    out_id = ITEM_IDS.get(item[0])
                if out_id is None:
                    continue
                count = max(1, int(o.get("MinStack", 1) or 1))
                minutes = int(rule.get("MinutesUntilReady", -1) or -1)
                days = int(rule.get("DaysUntilReady", -1) or -1)
                for trig in rule.get("Triggers") or []:
                    if trig.get("Condition"):
                        continue
                    kinds = {t.strip() for t in str(trig.get("Trigger", "")).split(",")}
                    if kinds & AUTO_TRIGGERS and "ItemPlacedInMachine" not in kinds:
                        ins, auto = [], 1
                    elif "ItemPlacedInMachine" in kinds:
                        ins, auto = matches(trig.get("RequiredItemId"), trig.get("RequiredTags")), 0
                        if not ins:
                            continue
                    else:
                        continue
                    f.write("\t".join(_clean(x) for x in [
                        mid, ",".join(str(i) for i in sorted(ins)), int(trig.get("RequiredCount", 1) or 1),
                        out_id, count, flavor or "-", copy, minutes, days, fuel, fuel_n, auto]) + "\n")
                    n += 1
    print(f"  machines.dat: {n} rules")


def write_shops(content, out_dir, season_bits):
    shops = _load_json(content, "Data/Shops.json")
    n = skipped = 0
    with open(os.path.join(out_dir, "shops.dat"), "w", encoding="utf-8") as f:
        for shop_id, sh in shops.items():
            for it in sh.get("Items", []) or []:
                if it.get("IsRecipe"):
                    continue
                raw = str(it.get("ItemId") or it.get("Id") or "")
                if raw.startswith("(") and not raw.startswith("(O)"):
                    continue      # furniture, tools, hats...: not sold here yet
                iid = ITEM_IDS.get(raw)
                if iid is None or iid < 0:
                    continue
                ok, mask, min_year = _parse_condition(it.get("Condition"), season_bits)
                if not ok:
                    skipped += 1
                    continue
                price = int(it.get("Price", -1) or -1)
                f.write(f"{_clean(shop_id)}\t{iid}\t{price}\t{mask}\t{min_year}\n")
                n += 1
    print(f"  shops.dat: {n} shop items ({skipped} with conditions not supported yet)")



SEASON_IDX = {"spring": 0, "summer": 1, "fall": 2, "winter": 3}
DIR_IDX = {"down": 0, "right": 1, "up": 2, "left": 3}


def _flat(v):
    return str(v).replace("\t", " ").replace("\r", " ").replace("\n", " ")


def _display(name, raw):
    if not raw or str(raw).startswith("["):
        return name    # localisation token -> use the internal name
    return str(raw)


def _dir(v):
    if isinstance(v, int):
        # Stardew numbers: 0 up, 1 right, 2 down, 3 left -> ours: 0 down 1 right 2 up 3 left
        return {0: 2, 1: 1, 2: 0, 3: 3}.get(v, 0)
    return DIR_IDX.get(str(v).lower(), 0)


def _load_npc_defs(content):
    """Returns {name: dict(display, bseason, bday, home, hx, hy, hdir, social)}."""
    out = {}
    chars = _load_json(content, "Data/Characters.json") if os.path.isfile(
        os.path.join(content, "Data", "Characters.json")) else {}
    for name, c in chars.items():
        home = None
        for h in c.get("Home") or []:
            if h.get("Location") and h.get("Tile") is not None:
                home = h
                break
        if not home:
            continue
        t = home["Tile"]
        hx, hy = (t.get("X", 0), t.get("Y", 0)) if isinstance(t, dict) else (0, 0)
        cs = str(c.get("CanSocialize") or "TRUE").strip().upper()
        out[name] = dict(display=_display(name, c.get("DisplayName")),
                         bseason=SEASON_IDX.get(str(c.get("BirthSeason") or "").lower(), -1),
                         bday=int(c.get("BirthDay") or 0), home=home["Location"], hx=int(hx), hy=int(hy),
                         hdir=_dir(home.get("Direction", "down")), social=0 if cs == "FALSE" else 1)
    if out:
        return out
    # pre-1.6 fallback: Data/NPCDispositions
    disp = _load_json(content, "Data/NPCDispositions.json") if os.path.isfile(
        os.path.join(content, "Data", "NPCDispositions.json")) else {}
    for name, line in disp.items():
        f = str(line).split("/")
        if len(f) < 11:
            continue
        bs, bd = (f[8].split() + ["", "0"])[:2]
        hp = f[10].split()
        if len(hp) < 3:
            continue
        out[name] = dict(display=f[11] if len(f) > 11 and f[11] else name,
                         bseason=SEASON_IDX.get(bs.lower(), -1), bday=int(bd or 0),
                         home=hp[0], hx=int(hp[1]), hy=int(hp[2]), hdir=0, social=1)
    return out


def write_events(content, out_dir):
    """Data/Events/<Location>.json -> data/events/<Location>.dat (base language only)."""
    src = os.path.join(content, "Data", "Events")
    if not os.path.isdir(src):
        print("  no Data/Events folder")
        return
    os.makedirs(os.path.join(out_dir, "events"), exist_ok=True)
    files = n = 0
    for fn in sorted(os.listdir(src)):
        if not fn.endswith(".json") or re.search(r"\.[a-z]{2}-[A-Z]{2}\.json$", fn):
            continue
        try:
            ev = json.load(open(os.path.join(src, fn), encoding="utf-8-sig"))
        except ValueError as e:
            print(f"  ! {fn}: {e}")
            continue
        with open(os.path.join(out_dir, "events", fn[:-5] + ".dat"), "w", encoding="utf-8") as f:
            for k, v in ev.items():
                f.write(f"{_flat(k)}\t{_flat(v)}\n")
                n += 1
        files += 1
    print(f"  {n} events in {files} locations")


def write_npcs(content, out_dir, textures):
    defs = _load_npc_defs(content)
    tastes = _load_json(content, "Data/NPCGiftTastes.json") if os.path.isfile(
        os.path.join(content, "Data", "NPCGiftTastes.json")) else {}
    os.makedirs(os.path.join(out_dir, "schedules"), exist_ok=True)
    os.makedirs(os.path.join(out_dir, "dialogue"), exist_ok=True)

    def ids(txt):
        out = []
        for t in str(txt).split():
            v = ITEM_IDS.get(t)
            if v is not None:
                out.append(str(v))
        return " ".join(out)

    with open(os.path.join(out_dir, "universal_tastes.dat"), "w", encoding="utf-8") as f:
        for i, key in enumerate(("Universal_Love", "Universal_Like", "Universal_Dislike",
                                 "Universal_Hate", "Universal_Neutral")):
            f.write(f"{i}\t{ids(tastes.get(key, ''))}\n")

    rainy_path = os.path.join(content, "Characters", "Dialogue", "rainy.json")
    rainy = json.load(open(rainy_path, encoding="utf-8-sig")) if os.path.isfile(rainy_path) else {}
    n_sched = n_dia = n_spr = 0
    with open(os.path.join(out_dir, "npcs.dat"), "w", encoding="utf-8") as f:
        for name, d in sorted(defs.items()):
            parts = str(tastes.get(name, "")).split("/")
            parts += [""] * (10 - len(parts))
            texts = [_flat(parts[i]) for i in (0, 2, 4, 6, 8)]
            lists = [ids(parts[i]) for i in (1, 3, 5, 7, 9)]
            f.write("\t".join([name, _flat(d["display"]), str(d["bseason"]), str(d["bday"]), d["home"],
                               str(d["hx"]), str(d["hy"]), str(d["hdir"]), str(d["social"])]
                              + lists + texts) + "\n")
            sp = os.path.join(content, "Characters", "schedules", name + ".json")
            if not os.path.isfile(sp):
                sp = os.path.join(content, "Characters", "Schedules", name + ".json")
            if os.path.isfile(sp):
                sched = json.load(open(sp, encoding="utf-8-sig"))
                with open(os.path.join(out_dir, "schedules", name + ".dat"), "w", encoding="utf-8") as g:
                    for k, v in sched.items():
                        g.write(f"{_flat(k)}\t{_flat(v)}\n")
                n_sched += 1
            dp = os.path.join(content, "Characters", "Dialogue", name + ".json")
            lines = {}
            if os.path.isfile(dp):
                lines = json.load(open(dp, encoding="utf-8-sig"))
            if name in rainy:
                lines["__rain"] = rainy[name]
            if lines:
                with open(os.path.join(out_dir, "dialogue", name + ".dat"), "w", encoding="utf-8") as g:
                    for k, v in lines.items():
                        g.write(f"{_flat(k)}\t{_flat(v)}\n")
                n_dia += 1
            for folder in ("Characters", "Portraits"):
                if find_png(content, f"{folder}/{name}"):
                    textures.add(f"{folder}/{name}")
                    n_spr += 1
    print(f"  {len(defs)} villagers, {n_sched} schedules, {n_dia} dialogue files, {n_spr} sprites/portraits")


DEFAULT_LAYOUT = """\
# stardew3ds farm layout -- editable, no rebuild needed.
# Positions are in tiles. Lines starting with # are ignored.
#
# building <name> <texture> <srcX> <srcY> <srcW> <srcH> <tileX> <tileY> <footW> <footH>
#   Sprite is drawn bottom-aligned to the footprint and centred on it.
#   The footprint blocks movement.
building farmhouse Buildings_houses 0 0 160 144 59 12 9 6
building shippingbin Buildings_Shipping_Bin 0 0 32 32 71 14 2 1
#
# door <tileX> <tileY>        A on this tile (walking into it from below) = go to bed
door 64 17
# spawn <tileX> <tileY>       where you wake up each morning
spawn 64 18
# bin <tileX> <tileY>         tiles that open the shipping bin
bin 71 14
bin 72 14
#
# bed <map> <tileX> <tileY> <w> <h>   A on these tiles = sleep
bed FarmHouse 9 5 2 3
#
# furniture <map> <id> <tileX> <tileY>   id from Data/Furniture.json; tile = top-left of
#                                        its footprint. 2048 is the starter bed.
furniture FarmHouse 2048 9 5
# wake <map> <tileX> <tileY>          where you wake up (nearest open tile is used)
wake FarmHouse 9 8
#
# farmer <name>                       what villagers call you
farmer Farmer
#
# outfit <shirt> <pants> <pantsR> <pantsG> <pantsB> <hair> <hairR> <hairG> <hairB>
#   shirt / pants = SpriteIndex from Data/Shirts.json / Data/Pants.json
outfit 0 0 46 85 183 0 138 78 38
"""


def location_maps(content):
    """Location name -> .tmx path. Base maps by file name, plus locations that
    Data/Locations creates from another map asset (how 1.6 mods add places)."""
    maps_dir = os.path.join(content, "Maps")
    out = {f[:-4]: os.path.join(maps_dir, f) for f in os.listdir(maps_dir) if f.lower().endswith(".tmx")}
    locs = os.path.join(content, "Data", "Locations.json")
    if os.path.isfile(locs):
        try:
            data = json.load(open(locs, encoding="utf-8-sig"))
        except ValueError:
            data = {}
        for name, d in data.items():
            col = (d or {}).get("CreateOnLoad") or {}
            mp = col.get("MapPath")
            if not mp:
                continue
            p = os.path.join(content, *str(mp).replace("\\", "/").split("/")) + ".tmx"
            if os.path.isfile(p):
                out[name] = p
    return out


# ---------------------------------------------------------------- UI fonts
# The 3DS screen is small, so the game's font is shrunk here once into a crisp
# pixel font (no blurry scaling on the console). Both fonts come from your
# own Content folder and only ever live on your SD card.
SMALL_FONT_PX = 16
FONT_SIZES = (("small", 16),)


def write_fonts(content, out_root):
    for name, px in FONT_SIZES:
        _write_small_font(content, out_root, name, px)
    _write_bold_font(content, out_root)


def _write_small_font(content, out_root, name, px):
    data_dir, tex_dir = os.path.join(out_root, "data"), os.path.join(out_root, "tex")
    fj, fp = os.path.join(content, "Fonts", "SmallFont.json"), os.path.join(content, "Fonts", "SmallFont.png")
    if os.path.isfile(fj) and os.path.isfile(fp):
        with open(fj, encoding="utf-8-sig") as f:
            font = json.load(f)
        src = Image.open(fp).convert("RGBA")
        line = int(font.get("LineSpacing", 33)) or 33
        k = px / line
        cells = {}
        for g in (font.get("Glyphs") or {}).values():
            ch = g.get("Character") or ""
            if len(ch) != 1 or ord(ch) > 255:
                continue
            b, cr = g["BoundsInTexture"], g["Cropping"]
            cw = max(1, int(round(float(g.get("WidthIncludingBearings", b["Width"])))))
            cell = Image.new("RGBA", (cw, line), (0, 0, 0, 0))
            if b["Width"] and b["Height"]:
                gl = src.crop((b["X"], b["Y"], b["X"] + b["Width"], b["Y"] + b["Height"]))
                cell.paste(gl, (int(float(g.get("LeftSideBearing", 0))) + cr["X"], cr["Y"]), gl)
            sm = cell.resize((max(1, round(cw * k)), px), Image.BOX)
            cells[ord(ch)] = sm.getchannel("A").point(lambda v: 255 if v > 110 else 0)
        # trim rows no glyph uses
        rows = [y for y in range(px) if any(c.crop((0, y, c.size[0], y + 1)).getbbox() for c in cells.values())]
        top, bot = (rows[0], rows[-1] + 1) if rows else (0, px)
        h = bot - top
        cw = max(c.size[0] for c in cells.values())
        cols = 16
        atlas = Image.new("RGBA", (cols * cw, ((len(cells) + cols - 1) // cols) * h), (0, 0, 0, 0))
        white = Image.new("RGBA", (cw, h), (255, 255, 255, 255))
        lines = [f"#font\t{h}\t{max(h + 2, round(line * k) - 3)}"]
        for i, code in enumerate(sorted(cells)):
            a = cells[code].crop((0, top, cells[code].size[0], bot))
            x, y = (i % cols) * cw, (i // cols) * h
            atlas.paste(white.crop((0, 0) + a.size), (x, y), a)
            lines.append(f"{code}\t{x}\t{y}\t{a.size[0]}")
        tmp = os.path.join(tex_dir, f"_uifont_{name}.png")
        os.makedirs(tex_dir, exist_ok=True)
        atlas.save(tmp)
        convert_texture(tmp, os.path.join(tex_dir, f"UiFont_{name}.s3t"))
        os.remove(tmp)
        with open(os.path.join(data_dir, f"font_{name}.dat"), "w", encoding="utf-8") as f:
            f.write("\n".join(lines) + "\n")
        print(f"  font_{name}.dat: {len(cells)} glyphs, {h}px")


def _write_bold_font(content, out_root):
    data_dir = os.path.join(out_root, "data")
    # bold pixel font for titles: 8x16 cells from ' ' onwards, widths measured
    bp = find_png(content, "LooseSprites/font_bold")
    if bp:
        im = Image.open(bp).convert("RGBA")
        a = im.getchannel("A")
        cols = im.size[0] // 8
        lines = ["#font\t16\t18"]
        for code in range(32, 256):
            i = code - 32
            x, y = (i % cols) * 8, (i // cols) * 16
            if y + 16 > im.size[1]:
                break
            bb = a.crop((x, y, x + 8, y + 16)).getbbox()
            if code == 32:
                lines.append(f"{code}\t{x}\t{y}\t4")
            elif bb:
                lines.append(f"{code}\t{x + bb[0]}\t{y}\t{bb[2] - bb[0]}")
        with open(os.path.join(data_dir, "font_bold.dat"), "w", encoding="utf-8") as f:
            f.write("\n".join(lines) + "\n")
        print(f"  font_bold.dat: {len(lines) - 1} glyphs")


# ---------------------------------------------------------------- parallel work
# Python threads can't use more than one core for this kind of work, so the
# heavy steps run in separate processes (--jobs=N, default: all cores).

def _season_merge(base, mods, alt, season):
    import cp
    t0 = time.time()
    cp.apply_all(base, mods, alt, log=lambda *_: None, season=season, maps_only=True)
    return season, time.time() - t0


def _season_maps(content, alt, season, spring_locs, out_maps, out_tex):
    """Convert maps that differ from spring; returns (season, count, textures, extra, errors)."""
    import filecmp
    alt_locs = location_maps(alt)
    n = 0
    alt_tex = set()
    errors = []
    for name, path in sorted(alt_locs.items()):
        spring = spring_locs.get(name)
        if spring and os.path.isfile(spring) and filecmp.cmp(spring, path, shallow=False):
            continue
        try:
            if convert_map(alt, f"{name}@{season}", out_maps, alt_tex, path):
                n += 1
        except Exception as e:  # noqa: BLE001
            errors.append(f"{name}@{season}: {e}")
    # tilesheets that only exist in this season's merge are converted here;
    # the rest are converted with the spring textures
    keep, extra = set(), 0
    for t in alt_tex:
        if find_png(content, t):
            keep.add(t)
        elif find_png(alt, t):
            convert_texture(find_png(alt, t), os.path.join(out_tex, tex_name(t) + ".s3t"))
            extra += 1
    shutil.rmtree(alt, ignore_errors=True)
    return season, n, keep, extra, errors


def _texture_batch(jobs):
    out = []
    for name, src, dst in jobs:
        try:
            w, h = convert_texture(src, dst)
            out.append((name, w, h, None))
        except Exception as e:  # noqa: BLE001
            out.append((name, 0, 0, str(e)))
    return out


def _pool(jobs):
    if jobs <= 1:
        return None
    import concurrent.futures
    return concurrent.futures.ProcessPoolExecutor(max_workers=jobs)


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    opts = {a.split("=")[0]: (a.split("=", 1)[1] if "=" in a else True) for a in sys.argv[1:] if a.startswith("--")}
    if len(args) < 2:
        print(__doc__)
        print("Options: --mods=<folder with Content Patcher packs, e.g. SVE / Ridgeside>")
        print("         --jobs=N  processes to use (default: all CPU cores, 1 = no parallel work)")
        sys.exit(1)
    t_start = time.time()
    stamp = [time.time()]

    def took(what):
        now = time.time()
        print(f"  [{what}: {now - stamp[0]:.0f}s]")
        stamp[0] = now

    jobs = int(opts.get("--jobs", 0) or 0) or (os.cpu_count() or 1)
    pool = _pool(jobs)
    print(f"Using {jobs} process{'es' if jobs != 1 else ''}")
    content = args[0]
    out_root = os.path.join(args[1], "3ds", "stardew3ds")
    season_merges = []
    if opts.get("--mods") and opts["--mods"] is not True:
        import cp
        merged = os.path.join(args[1], "_merged_content")
        BASE_CONTENT[0] = content
        MODS_DIR[0] = opts["--mods"]
        # summer / fall / winter merges run alongside the main (spring) one
        for season in ("summer", "fall", "winter"):
            alt = os.path.join(args[1], f"_merged_{season}")
            if pool:
                season_merges.append((season, alt, pool.submit(_season_merge, content, MODS_DIR[0], alt, season)))
            else:
                season_merges.append((season, alt, None))
        print("Mods...")
        cp.apply_all(content, opts["--mods"], merged)
        content = merged
        took("mods merged")
    if not os.path.isdir(os.path.join(content, "Maps")):
        sys.exit(f"'{content}' doesn't look like an unpacked Content folder "
                 "(no Maps/ inside). Run StardewXnbHack first.")

    locs = location_maps(content)
    out_maps, out_tex = os.path.join(out_root, "maps"), os.path.join(out_root, "tex")
    # seasonal map conversion: each season starts as soon as its merge is done
    season_maps = []
    for season, alt, fut in season_merges:
        if pool:
            def start(f, season=season, alt=alt):
                return pool.submit(_season_maps, content, alt, season, locs, out_maps, out_tex)
            season_maps.append((season, alt, fut, start))
        else:
            season_maps.append((season, alt, None, None))

    print("Maps...")
    textures = set()
    names = sorted(locs)
    for m in REQUIRED_MAPS:
        if m not in names:
            sys.exit(f"Maps/{m}.tmx missing -- is this really an unpacked Content folder?")
    # kick off the seasonal map work now (waits for each merge in its own worker)
    running = []
    for season, alt, fut, start in season_maps:
        if pool:
            fut.result()
            running.append(start(fut))
    ok = 0
    graph = {}
    for m in names:
        try:
            info = convert_map(content, m, out_maps, textures, locs[m])
            if info:
                ok += 1
                graph[m] = info
        except Exception as e:   # one odd map shouldn't stop the rest
            print(f"  ! {m}: {e}")
    print(f"  {ok}/{len(names)} maps converted")
    took("maps")
    # Seasonal variants of modded maps: kept only where they differ from
    # spring, saved as maps/<Location>@<season>.smap.
    if season_maps:
        results = []
        if pool:
            results = [f.result() for f in running]
        else:
            for season, alt, _, _ in season_maps:
                print(f"Seasonal maps: {season}...")
                _season_merge(BASE_CONTENT[0], MODS_DIR[0], alt, season)
                results.append(_season_maps(content, alt, season, locs, out_maps, out_tex))
        for season, n, keep, extra, errors in results:
            textures |= keep
            for e in errors:
                print(f"  ! {e}")
            print(f"  {n} maps differ in {season} ({extra} season-only tilesheets)")
        took("seasonal maps")

    os.makedirs(os.path.join(out_root, "data"), exist_ok=True)
    with open(os.path.join(out_root, "data", "graph.dat"), "w", encoding="utf-8") as f:
        for m, info in sorted(graph.items()):
            for (x, y, tgt, tx, ty) in info["edges"]:
                if tgt in graph:   # only routes to maps we actually have
                    f.write(f"{m}\t{x}\t{y}\t{tgt}\t{tx}\t{ty}\n")

    print("Data...")
    write_data(content, os.path.join(out_root, "data"))
    print("Villagers...")
    write_npcs(content, os.path.join(out_root, "data"), textures)
    print("Events...")
    write_events(content, os.path.join(out_root, "data"))
    print("Fonts...")
    write_fonts(content, out_root)
    took("data, villagers, events")

    for t in sorted(TEXTURES_FROM_DATA):
        if find_png(content, t):
            textures.add(t)
    for cp_path, required in EXTRA_TEXTURES:
        if find_png(content, cp_path):
            textures.add(cp_path)
        elif required:
            print(f"  ! required texture {cp_path}.png missing")

    print(f"Textures ({len(textures)})...")
    work = []
    for name in sorted(textures):
        src = find_png(content, name)
        if not src:
            print(f"  ! {name}.png missing")
            continue
        work.append((name, src, os.path.join(out_tex, tex_name(name) + ".s3t")))
    if pool and len(work) > 8:
        # biggest files first, spread round-robin so batches are even
        work.sort(key=lambda j: -os.path.getsize(j[1]))
        batches = [work[i::jobs * 4] for i in range(jobs * 4)]
        done = []
        for fut in [pool.submit(_texture_batch, b) for b in batches if b]:
            done += fut.result()
    else:
        done = _texture_batch(work)
    for name, w, h, err in sorted(done):
        print(f"  ! {name}: {err}" if err else f"  {name}  {w}x{h}")
    took("textures")
    if pool:
        pool.shutdown()

    lay = os.path.join(out_root, "layout.txt")
    if not os.path.exists(lay):
        with open(lay, "w") as f:
            f.write(DEFAULT_LAYOUT)
    with open(os.path.join(out_root, "VERSION"), "w") as f:
        f.write("assets 1\n")
    print(f"\nDone in {time.time() - t_start:.0f}s. Copy '{os.path.join(sys.argv[2], '3ds')}' to the root of your SD card.")


if __name__ == "__main__":
    main()
