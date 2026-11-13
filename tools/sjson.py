"""Forgiving JSON reader, like the one SMAPI mods are written for:
allows // and /* */ comments and trailing commas."""
import json
import os
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
    last = ""          # the last character kept that isn't whitespace
    in_str = False
    i = 0
    n = len(s)
    while i < n:
        c = s[i]
        if in_str:
            res.append(c)
            last = c
            if c == "\\" and i + 1 < n:
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
            last = c
        elif c == ",":
            if not _TRAILING.match(s, i) and last not in (",", "[", "{", ""):   # also drops doubled/leading commas
                res.append(c)
                last = c
        elif c == "." and (i + 1 < n and s[i + 1].isdigit()) and (not res or not res[-1].isdigit()):
            res.append("0.")              # .03 -> 0.03
            last = "."
        elif (c.isalnum() or c == "_") and (not res or res[-1] in "{,\n\t \r"):
            # possibly an unquoted key:  0: "x"   or  Name: 1
            m = _BARE_KEY.match(s, i) if last in ("{", ",") else None
            if m:
                res.append('"' + m.group(1) + '"')
                last = '"'
                i += len(m.group(1))
                continue
            res.append(c)
            last = c
        else:
            res.append(c)
            if not c.isspace():
                last = c
        i += 1
    return "".join(res)


_TRAILING = re.compile(r",\s*([}\]])")
_BARE_KEY = re.compile(r"([A-Za-z0-9_\-]+)\s*:")
_clean_cache = {}      # path -> (mtime, size, cleaned text): the seasons re-read the same mod files


def loads(text):
    if text.startswith("\ufeff"):
        text = text[1:]
    try:
        return json.loads(text, strict=False)      # most files are plain JSON: no clean-up needed
    except ValueError:
        return json.loads(_strip(text), strict=False)


def load(path):
    st = os.stat(path)
    hit = _clean_cache.get(path)
    if hit and hit[0] == st.st_mtime and hit[1] == st.st_size:
        return json.loads(hit[2], strict=False)
    with open(path, encoding="utf-8-sig", errors="replace") as f:
        text = f.read()
    if text.startswith("\ufeff"):
        text = text[1:]
    try:
        data = json.loads(text, strict=False)
        clean = text
    except ValueError:
        clean = _strip(text)
        data = json.loads(clean, strict=False)
    _clean_cache[path] = (st.st_mtime, st.st_size, clean)
    return data


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
