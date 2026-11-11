// Skills, crafting, placed machines/chests/sprinklers.
#include <algorithm>
#include <cmath>
#include <cstring>

#include "game.h"

namespace sv {

static const char* SKILL_NAMES[SK_COUNT] = {"Farming", "Fishing", "Foraging", "Mining", "Combat"};
static const int LEVEL_XP[10] = {100, 380, 770, 1300, 2150, 3300, 4800, 6900, 10000, 15000};

int Game::xpForLevel(int level) { return level < 1 ? 0 : LEVEL_XP[std::min(level, 10) - 1]; }

int Game::xpLevel(int skill) const {
    if (skill < 0 || skill >= SK_COUNT) return 0;
    int l = 0;
    while (l < 10 && skillXp[skill] >= LEVEL_XP[l]) l++;
    return l;
}

int Game::skillLevel(int skill) const {
    if (skill < 0 || skill >= SK_COUNT) return 0;
    return skillLevels[skill];
}

// XP counts up during the day; the level itself only goes up overnight,
// like the game, and that's when new recipes unlock.
void Game::gainXp(int skill, int xp) {
    if (skill < 0 || skill >= SK_COUNT || xp <= 0) return;
    skillXp[skill] += xp;
}

void Game::applyLevelUps() {
    for (int sk = 0; sk < SK_COUNT; sk++) {
        int target = xpLevel(sk);
        for (int l = skillLevels[sk] + 1; l <= target; l++) levelUps.push_back({sk, l});
        skillLevels[sk] = std::max(skillLevels[sk], target);
    }
}

std::vector<int> Game::professionChoices(int skill, int level) const {
    std::vector<int> out;
    if (level == 5) {
        out = {skill * 6, skill * 6 + 1};
    } else if (level == 10) {
        // the pair that follows whichever level 5 profession you took
        int base = has(skill * 6 + 1) ? skill * 6 + 4 : skill * 6 + 2;
        out = {base, base + 1};
    }
    for (int id : out)
        if (has(id)) return {};   // already chosen
    return out;
}

void Game::chooseProfession(int id) {
    if (id < 0 || id >= 30) return;
    professions.insert(id);
    const ProfessionDef* p = data.profession(id);
    if (p && (id == PR_FISHER || id == PR_ANGLER)) {}   // effects are checked where they apply
}

// ---------------------------------------------------------------- crafting

static int skillIndex(const std::string& name) {
    for (int i = 0; i < SK_COUNT; i++)
        if (strcasecmp(name.c_str(), SKILL_NAMES[i]) == 0) return i;
    if (strcasecmp(name.c_str(), "Luck") == 0) return -1;
    return -2;
}

bool Game::recipeKnown(const Recipe& r) const {
    if (mailReceived.count("craft:" + r.name)) return true;
    const std::string& u = r.unlock;
    if (u == "default") return true;
    char kind = u.empty() ? 0 : u[0];
    if (kind == 's') {   // s <Skill> <level>
        char name[32] = {0};
        int lvl = 0;
        if (sscanf(u.c_str(), "s %31s %d", name, &lvl) == 2) {
            int si = skillIndex(name);
            return si >= 0 && skillLevel(si) >= lvl;
        }
    } else if (kind == 'l') {   // l <n>: overall progress, we use total skill levels
        int lvl = atoi(u.c_str() + 1), total = 0;
        for (int i = 0; i < SK_COUNT; i++) total += skillLevel(i);
        return total >= lvl;
    } else if (kind == 'f') {   // f <npc> <hearts>
        char name[64] = {0};
        int hearts = 0;
        if (sscanf(u.c_str(), "f %63s %d", name, &hearts) == 2) {
            const NpcState* n = npcByName(name);
            return n && n->hearts() >= hearts;
        }
    }
    return false;   // "null": learned from mail, shops or events
}

int Game::countOf(const Ingredient& in) const {
    int n = 0;
    for (auto& s : inv) {
        if (s.empty() || s.type == SLOT_TOOL) continue;
        if (in.big) {
            if (s.type == SLOT_BIG && s.id == in.id) n += s.count;
        } else if (s.type == SLOT_OBJECT) {
            if (in.id >= 0 ? s.id == in.id : (data.object(s.id) && data.object(s.id)->category == in.id))
                n += s.count;
        }
    }
    return n;
}

bool Game::canCraft(const Recipe& r) const {
    for (auto& in : r.ingredients)
        if (countOf(in) < in.count) return false;
    return true;
}

bool Game::craft(const Recipe& r) {
    if (!recipeKnown(r) || !canCraft(r)) return false;
    Slot out;
    out.type = r.outBig ? SLOT_BIG : SLOT_OBJECT;
    out.id = r.outId;
    out.count = r.outCount;
    // room check: a free slot or a stack to join
    bool room = false;
    for (auto& s : inv)
        if (s.empty() || (s.stacksWith(out) && s.count + out.count <= 999)) room = true;
    if (!room) {
        say("Inventory full");
        return false;
    }
    for (auto& in : r.ingredients) {
        int need = in.count;
        for (auto& s : inv) {
            if (!need || s.empty() || s.type == SLOT_TOOL) continue;
            bool match = in.big ? (s.type == SLOT_BIG && s.id == in.id)
                                : (s.type == SLOT_OBJECT &&
                                   (in.id >= 0 ? s.id == in.id
                                               : (data.object(s.id) && data.object(s.id)->category == in.id)));
            if (!match) continue;
            int take = std::min(need, s.count);
            s.count -= take;
            need -= take;
            if (!s.count) s = Slot();
        }
    }
    addSlot(out);
    say("Crafted " + itemName(out));
    questCrafted(r.outBig, r.outId);
    sfx("coin");
    return true;
}

// ---------------------------------------------------------------- items

static bool startsWith(const std::string& s, const char* p) { return s.compare(0, strlen(p), p) == 0; }

std::string Game::itemName(const Slot& s) const {
    if (s.type == SLOT_TOOL) {
        const ToolDef* t = data.tool(s.id);
        return t ? t->name : "Tool";
    }
    if (s.type == SLOT_BIG) {
        const BigDef* b = data.big(s.id);
        return b ? b->name : "Item";
    }
    const ObjectDef* o = data.object(s.id);
    std::string base = o ? o->name : "Item";
    if (s.flavor < 0) return base;
    const ObjectDef* f = data.object(s.flavor);
    std::string src = f ? f->name : "";
    if (src.empty()) return base;
    if (startsWith(base, "Pickles")) return "Pickled " + src;
    if (base == "Aged Roe") return "Aged " + src + " Roe";
    if (base == "Roe") return src + " Roe";
    if (startsWith(base, "Smoked") || startsWith(base, "Dried")) {
        size_t sp = base.find(' ');
        return base.substr(0, sp) + " " + src;
    }
    return src + " " + base;
}

int Game::itemPrice(const Slot& s) const {
    if (s.type == SLOT_BIG) {
        const BigDef* b = data.big(s.id);
        return b ? b->price : 0;
    }
    if (s.type != SLOT_OBJECT) return 0;
    const ObjectDef* o = data.object(s.id);
    if (!o) return 0;
    int base = itemBasePrice(s);
    // profession bonuses on what you sell
    float k = 1.0f;
    int cat = o->category;
    if (has(PR_TILLER) && (cat == -75 || cat == -79 || cat == -80)) k *= 1.1f;
    if (has(PR_ARTISAN) && cat == -26) k *= 1.4f;
    if (cat == -4) k *= has(PR_ANGLER) ? 1.5f : (has(PR_FISHER) ? 1.25f : 1.0f);
    if (has(PR_BLACKSMITH) && (s.id >= 334 && s.id <= 337)) k *= 1.5f;
    if (has(PR_GEMOLOGIST) && (cat == -2 || cat == -12)) k *= 1.3f;
    return (int)(base * k);
}

int Game::itemBasePrice(const Slot& s) const {
    const ObjectDef* o = data.object(s.id);
    if (!o) return 0;
    int p = std::max(0, o->price);
    const ObjectDef* f = s.flavor >= 0 ? data.object(s.flavor) : nullptr;
    if (!f) return p;
    int b = std::max(0, f->price);
    const std::string& n = o->name;
    if (n == "Wine") return b * 3;
    if (n == "Juice") return (int)(b * 2.25f);
    if (n == "Jelly" || startsWith(n, "Pickles")) return b * 2 + 50;
    if (n == "Roe") return 30 + b / 2;
    if (n == "Aged Roe") return (30 + b / 2) * 2;
    if (startsWith(n, "Smoked")) return b * 2;
    if (startsWith(n, "Dried")) return (int)(b * 7.5f) + 25;
    if (n == "Honey") return p + b * 2;
    return std::max(p, b);
}

bool Game::addSlot(Slot add) {
    if (add.empty() || add.count <= 0) return true;
    if (add.type == SLOT_TOOL) {
        for (auto& s : inv)
            if (s.empty()) { s = add; return true; }
        say("Inventory full");
        return false;
    }
    // check there's room for all of it first
    int room = 0;
    for (auto& s : inv) {
        if (s.empty()) room += 999;
        else if (s.stacksWith(add)) room += 999 - s.count;
    }
    if (room < add.count) {
        say("Inventory full");
        return false;
    }
    for (auto& s : inv)
        if (s.stacksWith(add) && s.count < 999) {
            int n = std::min(999 - s.count, add.count);
            s.count += n;
            add.count -= n;
            if (!add.count) return true;
        }
    for (auto& s : inv)
        if (s.empty()) {
            s = add;
            return true;
        }
    return true;
}

// ---------------------------------------------------------------- placing

static const std::vector<Placed> NONE_PLACED;

const std::vector<Placed>& Game::placedHere() const {
    auto it = placed.find(cur->name);
    return it == placed.end() ? NONE_PLACED : it->second;
}

Placed* Game::placedAt(int x, int y) {
    auto it = placed.find(cur->name);
    if (it == placed.end()) return nullptr;
    for (auto& p : it->second)
        if (p.x == x && p.y == y) return &p;
    return nullptr;
}

const Placed* Game::placedAt(int x, int y) const {
    for (auto& p : placedHere())
        if (p.x == x && p.y == y) return &p;
    return nullptr;
}

bool Game::isChest(const Placed& p) const {
    if (!p.big) return false;
    const BigDef* b = data.big(p.id);
    return b && b->name.find("Chest") != std::string::npos && !data.isMachine(p.id);
}

static int sprinklerRadius(const std::string& name) {
    if (name == "Sprinkler") return 1;           // + shape
    if (name == "Quality Sprinkler") return 2;   // 3x3
    if (name == "Iridium Sprinkler") return 3;   // 5x5
    return 0;
}

bool Game::placeSelected(int tx, int ty) {
    Slot& sl = inv[selected];
    bool big = sl.type == SLOT_BIG;
    if (!big) {
        const ObjectDef* o = sl.type == SLOT_OBJECT ? data.object(sl.id) : nullptr;
        if (!o || !sprinklerRadius(o->name)) return false;
    }
    const TileMap& m = cur->map;
    if (!m.inBounds(tx, ty)) return false;
    size_t idx = (size_t)ty * m.width() + tx;
    if (tileSolid(tx, ty) || placedAt(tx, ty) || objectAt(tx, ty) || (m.flags(tx, ty) & CF_WATER) ||
        cur->debris[idx] || (cur->isFarm && cur->soils[idx].seed >= 0) || npcAt(tx, ty)) {
        say("Can't place that here");
        return true;
    }
    // not on the player's own tile
    if ((int)(px / 16) == tx && (int)((py - 4) / 16) == ty) return true;
    Placed p;
    p.x = tx;
    p.y = ty;
    p.big = big;
    p.id = sl.id;
    if (cur->isFarm && cur->soils[idx].tilled) cur->soils[idx] = Soil();
    if (isChest(p)) p.items.resize(CHEST_SIZE);
    placed[cur->name].push_back(p);
    if (--sl.count <= 0) sl = Slot();
    Placed& np = placed[cur->name].back();
    if (big) {   // machines that work on their own start now
        for (size_t i = 0; i < data.machineRules.size(); i++)
            if (data.machineRules[i].machine == np.id && data.machineRules[i].autoRun) {
                startMachine(np, (int)i, -1);
                break;
            }
    }
    return true;
}

void Game::startMachine(Placed& p, int ruleIdx, int input) {
    const MachineRule& r = data.machineRules[ruleIdx];
    p.rule = ruleIdx;
    p.input = input;
    p.held = Slot();
    p.held.type = SLOT_OBJECT;
    p.held.id = r.copyInput ? input : r.output;
    p.held.count = r.outCount;
    if (!r.flavor.empty() && input >= 0) p.held.flavor = input;
    int64_t now = nowMinutes();
    if (r.days > 0) p.readyAt = (int64_t)(absoluteDay() + r.days) * 1440 + DAY_START;
    else if (r.minutes > 0) p.readyAt = now + r.minutes;
    else p.readyAt = (int64_t)(absoluteDay() + 1) * 1440 + DAY_START;   // daily
}

bool Game::machineReady(const Placed& p) const { return p.rule >= 0 && p.readyAt >= 0 && p.readyAt <= nowMinutes(); }

float Game::machineProgress(const Placed& p) const {
    if (p.rule < 0 || p.readyAt < 0) return 0;
    const MachineRule& r = data.machineRules[p.rule];
    int64_t total = r.days > 0 ? (int64_t)r.days * 1440 : (r.minutes > 0 ? r.minutes : 1440);
    int64_t left = p.readyAt - nowMinutes();
    return std::max(0.0f, std::min(1.0f, 1.0f - (float)left / (float)total));
}

bool Game::interactPlaced(Placed& p) {
    if (isChest(p)) {
        chestOpen = true;
        chestX = p.x;
        chestY = p.y;
        return true;
    }
    if (!p.big) return false;
    if (machineReady(p)) {
        Slot out = p.held;
        if (!addSlot(out)) return true;
        say("+" + std::to_string(out.count) + " " + itemName(out));
        questItem(out.id, out.count);
        const MachineRule& r = data.machineRules[p.rule];
        p.rule = -1;
        p.held = Slot();
        p.readyAt = -1;
        if (r.autoRun) startMachine(p, (int)(&r - &data.machineRules[0]), -1);
        return true;
    }
    if (p.rule >= 0) {
        const MachineRule& r = data.machineRules[p.rule];
        int64_t left = p.readyAt - nowMinutes();
        std::string when = left >= 1440 ? std::to_string((left + 1439) / 1440) + " days"
                                        : (left >= 60 ? std::to_string((left + 59) / 60) + " hours"
                                                      : std::to_string(std::max<int64_t>(left, 10)) + " minutes");
        say(itemName(p.held) + (r.autoRun ? "" : " - ready in ") + (r.autoRun ? " soon" : when));
        return true;
    }
    // load it with the selected item
    Slot& sl = inv[selected];
    const BigDef* b = data.big(p.id);
    std::string mname = b ? b->name : "machine";
    if (sl.type == SLOT_TOOL) return false;   // tools act on it (pickaxe picks it up)
    if (sl.type != SLOT_OBJECT) {
        bool any = false;
        for (auto& r : data.machineRules) any |= r.machine == p.id && !r.autoRun;
        if (any) say("Hold something to put in the " + mname);
        return any;
    }
    for (size_t i = 0; i < data.machineRules.size(); i++) {
        const MachineRule& r = data.machineRules[i];
        if (r.machine != p.id || r.autoRun) continue;
        if (std::find(r.inputs.begin(), r.inputs.end(), sl.id) == r.inputs.end()) continue;
        if (sl.count < r.inputCount) {
            say("Needs " + std::to_string(r.inputCount) + " " + itemName(sl));
            return true;
        }
        if (r.fuel >= 0) {
            Ingredient fuel{r.fuel, r.fuelCount, false};
            if (countOf(fuel) < r.fuelCount) {
                const ObjectDef* fo = data.object(r.fuel);
                say("Needs " + std::to_string(r.fuelCount) + " " + (fo ? fo->name : std::string("fuel")));
                return true;
            }
        }
        int input = sl.id;
        sl.count -= r.inputCount;
        if (sl.count <= 0) sl = Slot();
        if (r.fuel >= 0) {
            int need = r.fuelCount;
            for (auto& s : inv)
                if (need && s.type == SLOT_OBJECT && s.id == r.fuel) {
                    int t = std::min(need, s.count);
                    s.count -= t;
                    need -= t;
                    if (!s.count) s = Slot();
                }
        }
        startMachine(p, (int)i, input);
        return true;
    }
    return false;   // not something this machine takes: fall through (e.g. eat)
}

bool Game::pickUpPlaced(int tx, int ty) {
    auto it = placed.find(cur->name);
    if (it == placed.end()) return false;
    auto& v = it->second;
    for (size_t i = 0; i < v.size(); i++) {
        Placed& p = v[i];
        if (p.x != tx || p.y != ty) continue;
        if (isChest(p)) {
            for (auto& s : p.items)
                if (!s.empty()) {
                    say("Empty the chest first");
                    return true;
                }
        }
        Slot item;
        item.type = p.big ? SLOT_BIG : SLOT_OBJECT;
        item.id = p.id;
        item.count = 1;
        Slot back;   // unfinished input comes back; finished output too
        if (machineReady(p)) back = p.held;
        else if (p.rule >= 0 && p.input >= 0) {
            back.type = SLOT_OBJECT;
            back.id = p.input;
            back.count = data.machineRules[p.rule].inputCount;
        }
        if (!addSlot(item)) return true;
        if (!back.empty()) addSlot(back);
        v.erase(v.begin() + i);
        if (chestOpen && chestX == tx && chestY == ty) chestOpen = false;
        return true;
    }
    return false;
}

void Game::sprinklersWater() {
    for (auto& kv : placed) {
        Location* loc = nullptr;
        auto li = locations.find(kv.first);
        if (li == locations.end()) continue;
        loc = li->second.get();
        if (!loc->isFarm) continue;
        int w = loc->map.width(), h = loc->map.height();
        for (auto& p : kv.second) {
            if (p.big) continue;
            const ObjectDef* o = data.object(p.id);
            int r = o ? sprinklerRadius(o->name) : 0;
            if (!r) continue;
            for (int dy = -2; dy <= 2; dy++)
                for (int dx = -2; dx <= 2; dx++) {
                    if (!dx && !dy) continue;
                    bool hit = r == 1 ? (std::abs(dx) + std::abs(dy) == 1)
                                      : (std::abs(dx) <= r - 1 && std::abs(dy) <= r - 1);
                    int x = p.x + dx, y = p.y + dy;
                    if (!hit || x < 0 || y < 0 || x >= w || y >= h) continue;
                    Soil& s = loc->soils[(size_t)y * w + x];
                    if (s.tilled) s.watered = true;
                }
        }
    }
}

void Game::machinesDayStart() {
    // auto machines whose output was taken restart; nothing else to do because
    // timers are absolute minutes and keep running overnight
    for (auto& kv : placed)
        for (auto& p : kv.second)
            if (p.big && p.rule < 0)
                for (size_t i = 0; i < data.machineRules.size(); i++)
                    if (data.machineRules[i].machine == p.id && data.machineRules[i].autoRun) {
                        startMachine(p, (int)i, -1);
                        break;
                    }
}

// ---------------------------------------------------------------- chests

Placed* Game::openChest() {
    if (!chestOpen) return nullptr;
    Placed* p = placedAt(chestX, chestY);
    if (!p || !isChest(*p)) {
        chestOpen = false;
        return nullptr;
    }
    return p;
}

void Game::chestTransfer(bool toChest, int index) {
    Placed* c = openChest();
    if (!c) return;
    if (toChest) {
        if (index < 0 || index >= INV_SIZE) return;
        Slot& s = inv[index];
        if (s.empty() || s.type == SLOT_TOOL) return;
        for (auto& d : c->items)
            if (d.stacksWith(s) && d.count < 999) {
                int n = std::min(999 - d.count, s.count);
                d.count += n;
                s.count -= n;
                if (!s.count) { s = Slot(); return; }
            }
        for (auto& d : c->items)
            if (d.empty()) { d = s; s = Slot(); return; }
        say("Chest is full");
    } else {
        if (index < 0 || index >= (int)c->items.size()) return;
        Slot& s = c->items[index];
        if (s.empty()) return;
        if (addSlot(s)) s = Slot();
    }
}

} // namespace sv
