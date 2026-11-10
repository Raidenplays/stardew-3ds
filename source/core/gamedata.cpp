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
    if (vfs.root().empty()) vfs.setRoot(root);
    auto readLines = [this](const std::string& full, std::vector<std::string>& out) {
        // full paths under root become layered table reads
        const std::string& r = vfs.root();
        if (full.compare(0, r.size(), r) == 0) return vfs.readTable(full.substr(r.size()), out);
        return readTextLines(full, out);
    };

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
        if (f.size() > 8 && !f[8].empty()) c.texture = f[8];
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
        if (f.size() > 6 && !f[6].empty()) o.texture = f[6];
        o.stringId = f.size() > 7 ? f[7] : f[0];
        objects[o.id] = o;
    }

    // crafting (optional: older conversions don't have these)
    lines.clear();
    readLines(root + "data/bigcraftables.dat", lines);
    for (auto& l : lines) {
        auto f = splitTabs(l);
        if (f.size() < 5) continue;
        BigDef b;
        b.id = atoi(f[0].c_str());
        b.name = f[1];
        b.price = atoi(f[2].c_str());
        b.spriteIndex = atoi(f[3].c_str());
        if (!f[4].empty()) b.texture = f[4];
        b.stringId = f.size() > 5 ? f[5] : f[0];
        bigs[b.id] = b;
    }
    lines.clear();
    readLines(root + "data/crafting.dat", lines);
    for (auto& l : lines) {
        auto f = splitTabs(l);
        if (f.size() < 6) continue;
        Recipe r;
        r.name = f[0];
        size_t p = 0;
        const std::string& ing = f[1];
        while (p < ing.size()) {
            size_t c = ing.find(',', p);
            std::string part = ing.substr(p, c == std::string::npos ? std::string::npos : c - p);
            Ingredient in{0, 1, false};
            int big = 0;
            if (sscanf(part.c_str(), "%d:%d:%d", &in.id, &in.count, &big) >= 2) {
                in.big = big == 1;
                r.ingredients.push_back(in);
            }
            if (c == std::string::npos) break;
            p = c + 1;
        }
        r.outBig = f[2] == "1";
        r.outId = atoi(f[3].c_str());
        r.outCount = std::max(1, atoi(f[4].c_str()));
        r.unlock = f[5];
        recipes.push_back(r);
    }
    lines.clear();
    readLines(root + "data/machines.dat", lines);
    for (auto& l : lines) {
        auto f = splitTabs(l);
        if (f.size() < 12) continue;
        MachineRule m;
        m.machine = atoi(f[0].c_str());
        size_t p = 0;
        while (p < f[1].size()) {
            size_t c = f[1].find(',', p);
            m.inputs.push_back(atoi(f[1].c_str() + p));
            if (c == std::string::npos) break;
            p = c + 1;
        }
        m.inputCount = std::max(1, atoi(f[2].c_str()));
        m.output = atoi(f[3].c_str());
        m.outCount = std::max(1, atoi(f[4].c_str()));
        m.flavor = f[5] == "-" ? "" : f[5];
        m.copyInput = f[6] == "1";
        m.minutes = atoi(f[7].c_str());
        m.days = atoi(f[8].c_str());
        m.fuel = atoi(f[9].c_str());
        m.fuelCount = atoi(f[10].c_str());
        m.autoRun = f[11] == "1";
        machineRules.push_back(m);
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
        if (f.size() > 6) t.price = atoi(f[6].c_str());
        if (f.size() > 7) t.upgradeFrom = f[7];
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

    // Villagers (optional: older conversions have none)
    lines.clear();
    if (readLines(root + "data/npcs.dat", lines)) {
        for (auto& l : lines) {
            auto f = splitTabs(l);
            if (f.size() < 9) continue;
            NpcDef n;
            n.name = f[0];
            n.display = f[1];
            n.birthSeason = atoi(f[2].c_str());
            n.birthDay = atoi(f[3].c_str());
            n.homeMap = f[4];
            n.homeX = atoi(f[5].c_str());
            n.homeY = atoi(f[6].c_str());
            n.homeDir = atoi(f[7].c_str());
            n.social = atoi(f[8].c_str()) != 0;
            for (int t = 0; t < TASTE_COUNT; t++) {
                if (9 + t < (int)f.size()) {
                    const char* p = f[9 + t].c_str();
                    while (*p) {
                        char* end;
                        long v = strtol(p, &end, 10);
                        if (end == p) break;
                        n.tastes[t].push_back((int)v);
                        p = end;
                    }
                }
                if (14 + t < (int)f.size()) n.tasteText[t] = f[14 + t];
            }
            std::vector<std::string> sub;
            if (readLines(root + "data/schedules/" + n.name + ".dat", sub))
                for (auto& sl : sub) {
                    size_t tab = sl.find('\t');
                    if (tab != std::string::npos) n.schedules[sl.substr(0, tab)] = sl.substr(tab + 1);
                }
            sub.clear();
            if (readLines(root + "data/dialogue/" + n.name + ".dat", sub))
                for (auto& dl : sub) {
                    size_t tab = dl.find('\t');
                    if (tab != std::string::npos) n.dialogue[dl.substr(0, tab)] = dl.substr(tab + 1);
                }
            npcs.push_back(std::move(n));
        }
    }
    lines.clear();
    if (readLines(root + "data/universal_tastes.dat", lines))
        for (auto& l : lines) {
            auto f = splitTabs(l);
            if (f.size() < 2) continue;
            int t = atoi(f[0].c_str());
            if (t < 0 || t >= TASTE_COUNT) continue;
            const char* p = f[1].c_str();
            while (*p) {
                char* end;
                long v = strtol(p, &end, 10);
                if (end == p) break;
                universalTastes[t].push_back((int)v);
                p = end;
            }
        }
    lines.clear();
    if (readLines(root + "data/location_names.dat", lines))
        for (auto& l : lines) {
            auto f = splitTabs(l);
            if (f.size() >= 2) locationNames[f[0]] = f[1];
        }
    lines.clear();
    if (readLines(root + "data/spawns.dat", lines))
        for (auto& l : lines) {
            auto f = splitTabs(l);
            if (f.empty()) continue;
            if (f[0] == "L" && f.size() >= 5) {
                LocationForage& lf = forage[f[1]];
                lf.minDaily = atoi(f[2].c_str());
                lf.maxDaily = atoi(f[3].c_str());
                lf.maxAtOnce = atoi(f[4].c_str());
            } else if (f[0] == "F" && f.size() >= 5) {
                forage[f[1]].items.push_back({(uint8_t)atoi(f[2].c_str()), atoi(f[3].c_str()), (float)atof(f[4].c_str())});
            } else if (f[0] == "A" && f.size() >= 11) {
                SpawnArea a;
                a.id = f[1]; a.location = f[2]; a.kind = f[3];
                a.minDaily = atoi(f[4].c_str()); a.maxDaily = atoi(f[5].c_str());
                a.seasonMask = (uint8_t)atoi(f[6].c_str());
                a.terrain = f[7];
                for (int k = 0; k < 2; k++) {
                    const std::string& src = k == 0 ? f[8] : f[9];
                    const char* p = src.c_str();
                    while (*p) {
                        SpawnRect r;
                        if (sscanf(p, "%d %d %d %d", &r.x1, &r.y1, &r.x2, &r.y2) == 4)
                            (k == 0 ? a.include : a.exclude).push_back(r);
                        const char* c = strchr(p, ',');
                        if (!c) break;
                        p = c + 1;
                    }
                }
                int season = 0;
                const char* p = f[10].c_str();
                while (*p && season < 4) {
                    if (*p == '|') { season++; p++; continue; }
                    int id = 0, w = 1;
                    if (sscanf(p, "%d:%d", &id, &w) >= 1) a.items[season].push_back({id, std::max(1, w)});
                    while (*p && *p != ',' && *p != '|') p++;
                    if (*p == ',') p++;
                }
                spawnAreas.push_back(a);
            }
        }
    lines.clear();
    if (readLines(root + "data/furniture.dat", lines))
        for (auto& l : lines) {
            auto f = splitTabs(l);
            if (f.size() < 7) continue;
            FurnitureDef d;
            d.id = atoi(f[0].c_str());
            d.name = f[1];
            d.type = f[2];
            d.spriteW = atoi(f[3].c_str());
            d.spriteH = atoi(f[4].c_str());
            d.boxW = atoi(f[5].c_str());
            d.boxH = atoi(f[6].c_str());
            furniture[d.id] = d;
        }
    lines.clear();
    if (readLines(root + "data/farmer_bob.dat", lines))
        for (auto& l : lines) {
            auto f = splitTabs(l);
            if (f.size() < 2) continue;
            int fr = atoi(f[0].c_str());
            if (fr < 0 || fr > 4096) continue;
            if ((int)farmerBob.size() <= fr) farmerBob.resize(fr + 1, 0);
            farmerBob[fr] = atoi(f[1].c_str());
        }
    lines.clear();
    if (readLines(root + "data/graph.dat", lines))
        for (auto& l : lines) {
            auto f = splitTabs(l);
            if (f.size() < 6) continue;
            graph[f[0]].push_back({atoi(f[1].c_str()), atoi(f[2].c_str()), f[3],
                                   atoi(f[4].c_str()), atoi(f[5].c_str())});
        }

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
            } else if (!strcmp(kw, "furniture")) {
                char m[64];
                FurniturePlacement fp;
                if (sscanf(l.c_str(), "furniture %63s %d %d %d", m, &fp.id, &fp.x, &fp.y) == 4) {
                    fp.map = m;
                    layout.furniture.push_back(fp);
                }
            } else if (!strcmp(kw, "outfit")) {
                int v[9];
                if (sscanf(l.c_str(), "outfit %d %d %d %d %d %d %d %d %d", &v[0], &v[1], &v[2], &v[3], &v[4],
                           &v[5], &v[6], &v[7], &v[8]) == 9) {
                    Outfit& o = layout.outfit;
                    o.shirt = v[0]; o.pants = v[1];
                    o.pantsR = (uint8_t)v[2]; o.pantsG = (uint8_t)v[3]; o.pantsB = (uint8_t)v[4];
                    o.hair = v[5];
                    o.hairR = (uint8_t)v[6]; o.hairG = (uint8_t)v[7]; o.hairB = (uint8_t)v[8];
                }
            } else if (!strcmp(kw, "farmer")) {
                if (l.size() > 7) layout.farmerName = l.substr(7);
            } else if (!strcmp(kw, "bin")) {
                int x, y;
                if (sscanf(l.c_str(), "bin %d %d", &x, &y) == 2) layout.bins.push_back({x, y});
            }
        }
    }
    return true;
}

const NpcDef* GameData::npc(const std::string& name) const {
    for (auto& n : npcs)
        if (n.name == name) return &n;
    return nullptr;
}

const CropDef* GameData::cropForSeed(int seedId) const {
    auto it = crops.find(seedId);
    return it == crops.end() ? nullptr : &it->second;
}

const BigDef* GameData::big(int id) const {
    auto it = bigs.find(id);
    return it == bigs.end() ? nullptr : &it->second;
}

bool GameData::isMachine(int bigId) const {
    for (auto& m : machineRules)
        if (m.machine == bigId) return true;
    return false;
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

int GameData::toolUpgrade(int index) const {
    if (index < 0 || index >= (int)tools.size()) return -1;
    for (size_t i = 0; i < tools.size(); i++)
        if (!tools[i].upgradeFrom.empty() && tools[i].upgradeFrom == tools[index].key) return (int)i;
    return -1;
}

const ToolDef* GameData::tool(int index) const {
    if (index < 0 || index >= (int)tools.size()) return nullptr;
    return &tools[index];
}

} // namespace sv
