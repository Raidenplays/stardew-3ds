// Daily forage and ore spawns (vanilla Data/Locations + Farm Type Manager areas).
#include <algorithm>

#include "game.h"

namespace sv {

// Ore node -> what breaking it gives you.
int oreNodeDrop(int node) {
    switch (node) {
        case 751: return 378;   // copper
        case 290: return 380;   // iron
        case 764: return 384;   // gold
        case 765: return 386;   // iridium
        case 46:  return 386;   // mystic stone
        case 95:  return 909;   // radioactive
        case 75:  return 535;   // geode
        case 76:  return 536;   // frozen geode
        case 77:  return 537;   // magma geode
        case 2:   return 72;    // diamond
        case 4:   return 64;    // ruby
        case 6:   return 70;    // jade
        case 8:   return 66;    // amethyst
        case 10:  return 68;    // topaz
        case 12:  return 60;    // emerald
        case 14:  return 62;    // aquamarine
        default:  return 390;   // stone
    }
}

bool Game::canSpawnAt(const Location& loc, int x, int y, const std::string& terrain, bool vanilla) const {
    const TileMap& m = loc.map;
    if (!m.inBounds(x, y)) return false;
    uint8_t f = m.flags(x, y);
    if (f & (CF_WATER | CF_NOSPAWN)) return false;
    if ((f & CF_BLOCKED) && !(f & CF_PASSABLE)) return false;
    if (m.tile(L_BACK, x, y) == NO_TILE || m.tile(L_BUILDINGS, x, y) != NO_TILE) return false;
    if (!m.action(x, y).empty()) return false;
    for (auto& w : m.warps)
        if (std::abs(w.x - x) + std::abs(w.y - y) <= 2) return false;   // keep doorways clear
    if (vanilla) return (f & CF_SPAWNABLE) != 0;
    if (terrain.find("all") != std::string::npos || terrain.empty()) return true;
    if (terrain.find("grass") != std::string::npos && (f & CF_GRASS)) return true;
    if (terrain.find("dirt") != std::string::npos && (f & CF_DIRT)) return true;
    if (terrain.find("diggable") != std::string::npos && (f & CF_DIGGABLE)) return true;
    return false;
}

WorldObject* Game::objectAt(int x, int y) {
    for (auto& o : cur->objects)
        if (o.x == x && o.y == y) return &o;
    return nullptr;
}

void Game::spawnObjects(Location& loc) {
    if (loc.isFarm || loc.mineLevel || loc.name.find('@') != std::string::npos || loc.spawnDay == absoluteDay()) return;   // mine floors make their own
    loc.spawnDay = absoluteDay();
    loc.objects.clear();
    const TileMap& m = loc.map;
    if (m.width() == 0) return;
    uint32_t r = (uint32_t)(absoluteDay() * 2654435761u) ^ (uint32_t)std::hash<std::string>()(loc.name);
    auto rnd = [&]() {
        r ^= r << 13; r ^= r >> 17; r ^= r << 5;
        return r;
    };
    auto taken = [&](int x, int y) {
        for (auto& o : loc.objects)
            if (o.x == x && o.y == y) return true;
        return false;
    };

    // vanilla forage
    auto lf = data.forage.find(loc.name);
    if (lf != data.forage.end() && m.outdoors) {
        std::vector<const ForageEntry*> pool;
        for (auto& e : lf->second.items)
            if (e.seasonMask & (1 << season)) pool.push_back(&e);
        if (!pool.empty()) {
            int span = std::max(0, lf->second.maxDaily - lf->second.minDaily);
            int want = std::min(lf->second.maxAtOnce, lf->second.minDaily + (int)(rnd() % (uint32_t)(span + 1)));
            for (int i = 0, tries = 0; i < want && tries < 400; tries++) {
                int x = (int)(rnd() % (uint32_t)m.width()), y = (int)(rnd() % (uint32_t)m.height());
                if (taken(x, y) || !canSpawnAt(loc, x, y, "", true)) continue;
                const ForageEntry* e = pool[rnd() % pool.size()];
                if ((rnd() % 1000) / 1000.0f > e->chance) continue;
                loc.objects.push_back({x, y, e->itemId, false});
                i++;
            }
        }
    }

    // Farm Type Manager areas
    for (const SpawnArea& a : data.spawnAreas) {
        if (a.location != loc.name || !(a.seasonMask & (1 << season))) continue;
        const auto& items = a.items[season & 3];
        if (items.empty()) continue;
        int total = 0;
        for (auto& it : items) total += it.second;
        int span = std::max(0, a.maxDaily - a.minDaily);
        int want = std::min(200, a.minDaily + (int)(rnd() % (uint32_t)(span + 1)));
        // candidate box: include rects, else the whole map
        std::vector<SpawnRect> boxes = a.include;
        if (boxes.empty()) boxes.push_back({0, 0, m.width() - 1, m.height() - 1});
        for (int i = 0, tries = 0; i < want && tries < want * 30 + 50; tries++) {
            const SpawnRect& b = boxes[rnd() % boxes.size()];
            int x = b.x1 + (int)(rnd() % (uint32_t)(b.x2 - b.x1 + 1));
            int y = b.y1 + (int)(rnd() % (uint32_t)(b.y2 - b.y1 + 1));
            bool excluded = false;
            for (auto& e : a.exclude)
                if (x >= e.x1 && x <= e.x2 && y >= e.y1 && y <= e.y2) excluded = true;
            if (excluded || taken(x, y) || !canSpawnAt(loc, x, y, a.terrain, false)) continue;
            int pick = (int)(rnd() % (uint32_t)total), id = items[0].first;
            for (auto& it : items) {
                if (pick < it.second) { id = it.first; break; }
                pick -= it.second;
            }
            if (id == 343 && (rnd() & 1)) id = 450;   // two stone looks
            loc.objects.push_back({x, y, id, a.kind == "ore"});
            i++;
        }
    }
}

} // namespace sv
