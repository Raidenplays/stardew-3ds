// Wild trees: placed from the map's Paths layer, chopped with the axe,
// planted from seeds, grow overnight.
#include <algorithm>
#include <cctype>

#include "game.h"

static const int TILE = 16;

namespace sv {

static const int OBJ_WOOD_T = 388;
static const int OBJ_SAP = 92;
static const int TREE_HEALTH = 10, STUMP_HEALTH = 5;

static bool isPathsSheet(const std::string& tex) {
    std::string t = tex;
    for (auto& ch : t) ch = (char)tolower((unsigned char)ch);
    return t.size() >= 5 && t.compare(t.size() - 5, 5, "paths") == 0;
}

// Paths layer markers used by the farm and outdoor maps: 9/10/11 are grown
// trees of type 1/2/3.
void Game::treesFromMap(Location& loc) {
    loc.trees.clear();
    loc.clumps.clear();
    const TileMap& m = loc.map;
    for (int y = 0; y < m.height(); y++)
        for (int x = 0; x < m.width(); x++) {
            uint16_t t = m.tile(L_PATHS, x, y);
            if (t == NO_TILE) continue;
            int ts = m.tileset(L_PATHS, x, y);
            if (ts >= (int)m.tilesets.size() || !isPathsSheet(m.tilesets[ts].texture)) continue;
            if (t == 24 || t == 25 || t == 26) {   // large / medium / small bush
                Location::Bush b;
                b.x = (int16_t)x;
                b.y = (int16_t)y;
                b.size = (uint8_t)(26 - t);
                loc.bushes.push_back(b);
                continue;
            }
            if (loc.isFarm && (t == 19 || t == 20 || t == 21)) {   // large log / boulder / stump (top-left)
                Location::Clump cl;
                cl.x = (int16_t)x;
                cl.y = (int16_t)y;
                cl.kind = t == 19 ? Location::CLUMP_LOG : t == 20 ? Location::CLUMP_BOULDER : Location::CLUMP_STUMP;
                cl.health = 10;
                loc.clumps.push_back(cl);
                continue;
            }
            if (t == 22 || t == 36) {              // grass: 1-4 tufts
                uint32_t h = (uint32_t)(x * 73856093u) ^ (uint32_t)(y * 19349663u);
                loc.grass[(size_t)y * m.width() + x] = (uint8_t)(1 + (h >> 7) % 4) | (t == 36 ? 8 : 0);
                continue;
            }
            int type = t == 9 ? 1 : t == 10 ? 2 : t == 11 ? 3 : 0;
            if (!type || !data.trees.count(type)) continue;
            Tree tr;
            tr.x = x;
            tr.y = y;
            tr.type = type;
            loc.trees.push_back(tr);
        }
}

int Game::bushAt(int tx, int ty) const {
    const auto& bs = cur->bushes;
    for (size_t i = 0; i < bs.size(); i++) {
        int w = bs[i].size == 2 ? 3 : bs[i].size == 1 ? 2 : 1;
        if (ty == bs[i].y && tx >= bs[i].x && tx < bs[i].x + w) return (int)i;
    }
    return -1;
}

int Game::clumpAt(int tx, int ty) const {
    const auto& cs = cur->clumps;
    for (size_t i = 0; i < cs.size(); i++)
        if (tx >= cs[i].x && tx < cs[i].x + 2 && ty >= cs[i].y && ty < cs[i].y + 2) return (int)i;
    return -1;
}

// Sprite of a clump on Maps/springobjects (2x2 tiles).
int clumpSprite(int kind) { return kind == 1 ? 602 : kind == 2 ? 672 : 600; }

// An axe or pickaxe hitting a big stump, log or boulder. True if there was one.
bool Game::hitClump(int tx, int ty, ToolKind kind, int level) {
    int i = clumpAt(tx, ty);
    if (i < 0) return false;
    Location::Clump& cl = cur->clumps[i];
    bool rock = cl.kind == Location::CLUMP_BOULDER;
    int need = cl.kind == Location::CLUMP_STUMP ? 1 : 2;      // copper axe; steel axe / pickaxe
    int idx = clumpSprite(cl.kind);
    float cx = cl.x * TILE + 16.0f, cy = cl.y * TILE + 20.0f;
    cl.shake = 0.3f;
    if ((kind == TOOL_PICKAXE) != rock) {           // wrong tool: just a thunk
        sfx(rock ? "clank" : "woodyHit");
        return true;
    }
    if (level < need) {
        sfx(rock ? "clank" : "axe");
        static const char* tools[3] = {"", "a Copper Axe", "a Steel tool"};
        say(std::string(rock ? "This boulder" : cl.kind == Location::CLUMP_LOG ? "This log" : "This stump") +
            " needs " + (rock ? "a Steel Pickaxe" : tools[need]) + ".");
        return true;
    }
    sfx(rock ? "hammer" : "axchop");
    burst("Maps_springobjects", (idx % 24) * 16, (idx / 24) * 16, 32, 32, cx, cy, 5);
    cl.health = (int8_t)(cl.health - (1 + level));
    if (cl.health > 0) return true;
    sfx(rock ? "boulderBreak" : "stumpCrack");
    burst("Maps_springobjects", (idx % 24) * 16, (idx / 24) * 16, 32, 32, cx, cy, 14, 6);
    int item = rock ? 390 : 709;                     // stone / hardwood
    int n = rock ? 15 : cl.kind == Location::CLUMP_LOG ? 8 : 2;
    if (addItem(item, n)) itemFlies(item, cx, cy, n);
    questItem(item, n);
    gainXp(rock ? SK_MINING : SK_FORAGING, rock ? 15 : 25);
    cur->clumps.erase(cur->clumps.begin() + i);
    return true;
}

int Game::grassAt(int tx, int ty) const {
    if (!cur->map.inBounds(tx, ty) || cur->grass.empty()) return 0;
    return cur->grass[(size_t)ty * cur->map.width() + tx] & 7;
}

bool Game::cutGrass(int tx, int ty) {
    if (!grassAt(tx, ty)) return false;
    uint8_t& gr = cur->grass[(size_t)ty * cur->map.width() + tx];
    int row = (gr & 8) ? 160 : (season == SUMMER ? 20 : season == FALL ? 40 : season == WINTER ? 80 : 0);
    burst("TerrainFeatures_grass", 0, row, 15, 20, tx * TILE + 8.0f, ty * TILE + 10.0f, 5, 4);
    gr = 0;
    return true;
}

// Each night farm grass fills out, and now and then spreads to a free neighbour.
void Game::growGrass() {
    if (!farm || season == WINTER) return;
    const TileMap& m = farm->map;
    int w = m.width(), h = m.height();
    std::vector<int> spread;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint8_t& g = farm->grass[(size_t)y * w + x];
            if (!(g & 7)) continue;
            if ((g & 7) < 4 && rand32() % 2) g++;
            if (rand32() % 100 < 6) {
                static const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
                int k = rand32() % 4, nx = x + dx[k], ny = y + dy[k];
                if (!m.inBounds(nx, ny)) continue;
                size_t i = (size_t)ny * w + nx;
                uint8_t f = m.flags(nx, ny);
                if (!(f & CF_DIGGABLE) || (f & CF_WATER) || farm->grass[i] || farm->debris[i] ||
                    farm->soils[i].tilled || m.tile(L_BUILDINGS, nx, ny) != NO_TILE)
                    continue;
                spread.push_back((int)i);
            }
        }
    for (int i : spread) {
        int x = i % w, y = i / w;
        bool under = false;
        for (auto& c : farm->clumps)
            if (x >= c.x && x < c.x + 2 && y >= c.y && y < c.y + 2) under = true;
        if (!under) farm->grass[i] = 1;
    }
}

Tree* Game::treeAt(int x, int y) {
    for (auto& t : cur->trees)
        if (t.x == x && t.y == y) return &t;
    return nullptr;
}

const Tree* Game::treeAt(int x, int y) const {
    for (auto& t : cur->trees)
        if (t.x == x && t.y == y) return &t;
    return nullptr;
}

bool Game::chopTree(int tx, int ty, int axeLevel) {
    Tree* t = treeAt(tx, ty);
    if (!t) return false;
    t->shake = 0.25f;
    sfx("axchop");
    if (t->stage < TREE_GROWN && !t->stump) {   // saplings come out in one go
        const TreeDef* d = data.trees.count(t->type) ? &data.trees.at(t->type) : nullptr;
        if (t->stage > 0 && d && d->seed >= 0 && addItem(d->seed, 1)) itemFlies(d->seed, t->x * TILE + 8.0f, (t->y + 1) * TILE - 4.0f);
        cur->trees.erase(cur->trees.begin() + (t - &cur->trees[0]));
        return true;
    }
    t->health -= (int8_t)(1 + axeLevel);
    const TreeDef* tdef = data.trees.count(t->type) ? &data.trees.at(t->type) : nullptr;
    const std::string tex = tdef ? tdef->texture[season & 3] : std::string();
    float cx = t->x * TILE + 8.0f, base = (t->y + 1) * TILE - 4.0f;
    if (!tex.empty()) {
        if (t->stump) burst(tex, 32, 104, 16, 24, cx, base, 5);                  // wood chips
        else burst(tex, 16, 112, 16, 16, cx, base - 40, 3, 6);                    // a few leaves drop
    }
    if (t->health > 0) return true;
    if (!t->stump) {
        int wood = 10 + (int)(rand32() % 3);
        if (has(PR_FORESTER)) wood = wood * 5 / 4;
        if (addItem(OBJ_WOOD_T, wood)) itemFlies(OBJ_WOOD_T, cx + (cx >= px ? 24.0f : -24.0f), base, wood);
        std::string msg = "+" + std::to_string(wood) + " Wood";
        if (has(PR_LUMBERJACK) && rand32() % 4 == 0 && data.object(709)) {
            if (addItem(709, 1)) itemFlies(709, cx, base);
            msg += ", +1 Hardwood";
        }
        const TreeDef* d = data.trees.count(t->type) ? &data.trees.at(t->type) : nullptr;
        if (d && d->seed >= 0 && rand32() % 4 != 0) {
            int n = 1 + (int)(rand32() % 2);
            if (addItem(d->seed, n)) itemFlies(d->seed, cx, base, n);
            const ObjectDef* so = data.object(d->seed);
            if (so) msg += ", +" + std::to_string(n) + " " + so->name;
        }
        say(msg);
        sfx("treecrack");
        if (!tex.empty()) {   // the top topples away from you (the stump stays)
            Effect e;
            e.kind = Effect::FALLING_TREE;
            e.tex = tex;
            e.sx = 0; e.sy = 0; e.sw = 48; e.sh = 96;
            e.x = cx;
            e.y = base - 8;
            e.dir = cx >= px ? 1 : -1;
            e.life = 1.4f;
            effects.push_back(e);
            for (int k = 0; k < 4; k++) burst(tex, 16, 112, 16, 16, cx + e.dir * (20 + k * 14), base - 6, 3, 6);
        }
        gainXp(SK_FORAGING, 12);
        t->stump = true;
        t->health = STUMP_HEALTH;
        return true;
    }
    {
        int w = 1 + (int)(rand32() % 2);
        if (addItem(OBJ_WOOD_T, w)) itemFlies(OBJ_WOOD_T, t->x * TILE + 8.0f, (t->y + 1) * TILE - 4.0f, w);
    }
    if (data.object(OBJ_SAP) && addItem(OBJ_SAP, 1)) itemFlies(OBJ_SAP, t->x * TILE + 8.0f, (t->y + 1) * TILE - 4.0f);
    gainXp(SK_FORAGING, 1);
    cur->trees.erase(cur->trees.begin() + (t - &cur->trees[0]));
    return true;
}

bool Game::plantTree(int tx, int ty) {
    Slot& sl = inv[selected];
    if (sl.type != SLOT_OBJECT) return false;
    const TreeDef* d = data.treeForSeed(sl.id);
    if (!d) return false;
    const TileMap& m = cur->map;
    if (!cur->isFarm || !m.inBounds(tx, ty)) {
        say("Plant trees on your farm");
        return true;
    }
    size_t idx = (size_t)ty * m.width() + tx;
    uint8_t f = m.flags(tx, ty);
    if (tileSolid(tx, ty) || treeAt(tx, ty) || placedAt(tx, ty) || cur->soils[idx].tilled ||
        (f & CF_WATER) || !(f & (CF_DIGGABLE | CF_GRASS | CF_DIRT))) {
        say("Can't plant that here");
        return true;
    }
    Tree t;
    t.x = tx;
    t.y = ty;
    t.type = d->type;
    t.stage = 0;
    t.health = 1;
    cur->trees.push_back(t);
    if (grassAt(tx, ty)) cur->grass[(size_t)ty * cur->map.width() + tx] = 0;
    if (--sl.count <= 0) sl = Slot();
    return true;
}

void Game::treesGrow() {
    if (season == WINTER) return;
    for (auto& kv : locations) {
        Location& loc = *kv.second;
        for (auto& t : loc.trees) {
            if (t.stump || t.stage >= TREE_GROWN) continue;
            auto it = data.trees.find(t.type);
            float chance = it != data.trees.end() ? it->second.growthChance : 0.2f;
            if ((rand32() % 1000) < (uint32_t)(chance * 1000)) {
                t.stage++;
                if (t.stage >= TREE_GROWN) t.health = TREE_HEALTH;
            }
        }
    }
}

} // namespace sv
