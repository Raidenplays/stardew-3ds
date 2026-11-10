"""Forgiving JSON reader, like the one SMAPI mods are written for:
allows // and /* */ comments and trailing commas."""
import json
import re


def _strip(text):
    out = []
    i, n = 0, len(text)
    in_str = False
    while i < n:
        c = text[i]
        if in_str:
            out.append(c)
            if c == "\\" and i + 1 < n:
                out.append(text[i + 1])
                i += 2
                continue
            if c == '"':
                in_str = False
            i += 1
            continue
        if c == '"':
            in_str = True
            out.append(c)
            i += 1
        elif text.startswith("//", i):
            j = text.find("\n", i)
            i = n if j < 0 else j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            i = n if j < 0 else j + 2
        else:
            out.append(c)
            i += 1
    s = "".join(out)
    # trailing commas before } or ] (outside strings: strings were kept intact,
    # so re-scan with a string-aware pass)
    res = []
    in_str = False
    i = 0
    while i < len(s):
        c = s[i]
        if in_str:
            res.append(c)
            if c == "\\" and i + 1 < len(s):
                res.append(s[i + 1])
                i += 2
                continue
            if c == '"':
                in_str = False
            i += 1
            continue
        if c == '"':
            in_str = True
            res.append(c)
        elif c == ",":
            m = re.match(r",\s*([}\]])", s[i:])
            prev = "".join(res).rstrip()[-1:]
            if not m and prev not in (",", "[", "{"):   # also drops doubled/leading commas
                res.append(c)
        elif c == "." and (i + 1 < len(s) and s[i + 1].isdigit()) and (not res or not res[-1].isdigit()):
            res.append("0.")              # .03 -> 0.03
        elif (c.isalnum() or c == "_") and (not res or res[-1] in "{,\n\t \r"):
            # possibly an unquoted key:  0: "x"   or  Name: 1
            m = re.match(r"([A-Za-z0-9_\-]+)\s*:", s[i:])
            back = "".join(res).rstrip()
            if m and back.endswith(("{", ",")):
                res.append('"' + m.group(1) + '"')
                i += len(m.group(1))
                continue
            res.append(c)
        else:
            res.append(c)
        i += 1
    return "".join(res)


def loads(text):
    if text.startswith("﻿"):
        text = text[1:]
    return json.loads(_strip(text), strict=False)


def load(path):
    with open(path, encoding="utf-8-sig", errors="replace") as f:
        return loads(f.read())


_ci_cache = {}


def ci_path(base, rel):
    """Resolve rel under base case-insensitively (mods are written on Windows).
    Returns the real path, or None."""
    parts = [p for p in rel.replace("\\", "/").split("/") if p and p != "."]
    cur = base
    for p in parts:
        if p == "..":
            cur = __import__("os").path.dirname(cur)
            continue
        cand = __import__("os").path.join(cur, p)
        if __import__("os").path.exists(cand):
            cur = cand
            continue
        try:
            listing = _ci_cache.get(cur)
            if listing is None:
                listing = {n.lower(): n for n in __import__("os").listdir(cur)}
                _ci_cache[cur] = listing
        except OSError:
            return None
        real = listing.get(p.lower())
        if real is None:
            return None
        cur = __import__("os").path.join(cur, real)
    return cur
