// Layered files: the base game folder plus enabled mod layers on top.
//
//   sdmc:/3ds/stardew3ds/                 base (from tools/convert.py)
//   sdmc:/3ds/stardew3ds/mods/<id>/       one layer per mod group (tools/layer.py)
//
// Maps and textures: the highest enabled layer that has the file wins.
// Data tables (data/**/*.dat) merge: a layer's file either replaces the table
// or, if it starts with "#delta", adds ("+\t<line>") and removes ("-\t<line>")
// lines. Tables keyed by their first column keep the last entry per key.
#pragma once
#include <string>
#include <unordered_set>
#include <vector>

namespace sv {

struct ModInfo {
    std::string id;           // folder name under mods/
    std::string name, version, author, description;
    bool present = true;      // listed in mods.txt but folder missing -> false
    bool enabled = false;
};

class Vfs {
public:
    void setRoot(const std::string& root);           // ends with '/'
    void setMods(const std::vector<std::string>& enabledInOrder);   // later = higher priority
    const std::string& root() const { return base; }

    std::string path(const std::string& rel) const;   // where to read rel from
    bool exists(const std::string& rel) const;
    bool readTable(const std::string& rel, std::vector<std::string>& lines) const;

    std::vector<ModInfo> scanMods() const;           // everything under mods/
    std::vector<std::string> enabledMods() const { return mods; }
    std::string signature() const;                   // "id@version;..." of enabled mods

private:
    std::string base;
    std::vector<std::string> mods;
    std::vector<std::unordered_set<std::string>> index;   // files in each enabled layer
};

bool readTextLines(const std::string& path, std::vector<std::string>& lines);

// mods.txt: one enabled mod id per line, in load order ("#" lines = comments).
std::vector<std::string> readModList(const std::string& root);
bool writeModList(const std::string& root, const std::vector<std::string>& ids);

// If the enabled mod set differs from the one the save was last played with,
// copy save.sav to backups/<stamp>-save.sav first. Returns the backup path ("" if none).
std::string backupSaveIfModsChanged(const std::string& root, const std::string& savePath,
                                    const std::string& signature);

} // namespace sv
