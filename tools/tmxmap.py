"""In-memory map used while applying mods: read/write .tmx (what the converter
reads), and the operations Content Patcher's EditMap needs."""
import base64
import gzip
import os
import re
import struct
import zlib
import xml.etree.ElementTree as ET
from xml.sax.saxutils import escape


class Tileset:
    def __init__(self, name="", image="", columns=0, count=0, tilewidth=16):
        self.name = name
        self.image = image              # as written in the map (relative path or asset name)
        self.columns = columns
        self.count = count
        self.tilewidth = tilewidth
        self.firstgid = 1
        self.tile_props = {}            # local id -> {prop: value}
        self.anims = {}                 # local id -> [(local frame id, ms)]

    def key(self):
        """Identity for merging sheets between maps: the image's base name."""
        b = os.path.basename(self.image.replace("\\", "/"))
        return os.path.splitext(b)[0].lower()


class Map:
    LAYERS = ("Back", "Buildings", "Paths", "Front", "AlwaysFront")

    def __init__(self):
        self.width = 0
        self.height = 0
        self.properties = {}
        self.tilesets = []
        self.layers = {}                # name -> (w, h, [gid,...])
        self.cell_props = {}            # layer name -> {(x, y): {prop: value}}

    # ---------------------------------------------------------------- basics
    def assign_gids(self):
        g = 1
        for ts in self.tilesets:
            ts.firstgid = g
            g += max(1, ts.count)

    def normalize_layers(self):
        """Make every layer exactly width x height."""
        for name, (lw, lh, cells) in list(self.layers.items()):
            if lw == self.width and lh == self.height:
                continue
            out = [0] * (self.width * self.height)
            for y in range(min(lh, self.height)):
                for x in range(min(lw, self.width)):
                    out[y * self.width + x] = cells[y * lw + x]
            self.layers[name] = (self.width, self.height, out)

    def resize(self, w, h):
        if w <= self.width and h <= self.height:
            return
        self.width, self.height = max(w, self.width), max(h, self.height)
        self.normalize_layers()

    def tileset_for_gid(self, gid):
        gid &= 0x1FFFFFFF
        if not gid:
            return None, 0
        best = None
        for ts in self.tilesets:
            if gid >= ts.firstgid and (best is None or ts.firstgid > best.firstgid):
                best = ts
        return best, (gid - best.firstgid) if best else 0

    def ensure_layer(self, name):
        if name not in self.layers:
            self.layers[name] = (self.width, self.height, [0] * (self.width * self.height))
        return self.layers[name][2]

    # ---------------------------------------------------------------- tmx io
    @staticmethod
    def _props(el):
        out = {}
        pe = el.find("properties") if el is not None else None
        if pe is not None:
            for p in pe.findall("property"):
                v = p.get("value")
                out[p.get("name")] = v if v is not None else (p.text or "")
        return out

    @classmethod
    def read_tmx(cls, path):
        root = ET.parse(path).getroot()
        m = cls()
        m.width, m.height = int(root.get("width")), int(root.get("height"))
        m.properties = cls._props(root)
        base = os.path.dirname(path)
        for t in root.findall("tileset"):
            first = int(t.get("firstgid", 1))
            if t.get("source"):
                tsx = os.path.join(base, t.get("source"))
                t = ET.parse(tsx).getroot()
            img = t.find("image")
            src = img.get("source") if img is not None else ""
            tw = int(t.get("tilewidth", 16))
            iw = int(img.get("width", 0)) if img is not None else 0
            ih = int(img.get("height", 0)) if img is not None else 0
            cols = int(t.get("columns", iw // tw if tw else 0) or 0)
            count = int(t.get("tilecount", (iw // tw) * (ih // tw) if tw else 0) or 0)
            ts = Tileset(t.get("name", ""), src.replace("\\", "/"), cols, count, tw)
            ts.firstgid = first
            for tile in t.findall("tile"):
                tid = int(tile.get("id"))
                pr = cls._props(tile)
                if pr:
                    ts.tile_props[tid] = pr
                an = tile.find("animation")
                if an is not None:
                    ts.anims[tid] = [(int(fr.get("tileid")), int(fr.get("duration"))) for fr in an.findall("frame")]
            m.tilesets.append(ts)
        m.tilesets.sort(key=lambda s: s.firstgid)
        for lay in root.findall("layer"):
            lw, lh = int(lay.get("width", m.width)), int(lay.get("height", m.height))
            m.layers[lay.get("name")] = (lw, lh, _decode(lay.find("data")))
        for og in root.findall("objectgroup"):
            lname = og.get("name", "")
            for ob in og.findall("object"):
                pr = cls._props(ob)
                if not pr:
                    continue
                x = int(float(ob.get("x", 0)) // 16)
                y = int(float(ob.get("y", 0)) // 16)
                m.cell_props.setdefault(lname, {})[(x, y)] = pr
        m.normalize_layers()
        # re-number gids compactly so later merges are simple
        m._renumber()
        return m

    def _renumber(self):
        old = [(ts, ts.firstgid) for ts in self.tilesets]
        self.assign_gids()
        remap = {}
        for ts, of in old:
            if ts.firstgid != of:
                remap[of] = ts
        if not remap:
            return
        # need old firstgid -> ts lookup for every gid
        olds = sorted(old, key=lambda t: t[1])
        for name, (w, h, cells) in self.layers.items():
            for i, g in enumerate(cells):
                if not g:
                    continue
                gg = g & 0x1FFFFFFF
                ts = None
                for t, of in olds:
                    if gg >= of:
                        ts, tof = t, of
                if ts is not None:
                    cells[i] = ts.firstgid + (gg - tof)

    def write_tmx(self, path, image_for):
        """image_for(tileset) -> image path to write, relative to the tmx."""
        self.assign_gids()
        out = ['<?xml version="1.0" encoding="UTF-8"?>',
               f'<map version="1.10" orientation="orthogonal" renderorder="right-down" width="{self.width}" '
               f'height="{self.height}" tilewidth="16" tileheight="16" infinite="0">']
        if self.properties:
            out.append(" <properties>")
            for k, v in self.properties.items():
                out.append(f'  <property name="{escape(str(k), {chr(34): "&quot;"})}" value="{escape(str(v), {chr(34): "&quot;"})}"/>')
            out.append(" </properties>")
        for ts in self.tilesets:
            rows = (ts.count + max(1, ts.columns) - 1) // max(1, ts.columns)
            out.append(f' <tileset firstgid="{ts.firstgid}" name="{escape(ts.name, {chr(34): "&quot;"})}" '
                       f'tilewidth="16" tileheight="16" tilecount="{ts.count}" columns="{ts.columns}">')
            out.append(f'  <image source="{escape(image_for(ts), {chr(34): "&quot;"})}" width="{ts.columns * 16}" height="{rows * 16}"/>')
            for tid in sorted(set(ts.tile_props) | set(ts.anims)):
                out.append(f'  <tile id="{tid}">')
                if tid in ts.tile_props:
                    out.append("   <properties>")
                    for k, v in ts.tile_props[tid].items():
                        out.append(f'    <property name="{escape(str(k), {chr(34): "&quot;"})}" value="{escape(str(v), {chr(34): "&quot;"})}"/>')
                    out.append("   </properties>")
                if tid in ts.anims:
                    out.append("   <animation>")
                    for fr, ms in ts.anims[tid]:
                        out.append(f'    <frame tileid="{fr}" duration="{ms}"/>')
                    out.append("   </animation>")
                out.append("  </tile>")
            out.append(" </tileset>")
        for name, (w, h, cells) in self.layers.items():
            data = base64.b64encode(zlib.compress(struct.pack(f"<{len(cells)}I", *[c & 0xFFFFFFFF for c in cells]))).decode()
            out.append(f' <layer name="{escape(name)}" width="{w}" height="{h}">'
                       f'<data encoding="base64" compression="zlib">{data}</data></layer>')
        oid = 1
        for lname, cells in self.cell_props.items():
            if not cells:
                continue
            out.append(f' <objectgroup name="{escape(lname)}">')
            for (x, y), pr in sorted(cells.items()):
                out.append(f'  <object id="{oid}" name="TileData" x="{x * 16}" y="{y * 16}" width="16" height="16">')
                out.append("   <properties>")
                for k, v in pr.items():
                    out.append(f'    <property name="{escape(str(k), {chr(34): "&quot;"})}" value="{escape(str(v), {chr(34): "&quot;"})}"/>')
                out.append("   </properties>")
                out.append("  </object>")
                oid += 1
            out.append(" </objectgroup>")
        out.append("</map>")
        os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
        with open(path, "w", encoding="utf-8") as f:
            f.write("\n".join(out))

    # ---------------------------------------------------------------- patches
    def _sheet_in_self(self, ts):
        k = ts.key()
        for mine in self.tilesets:
            if mine.key() == k:
                return mine
        new = Tileset(ts.name or ts.key(), ts.image, ts.columns, ts.count, ts.tilewidth)
        new.tile_props = {k2: dict(v) for k2, v in ts.tile_props.items()}
        new.anims = {k2: list(v) for k2, v in ts.anims.items()}
        new._src_dir = getattr(ts, "_src_dir", None)
        self.tilesets.append(new)
        self._renumber_append(new)
        return new

    def _renumber_append(self, new):
        # new sheet goes after the current max gid; existing gids stay valid
        top = 1
        for ts in self.tilesets:
            if ts is not new:
                top = max(top, ts.firstgid + max(1, ts.count))
        new.firstgid = top

    def patch_area(self, src, from_area, to_area, mode="ReplaceByLayer"):
        """Copy src's tiles in from_area into to_area (Content Patcher EditMap)."""
        fx, fy, fw, fh = from_area or (0, 0, src.width, src.height)
        tx, ty = (to_area[0], to_area[1]) if to_area else (0, 0)
        tw, th = (to_area[2], to_area[3]) if to_area and len(to_area) > 3 else (fw, fh)
        fw, fh = min(fw, tw), min(fh, th)
        self.resize(tx + fw, ty + fh)
        mode = (mode or "ReplaceByLayer").lower()
        # keep gid allocation stable: map src sheets into ours first
        sheet_map = {id(ts): self._sheet_in_self(ts) for ts in src.tilesets}
        if mode == "replace":
            for name in list(self.layers):
                cells = self.layers[name][2]
                for y in range(ty, ty + fh):
                    for x in range(tx, tx + fw):
                        cells[y * self.width + x] = 0
            for name in self.cell_props:
                for key in [k for k in self.cell_props[name] if tx <= k[0] < tx + fw and ty <= k[1] < ty + fh]:
                    del self.cell_props[name][key]
        for name, (sw, sh, scells) in src.layers.items():
            dst = self.ensure_layer(name)
            for y in range(fh):
                for x in range(fw):
                    sx, sy = fx + x, fy + y
                    if sx >= sw or sy >= sh:
                        continue
                    g = scells[sy * sw + sx]
                    if not g:
                        if mode == "overlay":
                            continue
                        dst[(ty + y) * self.width + (tx + x)] = 0
                        continue
                    sts, local = src.tileset_for_gid(g)
                    if sts is None:
                        continue
                    mine = sheet_map[id(sts)]
                    dst[(ty + y) * self.width + (tx + x)] = mine.firstgid + local
        for name, cells in src.cell_props.items():
            for (x, y), pr in cells.items():
                if fx <= x < fx + fw and fy <= y < fy + fh:
                    self.cell_props.setdefault(name, {})[(tx + x - fx, ty + y - fy)] = dict(pr)
        # map properties from the patch file are merged too (warps, music, ...)
        for k, v in src.properties.items():
            if k not in self.properties:
                self.properties[k] = v

    def set_tile(self, layer, x, y, tilesheet_key=None, index=None, props=None, remove=False):
        self.resize(x + 1, y + 1)
        cells = self.ensure_layer(layer)
        if remove:
            cells[y * self.width + x] = 0
            self.cell_props.get(layer, {}).pop((x, y), None)
            return
        if index is not None:
            ts = None
            if tilesheet_key:
                for t in self.tilesets:
                    if t.name.lower() == tilesheet_key.lower() or t.key() == tilesheet_key.lower():
                        ts = t
            if ts is None:
                ts, _ = self.tileset_for_gid(cells[y * self.width + x])
            if ts is None and self.tilesets:
                ts = self.tilesets[0]
            if ts is not None:
                cells[y * self.width + x] = ts.firstgid + int(index)
        if props:
            cp = self.cell_props.setdefault(layer, {}).setdefault((x, y), {})
            for k, v in props.items():
                if v is None:
                    cp.pop(k, None)
                else:
                    cp[k] = str(v)


def _decode(data_el):
    enc = data_el.get("encoding")
    comp = data_el.get("compression")
    if enc == "csv":
        return [int(v) for v in re.split(r"[,\s]+", (data_el.text or "").strip()) if v]
    if enc == "base64":
        raw = base64.b64decode((data_el.text or "").strip())
        if comp == "zlib":
            raw = zlib.decompress(raw)
        elif comp == "gzip":
            raw = gzip.decompress(raw)
        return list(struct.unpack(f"<{len(raw) // 4}I", raw))
    return [int(t.get("gid", 0)) for t in data_el.findall("tile")]


def read_map(path):
    if path.lower().endswith(".tbin"):
        import tbin
        return tbin.read_tbin(path)
    return Map.read_tmx(path)
