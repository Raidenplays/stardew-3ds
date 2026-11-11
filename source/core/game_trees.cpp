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
    const TileMap& m = loc.map;
    for (int y = 0; y < m.height(); y++)
        for (int x = 0; x < m.width(); x++) {
            uint16_t t = m.tile(L_PATHS, x, y);
            if (t == NO_TILE) continue;
            int ts = m.tileset(L_PATHS, x, y);
            if (ts >= (int)m.tilesets.size() || !isPathsSheet(m.tilesets[ts].texture)) continue;
            int type = t == 9 ? 1 : t == 10 ? 2 : t == 11 ? 3 : 0;
            if (!type || !data.trees.count(type)) continue;
            Tree tr;
            tr.x = x;
            tr.y = y;
            tr.type = type;
            loc.trees.push_back(tr);
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
        if (t->stage > 0 && d && d->seed >= 0) addItem(d->seed, 1);
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
        addItem(OBJ_WOOD_T, wood);
        std::string msg = "+" + std::to_string(wood) + " Wood";
        if (has(PR_LUMBERJACK) && rand32() % 4 == 0 && data.object(709)) {
            addItem(709, 1);
            msg += ", +1 Hardwood";
        }
        const TreeDef* d = data.trees.count(t->type) ? &data.trees.at(t->type) : nullptr;
        if (d && d->seed >= 0 && rand32() % 4 != 0) {
            int n = 1 + (int)(rand32() % 2);
            addItem(d->seed, n);
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
    addItem(OBJ_WOOD_T, 1 + (int)(rand32() % 2));
    if (data.object(OBJ_SAP)) addItem(OBJ_SAP, 1);
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
