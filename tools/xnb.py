"""Reads uncompressed XNB files that hold Dictionary<string, string> data
(what SVE ships in assets/XNBs). Returns a dict, or raises ValueError."""
import struct


def _v7(d, i):
    r = s = 0
    while True:
        b = d[i]
        i += 1
        r |= (b & 0x7F) << s
        s += 7
        if not b & 0x80:
            return r, i


def _str(d, i):
    n, i = _v7(d, i)
    return d[i:i + n].decode("utf-8", "replace"), i + n


def read_dict(path):
    d = open(path, "rb").read()
    if d[:3] != b"XNB":
        raise ValueError("not an XNB file")
    if d[5] & 0xC0:
        raise ValueError("compressed XNB not supported")
    i = 10
    nreaders, i = _v7(d, i)
    readers = []
    for _ in range(nreaders):
        name, i = _str(d, i)
        i += 4                     # reader version
        readers.append(name)
    if not readers or "DictionaryReader" not in readers[0]:
        raise ValueError(f"not a dictionary XNB ({readers[:1]})")
    _, i = _v7(d, i)               # shared resource count
    _, i = _v7(d, i)               # reader index of the root object
    count = struct.unpack_from("<i", d, i)[0]
    i += 4
    out = {}
    for _ in range(count):
        vals = []
        for _ in range(2):
            idx, i = _v7(d, i)     # strings are written as objects: type index first
            if idx == 0:
                vals.append(None)
                continue
            s, i = _str(d, i)
            vals.append(s)
        out[vals[0]] = vals[1]
    return out
