#!/usr/bin/env python3
"""Bring a PC Stardew Valley save to the 3DS.

    python3 saveimport.py <save file or folder> <converted 3ds/stardew3ds folder>

Reads the PC save (the XML file named like its folder, e.g. Raiden_123456789)
and the converted data (for item ids), and writes import.txt next to the 3DS
save. The next time stardew3ds starts it builds your farm from it (backing up
any farm already there) and saves it in its own format.

Only reads your save: nothing is changed on the PC side.
"""
import os
import sys
import xml.etree.ElementTree as ET

XSI_TYPE = "{http://www.w3.org/2001/XMLSchema-instance}type"
XSI_NIL = "{http://www.w3.org/2001/XMLSchema-instance}nil"
SEASONS = {"spring": 0, "summer": 1, "fall": 2, "winter": 3}


def txt(e, tag, default=""):
    if e is None:
        return default
    c = e.find(tag)
    if c is None and tag:   # saves from 2016-2019 spell some fields with the other capital (stack / Stack)
        c = e.find(tag[0].swapcase() + tag[1:])
    return c.text if c is not None and c.text is not None else default


def num(e, tag, default=0):
    try:
        return int(float(txt(e, tag, str(default))))
    except ValueError:
        return default


def truthy(e, tag):
    return txt(e, tag, "false").strip().lower() == "true"


def xtype(e):
    return e.get(XSI_TYPE, "") if e is not None else ""


def clean(s):
    return str(s).replace("\t", " ").replace("\n", " ")


class Ids:
    """Item ids as the converted data numbers them."""

    def __init__(self, root):
        self.obj = {}
        self.big = {}
        self.crop_by_harvest = {}
        d = os.path.join(root, "data")
        p = os.path.join(d, "item_ids.dat")
        if os.path.isfile(p):
            for line in open(p, encoding="utf-8"):
                f = line.rstrip("\n").split("\t")
                if len(f) >= 2:
                    self.obj[f[0]] = int(f[1])
        p = os.path.join(d, "bigcraftables.dat")
        if os.path.isfile(p):
            for line in open(p, encoding="utf-8"):
                f = line.rstrip("\n").split("\t")
                if len(f) >= 6:
                    self.big[f[5]] = int(f[0])
        p = os.path.join(d, "crops.dat")   # seed, seasons, phases, regrow, harvest...
        if os.path.isfile(p):
            for line in open(p, encoding="utf-8"):
                f = line.rstrip("\n").split("\t")
                if len(f) >= 5 and f[0].lstrip("-").isdigit() and f[4].lstrip("-").isdigit():
                    self.crop_by_harvest.setdefault(int(f[4]), int(f[0]))
        self.category = {}
        p = os.path.join(d, "objects.dat")   # id, name, price, sprite, category...
        if os.path.isfile(p):
            for line in open(p, encoding="utf-8"):
                f = line.rstrip("\n").split("\t")
                if len(f) >= 5 and f[0].lstrip("-").isdigit():
                    try:
                        self.category[int(f[0])] = int(f[4])
                    except ValueError:
                        pass

    def o(self, sid):
        sid = str(sid or "").strip()
        for p in ("(O)", "(o)"):
            if sid.startswith(p):
                sid = sid[len(p):]
        if sid in self.obj:
            return self.obj[sid]
        if sid.lstrip("-").isdigit():
            return int(sid)
        return None

    def b(self, sid):
        sid = str(sid or "").strip()
        if sid.startswith("(BC)"):
            sid = sid[4:]
        if sid in self.big:
            return self.big[sid]
        return int(sid) if sid.isdigit() else None


TOOL_TYPES = {"Axe": "Axe", "Pickaxe": "Pickaxe", "Hoe": "Hoe", "WateringCan": "WateringCan",
              "FishingRod": "FishingRod", "MilkPail": "MilkPail", "Shears": "Shears", "Pan": "Pan"}


def item_line(it, ids, kind=None):
    """One inventory/chest item -> 'O id count quality' / 'B id count' / 'T kind level' / 'W weaponId'."""
    t = xtype(it) or kind or ""
    if t in TOOL_TYPES:
        return ["T", TOOL_TYPES[t], num(it, "UpgradeLevel")]
    if t == "MeleeWeapon":
        wid = txt(it, "itemId") or txt(it, "InitialParentTileIndex") or txt(it, "ParentSheetIndex")
        return ["W", wid]
    is_big = truthy(it, "bigCraftable") or t in ("Chest", "Cask") and truthy(it, "bigCraftable")
    sid = txt(it, "itemId") or txt(it, "ParentSheetIndex")
    if is_big:
        b = ids.b(sid)
        return ["B", b, max(1, num(it, "Stack", 1))] if b is not None else None
    if t == "Boots":
        return ["T", "Boots_" + (sid or txt(it, "indexInTileSheet")).replace("(B)", ""), 0]
    if t == "Hat":
        return ["T", "Hat_" + (sid or txt(it, "which")).replace("(H)", ""), 0]
    if t == "Ring":
        o = ids.o(sid or txt(it, "indexInTileSheet"))
        return ["O", o, 1, 0] if o is not None else None
    if t == "CombinedRing":   # two rings forged together: keep the first
        inner = it.find("combinedRings")
        if inner is not None and len(inner):
            return item_line(inner[0], ids)
        return None
    if t not in ("", "Object", "ColoredObject"):
        return None   # clothes, furniture, trinkets: not in the 3DS game yet
    o = ids.o(sid)
    if o is None:
        return None
    return ["O", o, max(1, num(it, "Stack", 1)), num(it, "quality")]


def dict_items(e):
    """SerializableDictionary <item><key>..</key><value>..</value></item>."""
    if e is None:
        return
    for item in e.findall("item"):
        k = item.find("key")
        v = item.find("value")
        if k is not None and v is not None and len(k) and len(v):
            yield k[0], v[0]


def vec(k):
    return num(k, "X"), num(k, "Y")


def location_lines(loc, name, ids, out, extras):
    # furniture (the house): id and top-left tile
    fl = loc.find("furniture")
    if fl is not None:
        for fu in fl:
            fid = txt(fu, "itemId") or txt(fu, "parentSheetIndex") or txt(fu, "ParentSheetIndex")
            tl = fu.find("tileLocation")
            if tl is None:
                tl = fu.find("TileLocation")
            if fid and fid.lstrip("-").isdigit() and tl is not None:
                out.append(["furniture", name, int(fid), num(tl, "X"), num(tl, "Y")])
    # objects: debris, machines, chests, sprinklers
    for k, o in dict_items(loc.find("objects")):
        x, y = vec(k)
        oname = txt(o, "name") or txt(o, "Name")
        big = truthy(o, "bigCraftable")
        sid = txt(o, "itemId") or txt(o, "ParentSheetIndex")
        if not big and oname in ("Weeds", "Stone", "Twig") or (not big and oname.startswith("Weeds")):
            out.append(["debris", name, x, y, "W" if oname.startswith("Weeds") else "S" if oname == "Stone" else "T"])
            continue
        if big:
            b = ids.b(sid)
            if b is None:
                extras.append(["unknownbig", name, x, y, sid])
                continue
            out.append(["place", name, x, y, "B", b])
        else:
            oid = ids.o(sid)
            if oid is None:
                continue
            if ids.category.get(oid) == -8:   # sprinklers and other placeables
                out.append(["place", name, x, y, "O", oid])
            else:                              # an egg, forage... lying there to pick up
                out.append(["drop", name, x, y, oid])
                continue
        held = o.find("heldObject")
        if held is not None and held.get(XSI_NIL) != "true" and len(held):
            hl = item_line(held, ids)
            if hl and hl[0] == "O":
                out.append(["held", name, x, y, hl[1], hl[2], hl[3], num(o, "minutesUntilReady")])
        items = o.find("items")
        if items is not None:
            for it in items:
                if it.get(XSI_NIL) == "true":
                    continue
                il = item_line(it, ids)
                if il:
                    out.append(["chest", name, x, y] + il)
    # terrain: tilled soil and crops, trees, grass
    for k, tf in dict_items(loc.find("terrainFeatures")):
        x, y = vec(k)
        t = xtype(tf)
        if t == "HoeDirt":
            seed, phase, dayin, grown, dead = -1, 0, 0, 0, 0
            crop = tf.find("crop")
            if crop is not None and crop.get(XSI_NIL) != "true" and len(crop):
                seed = ids.o(txt(crop, "netSeedIndex") or txt(crop, "seedIndex"))
                if seed is None or seed < 0:
                    h = ids.o(txt(crop, "indexOfHarvest"))
                    seed = ids.crop_by_harvest.get(h, -1) if h is not None else -1
                phase = num(crop, "currentPhase")
                dayin = num(crop, "dayOfCurrentPhase")
                grown = 1 if truthy(crop, "fullyGrown") or truthy(crop, "fullGrown") else 0
                dead = 1 if truthy(crop, "dead") else 0
            out.append(["soil", name, x, y, 1 if num(tf, "state") == 1 else 0, seed, phase, dayin, grown, dead])
        elif t == "Tree":
            tt = txt(tf, "treeType", "1")
            out.append(["tree", name, x, y, tt if tt.isdigit() else "1", num(tf, "growthStage", 5), 1 if truthy(tf, "stump") else 0])
        elif t == "Grass":
            out.append(["grass", name, x, y, max(1, num(tf, "numberOfWeeds", 1)), 1 if num(tf, "grassType", 1) == 7 else 0])
        elif t == "FruitTree":
            sap = ids.o(txt(tf, "treeId") or txt(tf, "treeType"))
            fr = tf.find("fruit")
            nfruit = len([c for c in fr if c.get(XSI_NIL) != "true"]) if fr is not None else num(tf, "fruitsOnTree")
            if sap is not None:
                out.append(["fruittree", name, x, y, sap, num(tf, "daysUntilMature"), nfruit])


def import_save(save_path, root):
    if os.path.isdir(save_path):
        base = os.path.basename(os.path.normpath(save_path))
        save_path = os.path.join(save_path, base)
    if not os.path.isfile(save_path):
        raise SystemExit(f"No save file at {save_path} (pick the file named like its folder, not SaveGameInfo)")
    ids = Ids(root)
    if not ids.obj and not os.path.isfile(os.path.join(root, "data", "objects.dat")):
        raise SystemExit("Convert your game first: the converted data folder wasn't found.")
    tree = ET.parse(save_path)
    sg = tree.getroot()
    p = sg.find("player")
    if p is None:
        raise SystemExit("That doesn't look like a Stardew Valley save.")
    out, extras = [["version", 1]], []
    out.append(["name", clean(txt(p, "name", "Farmer"))])
    out.append(["farm", clean(txt(p, "farmName", "Stardew"))])
    out.append(["money", num(p, "money") or num(sg, "money")])
    out.append(["earned", num(p, "totalMoneyEarned")])
    out.append(["date", SEASONS.get(txt(sg, "currentSeason", "spring").lower(), 0), num(sg, "dayOfMonth", 1), num(sg, "year", 1)])
    out.append(["farmtype", num(sg, "whichFarm")])
    out.append(["gender", "female" if txt(p, "gender", "Male") == "Female" or not truthy(p, "isMale") and p.find("isMale") is not None else "male"])
    xp = p.find("experiencePoints")
    if xp is not None:
        for i, v in enumerate(xp.findall("int")[:5]):
            out.append(["xp", i, int(v.text or 0)])
    prof = p.find("professions")
    if prof is not None:
        for v in prof.findall("int"):
            out.append(["profession", int(v.text or 0)])
    out.append(["maxstamina", num(p, "maxStamina", 270)])
    out.append(["maxhealth", num(p, "maxHealth", 100)])
    out.append(["backpack", num(p, "maxItems", 12)])
    items = p.find("items")
    if items is not None:
        for slot, it in enumerate(items):
            if it.get(XSI_NIL) == "true":
                continue
            il = item_line(it, ids)
            if il:
                out.append(["inv", slot] + il)
    for e, tag in enumerate(("leftRing", "rightRing", "boots", "hat")):
        worn = p.find(tag)
        if worn is not None and len(worn):
            il = item_line(worn, ids, "Ring" if e < 2 else "Boots" if e == 2 else "Hat")
            if il:
                out.append(["equip", e] + il)
    for k, v in dict_items(p.find("friendshipData")):
        who = clean(k.text or "")
        out.append(["friend", who, num(v, "Points")])
        status = txt(v, "Status").lower()
        if status in ("dating", "engaged", "married"):
            out.append([status if status != "married" else "spouse", who])
    if p.find("friendshipData") is None:   # before 1.3: name -> [points, gifts this week, talked today, ...]
        for k, v in dict_items(p.find("friendships")):
            pts = v.find("int")
            out.append(["friend", clean(k.text or ""), int(pts.text or 0) if pts is not None else 0])
    for tag, key in (("mailReceived", "mail"), ("eventsSeen", "event")):
        e = p.find(tag)
        if e is not None:
            for s in e:
                if s.text:
                    out.append([key, clean(s.text)])
    out.append(["mine", num(p, "deepestMineLevel")])
    if txt(p, "spouse"):
        out.append(["spouse", clean(txt(p, "spouse"))])
    for tag in ("caveChoice",):
        if p.find(tag) is not None and txt(p, tag):
            extras.append([tag, clean(txt(p, tag))])
    for tag, key in (("craftingRecipes", "craftrecipe"), ("cookingRecipes", "cookrecipe")):
        for k, v in dict_items(p.find(tag)):
            out.append([key, clean(k.text or "")])
    out.append(["house", num(p, "houseUpgradeLevel")])
    # a pet anywhere in the save
    for el in sg.iter("NPC"):
        if xtype(el) in ("Pet", "Cat", "Dog"):
            kind = txt(el, "petType") or xtype(el)
            if kind not in ("Cat", "Dog"):
                kind = "Cat" if truthy(p, "catPerson") else "Dog" if kind == "Pet" and p.find("catPerson") is not None else "Cat"
            breed = txt(el, "whichBreed") or txt(p, "whichPetBreed") or "0"
            out.append(["pet", kind, int(breed) if breed.isdigit() else 0, clean(txt(el, "name", "")),
                        num(el, "friendshipTowardFarmer")])
            break

    # museum donations (ArchaeologyHouse museumPieces)
    for loc in (sg.find("locations") if sg.find("locations") is not None else []):
        if txt(loc, "name") == "ArchaeologyHouse":
            for k, v in dict_items(loc.find("museumPieces")):
                mid = ids.o(v.text if v.text else txt(v, "string"))
                if mid is not None:
                    out.append(["museum", mid])
    locs = sg.find("locations")
    bidx = 0
    if locs is not None:
        for loc in locs:
            name = txt(loc, "name")
            if name in ("Farm", "FarmHouse", "Greenhouse"):
                location_lines(loc, name, ids, out, extras)
            if name == "Farm":
                b_el = loc.find("buildings")
                for b in (b_el if b_el is not None else []):
                    btype = txt(b, "buildingType")
                    if not btype:
                        continue
                    days = num(b, "daysOfConstructionLeft")
                    up = num(b, "daysUntilUpgrade")
                    out.append(["building", clean(btype), num(b, "tileX"), num(b, "tileY"), max(days, up), bidx])
                    if btype == "Fish Pond":
                        fish = ids.o(txt(b, "fishType"))
                        if fish is not None and fish >= 0 and num(b, "currentOccupants") > 0:
                            out.append(["pond", bidx, fish, num(b, "currentOccupants")])
                    indoors = b.find("indoors")
                    if indoors is not None and len(indoors):
                        location_lines(indoors, f"@B{bidx}", ids, out, extras)
                        an = indoors.find("animals")
                        for _, a in dict_items(an if an is not None else indoors.find("Animals")):
                            out.append(["animal", clean(txt(a, "type")), clean(txt(a, "name", "Friend")), bidx,
                                        num(a, "age"), num(a, "friendshipTowardFarmer")])
                    bidx += 1
                # animals out on the farm when you saved
                for _, a in dict_items(loc.find("animals")):
                    home = txt(a, "homeInterior") or ""
                    out.append(["animalout", clean(txt(a, "type")), clean(txt(a, "name", "Friend")),
                                num(a, "age"), num(a, "friendshipTowardFarmer"), clean(home)])
    # community center progress, kept for when bundles arrive
    # the Community Center: this save's own bundles (remixed or not), and what's been given
    for k, v in dict_items(sg.find("bundleData")):
        if k.text and v.text:
            out.append(["bundledef", clean(k.text), clean(v.text)])
    nws = sg.find("netWorldState")
    bundles_el = nws.find("Bundles") if nws is not None else None
    if bundles_el is None and locs is not None:   # before 1.5 the Community Center itself kept them
        for loc in locs:
            if txt(loc, "name") == "CommunityCenter":
                bundles_el = loc.find("bundles")
    for k, v in dict_items(bundles_el):
        bits = "".join("1" if (b.text or "").lower() == "true" else "0" for b in v.findall("boolean"))
        out.append(["bundle", k.text or "0", bits])

    dest = os.path.join(root, "import.txt")
    with open(dest, "w", encoding="utf-8") as f:
        for line in out:
            f.write("\t".join(str(x) for x in line) + "\n")
        for line in extras:
            f.write("\t".join(["extra"] + [str(x) for x in line]) + "\n")
    kinds = {}
    for line in out:
        kinds[line[0]] = kinds.get(line[0], 0) + 1
    print(f"Imported {txt(p, 'name')}'s farm ({txt(sg, 'currentSeason')} {num(sg, 'dayOfMonth')}, year {num(sg, 'year')}):")
    print("  " + ", ".join(f"{v} {k}" for k, v in sorted(kinds.items())))
    print(f"Wrote {dest}. Start stardew3ds to load it (a farm already there is backed up first).")
    return dest


if __name__ == "__main__":
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)
    import_save(sys.argv[1], sys.argv[2])
