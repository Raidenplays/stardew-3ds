#include "gamedata.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace sv {

const char* seasonName(int s) {
    static const char* n[] = {"spring", "summer", "fall", "winter"};
    return n[s & 3];
}
const char* seasonShort(int s) {
    static const char* n[] = {"Spr.", "Sum.", "Fall", "Win."};
    return n[s & 3];
}

int CropDef::totalDays() const {
    int t = 0;
    for (int d : phaseDays) t += d;
    return t;
}

static std::vector<std::string> splitTabs(const std::string& line) {
    std::vector<std::string> out;
    size_t start = 0;
    while (true) {
        size_t p = line.find('\t', start);
        out.push_back(line.substr(start, p == std::string::npos ? std::string::npos : p - start));
        if (p == std::string::npos) break;
        start = p + 1;
    }
    return out;
}

static bool readLines(const std::string& path, std::vector<std::string>& lines) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    std::string cur;
    char buf[1024];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) {
        for (size_t i = 0; i < n; i++) {
            char c = buf[i];
            if (c == '\n') { lines.push_back(cur); cur.clear(); }
            else if (c != '\r') cur += c;
        }
    }
    if (!cur.empty()) lines.push_back(cur);
    fclose(f);
    return true;
}

static std::string lower(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

static ToolKind classify(const std::string& cls, const std::string& key) {
    std::string c = lower(cls), k = lower(key);
    if (c == "hoe") return TOOL_HOE;
    if (c == "wateringcan") return TOOL_CAN;
    if (c == "axe") return TOOL_AXE;
    if (c == "pickaxe") return TOOL_PICKAXE;
    if (c == "meleeweapon" && k.find("scythe") != std::string::npos) return TOOL_SCYTHE;
    if (k.find("scythe") != std::string::npos) return TOOL_SCYTHE;
    return TOOL_OTHER;
}

bool GameData::load(const std::string& root, std::string* err) {
    std::vector<std::string> lines;

    if (!readLines(root + "data/crops.dat", lines)) {
        if (err) *err = "missing data/crops.dat";
        return false;
    }
    for (auto& l : lines) {
        auto f = splitTabs(l);
        if (f.size() < 7) continue;
        CropDef c;
        c.seedId = atoi(f[0].c_str());
        c.seasonMask = (uint8_t)atoi(f[1].c_str());
        const char* p = f[2].c_str();
        while (*p) {
            c.phaseDays.push_back(atoi(p));
            const char* q = strchr(p, ',');
            if (!q) break;
            p = q + 1;
        }
        if (c.phaseDays.empty()) c.phaseDays.push_back(1);
        c.regrowDays = atoi(f[3].c_str());
        c.harvestId = atoi(f[4].c_str());
        c.spriteIndex = atoi(f[5].c_str());
        c.raised = atoi(f[6].c_str()) != 0;
        if (f.size() > 7) c.harvestMin = std::max(1, atoi(f[7].c_str()));
        crops[c.seedId] = c;
    }

    lines.clear();
    if (!readLines(root + "data/objects.dat", lines)) {
        if (err) *err = "missing data/objects.dat";
        return false;
    }
    for (auto& l : lines) {
        auto f = splitTabs(l);
        if (f.size() < 6) continue;
        ObjectDef o;
        o.id = atoi(f[0].c_str());
        o.name = f[1];
        o.price = atoi(f[2].c_str());
        o.spriteIndex = atoi(f[3].c_str());
        o.category = atoi(f[4].c_str());
        o.edibility = atoi(f[5].c_str());
        objects[o.id] = o;
    }

    lines.clear();
    if (!readLines(root + "data/tools.dat", lines)) {
        if (err) *err = "missing data/tools.dat";
        return false;
    }
    for (auto& l : lines) {
        auto f = splitTabs(l);
        if (f.size() < 6) continue;
        ToolDef t;
        t.key = f[0];
        t.className = f[1];
        t.name = f[2];
        t.upgrade = atoi(f[3].c_str());
        t.spriteIndex = atoi(f[4].c_str());
        t.texture = f[5];
        t.kind = classify(t.className, t.key);
        tools.push_back(t);
    }

    // shops.dat is optional (older conversions don't have it)
    lines.clear();
    if (readLines(root + "data/shops.dat", lines)) {
        for (auto& l : lines) {
            auto f = splitTabs(l);
            if (f.size() < 5) continue;
            ShopItem it;
            it.itemId = atoi(f[1].c_str());
            it.price = atoi(f[2].c_str());
            it.seasonMask = (uint8_t)atoi(f[3].c_str());
            it.minYear = atoi(f[4].c_str());
            shops[f[0]].push_back(it);
        }
    }
    this->root = root;

    // layout.txt is optional -- defaults live in Layout.
    lines.clear();
    if (readLines(root + "layout.txt", lines)) {
        layout.bins.clear();
        for (auto& l : lines) {
            if (l.empty() || l[0] == '#') continue;
            char kw[32] = {0};
            if (sscanf(l.c_str(), "%31s", kw) != 1) continue;
            if (!strcmp(kw, "building")) {
                char name[64], tex[128];
                BuildingDef b;
                if (sscanf(l.c_str(), "building %63s %127s %d %d %d %d %d %d %d %d", name, tex,
                           &b.srcX, &b.srcY, &b.srcW, &b.srcH, &b.tileX, &b.tileY, &b.footW,
                           &b.footH) == 10) {
                    b.name = name;
                    b.texture = tex;
                    layout.buildings.push_back(b);
                }
            } else if (!strcmp(kw, "door")) {
                sscanf(l.c_str(), "door %d %d", &layout.doorX, &layout.doorY);
            } else if (!strcmp(kw, "spawn")) {
                sscanf(l.c_str(), "spawn %d %d", &layout.spawnX, &layout.spawnY);
            } else if (!strcmp(kw, "bed")) {
                char m[64];
                if (sscanf(l.c_str(), "bed %63s %d %d %d %d", m, &layout.bedX, &layout.bedY,
                           &layout.bedW, &layout.bedH) == 5)
                    layout.bedMap = m;
            } else if (!strcmp(kw, "wake")) {
                char m[64];
                if (sscanf(l.c_str(), "wake %63s %d %d", m, &layout.wakeX, &layout.wakeY) == 3)
                    layout.wakeMap = m;
            } else if (!strcmp(kw, "bin")) {
                int x, y;
                if (sscanf(l.c_str(), "bin %d %d", &x, &y) == 2) layout.bins.push_back({x, y});
            }
        }
    }
    return true;
}

const CropDef* GameData::cropForSeed(int seedId) const {
    auto it = crops.find(seedId);
    return it == crops.end() ? nullptr : &it->second;
}

const ObjectDef* GameData::object(int id) const {
    auto it = objects.find(id);
    return it == objects.end() ? nullptr : &it->second;
}

const ToolDef* GameData::toolByKind(ToolKind k) const {
    const ToolDef* best = nullptr;
    for (auto& t : tools)
        if (t.kind == k && (!best || t.upgrade < best->upgrade)) best = &t;
    return best;
}

const ToolDef* GameData::tool(int index) const {
    if (index < 0 || index >= (int)tools.size()) return nullptr;
    return &tools[index];
}

} // namespace sv
