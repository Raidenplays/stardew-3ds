#!/usr/bin/env python3
"""Checks tools/xnbfile.py (the converter's reader for packed game files)
against the synthetic samples from make_xnb_samples.py.

Usage: xnbpy_test.py SAMPLES_DIR"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools"))
import xnbfile  # noqa: E402

d = sys.argv[1]
fails = 0
premul = open(os.path.join(d, "tex_premul.rgba"), "rb").read()
for mode in ("raw", "lz4", "lzx"):
    r = xnbfile.load(os.path.join(d, f"tex_{mode}.xnb"))
    assert "Texture2DReader" in r.type_id()
    fmt, w, h, mips, n = r.i32(), r.u32(), r.u32(), r.u32(), r.u32()
    ok = (fmt, w, h, n) == (0, 96, 400, len(premul)) and r.bytes(n) == premul
    print(f"tex_{mode}: {'ok' if ok else 'BAD'}")
    fails += not ok

dec = xnbfile.Decoder()
v = dec.read_object(xnbfile.load(os.path.join(d, "dict_lzx.xnb")))
want = {f"key{i}": f"value {i}/" + "x" * (i % 40) for i in range(300)}
print("dict_lzx:", "ok" if v == want else "BAD")
fails += v != want
v = dec.read_object(xnbfile.load(os.path.join(d, "dict_int.xnb")))
want = {"5": "five", "17": "seventeen", "-2": "neg"}
print("dict_int:", "ok" if v == want else f"BAD {v}")
fails += v != want
print("all xnbfile tests passed" if not fails else f"{fails} FAILED")
sys.exit(1 if fails else 0)
