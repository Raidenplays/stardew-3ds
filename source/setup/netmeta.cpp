#include "setup/netmeta.h"

#include <cstdio>
#include <cstring>
#include <set>

namespace netmeta {
namespace {

// ---- ECMA-335 metadata tables -------------------------------------------------
enum Tbl {
    T_Module = 0x00, T_TypeRef = 0x01, T_TypeDef = 0x02, T_FieldPtr = 0x03, T_Field = 0x04,
    T_MethodPtr = 0x05, T_MethodDef = 0x06, T_ParamPtr = 0x07, T_Param = 0x08, T_InterfaceImpl = 0x09,
    T_MemberRef = 0x0A, T_Constant = 0x0B, T_CustomAttribute = 0x0C, T_FieldMarshal = 0x0D,
    T_DeclSecurity = 0x0E, T_ClassLayout = 0x0F, T_FieldLayout = 0x10, T_StandAloneSig = 0x11,
    T_EventMap = 0x12, T_EventPtr = 0x13, T_Event = 0x14, T_PropertyMap = 0x15, T_PropertyPtr = 0x16,
    T_Property = 0x17, T_MethodSemantics = 0x18, T_MethodImpl = 0x19, T_ModuleRef = 0x1A,
    T_TypeSpec = 0x1B, T_ImplMap = 0x1C, T_FieldRVA = 0x1D, T_ENCLog = 0x1E, T_ENCMap = 0x1F,
    T_Assembly = 0x20, T_AssemblyProcessor = 0x21, T_AssemblyOS = 0x22, T_AssemblyRef = 0x23,
    T_AssemblyRefProcessor = 0x24, T_AssemblyRefOS = 0x25, T_File = 0x26, T_ExportedType = 0x27,
    T_ManifestResource = 0x28, T_NestedClass = 0x29, T_GenericParam = 0x2A, T_MethodSpec = 0x2B,
    T_GenericParamConstraint = 0x2C, T_COUNT = 0x2D
};

// column kinds: fixed sizes, heap indexes, simple table indexes, coded indexes
enum Col : int {
    C_U8 = -1, C_U16 = -2, C_U32 = -3, C_STR = -4, C_GUID = -5, C_BLOB = -6,
    // >= 0x100: simple index into table (v - 0x100); >= 0x200: coded index kind (v - 0x200)
};
constexpr int IDX(int t) { return 0x100 + t; }
enum Coded {
    CI_TypeDefOrRef, CI_HasConstant, CI_HasCustomAttribute, CI_HasFieldMarshal, CI_HasDeclSecurity,
    CI_MemberRefParent, CI_HasSemantics, CI_MethodDefOrRef, CI_MemberForwarded, CI_Implementation,
    CI_CustomAttributeType, CI_ResolutionScope, CI_TypeOrMethodDef, CI_COUNT
};
constexpr int CI(int c) { return 0x200 + c; }

struct CodedDef { int bits; std::vector<int> tables; };   // -1 = unused slot
const CodedDef CODED[CI_COUNT] = {
    {2, {T_TypeDef, T_TypeRef, T_TypeSpec}},
    {2, {T_Field, T_Param, T_Property}},
    {5, {T_MethodDef, T_Field, T_TypeRef, T_TypeDef, T_Param, T_InterfaceImpl, T_MemberRef, T_Module,
         T_DeclSecurity, T_Property, T_Event, T_StandAloneSig, T_ModuleRef, T_TypeSpec, T_Assembly,
         T_AssemblyRef, T_File, T_ExportedType, T_ManifestResource, T_GenericParam,
         T_GenericParamConstraint, T_MethodSpec}},
    {1, {T_Field, T_Param}},
    {2, {T_TypeDef, T_MethodDef, T_Assembly}},
    {3, {T_TypeDef, T_TypeRef, T_ModuleRef, T_MethodDef, T_TypeSpec}},
    {1, {T_Event, T_Property}},
    {1, {T_MethodDef, T_MemberRef}},
    {1, {T_Field, T_MethodDef}},
    {2, {T_File, T_AssemblyRef, T_ExportedType}},
    {3, {-1, -1, T_MethodDef, T_MemberRef, -1}},
    {2, {T_Module, T_ModuleRef, T_AssemblyRef, T_TypeRef}},
    {1, {T_TypeDef, T_MethodDef}},
};

const std::vector<int> COLS[T_COUNT] = {
    /*Module*/ {C_U16, C_STR, C_GUID, C_GUID, C_GUID},
    /*TypeRef*/ {CI(CI_ResolutionScope), C_STR, C_STR},
    /*TypeDef*/ {C_U32, C_STR, C_STR, CI(CI_TypeDefOrRef), IDX(T_Field), IDX(T_MethodDef)},
    /*FieldPtr*/ {IDX(T_Field)},
    /*Field*/ {C_U16, C_STR, C_BLOB},
    /*MethodPtr*/ {IDX(T_MethodDef)},
    /*MethodDef*/ {C_U32, C_U16, C_U16, C_STR, C_BLOB, IDX(T_Param)},
    /*ParamPtr*/ {IDX(T_Param)},
    /*Param*/ {C_U16, C_U16, C_STR},
    /*InterfaceImpl*/ {IDX(T_TypeDef), CI(CI_TypeDefOrRef)},
    /*MemberRef*/ {CI(CI_MemberRefParent), C_STR, C_BLOB},
    /*Constant*/ {C_U8, C_U8, CI(CI_HasConstant), C_BLOB},
    /*CustomAttribute*/ {CI(CI_HasCustomAttribute), CI(CI_CustomAttributeType), C_BLOB},
    /*FieldMarshal*/ {CI(CI_HasFieldMarshal), C_BLOB},
    /*DeclSecurity*/ {C_U16, CI(CI_HasDeclSecurity), C_BLOB},
    /*ClassLayout*/ {C_U16, C_U32, IDX(T_TypeDef)},
    /*FieldLayout*/ {C_U32, IDX(T_Field)},
    /*StandAloneSig*/ {C_BLOB},
    /*EventMap*/ {IDX(T_TypeDef), IDX(T_Event)},
    /*EventPtr*/ {IDX(T_Event)},
    /*Event*/ {C_U16, C_STR, CI(CI_TypeDefOrRef)},
    /*PropertyMap*/ {IDX(T_TypeDef), IDX(T_Property)},
    /*PropertyPtr*/ {IDX(T_Property)},
    /*Property*/ {C_U16, C_STR, C_BLOB},
    /*MethodSemantics*/ {C_U16, IDX(T_MethodDef), CI(CI_HasSemantics)},
    /*MethodImpl*/ {IDX(T_TypeDef), CI(CI_MethodDefOrRef), CI(CI_MethodDefOrRef)},
    /*ModuleRef*/ {C_STR},
    /*TypeSpec*/ {C_BLOB},
    /*ImplMap*/ {C_U16, CI(CI_MemberForwarded), C_STR, IDX(T_ModuleRef)},
    /*FieldRVA*/ {C_U32, IDX(T_Field)},
    /*ENCLog*/ {C_U32, C_U32},
    /*ENCMap*/ {C_U32},
    /*Assembly*/ {C_U32, C_U16, C_U16, C_U16, C_U16, C_U32, C_BLOB, C_STR, C_STR},
    /*AssemblyProcessor*/ {C_U32},
    /*AssemblyOS*/ {C_U32, C_U32, C_U32},
    /*AssemblyRef*/ {C_U16, C_U16, C_U16, C_U16, C_U32, C_BLOB, C_STR, C_STR, C_BLOB},
    /*AssemblyRefProcessor*/ {C_U32, IDX(T_AssemblyRef)},
    /*AssemblyRefOS*/ {C_U32, C_U32, C_U32, IDX(T_AssemblyRef)},
    /*File*/ {C_U32, C_STR, C_BLOB},
    /*ExportedType*/ {C_U32, C_U32, C_STR, C_STR, CI(CI_Implementation)},
    /*ManifestResource*/ {C_U32, C_U32, C_STR, CI(CI_Implementation)},
    /*NestedClass*/ {IDX(T_TypeDef), IDX(T_TypeDef)},
    /*GenericParam*/ {C_U16, C_U16, CI(CI_TypeOrMethodDef), C_STR},
    /*MethodSpec*/ {CI(CI_MethodDefOrRef), C_BLOB},
    /*GenericParamConstraint*/ {IDX(T_GenericParam), CI(CI_TypeDefOrRef)},
};

struct Meta {
    const uint8_t* d = nullptr;
    size_t size = 0;
    size_t strings = 0, stringsSize = 0, blob = 0, blobSize = 0;
    int strIdx = 2, guidIdx = 2, blobIdx = 2;
    uint32_t rows[T_COUNT] = {};
    size_t tableOff[T_COUNT] = {};
    int rowSize[T_COUNT] = {};
    std::vector<int> colOff[T_COUNT], colSize[T_COUNT];

    uint32_t rd(size_t off, int n) const {
        if (off + n > size) return 0;
        uint32_t v = 0;
        for (int i = 0; i < n; i++) v |= (uint32_t)d[off + i] << (8 * i);
        return v;
    }
    int colWidth(int c) const {
        switch (c) {
            case C_U8: return 1;
            case C_U16: return 2;
            case C_U32: return 4;
            case C_STR: return strIdx;
            case C_GUID: return guidIdx;
            case C_BLOB: return blobIdx;
        }
        if (c >= 0x200) {
            const CodedDef& cd = CODED[c - 0x200];
            uint32_t mx = 0;
            for (int t : cd.tables) if (t >= 0 && rows[t] > mx) mx = rows[t];
            return mx < (1u << (16 - cd.bits)) ? 2 : 4;
        }
        return rows[c - 0x100] < 0x10000 ? 2 : 4;
    }
    uint32_t get(int t, uint32_t row, int col) const {   // row is 1-based
        if (row == 0 || row > rows[t]) return 0;
        return rd(tableOff[t] + (size_t)(row - 1) * rowSize[t] + colOff[t][col], colSize[t][col]);
    }
    // coded index -> (table, row)
    void coded(int ci, uint32_t v, int& table, uint32_t& row) const {
        const CodedDef& cd = CODED[ci];
        uint32_t tag = v & ((1u << cd.bits) - 1);
        table = tag < cd.tables.size() ? cd.tables[tag] : -1;
        row = v >> cd.bits;
    }
    std::string str(uint32_t idx) const {
        if (idx >= stringsSize) return "";
        const char* s = (const char*)d + strings + idx;
        size_t n = strnlen(s, stringsSize - idx);
        return std::string(s, n);
    }
    std::vector<uint8_t> blobAt(uint32_t idx) const {
        if (idx >= blobSize) return {};
        size_t p = blob + idx;
        uint32_t n;
        uint8_t b = d[p];
        if ((b & 0x80) == 0) { n = b; p += 1; }
        else if ((b & 0xC0) == 0x80) { n = ((b & 0x3F) << 8) | d[p + 1]; p += 2; }
        else { n = ((b & 0x1F) << 24) | (d[p + 1] << 16) | (d[p + 2] << 8) | d[p + 3]; p += 4; }
        if (p + n > size) return {};
        return std::vector<uint8_t>(d + p, d + p + n);
    }
};

uint32_t cu(const std::vector<uint8_t>& d, size_t& i) {   // compressed unsigned
    if (i >= d.size()) return 0;
    uint8_t b = d[i];
    if ((b & 0x80) == 0) { i += 1; return b; }
    if ((b & 0xC0) == 0x80) { uint32_t v = ((b & 0x3F) << 8) | d[i + 1]; i += 2; return v; }
    uint32_t v = ((b & 0x1F) << 24) | (d[i + 1] << 16) | (d[i + 2] << 8) | d[i + 3];
    i += 4;
    return v;
}

const char* primName(uint8_t e) {
    switch (e) {
        case 0x02: return "bool"; case 0x03: return "char"; case 0x04: return "sbyte"; case 0x05: return "byte";
        case 0x06: return "short"; case 0x07: return "ushort"; case 0x08: return "int"; case 0x09: return "uint";
        case 0x0a: return "long"; case 0x0b: return "ulong"; case 0x0c: return "float"; case 0x0d: return "double";
        case 0x0e: return "string"; case 0x1c: return "object";
    }
    return nullptr;
}

struct Loader {
    const Meta& m;
    std::map<uint32_t, uint32_t> nested;   // typedef row -> enclosing typedef row
    explicit Loader(const Meta& m) : m(m) {}

    std::string typeDefName(uint32_t row) const {
        std::string ns = m.str(m.get(T_TypeDef, row, 2)), n = m.str(m.get(T_TypeDef, row, 1));
        auto it = nested.find(row);
        if (it != nested.end()) return typeDefName(it->second) + "+" + n;
        return ns.empty() ? n : ns + "." + n;
    }
    std::string typeRefName(uint32_t row) const {
        int t; uint32_t r;
        m.coded(CI_ResolutionScope, m.get(T_TypeRef, row, 0), t, r);
        std::string ns = m.str(m.get(T_TypeRef, row, 2)), n = m.str(m.get(T_TypeRef, row, 1));
        if (t == T_TypeRef && r) return typeRefName(r) + "+" + n;
        return ns.empty() ? n : ns + "." + n;
    }
    // TypeDefOrRef (tag 0/1/2) -> sig
    bool tdor(int tag, uint32_t row, TypeSig& out, bool value) const {
        if (tag == 0) { out.kind = TypeSig::NAMED; out.name = typeDefName(row); out.value = value; return true; }
        if (tag == 1) { out.kind = TypeSig::NAMED; out.name = typeRefName(row); out.value = value; return true; }
        auto b = m.blobAt(m.get(T_TypeSpec, row, 0));
        size_t i = 0;
        return type(b, i, out);
    }
    bool type(const std::vector<uint8_t>& d, size_t& i, TypeSig& out) const {
        if (i >= d.size()) return false;
        uint8_t e = d[i++];
        while (e == 0x1f || e == 0x20) { cu(d, i); if (i >= d.size()) return false; e = d[i++]; }
        if (const char* p = primName(e)) {
            out.kind = TypeSig::PRIM; out.name = p; out.value = e != 0x0e && e != 0x1c;
            return true;
        }
        if (e == 0x11 || e == 0x12) {
            uint32_t v = cu(d, i);
            return tdor(v & 3, v >> 2, out, e == 0x11);
        }
        if (e == 0x15) {
            uint8_t k = d[i++];
            uint32_t v = cu(d, i);
            TypeSig base;
            if (!tdor(v & 3, v >> 2, base, k == 0x11)) return false;
            out.kind = TypeSig::GENERIC; out.name = base.name; out.value = k == 0x11;
            uint32_t argc = cu(d, i);
            for (uint32_t a = 0; a < argc; a++) {
                TypeSig t;
                if (!type(d, i, t)) return false;
                out.args.push_back(t);
            }
            return true;
        }
        if (e == 0x1d) {
            TypeSig t;
            if (!type(d, i, t)) return false;
            out.kind = TypeSig::ARRAY; out.args.push_back(t); out.value = false;
            return true;
        }
        if (e == 0x13 || e == 0x1e) { out.kind = TypeSig::VAR; out.name = std::to_string(cu(d, i)); return true; }
        return false;
    }
};

}  // namespace

bool Schema::load(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) { error = "can't open " + path; return false; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> buf(n > 0 ? (size_t)n : 0);
    size_t got = n > 0 ? fread(buf.data(), 1, buf.size(), f) : 0;
    fclose(f);
    if (got != buf.size()) { error = "read error"; return false; }
    return loadMemory(buf);
}

bool Schema::loadMemory(const std::vector<uint8_t>& file) {
    classes.clear();
    const uint8_t* d = file.data();
    size_t size = file.size();
    auto u16 = [&](size_t o) -> uint32_t { return o + 2 <= size ? d[o] | (d[o + 1] << 8) : 0; };
    auto u32 = [&](size_t o) -> uint32_t {
        return o + 4 <= size ? d[o] | (d[o + 1] << 8) | (d[o + 2] << 16) | ((uint32_t)d[o + 3] << 24) : 0;
    };
    // PE headers -> CLI header -> metadata root
    if (size < 0x40 || d[0] != 'M' || d[1] != 'Z') { error = "not a .dll"; return false; }
    size_t pe = u32(0x3C);
    if (pe + 24 > size || memcmp(d + pe, "PE\0\0", 4) != 0) { error = "not a PE file"; return false; }
    size_t coff = pe + 4;
    uint32_t nsec = u16(coff + 2), optSize = u16(coff + 16);
    size_t opt = coff + 20;
    bool pe32plus = u16(opt) == 0x20B;
    size_t dirs = opt + (pe32plus ? 112 : 96);
    uint32_t cliRva = u32(dirs + 14 * 8);
    size_t secs = opt + optSize;
    auto rva2off = [&](uint32_t rva) -> size_t {
        for (uint32_t s = 0; s < nsec; s++) {
            size_t sh = secs + s * 40;
            uint32_t va = u32(sh + 12), vs = u32(sh + 8), raw = u32(sh + 20), rs = u32(sh + 16);
            uint32_t span = vs > rs ? vs : rs;
            if (rva >= va && rva < va + span) return raw + (rva - va);
        }
        return (size_t)-1;
    };
    size_t cli = rva2off(cliRva);
    if (!cliRva || cli == (size_t)-1) { error = "not a .NET assembly"; return false; }
    size_t root = rva2off(u32(cli + 8));
    if (root == (size_t)-1 || u32(root) != 0x424A5342) { error = "bad metadata"; return false; }
    uint32_t verLen = u32(root + 12);
    size_t p = root + 16 + verLen;
    uint32_t nstreams = u16(p + 2);
    p += 4;
    Meta m;
    m.d = d;
    m.size = size;
    size_t tables = 0;
    for (uint32_t s = 0; s < nstreams; s++) {
        uint32_t off = u32(p), sz = u32(p + 4);
        const char* name = (const char*)d + p + 8;
        size_t nl = strnlen(name, 32);
        if (!strcmp(name, "#~") || !strcmp(name, "#-")) tables = root + off;
        else if (!strcmp(name, "#Strings")) { m.strings = root + off; m.stringsSize = sz; }
        else if (!strcmp(name, "#Blob")) { m.blob = root + off; m.blobSize = sz; }
        p += 8 + ((nl + 4) & ~3u);
    }
    if (!tables || !m.strings || !m.blob) { error = "metadata streams missing"; return false; }
    uint8_t heaps = d[tables + 6];
    m.strIdx = heaps & 1 ? 4 : 2;
    m.guidIdx = heaps & 2 ? 4 : 2;
    m.blobIdx = heaps & 4 ? 4 : 2;
    uint64_t valid = (uint64_t)u32(tables + 8) | ((uint64_t)u32(tables + 12) << 32);
    p = tables + 24;
    for (int t = 0; t < 64; t++) {
        if (!(valid >> t & 1)) continue;
        if (t >= T_COUNT) { error = "unknown metadata table"; return false; }
        m.rows[t] = u32(p);
        p += 4;
    }
    if (heaps & 0x40) p += 4;   // extra data
    for (int t = 0; t < T_COUNT; t++) {
        int off = 0;
        for (int c : COLS[t]) {
            int w = m.colWidth(c);
            m.colOff[t].push_back(off);
            m.colSize[t].push_back(w);
            off += w;
        }
        m.rowSize[t] = off;
        m.tableOff[t] = p;
        p += (size_t)off * m.rows[t];
    }
    if (p > size) { error = "metadata truncated"; return false; }

    Loader L(m);
    for (uint32_t r = 1; r <= m.rows[T_NestedClass]; r++) L.nested[m.get(T_NestedClass, r, 0)] = m.get(T_NestedClass, r, 1);

    // attributes on fields/properties (by attribute class name)
    std::map<std::pair<int, uint32_t>, std::set<std::string>> attrs;
    for (uint32_t r = 1; r <= m.rows[T_CustomAttribute]; r++) {
        int pt; uint32_t pr;
        m.coded(CI_HasCustomAttribute, m.get(T_CustomAttribute, r, 0), pt, pr);
        if (pt != T_Field && pt != T_Property) continue;
        int tt; uint32_t tr;
        m.coded(CI_CustomAttributeType, m.get(T_CustomAttribute, r, 1), tt, tr);
        if (tt != T_MemberRef) continue;
        int ct; uint32_t crow;
        m.coded(CI_MemberRefParent, m.get(T_MemberRef, tr, 0), ct, crow);
        if (ct != T_TypeRef) continue;
        attrs[{pt, pr}].insert(m.str(m.get(T_TypeRef, crow, 1)));
    }
    // property accessors: getter/setter public?
    struct Acc { bool get = false, set = false, getPub = false, setPub = false, isStatic = false; };
    std::map<uint32_t, Acc> acc;
    for (uint32_t r = 1; r <= m.rows[T_MethodSemantics]; r++) {
        int at; uint32_t ar;
        m.coded(CI_HasSemantics, m.get(T_MethodSemantics, r, 2), at, ar);
        if (at != T_Property) continue;
        uint32_t sem = m.get(T_MethodSemantics, r, 0), meth = m.get(T_MethodSemantics, r, 1);
        uint32_t flags = m.get(T_MethodDef, meth, 2);
        bool pub = (flags & 7) == 6, st = flags & 0x10;
        Acc& a = acc[ar];
        if (sem & 2) { a.get = true; a.getPub = pub; }       // getter
        if (sem & 1) { a.set = true; a.setPub = pub; }       // setter
        if (st) a.isStatic = true;
    }
    std::map<uint32_t, std::pair<uint32_t, uint32_t>> propsOf;   // typedef -> [first, last) property rows
    for (uint32_t r = 1; r <= m.rows[T_PropertyMap]; r++) {
        uint32_t first = m.get(T_PropertyMap, r, 1);
        uint32_t last = r < m.rows[T_PropertyMap] ? m.get(T_PropertyMap, r + 1, 1) : m.rows[T_Property] + 1;
        propsOf[m.get(T_PropertyMap, r, 0)] = {first, last};
    }
    std::map<uint32_t, int64_t> constOf;   // field row -> literal value
    for (uint32_t r = 1; r <= m.rows[T_Constant]; r++) {
        int pt; uint32_t pr;
        m.coded(CI_HasConstant, m.get(T_Constant, r, 2), pt, pr);
        if (pt != T_Field) continue;
        auto v = m.blobAt(m.get(T_Constant, r, 3));
        int64_t x = 0;
        for (size_t k = 0; k < v.size() && k < 8; k++) x |= (int64_t)v[k] << (8 * k);
        if (!v.empty() && v.size() < 8 && (v.back() & 0x80)) x -= (int64_t)1 << (8 * v.size());   // sign-extend
        constOf[pr] = x;
    }

    for (uint32_t t = 1; t <= m.rows[T_TypeDef]; t++) {
        Klass k;
        k.name = L.typeDefName(t);
        int et; uint32_t er;
        m.coded(CI_TypeDefOrRef, m.get(T_TypeDef, t, 3), et, er);
        std::string base;
        if (er) {
            TypeSig bs;
            if (L.tdor(et == T_TypeDef ? 0 : et == T_TypeRef ? 1 : 2, er, bs, false)) base = bs.name;
        }
        uint32_t f0 = m.get(T_TypeDef, t, 4);
        uint32_t f1 = t < m.rows[T_TypeDef] ? m.get(T_TypeDef, t + 1, 4) : m.rows[T_Field] + 1;
        if (base == "System.Enum") {
            k.kind = Klass::ENUM;
            for (uint32_t f = f0; f < f1; f++) {
                uint32_t fl = m.get(T_Field, f, 0);
                if ((fl & 0x10) && (fl & 0x40)) {   // static literal
                    auto c = constOf.find(f);
                    if (c != constOf.end() && !k.enumValues.count(c->second)) k.enumValues[c->second] = m.str(m.get(T_Field, f, 1));
                }
            }
            classes[k.name] = k;
            continue;
        }
        if (base == "System.ValueType") k.kind = Klass::STRUCT;
        else if (!base.empty() && base != "System.Object") k.base = base;
        // MonoGame ReflectiveReader: properties first, then fields
        auto pit = propsOf.find(t);
        if (pit != propsOf.end()) {
            for (uint32_t pr = pit->second.first; pr < pit->second.second; pr++) {
                const Acc& a = acc[pr];
                if (a.isStatic || !a.get || !a.set) continue;
                auto& at = attrs[{T_Property, pr}];
                if (at.count("ContentSerializerIgnoreAttribute")) continue;
                if (!at.count("ContentSerializerAttribute") && !(a.getPub && a.setPub)) continue;
                auto sig = m.blobAt(m.get(T_Property, pr, 2));
                size_t i = 1;
                if (cu(sig, i)) continue;   // indexer
                Member mem;
                mem.name = m.str(m.get(T_Property, pr, 1));
                if (!L.type(sig, i, mem.type)) { error = "bad signature on " + k.name + "." + mem.name; return false; }
                k.members.push_back(mem);
            }
        }
        for (uint32_t f = f0; f < f1; f++) {
            uint32_t fl = m.get(T_Field, f, 0);
            if ((fl & 0x10) || (fl & 0x40)) continue;   // static / literal
            auto& at = attrs[{T_Field, f}];
            if (at.count("ContentSerializerIgnoreAttribute")) continue;
            if (!at.count("ContentSerializerAttribute") && ((fl & 7) != 6 || (fl & 0x20))) continue;   // public, not readonly
            auto sig = m.blobAt(m.get(T_Field, f, 2));
            size_t i = 1;
            Member mem;
            mem.name = m.str(m.get(T_Field, f, 1));
            if (!L.type(sig, i, mem.type)) { error = "bad signature on " + k.name + "." + mem.name; return false; }
            k.members.push_back(mem);
        }
        classes[k.name] = k;
    }
    return true;
}

const Klass* Schema::get(const std::string& name) const {
    auto it = classes.find(name);
    return it == classes.end() ? nullptr : &it->second;
}

bool Schema::allMembers(const std::string& name, std::vector<const Member*>& out) const {
    const Klass* k = get(name);
    if (!k) return false;
    if (!k->base.empty() && !allMembers(k->base, out)) return false;
    for (const Member& m : k->members) out.push_back(&m);
    return true;
}

}  // namespace netmeta
