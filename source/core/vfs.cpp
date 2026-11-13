#include "vfs.h"

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <unordered_map>

namespace sv {

bool readTextLines(const std::string& path, std::vector<std::string>& lines) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    std::string cur;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0)
        for (size_t i = 0; i < n; i++) {
            char c = buf[i];
            if (c == '\n') {
                lines.push_back(cur);
                cur.clear();
            } else if (c != '\r') {
                cur += c;
            }
        }
    if (!cur.empty()) lines.push_back(cur);
    fclose(f);
    return true;
}

static void listFiles(const std::string& dir, const std::string& prefix, std::unordered_set<std::string>& out) {
    DIR* d = opendir(dir.c_str());
    if (!d) return;
    while (dirent* e = readdir(d)) {
        std::string n = e->d_name;
        if (n == "." || n == "..") continue;
        std::string full = dir + "/" + n;
        struct stat st;
        if (stat(full.c_str(), &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) listFiles(full, prefix + n + "/", out);
        else out.insert(prefix + n);
    }
    closedir(d);
}

void Vfs::setRoot(const std::string& root) {
    base = root;
    if (!base.empty() && base.back() != '/') base += '/';
}

void Vfs::setMods(const std::vector<std::string>& enabled) {
    mods = enabled;
    index.assign(mods.size(), {});
    for (size_t i = 0; i < mods.size(); i++) listFiles(base + "mods/" + mods[i], "", index[i]);
}

std::string Vfs::path(const std::string& rel) const {
    for (size_t i = mods.size(); i-- > 0;)
        if (index[i].count(rel)) return base + "mods/" + mods[i] + "/" + rel;
    return base + rel;
}

bool Vfs::exists(const std::string& rel) const {
    for (size_t i = mods.size(); i-- > 0;)
        if (index[i].count(rel)) return true;
    FILE* f = fopen((base + rel).c_str(), "rb");
    if (f) fclose(f);
    return f != nullptr;
}

// Tables where the first column is a unique key (later layers win).
static bool keyedTable(const std::string& rel) {
    static const char* keyed[] = {"data/crops.dat", "data/objects.dat", "data/npcs.dat", "data/tools.dat",
                                  "data/furniture.dat", "data/location_names.dat", "data/item_ids.dat",
                                  "data/farmer_bob.dat", "data/universal_tastes.dat",
                                  "data/bigcraftables.dat", "data/crafting.dat", "data/trees.dat", "data/quests.dat", "data/mail.dat"};
    for (const char* k : keyed)
        if (rel == k) return true;
    return rel.rfind("data/events/", 0) == 0 || rel.rfind("data/dialogue/", 0) == 0 ||
           rel.rfind("data/schedules/", 0) == 0;
}

bool Vfs::readTable(const std::string& rel, std::vector<std::string>& lines) const {
    bool any = readTextLines(base + rel, lines);
    for (size_t i = 0; i < mods.size(); i++) {
        if (!index[i].count(rel)) continue;
        std::vector<std::string> layer;
        if (!readTextLines(base + "mods/" + mods[i] + "/" + rel, layer)) continue;
        any = true;
        if (layer.empty() || layer[0] != "#delta") {
            lines.swap(layer);    // whole-table replacement
            continue;
        }
        for (size_t k = 1; k < layer.size(); k++) {
            const std::string& l = layer[k];
            if (l.size() < 2 || l[1] != '\t') continue;
            std::string body = l.substr(2);
            if (l[0] == '+') {
                lines.push_back(body);
            } else if (l[0] == '-') {
                auto it = std::find(lines.begin(), lines.end(), body);
                if (it != lines.end()) lines.erase(it);
            }
        }
    }
    if (keyedTable(rel) && !mods.empty()) {
        // keep the last line for each key, in the order those last lines appear
        std::unordered_map<std::string, size_t> last;
        for (size_t i = 0; i < lines.size(); i++) last[lines[i].substr(0, lines[i].find('\t'))] = i;
        std::vector<std::string> out;
        out.reserve(last.size());
        for (size_t i = 0; i < lines.size(); i++)
            if (last[lines[i].substr(0, lines[i].find('\t'))] == i) out.push_back(lines[i]);
        lines.swap(out);
    }
    return any;
}

std::vector<ModInfo> Vfs::scanMods() const {
    std::vector<ModInfo> out;
    DIR* d = opendir((base + "mods").c_str());
    if (!d) return out;
    while (dirent* e = readdir(d)) {
        std::string n = e->d_name;
        if (n == "." || n == "..") continue;
        ModInfo m;
        m.id = n;
        m.name = n;
        std::vector<std::string> info;
        if (!readTextLines(base + "mods/" + n + "/mod.txt", info)) continue;   // not a layer
        for (auto& l : info) {
            size_t c = l.find(':');
            if (c == std::string::npos) continue;
            std::string k = l.substr(0, c), v = l.substr(c + 1);
            while (!v.empty() && v[0] == ' ') v.erase(0, 1);
            if (k == "name") m.name = v;
            else if (k == "version") m.version = v;
            else if (k == "author") m.author = v;
            else if (k == "description") m.description = v;
        }
        out.push_back(m);
    }
    closedir(d);
    std::sort(out.begin(), out.end(), [](const ModInfo& a, const ModInfo& b) { return a.name < b.name; });
    return out;
}

std::string Vfs::signature() const {
    std::string sig;
    for (auto& id : mods) {
        std::vector<std::string> info;
        std::string ver;
        if (readTextLines(base + "mods/" + id + "/mod.txt", info))
            for (auto& l : info)
                if (l.rfind("version:", 0) == 0) ver = l.substr(8);
        sig += id + "@" + ver + ";";
    }
    return sig;
}

std::vector<std::string> readModList(const std::string& root) {
    std::vector<std::string> lines, out;
    readTextLines(root + "mods.txt", lines);
    for (auto& l : lines)
        if (!l.empty() && l[0] != '#') out.push_back(l);
    return out;
}

bool writeModList(const std::string& root, const std::vector<std::string>& ids) {
    FILE* f = fopen((root + "mods.txt").c_str(), "wb");
    if (!f) return false;
    fputs("# enabled mods, in load order (later ones win)\n", f);
    for (auto& id : ids) fprintf(f, "%s\n", id.c_str());
    fclose(f);
    return true;
}

std::string backupSaveIfModsChanged(const std::string& root, const std::string& savePath, const std::string& sig) {
    std::vector<std::string> prev;
    bool hadSig = readTextLines(savePath + ".mods", prev);
    std::string old = prev.empty() ? "" : prev[0];
    std::string backup;
    FILE* save = fopen(savePath.c_str(), "rb");
    if (save && (!hadSig || old != sig)) {
        mkdir((root + "backups").c_str(), 0777);
        char stamp[32];
        time_t t = time(nullptr);
        strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", localtime(&t));
        size_t cut = savePath.find_last_of('/');   // each farm's backups carry its own file name
        backup = root + "backups/" + stamp + "-" + (cut == std::string::npos ? savePath : savePath.substr(cut + 1));
        FILE* out = fopen(backup.c_str(), "wb");
        if (out) {
            char buf[8192];
            size_t n;
            while ((n = fread(buf, 1, sizeof buf, save)) > 0) fwrite(buf, 1, n, out);
            fclose(out);
            // remember which mods that backup was played with
            FILE* note = fopen((backup + ".mods").c_str(), "wb");
            if (note) {
                fprintf(note, "%s\n", old.c_str());
                fclose(note);
            }
        } else {
            backup.clear();
        }
    }
    if (save) fclose(save);
    FILE* f = fopen((savePath + ".mods").c_str(), "wb");
    if (f) {
        fprintf(f, "%s\n", sig.c_str());
        fclose(f);
    }
    return backup;
}

} // namespace sv
