// The machines that don't run on a simple "put X in, get Y out" rule: bee houses, tappers, lightning
// rods, solar panels, crystalariums, seed makers, crab pots, geode crushers and incubators.
#include "game.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace sv {

static const int TILE = 16;

enum { M_BEE = 10, M_ROD = 9, M_TAPPER = 105, M_HEAVY_TAPPER = 264, M_CRYSTAL = 21, M_SEED = 25, M_SOLAR = 231,
       M_CRUSHER = 182, M_INCUBATOR = 101, M_OSTRICH = 254, M_SLIME = 156, CRAB_POT = 710 };

bool Game::specialMachine(const Placed& p) const {
    if (!p.big) return p.id == CRAB_POT;
    switch (p.id) {
        case M_BEE: case M_ROD: case M_TAPPER: case M_HEAVY_TAPPER: case M_CRYSTAL: case M_SEED: case M_SOLAR:
        case M_CRUSHER: case M_INCUBATOR: case M_OSTRICH: case M_SLIME: return true;
        default: return false;
    }
}

static void hold(Placed& p, int item, int count, int64_t readyAt, int flavor = -1) {
    p.held = Slot();
    p.held.type = SLOT_OBJECT;
    p.held.id = item;
    p.held.count = count;
    p.held.flavor = flavor;
    p.readyAt = readyAt;
    p.rule = -1;
}

// What a tapper next to this tree gives, and how many days it takes.
static bool tapperYield(int treeType, int& item, int& days) {
    switch (treeType) {
        case 1: item = 725; days = 7; return true;    // oak resin
        case 2: item = 724; days = 9; return true;    // maple syrup
        case 3: item = 726; days = 5; return true;    // pine tar
        case 7: item = 420; days = 2; return true;    // mushroom tree
        case 8: item = 92; days = 1; return true;     // mahogany: sap
        default: return false;
    }
}

// Machines that start themselves: called when one is placed, emptied, and every morning.
void Game::specialStart(Placed& p, const std::string& where) {
    if (!p.held.empty()) return;
    int64_t dayStart = (int64_t)absoluteDay() * 1440 + DAY_START;
    auto loc = locations.find(where);
    const Location* L = loc != locations.end() ? loc->second.get() : nullptr;
    if (p.big && p.id == M_BEE) {
        if (season == WINTER || !L || !L->map.outdoors) return;
        int flower = -1;                               // a flower in bloom within five tiles flavours the honey
        int w = L->map.width();
        for (int dy = -5; dy <= 5 && flower < 0; dy++)
            for (int dx = -5; dx <= 5 && flower < 0; dx++) {
                int x = p.x + dx, y = p.y + dy;
                if (!L->map.inBounds(x, y) || std::abs(dx) + std::abs(dy) > 5) continue;
                const Soil& s = L->soils[(size_t)y * w + x];
                const CropDef* c = s.seed >= 0 ? data.cropForSeed(s.seed) : nullptr;
                const ObjectDef* o = c ? data.object(c->harvestId) : nullptr;
                if (c && o && o->category == -80 && s.phase >= c->phaseDays.size()) flower = c->harvestId;
            }
        hold(p, 340, 1, dayStart + 4 * 1440, flower);
    } else if (p.big && (p.id == M_TAPPER || p.id == M_HEAVY_TAPPER)) {
        if (!L) return;
        for (auto& t : L->trees) {
            int item, days;
            if (t.stage < 5 || t.stump || std::abs(t.x - p.x) > 1 || std::abs(t.y - p.y) > 1 || !tapperYield(t.type, item, days)) continue;
            if (!data.object(item)) continue;
            if (p.id == M_HEAVY_TAPPER) days = std::max(1, days / 2);
            hold(p, item, 1, dayStart + (int64_t)days * 1440);
            return;
        }
    } else if (p.big && p.id == M_SOLAR) {
        if (L && L->map.outdoors) hold(p, 787, 1, dayStart + 7 * 1440);
    } else if (!p.big && p.id == CRAB_POT) {
        if (p.input != 685) return;                    // needs bait
        static const int catchIds[] = {715, 716, 717, 718, 719, 720, 721, 722, 723, 372};
        static const int trash[] = {168, 169, 170, 171, 172};
        int item = rand32() % 100 < 80 ? catchIds[rand32() % 10] : trash[rand32() % 5];
        if (!data.object(item)) item = 168;
        p.input = -1;                                  // the bait's used up
        hold(p, item, 1, dayStart + 1440);
    }
}

// Every morning: restart what runs by itself; lightning rods catch last night's storm.
void Game::specialMorning(bool stormed) {
    for (auto& kv : placed)
        for (auto& p : kv.second) {
            if (!specialMachine(p)) continue;
            if (p.big && p.id == M_ROD) {
                if (stormed && p.held.empty() && data.object(787) && rand32() % 100 < 70)
                    hold(p, 787, 1, (int64_t)absoluteDay() * 1440 + DAY_START);
                continue;
            }
            if (p.big && p.id == M_BEE && season == WINTER && !p.held.empty() && p.readyAt > nowMinutes()) p.readyAt += 1440;   // bees rest
            if (p.big && (p.id == M_INCUBATOR || p.id == M_OSTRICH || p.id == M_SLIME)) { incubatorHatch(p, kv.first); continue; }
            specialStart(p, kv.first);
        }
    for (auto& b : buildings) {                        // slimes in a hutch leave slime behind every night
        if (b.type != "Slime Hutch" || b.daysLeft > 0 || b.pending.empty() || !data.object(766)) continue;
        int n = (int)b.pending.size() * 3 + (int)(rand32() % 5);
        bool stacked = false;
        for (auto& it : b.items)
            if (it.type == SLOT_OBJECT && it.id == 766 && it.count < 999) { it.count = std::min(999, it.count + n); stacked = true; }
        if (!stacked) { Slot s; s.type = SLOT_OBJECT; s.id = 766; s.count = n; b.items.push_back(s); }
    }
}

// An egg in the incubator becomes an animal in that coop (or barn) once it's ready and there's room.
void Game::incubatorHatch(Placed& p, const std::string& where) {
    if (p.held.empty() || p.readyAt > nowMinutes()) return;
    FarmBuilding* b = buildingForInterior(where);
    if (!b) return;
    if (p.id == M_SLIME) {                             // a slime egg: one more slime for the hutch
        if (b->type != "Slime Hutch" || b->pending.size() >= 20) return;
        b->pending.push_back(p.held);
        p.held = Slot();
        p.readyAt = -1;
        if (morningNote.empty()) morningNote = "A slime hatched in the hutch.";
        return;
    }
    const char* type = "White Chicken";
    switch (p.held.id) {
        case 180: case 182: type = "Brown Chicken"; break;
        case 442: type = "Duck"; break;
        case 305: type = "Void Chicken"; break;
        case 107: type = "Dinosaur"; break;
        case 928: type = "Golden Chicken"; break;
        case 289: type = "Ostrich"; break;
        default: break;
    }
    const AnimalType* at = data.animalType(type);
    const BuildingType* bt = typeOf(*b);
    if (!at || !bt) { p.held = Slot(); p.readyAt = -1; return; }
    int living = 0;
    for (auto& a : animals) living += a.home == b->id;
    if (living >= bt->maxOcc) return;                  // waits for room
    static const char* NAMES[] = {"Peep", "Nugget", "Pip", "Dot", "Sunny", "Pebble", "Clover", "Biscuit"};
    FarmAnimal a;
    a.type = type;
    a.name = NAMES[rand32() % 8];
    a.home = b->id;
    a.x = -1;
    animals.push_back(a);
    p.held = Slot();
    p.readyAt = -1;
    if (morningNote.empty()) morningNote = "An egg hatched in the night! Say hello to " + a.name + ".";
}

bool Game::specialInteract(Placed& p) {
    const std::string where = cur->name;
    bool ready = !p.held.empty() && p.readyAt >= 0 && p.readyAt <= nowMinutes();
    bool incubator = p.big && (p.id == M_INCUBATOR || p.id == M_OSTRICH || p.id == M_SLIME);
    if (ready && !incubator) {
        Slot out = p.held;
        if (!addSlot(out)) return true;
        say("+" + std::to_string(out.count) + " " + itemName(out));
        questItem(out.id, out.count);
        if (!p.big && p.id == CRAB_POT) gainXp(SK_FISHING, 5);
        int keep = p.big && p.id == M_CRYSTAL ? p.input : -1;
        p.held = Slot();
        p.readyAt = -1;
        if (keep >= 0) {                               // the crystalarium keeps copying
            const ObjectDef* o = data.object(keep);
            int days = o ? std::max(1, std::min(5, o->price / 150)) : 2;
            hold(p, keep, 1, (int64_t)(absoluteDay() + days) * 1440 + DAY_START);
            p.input = keep;
        } else {
            specialStart(p, where);
        }
        return true;
    }
    Slot& sl = inv[selected];
    const ObjectDef* so = sl.type == SLOT_OBJECT ? data.object(sl.id) : nullptr;
    auto waiting = [&](const char* what) {
        int64_t left = p.readyAt - nowMinutes();
        std::string when = left >= 1440 ? std::to_string((left + 1439) / 1440) + " days" : left >= 60 ? std::to_string((left + 59) / 60) + " hours"
                                                                                                    : std::to_string(std::max<int64_t>(left, 10)) + " minutes";
        say(std::string(what) + ": ready in " + when);
    };
    if (!p.big && p.id == CRAB_POT) {
        if (!p.held.empty()) { waiting("Crab pot"); return true; }
        if (so && sl.id == 685) {
            p.input = 685;
            if (--sl.count <= 0) sl = Slot();
            specialStart(p, where);
            sfx("Ship");
            say("Baited. Check it tomorrow.");
        } else say("The crab pot needs bait.");
        return true;
    }
    switch (p.id) {
        case M_BEE:
            if (!p.held.empty()) waiting("Honey");
            else say(season == WINTER ? "The bees are resting until spring." : "Bee houses only work outdoors.");
            return true;
        case M_TAPPER: case M_HEAVY_TAPPER:
            if (!p.held.empty()) waiting(itemName(p.held).c_str());
            else { specialStart(p, where); say(p.held.empty() ? "Put the tapper right beside a full-grown tree." : "The tapper is working."); }
            return true;
        case M_ROD:
            say("A lightning rod. It catches storms and turns them into battery packs.");
            return true;
        case M_SOLAR:
            if (!p.held.empty()) waiting("Battery pack");
            else say("Solar panels only work outdoors.");
            return true;
        case M_CRYSTAL:
            if (!p.held.empty()) {
                if (so && (so->category == -2 || so->category == -12) && sl.id != p.input) {   // swap what it's copying
                    Slot old;
                    old.type = SLOT_OBJECT; old.id = p.input; old.count = 1;
                    p.held = Slot();
                    if (!addSlot(old)) return true;
                } else { waiting(itemName(p.held).c_str()); return true; }
            }
            if (so && (so->category == -2 || so->category == -12)) {
                int id = sl.id;
                if (--sl.count <= 0) sl = Slot();
                int days = std::max(1, std::min(5, so->price / 150));
                hold(p, id, 1, (int64_t)(absoluteDay() + days) * 1440 + DAY_START);
                p.input = id;
                sfx("Ship");
                say("The crystalarium will grow more " + so->name + ".");
            } else say("Put a gem or mineral in the crystalarium.");
            return true;
        case M_SEED: {
            if (!p.held.empty()) { waiting("Seeds"); return true; }
            int seed = -1;
            if (so)
                for (auto& c : data.crops)
                    if (c.second.harvestId == sl.id) { seed = c.first; break; }
            if (seed < 0 || !data.object(seed)) { say("The seed maker takes a crop and gives back its seeds."); return true; }
            if (--sl.count <= 0) sl = Slot();
            hold(p, seed, 1 + (int)(rand32() % 3), nowMinutes() + 20);
            sfx("Ship");
            return true;
        }
        case M_CRUSHER: {
            if (!p.held.empty()) { waiting("Geode"); return true; }
            auto it = so ? data.geodes.find(sl.id) : data.geodes.end();
            if (it == data.geodes.end()) { say("The geode crusher opens geodes. It burns one coal each."); return true; }
            if (countItem(382) < 1) { say("Needs 1 coal."); return true; }
            const GameData::GeodeDef& gd = it->second;
            int item = -1;
            for (int tries = 0; tries < 8 && (item < 0 || !data.object(item)) && !gd.items.empty(); tries++) item = gd.items[rand32() % gd.items.size()];
            if (item < 0 || !data.object(item)) item = 390;
            takeItem(382, 1);
            if (--inv[selected].count <= 0) inv[selected] = Slot();
            hold(p, item, 1, nowMinutes() + 60);
            sfx("hammer");
            return true;
        }
        case M_INCUBATOR: case M_OSTRICH: {
            if (!p.held.empty()) { waiting("The egg"); return true; }
            bool egg = so && (so->category == -5 || sl.id == 107 || sl.id == 289 || sl.id == 928) && sl.id != 430;
            if (!egg) { say("Put an egg in the incubator."); return true; }
            if (!buildingForInterior(where)) { say("Incubators work inside a coop or barn."); return true; }
            int days = sl.id == 107 ? 12 : sl.id == 289 ? 10 : sl.id == 442 ? 6 : 6;
            int id = sl.id;
            if (--sl.count <= 0) sl = Slot();
            hold(p, id, 1, (int64_t)(absoluteDay() + days) * 1440 + DAY_START);
            sfx("Ship");
            say("It'll hatch in about " + std::to_string(days) + " days.");
            return true;
        }
        case M_SLIME: {
            if (!p.held.empty()) { waiting("The slime egg"); return true; }
            bool egg = so && (sl.id == 413 || sl.id == 437 || sl.id == 439 || sl.id == 680 || sl.id == 857);
            if (!egg) { say("Put a slime egg in the incubator."); return true; }
            FarmBuilding* b = buildingForInterior(where);
            if (!b || b->type != "Slime Hutch") { say("Slime incubators work inside a slime hutch."); return true; }
            int id = sl.id;
            if (--sl.count <= 0) sl = Slot();
            hold(p, id, 1, (int64_t)(absoluteDay() + 3) * 1440 + DAY_START);
            sfx("Ship");
            say("It'll hatch in a few days.");
            return true;
        }
        default: return false;
    }
}

// Walking into a slime hutch: the slimes that live there are about, and what they've left is yours.
void Game::slimeHutchEnter() {
    FarmBuilding* b = cur ? buildingForInterior(cur->name) : nullptr;
    if (!b || b->type != "Slime Hutch") return;
    cur->monsters.clear();
    const TileMap& mp = cur->map;
    std::vector<std::pair<int, int>> open;
    for (int y = 2; y < mp.height() - 2; y++)
        for (int x = 1; x < mp.width() - 1; x++)
            if (!tileSolid(x, y) && mp.tile(L_BACK, x, y) != NO_TILE && std::abs(x * TILE - (int)px) + std::abs(y * TILE - (int)py) > 4 * TILE) open.push_back({x, y});
    for (auto& egg : b->pending) {
        if (open.empty()) break;
        auto c = open[rand32() % open.size()];
        Location::Monster mo;
        mo.kind = "Green Slime";
        const MonsterDef* d = data.monster(mo.kind);
        mo.hp = d ? d->health : 20;
        mo.damage = wearing("Slime Charmer Ring") ? 0 : d ? d->damage : 5;
        mo.tint = egg.id == 437 ? 0x64C8FFFFu : egg.id == 439 ? 0xE0483CFFu : egg.id == 680 ? 0xA050E0FFu : egg.id == 857 ? 0xF0A030FFu : 0x60E048FFu;
        mo.x = c.first * TILE + 8.0f;
        mo.y = c.second * TILE + 14.0f;
        cur->monsters.push_back(mo);
    }
    if (!b->items.empty()) collectBuilding(*b, nullptr);
}

} // namespace sv
