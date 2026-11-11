// Reads class layouts (serialized fields/properties and their types) from a
// .NET assembly's metadata -- the player's own StardewValley.GameData.dll --
// so packed Data/*.xnb files can be decoded on the 3DS. Only the metadata
// tables are read; no code. Mirrors tools/netschema.py.
#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace netmeta {

// A type as written in a signature.
struct TypeSig {
    enum Kind { PRIM, NAMED, GENERIC, ARRAY, VAR } kind = PRIM;
    std::string name;                  // prim name ("int", "string"...) or full type name
    std::vector<TypeSig> args;         // generic args / array element
    bool value = false;                // value type?
};

struct Member {
    std::string name;
    TypeSig type;
};

struct Klass {
    std::string name;
    std::string base;                  // full name of base class ("" = none)
    enum Kind { CLASS, STRUCT, ENUM } kind = CLASS;
    std::vector<Member> members;       // ReflectiveReader order (properties, then fields)
    std::map<int64_t, std::string> enumValues;
};

class Schema {
public:
    bool load(const std::string& dllPath);
    bool loadMemory(const std::vector<uint8_t>& file);
    const Klass* get(const std::string& name) const;
    // members in read order, base class first; false if a base is unknown
    bool allMembers(const std::string& name, std::vector<const Member*>& out) const;
    std::string error;
    size_t count() const { return classes.size(); }

private:
    std::map<std::string, Klass> classes;
};

}  // namespace netmeta
