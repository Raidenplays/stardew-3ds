"""Reads the data layout (classes, their serialized fields and properties, and
their types) out of a .NET assembly's metadata -- StardewValley.GameData.dll
from the player's own game folder. Only type metadata is read; no code.

The packed Data/*.xnb files are written by XNA/MonoGame's ReflectiveWriter,
which stores each object's members in a fixed order with no names, so this
layout is what lets xnbdata.py turn them back into JSON.
"""
# ELEMENT_TYPE codes from ECMA-335 II.23.1.16
_PRIM = {0x02: "bool", 0x03: "char", 0x04: "sbyte", 0x05: "byte", 0x06: "short",
         0x07: "ushort", 0x08: "int", 0x09: "uint", 0x0a: "long", 0x0b: "ulong",
         0x0c: "float", 0x0d: "double", 0x0e: "string", 0x1c: "object"}


class T:
    """A type as it appears in a signature."""
    __slots__ = ("kind", "name", "args", "value")

    def __init__(self, kind, name=None, args=(), value=False):
        self.kind = kind        # prim | named | generic | array | var
        self.name = name        # prim name, or full type name
        self.args = list(args)  # generic args / array element
        self.value = value      # value type?

    def __repr__(self):
        if self.kind == "prim":
            return self.name
        if self.kind == "array":
            return f"{self.args[0]!r}[]"
        if self.kind == "generic":
            return f"{self.name}<{', '.join(map(repr, self.args))}>"
        return self.name


class Member:
    __slots__ = ("name", "type")

    def __init__(self, name, type_):
        self.name, self.type = name, type_


class Klass:
    def __init__(self, name):
        self.name = name
        self.base = None          # full name of the base class
        self.kind = "class"       # class | struct | enum
        self.members = []         # serialized members in ReflectiveReader order
        self.enum_values = {}     # enum: value -> name
        self.generic_params = []


def _full(ns, name):
    return f"{ns}.{name}" if ns else name


class Schema:
    def __init__(self, dll_path):
        import dnfile   # only needed for data models, not plain string tables
        self.pe = dnfile.dnPE(dll_path)
        self.md = self.pe.net.mdtables
        self.classes = {}
        self._load()

    # --- metadata helpers ------------------------------------------------
    def _blob(self, b):
        return bytes(b.value if hasattr(b, "value") else b)

    @staticmethod
    def _cu(d, i):   # compressed unsigned int
        b = d[i]
        if b & 0x80 == 0:
            return b, i + 1
        if b & 0xC0 == 0x80:
            return ((b & 0x3F) << 8) | d[i + 1], i + 2
        return ((b & 0x1F) << 24) | (d[i + 1] << 16) | (d[i + 2] << 8) | d[i + 3], i + 4

    def _tdor_name(self, table, idx):
        if table == 0:
            r = self.md.TypeDef.rows[idx - 1]
            return self._typedef_name(idx)
        if table == 1:
            r = self.md.TypeRef.rows[idx - 1]
            return self._typeref_name(r)
        r = self.md.TypeSpec.rows[idx - 1]
        t, _ = self._type(self._blob(r.Signature), 0)
        return t

    def _typeref_name(self, r):
        scope = r.ResolutionScope
        if scope is not None and getattr(scope, "table", None) is not None and scope.table.name == "TypeRef":
            return self._typeref_name(scope.row) + "+" + str(r.TypeName)
        return _full(str(r.TypeNamespace), str(r.TypeName))

    def _typedef_name(self, idx):
        r = self.md.TypeDef.rows[idx - 1]
        name = _full(str(r.TypeNamespace), str(r.TypeName))
        outer = self._nested.get(idx)
        if outer:
            name = self._typedef_name(outer) + "+" + str(r.TypeName)
        return name

    def _type(self, d, i):
        e = d[i]
        i += 1
        while e in (0x1f, 0x20):          # custom modifiers
            _, i = self._cu(d, i)
            e = d[i]
            i += 1
        if e in _PRIM:
            return T("prim", _PRIM[e], value=e not in (0x0e, 0x1c)), i
        if e in (0x11, 0x12):             # valuetype / class
            v, i = self._cu(d, i)
            n = self._tdor_name(v & 3, v >> 2)
            if isinstance(n, T):
                return n, i
            return T("named", n, value=e == 0x11), i
        if e == 0x15:                     # generic instance
            kind = d[i]
            i += 1
            v, i = self._cu(d, i)
            n = self._tdor_name(v & 3, v >> 2)
            argc, i = self._cu(d, i)
            args = []
            for _ in range(argc):
                a, i = self._type(d, i)
                args.append(a)
            return T("generic", n, args, value=kind == 0x11), i
        if e == 0x1d:                     # single-dim array
            a, i = self._type(d, i)
            return T("array", None, [a]), i
        if e in (0x13, 0x1e):             # generic param of type / method
            n, i = self._cu(d, i)
            return T("var", n), i
        raise ValueError(f"unsupported signature element 0x{e:02x}")

    # --- loading ------------------------------------------------------------
    def _load(self):
        md = self.md
        self._nested = {}
        if md.NestedClass:
            for r in md.NestedClass.rows:
                self._nested[r.NestedClass.row_index] = r.EnclosingClass.row_index

        # attributes on fields / properties: ContentSerializerIgnore / ContentSerializer
        attrs = {}
        for r in md.CustomAttribute.rows:
            p = r.Parent
            if p.table is None or p.table.name not in ("Field", "Property"):
                continue
            ty = r.Type
            name = None
            if ty.table is not None and ty.table.name == "MemberRef":
                cls = ty.row.Class
                if cls.table is not None and cls.table.name == "TypeRef":
                    name = str(cls.row.TypeName)
            if name:
                attrs.setdefault((p.table.name, p.row_index), set()).add(name)

        # property accessors (getter/setter and whether they're public)
        acc = {}
        for r in md.MethodSemantics.rows:
            a = r.Association
            if a.table is None or a.table.name != "Property":
                continue
            m = r.Method.row
            role = "get" if r.Semantics.msGetter else "set" if r.Semantics.msSetter else None
            if role:
                acc.setdefault(a.row_index, {})[role] = bool(m.Flags.mdPublic)
                if m.Flags.mdStatic:
                    acc[a.row_index]["static"] = True

        props_of = {}
        if md.PropertyMap:
            for r in md.PropertyMap.rows:
                props_of[r.Parent.row_index] = [x.row_index for x in r.PropertyList]

        gparams = {}
        if md.GenericParam:
            for r in md.GenericParam.rows:
                o = r.Owner
                if o.table is not None and o.table.name == "TypeDef":
                    gparams.setdefault(o.row_index, []).append((r.Number, str(r.Name)))

        for ti, t in enumerate(md.TypeDef.rows, start=1):
            name = self._typedef_name(ti)
            k = Klass(name)
            k.generic_params = [n for _, n in sorted(gparams.get(ti, []))]
            base = None
            if t.Extends is not None and t.Extends.table is not None and t.Extends.row_index:
                tn = t.Extends.table.name
                base = self._tdor_name({"TypeDef": 0, "TypeRef": 1, "TypeSpec": 2}[tn], t.Extends.row_index)
                if isinstance(base, T):
                    base = base.name
            if base == "System.Enum":
                k.kind = "enum"
                for f in t.FieldList:
                    fr = f.row
                    if fr.Flags.fdStatic and fr.Flags.fdLiteral:
                        k.enum_values[str(fr.Name)] = None
                self.classes[name] = k
                continue
            if base == "System.ValueType":
                k.kind = "struct"
            elif base and base != "System.Object":
                k.base = base
            # MonoGame ReflectiveReader: properties first, then fields
            for pi in props_of.get(ti, []):
                pr = md.Property.rows[pi - 1]
                a = acc.get(pi, {})
                if a.get("static") or "get" not in a or "set" not in a:
                    continue
                at = attrs.get(("Property", pi), set())
                if "ContentSerializerIgnoreAttribute" in at:
                    continue
                if "ContentSerializerAttribute" not in at and not (a["get"] and a["set"]):
                    continue
                d = self._blob(pr.Type)
                i = 1
                pc, i = self._cu(d, i)
                if pc:            # indexer
                    continue
                ty, _ = self._type(d, i)
                k.members.append(Member(str(pr.Name), ty))
            for f in t.FieldList:
                fr = f.row
                fi = f.row_index
                fl = fr.Flags
                if fl.fdStatic or fl.fdLiteral:
                    continue
                at = attrs.get(("Field", fi), set())
                if "ContentSerializerIgnoreAttribute" in at:
                    continue
                if "ContentSerializerAttribute" not in at:
                    if not fl.fdPublic or fl.fdInitOnly:
                        continue
                ty, _ = self._type(self._blob(fr.Signature), 1)
                k.members.append(Member(str(fr.Name), ty))
            self.classes[name] = k
        # enum literal values (Constant table)
        if md.Constant:
            for r in md.Constant.rows:
                p = r.Parent
                if p.table is None or p.table.name != "Field":
                    continue
                fr = p.row
                v = self._blob(r.Value)
                val = int.from_bytes(v, "little", signed=True)
                for k in self.classes.values():
                    pass
                self._const_by_field = getattr(self, "_const_by_field", {})
                self._const_by_field[p.row_index] = val
        for ti, t in enumerate(md.TypeDef.rows, start=1):
            k = self.classes.get(self._typedef_name(ti))
            if k and k.kind == "enum":
                k.enum_values = {}
                for f in t.FieldList:
                    if f.row.Flags.fdStatic and f.row.Flags.fdLiteral:
                        v = getattr(self, "_const_by_field", {}).get(f.row_index)
                        if v is not None and v not in k.enum_values:
                            k.enum_values[v] = str(f.row.Name)

    def get(self, name):
        return self.classes.get(name)

    def all_members(self, name):
        """Members in read order: base class members first."""
        k = self.classes.get(name)
        if not k:
            return None
        out = []
        if k.base:
            b = self.all_members(k.base)
            if b is None:
                raise KeyError(f"base class {k.base} of {name} not in schema")
            out += b
        return out + k.members
