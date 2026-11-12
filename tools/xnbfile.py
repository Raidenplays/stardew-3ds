"""Reads Stardew Valley's packed Content/*.xnb files (what a normal Steam
install has) into Python values, so the converter can work from the game
folder as-is instead of needing an unpacked copy.

Decompression uses the same C++ code as the 3DS setup (tools/native), loaded
with ctypes: on Android the app ships it as libs3dxnb.so; on a PC it is
compiled on first use if a C++ compiler is around.

Data models (Stardew 1.6's Data/Objects etc.) are read with the class layouts
from the player's own StardewValley.GameData.dll (see netschema.py).
"""
import ctypes
import os
import re
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
_lib = None


def _load_lib():
    global _lib
    if _lib is not None:
        return _lib
    names = [os.environ.get("S3D_XNB_LIB", ""), os.path.join(HERE, "native", "libs3dxnb.so"),
             os.path.join(HERE, "native", "s3dxnb.dll"), os.path.join(HERE, "native", "libs3dxnb.dylib"),
             "libs3dxnb.so"]
    for n in names:
        if not n:
            continue
        try:
            _lib = ctypes.CDLL(n)
            break
        except OSError:
            continue
    if _lib is None:
        _lib = _build_lib()
    _lib.s3d_xnb_decompress.argtypes = [ctypes.c_char_p, ctypes.c_size_t, ctypes.c_int, ctypes.c_char_p, ctypes.c_size_t]
    _lib.s3d_xnb_decompress.restype = ctypes.c_int
    return _lib


def _build_lib():
    """PC fallback: compile tools/native with whatever C++ compiler exists."""
    src = os.path.join(HERE, "..", "source")
    if sys.platform == "win32":
        out = os.path.join(HERE, "native", "s3dxnb.dll")
    elif sys.platform == "darwin":
        out = os.path.join(HERE, "native", "libs3dxnb.dylib")
    else:
        out = os.path.join(HERE, "native", "libs3dxnb.so")
    files = [os.path.join(HERE, "native", "s3dxnb.cpp"), os.path.join(src, "setup", "lzx.cpp"),
             os.path.join(src, "setup", "xnb.cpp")]
    for cxx in ("c++", "g++", "clang++"):
        try:
            subprocess.run([cxx, "-O2", "-std=c++17", "-shared", "-fPIC", "-I" + src, "-o", out] + files,
                           check=True, capture_output=True)
            return ctypes.CDLL(out)
        except (OSError, subprocess.CalledProcessError):
            continue
    raise RuntimeError("Reading packed .xnb files needs the s3dxnb helper. Install a C++ compiler "
                       "(it builds itself), use the Android converter app, or give an unpacked Content folder.")


class XnbError(ValueError):
    pass


class Reader:
    def __init__(self, data):
        self.d = data
        self.i = 0
        self.readers = []

    def u8(self):
        v = self.d[self.i]
        self.i += 1
        return v

    def i32(self):
        v = struct.unpack_from("<i", self.d, self.i)[0]
        self.i += 4
        return v

    def u32(self):
        v = struct.unpack_from("<I", self.d, self.i)[0]
        self.i += 4
        return v

    def i64(self):
        v = struct.unpack_from("<q", self.d, self.i)[0]
        self.i += 8
        return v

    def f32(self):
        v = struct.unpack_from("<f", self.d, self.i)[0]
        self.i += 4
        return v

    def f64(self):
        v = struct.unpack_from("<d", self.d, self.i)[0]
        self.i += 8
        return v

    def v7(self):
        r = s = 0
        while True:
            b = self.d[self.i]
            self.i += 1
            r |= (b & 0x7F) << s
            s += 7
            if not b & 0x80:
                return r

    def string(self):
        n = self.v7()
        s = self.d[self.i:self.i + n].decode("utf-8", "replace")
        self.i += n
        return s

    def char(self):
        b = self.u8()
        if b < 0x80:
            return chr(b)
        extra = 1 if (b & 0xE0) == 0xC0 else 2 if (b & 0xF0) == 0xE0 else 3
        cp = b & (0x3F >> extra)
        for _ in range(extra):
            cp = (cp << 6) | (self.u8() & 0x3F)
        return chr(cp)

    def bytes(self, n):
        v = self.d[self.i:self.i + n]
        if len(v) != n:
            raise XnbError("truncated")
        self.i += n
        return v

    def type_id(self):
        """Reader name for the next object, or None for null."""
        n = self.v7()
        if n == 0:
            return None
        if n > len(self.readers):
            raise XnbError(f"bad type id {n}")
        return self.readers[n - 1]


# The web converter has no native helper to load: it plugs in its own
# decompressor here (same code, built for the browser).
DECOMPRESS = None


def _decompress(body, mode, out_size):
    """mode 0 = LZX, 1 = LZ4. Returns the bytes, or None when the data is bad."""
    if DECOMPRESS is not None:
        return DECOMPRESS(body, mode, out_size)
    out = ctypes.create_string_buffer(out_size)
    if not _load_lib().s3d_xnb_decompress(body, len(body), mode, out, out_size):
        return None
    return out.raw


def load(path):
    """Decompresses an .xnb; returns a Reader positioned at the root object."""
    with open(path, "rb") as f:
        buf = f.read()
    return load_bytes(buf)


def load_bytes(buf):
    if len(buf) < 10 or buf[:3] != b"XNB":
        raise XnbError("not an XNB file")
    flags = buf[5]
    size = min(struct.unpack_from("<I", buf, 6)[0], len(buf))
    if flags & 0xC0:
        out_size = struct.unpack_from("<I", buf, 10)[0]
        body = buf[14:size]
        data = _decompress(body, 0 if flags & 0x80 else 1, out_size)
        if data is None:
            raise XnbError("compressed data is damaged")
    else:
        data = buf[10:size]
    r = Reader(data)
    n = r.v7()
    for _ in range(n):
        r.readers.append(r.string())
        r.i32()
    r.v7()     # shared resources
    return r


# --- reader names --------------------------------------------------------------

def split_generic(name):
    """'A`2[[X, asm],[Y, asm]]' -> ('A`2', ['X', 'Y']) (assembly names dropped)."""
    m = name.find("[")
    if m < 0:
        return _strip_asm(name), []
    base = name[:m]
    if not name.rstrip().endswith("]"):      # 'A`1[[X]], Assembly'
        name = name[:name.rfind("]") + 1]
    inner = name[m + 1:name.rfind("]")]
    args, depth, cur = [], 0, ""
    for ch in inner:
        if ch == "[":
            depth += 1
            if depth == 1:
                cur = ""
                continue
        elif ch == "]":
            depth -= 1
            if depth == 0:
                args.append(_strip_asm(cur))
                continue
        if depth >= 1:
            cur += ch
    return base, args


def _strip_asm(t):
    """'System.String, mscorlib, Version=...' -> 'System.String' (keeps generic brackets)."""
    depth = 0
    for k, ch in enumerate(t):
        if ch == "[":
            depth += 1
        elif ch == "]":
            depth -= 1
        elif ch == "," and depth == 0:
            return t[:k].strip()
    return t.strip()


_PRIM_NET = {"System.String": "string", "System.Int32": "int", "System.Boolean": "bool",
             "System.Single": "float", "System.Double": "double", "System.Int64": "long",
             "System.UInt32": "uint", "System.Int16": "short", "System.Byte": "byte",
             "System.Char": "char", "System.Object": "object", "System.SByte": "sbyte",
             "System.UInt16": "ushort", "System.UInt64": "ulong"}

_XNA_STRUCTS = ("Microsoft.Xna.Framework.Vector2", "Microsoft.Xna.Framework.Point",
                "Microsoft.Xna.Framework.Rectangle", "Microsoft.Xna.Framework.Color",
                "Microsoft.Xna.Framework.Vector3", "Microsoft.Xna.Framework.Vector4")


class Decoder:
    """Turns reader output into JSON-ready values, the way StardewXnbHack
    writes them (member names as in the game, enums by name)."""

    def __init__(self, schema=None):
        self.schema = schema

    # value read when the static type is known (value types are stored raw)
    def read_static(self, r, t):
        """t: netschema.T describing the declared type."""
        if t.kind == "prim":
            if t.value:
                return self._prim(r, t.name)
            return self.read_object(r)            # string / object: has a type id
        if t.kind == "array" or not t.value:
            return self.read_object(r)
        # value types: no type id
        name = t.name
        if t.kind == "generic" and name == "System.Nullable`1":
            has = r.u8()
            return self.read_static(r, t.args[0]) if has else None
        if name in _XNA_STRUCTS:
            return self._xna(r, name)
        k = self.schema.get(name) if self.schema else None
        if k is not None and k.kind == "enum":
            v = r.i32()
            return _enum_name(k, v)
        if k is not None:
            return self._reflective(r, name)
        if name.startswith("System."):
            p = _PRIM_NET.get(name)
            if p:
                return self._prim(r, p)
        raise XnbError(f"don't know how to read value type {name}")

    def _prim(self, r, p):
        if p == "int":
            return r.i32()
        if p == "bool":
            return r.u8() != 0
        if p == "float":
            return _f(r.f32())
        if p == "double":
            return r.f64()
        if p == "long":
            return r.i64()
        if p == "uint":
            return r.u32()
        if p in ("short",):
            return struct.unpack("<h", r.bytes(2))[0]
        if p in ("ushort",):
            return struct.unpack("<H", r.bytes(2))[0]
        if p in ("byte",):
            return r.u8()
        if p in ("sbyte",):
            return struct.unpack("<b", r.bytes(1))[0]
        if p == "ulong":
            return struct.unpack("<Q", r.bytes(8))[0]
        if p == "char":
            return r.char()
        if p == "string":
            return r.string()
        raise XnbError(f"primitive {p}")

    def _xna(self, r, name):
        n = name.rsplit(".", 1)[1]
        if n == "Vector2":   # SMAPI's JSON writes these as "X, Y"
            x, y = _f(r.f32()), _f(r.f32())
            return f"{_num(x)}, {_num(y)}"
        if n == "Vector3":
            return {"X": _f(r.f32()), "Y": _f(r.f32()), "Z": _f(r.f32())}
        if n == "Vector4":
            return {"X": _f(r.f32()), "Y": _f(r.f32()), "Z": _f(r.f32()), "W": _f(r.f32())}
        if n == "Point":
            return {"X": r.i32(), "Y": r.i32()}
        if n == "Rectangle":
            return {"X": r.i32(), "Y": r.i32(), "Width": r.i32(), "Height": r.i32()}
        if n == "Color":
            c = r.bytes(4)
            return {"R": c[0], "G": c[1], "B": c[2], "A": c[3]}
        raise XnbError(name)

    def _reflective(self, r, cls):
        members = self.schema.all_members(cls) if self.schema else None
        if members is None:
            raise XnbError(f"class {cls} isn't in StardewValley.GameData.dll")
        out = {}
        for m in members:
            out[m.name] = self.read_static(r, m.type)
        return out

    # object with a type id in front
    def read_object(self, r):
        rd = r.type_id()
        if rd is None:
            return None
        return self.read_with(r, rd)

    def read_with(self, r, reader_name):
        base, args = split_generic(reader_name)
        short = base.rsplit(".", 1)[-1]
        if short == "StringReader":
            return r.string()
        if short == "Int32Reader":
            return r.i32()
        if short == "BooleanReader":
            return r.u8() != 0
        if short == "SingleReader":
            return _f(r.f32())
        if short == "DoubleReader":
            return r.f64()
        if short == "CharReader":
            return r.char()
        if short in ("Vector2Reader", "PointReader", "RectangleReader", "ColorReader", "Vector3Reader"):
            return self._xna(r, "Microsoft.Xna.Framework." + short[:-6])
        if short == "ListReader`1" or short == "ArrayReader`1":
            et = self._t(args[0])
            n = r.u32()
            return [self.read_static(r, et) for _ in range(n)]
        if short == "DictionaryReader`2":
            kt, vt = self._t(args[0]), self._t(args[1])
            n = r.u32()
            out = {}
            for _ in range(n):
                k = self.read_static(r, kt)
                out[str(k) if not isinstance(k, str) else k] = self.read_static(r, vt)
            return out
        if short == "NullableReader`1":
            has = r.u8()
            return self.read_static(r, self._t(args[0])) if has else None
        if short == "EnumReader`1":
            k = self.schema.get(args[0]) if self.schema else None
            v = r.i32()
            return _enum_name(k, v) if k else v
        if short == "ReflectiveReader`1":
            return self._reflective(r, args[0])
        raise XnbError(f"unsupported reader {short}")

    def _t(self, net_name):
        """A .NET type name from a reader string -> netschema.T."""
        from netschema import T
        base, args = split_generic(net_name)
        if not args:
            p = _PRIM_NET.get(net_name)
            if p:
                return T("prim", p, value=p not in ("string", "object"))
            if net_name.endswith("[]"):
                return T("array", None, [self._t(net_name[:-2])])
            if net_name in _XNA_STRUCTS:
                return T("named", net_name, value=True)
            k = self.schema.get(net_name) if self.schema else None
            return T("named", net_name, value=bool(k and k.kind in ("struct", "enum")))
        return T("generic", base, [self._t(a) for a in args],
                 value=base == "System.Nullable`1")


def _enum_name(k, v):
    """Enum value -> name; [Flags] combos as 'A, B' (ascending, like .NET)."""
    if v in k.enum_values:
        return k.enum_values[v]
    parts, rest = [], v
    for val in sorted(x for x in k.enum_values if x > 0):
        if val & (val - 1) == 0 and rest & val:
            parts.append(k.enum_values[val])
            rest &= ~val
    return ", ".join(parts) if parts and rest == 0 else v


def _num(v):
    return str(int(v)) if v == int(v) else repr(v)


def _f(x):
    """Floats as .NET prints them (shortest round-trip of the 32-bit value)."""
    if x == int(x) and abs(x) < 1e15:
        return x
    for p in range(6, 18):
        s = float(f"{x:.{p}g}")
        if struct.pack("<f", s) == struct.pack("<f", x) if abs(x) < 3.4e38 else s == x:
            return s
    return x
