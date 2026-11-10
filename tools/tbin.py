"""Reader for .tbin maps (the binary map format from the tIDE editor,
still used by many Stardew mods). Produces the same in-memory map that
tmxmap.py works with."""
import struct

from tmxmap import Map, Tileset


class _R:
    def __init__(self, data):
        self.d = data
        self.p = 0

    def take(self, n):
        if self.p + n > len(self.d):
            raise ValueError("unexpected end of tbin")
        v = self.d[self.p:self.p + n]
        self.p += n
        return v

    def u8(self):
        return self.take(1)[0]

    def i32(self):
        return struct.unpack("<i", self.take(4))[0]

    def f32(self):
        return struct.unpack("<f", self.take(4))[0]

    def string(self):
        n = self.i32()
        return self.take(n).decode("utf-8", "replace")

    def size(self):
        return self.i32(), self.i32()

    def props(self):
        out = {}
        for _ in range(self.i32()):
            k = self.string()
            t = self.u8()
            if t == 0:
                v = "T" if self.u8() else "F"
            elif t == 1:
                v = str(self.i32())
            elif t == 2:
                v = repr(self.f32())
            elif t == 3:
                v = self.string()
            else:
                raise ValueError(f"bad property type {t}")
            out[k] = v
        return out


def read_tbin(path):
    with open(path, "rb") as f:
        r = _R(f.read())
    if r.take(6) != b"tBIN10":
        raise ValueError("not a tBIN10 file")
    m = Map()
    r.string()          # id
    r.string()          # description
    m.properties = r.props()

    sheets = {}
    for _ in range(r.i32()):
        sid = r.string()
        r.string()                      # description
        image = r.string()
        sw, sh = r.size()               # sheet size, in tiles
        tw, th = r.size()
        r.size()                        # margin
        r.size()                        # spacing
        props = r.props()
        ts = Tileset(name=sid, image=image.replace("\\", "/"), columns=sw, count=sw * sh, tilewidth=tw)
        # tbin keeps per-tile-index props on the sheet as "@TileIndex@<n>@<Prop>"
        for k, v in props.items():
            if k.startswith("@TileIndex@"):
                parts = k.split("@")
                if len(parts) >= 4 and parts[2].isdigit():
                    ts.tile_props.setdefault(int(parts[2]), {})[parts[3]] = v
        m.tilesets.append(ts)
        sheets[sid] = ts
    m.assign_gids()

    w = h = None
    for _ in range(r.i32()):
        lid = r.string()
        r.u8()                          # visible
        r.string()                      # description
        lw, lh = r.size()
        r.size()                        # tile size
        r.props()                       # layer properties
        if w is None:
            w, h = lw, lh
            m.width, m.height = w, h
        else:
            m.width, m.height = max(m.width, lw), max(m.height, lh)
        cells = [0] * (lw * lh)
        cur = None
        for y in range(lh):
            x = 0
            while x < lw:
                c = chr(r.u8())
                if c == "T":
                    cur = sheets.get(r.string())
                elif c == "N":
                    x += r.i32()
                elif c == "S":
                    idx = r.i32()
                    r.u8()              # blend mode
                    tp = r.props()
                    if cur is not None:
                        cells[y * lw + x] = cur.firstgid + idx
                        if tp:
                            m.cell_props.setdefault(lid, {})[(x, y)] = tp
                    x += 1
                elif c == "A":
                    interval = r.i32()
                    nframes = r.i32()
                    frames = []
                    fs = cur
                    first_idx = None
                    while len(frames) < nframes:
                        cc = chr(r.u8())
                        if cc == "T":
                            fs = sheets.get(r.string())
                        elif cc == "S":
                            fi = r.i32()
                            r.u8()
                            r.props()
                            if first_idx is None:
                                first_idx = (fs, fi)
                            frames.append((fs, fi))
                        else:
                            raise ValueError(f"bad animation frame tag {cc!r}")
                    tp = r.props()
                    if first_idx and first_idx[0] is not None:
                        sh_, fi = first_idx
                        cells[y * lw + x] = sh_.firstgid + fi
                        same = [f for f in frames if f[0] is sh_]
                        if len(same) > 1:
                            sh_.anims[fi] = [(f[1], interval) for f in same]
                        if tp:
                            m.cell_props.setdefault(lid, {})[(x, y)] = tp
                    cur = fs if fs is not None else cur
                    x += 1
                else:
                    raise ValueError(f"bad tile tag {c!r} at layer {lid}")
        m.layers[lid] = (lw, lh, cells)
    m.normalize_layers()
    return m
