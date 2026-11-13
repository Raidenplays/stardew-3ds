// Farm buildings (Robin) and farm animals (Marnie): coops, barns, silos, sheds,
// wells; chickens, cows and the rest living inside, fed with hay, giving
// eggs overnight or milk / wool to the pail and shears.
#include <algorithm>
#include "game.h"

#include <cmath>
#include <cstdlib>

namespace sv {

static const int TILE = 16;

FarmBuilding* Game::buildingById(int id) {
    for (auto& b : buildings) if (b.id == id) return &b;
    return nullptr;
}

FarmBuilding* Game::buildingAtTile(int tx, int ty) {
    for (auto& b : buildings) {
        const BuildingType* t = typeOf(b);
        if (t && tx >= b.x && tx < b.x + t->w && ty >= b.y && ty < b.y + t->h) return &b;
    }
    return nullptr;
}

std::string Game::interiorName(const FarmBuilding& b) const {
    const BuildingType* t = typeOf(b);
    return (t ? t->interior : b.type) + "@" + std::to_string(b.id);
}

FarmBuilding* Game::buildingForInterior(const std::string& loc) {
    size_t at = loc.find('@');
    if (at == std::string::npos) return nullptr;
    return buildingById(atoi(loc.c_str() + at + 1));
}

int Game::hayCapacity() const {
    int cap = 0;
    for (auto& b : buildings)
        if (const BuildingType* t = typeOf(b))
            if (b.daysLeft <= 0) cap += t->hay;
    return cap;
}

int Game::countItem(int id) const {
    int n = 0;
    for (auto& s : inv) if (s.type == SLOT_OBJECT && s.id == id) n += s.count;
    return n;
}

void Game::takeItem(int id, int n) {
    for (auto& s : inv) {
        if (n <= 0) break;
        if (s.type != SLOT_OBJECT || s.id != id) continue;
        int k = std::min(n, s.count);
        s.count -= k;
        n -= k;
        if (s.count <= 0) s = Slot();
    }
}

bool Game::canAfford(const BuildingType& t, std::string* why) const {
    if (money < t.cost) { if (why) *why = "Not enough money"; return false; }
    for (auto& m : t.mats)
        if (countItem(m.first) < m.second) {
            const ObjectDef* o = data.object(m.first);
            if (why) *why = "Need " + std::to_string(m.second) + " " + (o ? o->name : "materials");
            return false;
        }
    return true;
}

// "Big Coop" counts as a "Coop" (and a Deluxe as both).
bool Game::buildingSatisfies(const std::string& have, const std::string& need) const {
    std::string t = have;
    for (int guard = 0; guard < 8 && !t.empty(); guard++) {
        if (t == need) return true;
        const BuildingType* bt = data.buildingType(t);
        t = bt ? bt->upgradeFrom : "";
    }
    return false;
}

bool Game::startBuilding(const std::string& type) {
    const BuildingType* t = data.buildingType(type);
    if (!t) return false;
    for (auto& b : buildings)
        if (t->builder != "Wizard" && (b.daysLeft > 0 || !b.upgradingTo.empty())) { say("Robin is already busy with a building."); return false; }
    std::string why;
    if (!canAfford(*t, &why)) { say(why); return false; }
    if (!t->upgradeFrom.empty()) {   // an upgrade: the old building gets it
        for (auto& b : buildings)
            if (b.type == t->upgradeFrom) {
                money -= t->cost;
                for (auto& m : t->mats) takeItem(m.first, m.second);
                b.upgradingTo = type;
                b.daysLeft = std::max(1, t->days);
                sfx("purchase");
                say("Robin will upgrade your " + b.type + " to a " + type + ". It'll take " + std::to_string(b.daysLeft) + " days.");
                return true;
            }
        say("You need a " + t->upgradeFrom + " first.");
        return false;
    }
    // a new building: choose the spot on the farm first (paid when placed)
    placing = type;
    warpTo("Farm", data.layout.doorX, data.layout.doorY + 3, false);
    placeX = data.layout.doorX - t->w / 2;
    placeY = data.layout.doorY + 5;
    say("Pick a spot: move, A to build here, L to cancel.");
    return true;
}

bool Game::placeOk(int x, int y) const {
    const BuildingType* t = data.buildingType(placing);
    if (!t || !farm) return false;
    const TileMap& m = farm->map;
    int ptx = (int)(px / TILE), pty = (int)(py / TILE);
    for (int dy = 0; dy < t->h; dy++)
        for (int dx = 0; dx < t->w; dx++) {
            int tx = x + dx, ty = y + dy;
            if (!m.inBounds(tx, ty) || m.tile(L_BACK, tx, ty) == NO_TILE) return false;
            uint8_t f = m.flags(tx, ty);
            if ((f & CF_WATER) || ((f & CF_BLOCKED) && !(f & CF_PASSABLE))) return false;
            if (m.tile(L_BUILDINGS, tx, ty) != NO_TILE) return false;
            if (tx == ptx && ty == pty) return false;
            if (const_cast<Game*>(this)->tileSolid(tx, ty)) return false;
            if (farm->soils[(size_t)ty * m.width() + tx].tilled) return false;
        }
    return true;
}

void Game::updatePlacing(const Input& in, float dt) {
    const BuildingType* t = data.buildingType(placing);
    if (!t) { placing.clear(); return; }
    placeRepeat -= dt;
    int mx = in.mx > 0.5f ? 1 : in.mx < -0.5f ? -1 : 0, my = in.my > 0.5f ? 1 : in.my < -0.5f ? -1 : 0;
    if ((mx || my) && placeRepeat <= 0) {
        placeX = std::max(0, std::min(farm->map.width() - t->w, placeX + mx));
        placeY = std::max(0, std::min(farm->map.height() - t->h, placeY + my));
        placeRepeat = 0.12f;
    }
    if (!mx && !my) placeRepeat = 0;
    if (in.prev) {
        placing.clear();
        say("Never mind.");
        return;
    }
    if (!in.action) return;
    if (!placeOk(placeX, placeY)) { sfx("cancel"); say("Can't build there."); return; }
    std::string why;
    if (!canAfford(*t, &why)) { say(why); placing.clear(); return; }
    money -= t->cost;
    for (auto& mt : t->mats) takeItem(mt.first, mt.second);
    FarmBuilding b;
    b.id = nextBuildingId++;
    b.type = placing;
    b.x = placeX;
    b.y = placeY;
    b.daysLeft = t->builder == "Wizard" ? 0 : std::max(1, t->days);   // the Wizard's magic is instant
    // clear the grass under it
    for (int dy = 0; dy < t->h; dy++)
        for (int dx = 0; dx < t->w; dx++) farm->grass[(size_t)(placeY + dy) * farm->map.width() + placeX + dx] = 0;
    buildings.push_back(b);
    placing.clear();
    sfx("axe");
    say("Robin will have your " + b.type + " ready in " + std::to_string(b.daysLeft) + (b.daysLeft == 1 ? " day." : " days."));
}

// ------------------------------------------------------------------ animals

int Game::roomFor(const AnimalType& a) const {
    for (auto& b : buildings) {
        const BuildingType* t = typeOf(b);
        if (!t || b.daysLeft > 0 || t->maxOcc <= 0) continue;
        if (t->occupants.find(a.house) == std::string::npos) continue;
        if (!a.requires.empty() && !buildingSatisfies(b.type, a.requires)) continue;
        int n = 0;
        for (auto& an : animals) n += an.home == b.id;
        if (n < t->maxOcc) return b.id;
    }
    return 0;
}

static const char* NAMES[] = {"Daisy", "Clover", "Pip", "Biscuit", "Maple", "Hazel", "Peanut", "Rosie", "Buttercup",
                              "Pepper", "Nugget", "Willow", "Sunny", "Mochi", "Toffee", "Bean", "Poppy", "Juniper",
                              "Waffles", "Marble", "Ginger", "Olive", "Pickles", "Cocoa"};

bool Game::buyAnimal(const std::string& type) {
    const AnimalType* a = data.animalType(type);
    if (!a || a->price <= 0) return false;
    if (money < a->price) { say("Not enough money"); return false; }
    int home = roomFor(*a);
    if (!home) {
        say(std::string("You need a ") + (a->requires.empty() ? a->house : a->requires) + " with room for it.");
        return false;
    }
    money -= a->price;
    FarmAnimal an;
    an.type = type;
    an.name = NAMES[rand32() % (sizeof NAMES / sizeof *NAMES)];
    an.home = home;
    an.x = -1;   // placed when its building is first visited
    animals.push_back(an);
    sfx("purchase");
    if (!a->sound.empty()) sfx(a->sound.c_str());
    say(an.name + " the " + type + " is waiting in your " + buildingById(home)->type + "!");
    return true;
}

bool Game::buyAnimalTool(const std::string& key) {
    int t = data.toolByKey(key);
    if (t < 0) return false;
    for (auto& s : inv) if (s.type == SLOT_TOOL && s.id == t) { say("You already have one."); return false; }
    if (money < 1000) { say("Not enough money"); return false; }
    if (!giveTool(key)) return false;
    money -= 1000;
    sfx("purchase");
    return true;
}

std::vector<std::pair<int, int>> Game::troughTiles(const Location& loc) const {
    std::vector<std::pair<int, int>> out;
    const TileMap& m = loc.map;
    for (int y = 0; y < m.height(); y++)
        for (int x = 0; x < m.width(); x++) {
            if (m.tile(L_BACK, x, y) != 33) continue;   // troughs: Back tile 33 on the coop sheet
            int ts = m.tileset(L_BACK, x, y);
            if (ts < (int)m.tilesets.size() && m.tilesets[ts].texture.find("oop") != std::string::npos) out.push_back({x, y});
        }
    return out;
}

// Somewhere open inside for an animal to stand / an egg to lie.
static bool openSpot(Game& g, const Location& loc, uint32_t r, int& ox, int& oy) {
    const TileMap& m = loc.map;
    for (int tries = 0; tries < 60; tries++) {
        int x = 1 + (int)((r >> 3) % std::max(1, m.width() - 2)), y = 3 + (int)((r >> 13) % std::max(1, m.height() - 5));
        r = r * 1664525u + 1013904223u;
        if (m.tile(L_BACK, x, y) == NO_TILE || m.tile(L_BUILDINGS, x, y) != NO_TILE) continue;
        if (m.flags(x, y) & CF_BLOCKED) continue;
        bool taken = false;
        for (auto& o : loc.objects) taken |= o.x == x && o.y == y;
        if (taken || m.warpAt(x, y)) continue;
        ox = x;
        oy = y;
        return true;
    }
    (void)g;
    return false;
}

// Overnight: construction, eating, growing up, eggs and milk.
// Fish ponds: fish breed up to the pond's cap and leave roe (or something better) in the bucket.
static int pondSpawnDays(const FishPondDef* p, const ObjectDef* o) {
    if (p && p->spawn > 0) return p->spawn;
    int pr = o ? o->price : 100;
    return pr <= 30 ? 1 : pr <= 80 ? 2 : pr <= 120 ? 3 : pr <= 250 ? 4 : 5;
}

void Game::pondsNight() {
    for (auto& b : buildings) {
        if (b.type != "Fish Pond" || b.daysLeft > 0 || b.pondFish < 0 || b.pondPop <= 0) continue;
        const FishPondDef* p = data.fishPond(b.pondFish);
        int cap = p ? p->maxPop : 10;
        if (b.pondPop < cap && ++b.pondDays >= pondSpawnDays(p, data.object(b.pondFish))) {
            b.pondPop++;
            b.pondDays = 0;
        }
        if (b.pondOut >= 0 || !p) continue;
        float ch = p->minChance + (p->maxChance - p->minChance) * (b.pondPop - 1) / 9.0f;
        if ((rand32() % 10000) / 10000.0f >= ch) continue;
        for (auto& it : p->items) {
            if (b.pondPop < it.pop || (rand32() % 10000) / 10000.0f >= it.chance || !data.object(it.id)) continue;
            b.pondOut = it.id;
            b.pondOutN = it.mn + (it.mx > it.mn ? (int)(rand32() % (it.mx - it.mn + 1)) : 0);
            if (it.id == 812) b.pondOutN += b.pondPop / 3;   // bigger ponds, more roe
            break;
        }
    }
}

bool Game::pondInteract(FarmBuilding& b) {
    const ObjectDef* fo = b.pondFish >= 0 ? data.object(b.pondFish) : nullptr;
    if (b.pondOut >= 0) {
        Slot sl;
        sl.type = SLOT_OBJECT;
        sl.id = b.pondOut;
        sl.count = std::max(1, b.pondOutN);
        if (b.pondOut == 812) sl.flavor = b.pondFish;
        if (!addSlot(sl)) { say("No room in your bag."); return true; }
        itemFlies(b.pondOut, (b.x + 4) * TILE, (b.y + 4) * TILE, sl.count);
        sfx("coin");
        b.pondOut = -1;
        b.pondOutN = 0;
        return true;
    }
    Slot& s = inv[selected];
    const ObjectDef* so = s.type == SLOT_OBJECT ? data.object(s.id) : nullptr;
    if (so && so->category == -4) {
        if (!data.fishPond(s.id)) { say("That fish can't live in a pond."); return true; }
        if (b.pondFish >= 0 && b.pondFish != s.id) { say("This pond already has " + (fo ? fo->name : std::string("fish")) + "."); return true; }
        const FishPondDef* p = data.fishPond(s.id);
        if (b.pondPop >= p->maxPop) { say("The pond is full."); return true; }
        b.pondFish = s.id;
        b.pondPop++;
        if (--s.count <= 0) s = Slot();
        sfx("slosh");
        say(std::string("Added a ") + so->name + " to the pond (" + std::to_string(b.pondPop) + "/" + std::to_string(p->maxPop) + ").");
        return true;
    }
    if (!fo) { say("An empty fish pond. Hold a fish and press A to put it in."); return true; }
    const FishPondDef* p = data.fishPond(b.pondFish);
    say(fo->name + " pond: " + std::to_string(b.pondPop) + "/" + std::to_string(p ? p->maxPop : 10) + " fish.");
    return true;
}

// Take everything a building is holding (flour from the mill, the junimos' harvest).
bool Game::collectBuilding(FarmBuilding& b, const char* empty) {
    bool any = false;
    for (auto& s : b.items) {
        if (s.empty()) continue;
        if (!addSlot(s)) break;
        itemFlies(s.id, (b.x + 1) * TILE + 8.0f, (b.y + 1) * TILE + 8.0f, s.count);
        s = Slot();
        any = true;
    }
    b.items.erase(std::remove_if(b.items.begin(), b.items.end(), [](const Slot& s) { return s.empty(); }), b.items.end());
    if (any) sfx("coin");
    else if (empty) say(empty);
    return true;
}

// The mill: wheat -> flour, beets -> 3 sugar, unmilled rice -> rice, ready the next morning.
bool Game::millInteract(FarmBuilding& b) {
    Slot& s = inv[selected];
    int out = -1, per = 1;
    if (s.type == SLOT_OBJECT) {
        if (s.id == 262) out = 246;
        else if (s.id == 284) { out = 245; per = 3; }
        else if (s.id == 271) out = 423;
    }
    if (out >= 0 && data.object(out)) {
        Slot o;
        o.type = SLOT_OBJECT; o.id = out; o.count = s.count * per;
        b.pending.push_back(o);
        say("Milling " + std::to_string(s.count) + " " + itemName(s) + ". It'll be ready tomorrow.");
        s = Slot();
        sfx("Ship");
        return true;
    }
    return collectBuilding(b, b.pending.empty() ? "The mill is empty. Bring wheat, beets or unmilled rice." : "The mill is still grinding. Come back tomorrow.");
}

// Junimos from each hut bring in the ripe crops around it (not in winter, not in the rain).
void Game::junimosHarvest() {
    if (!farm || season == WINTER || raining) return;
    int fw = farm->map.width(), fh = farm->map.height();
    for (auto& b : buildings) {
        if (b.type != "Junimo Hut" || b.daysLeft > 0) continue;
        for (int y = std::max(0, b.y - 7); y <= std::min(fh - 1, b.y + 9); y++)
            for (int x = std::max(0, b.x - 7); x <= std::min(fw - 1, b.x + 9); x++) {
                Soil& s = farm->soils[(size_t)y * fw + x];
                const CropDef* c = s.seed >= 0 ? data.cropForSeed(s.seed) : nullptr;
                if (!c || s.dead || s.phase < c->phaseDays.size() || (s.regrown && s.regrowWait > 0)) continue;
                Slot got;
                got.type = SLOT_OBJECT; got.id = c->harvestId; got.count = std::max(1, c->harvestMin);
                bool stacked = false;
                for (auto& it : b.items)
                    if (it.stacksWith(got) && it.count < 999) { it.count += got.count; stacked = true; break; }
                if (!stacked) {
                    if (b.items.size() >= 36) continue;   // the hut is full
                    b.items.push_back(got);
                }
                if (c->regrowDays > 0) { s.regrowWait = (int8_t)c->regrowDays; s.regrown = true; }
                else { s.seed = -1; s.phase = 0; s.dayInPhase = 0; }
            }
    }
}

void Game::animalsNight() {
    pondsNight();
    junimosHarvest();
    for (auto& b : buildings) {   // the mill finishes overnight
        if (b.type == "Slime Hutch") continue;   // (there, "pending" is the slimes themselves)
        for (auto& p : b.pending) {
            bool stacked = false;
            for (auto& it : b.items)
                if (it.stacksWith(p) && it.count + p.count <= 999) { it.count += p.count; stacked = true; break; }
            if (!stacked) b.items.push_back(p);
        }
        b.pending.clear();
    }
    for (auto& b : buildings) {
        if (b.daysLeft <= 0) continue;
        if (--b.daysLeft > 0) continue;
        if (!b.upgradingTo.empty()) {
            locations.erase(interiorName(b));   // the bigger interior replaces the old one
            b.type = b.upgradingTo;
            b.upgradingTo.clear();
            b.troughs.clear();
        }
        say("Robin finished your " + b.type + "!");
    }
    for (auto& a : animals) {
        const AnimalType* t = data.animalType(a.type);
        FarmBuilding* b = buildingById(a.home);
        if (!t || !b) continue;
        Location* in = loadLocation(interiorName(*b));
        // eat: hay from a trough
        a.fed = false;
        if (in) {
            auto tr = troughTiles(*in);
            b->troughs.resize(tr.size());
            if (b->type.rfind("Deluxe", 0) == 0)   // deluxe buildings fill their own troughs from the silos
                for (auto& h : b->troughs)
                    if (!h && hay > 0) { hay--; h = 1; }
            for (size_t i = 0; i < b->troughs.size(); i++)
                if (b->troughs[i]) { b->troughs[i] = 0; a.fed = true; break; }
        }
        a.friendship = std::max(0, std::min(1000, a.friendship + (a.petted ? 0 : -10) + (a.fed ? 0 : -20)));
        a.petted = false;
        a.age++;
        if (!a.fed || a.age < t->daysToMature || t->produce.empty()) continue;
        if (++a.daysSinceProduce < t->daysToProduce) continue;
        a.daysSinceProduce = 0;
        bool deluxe = !t->deluxe.empty() && (int)(rand32() % 1200) < a.friendship / 2;
        int item = deluxe ? t->deluxe[rand32() % t->deluxe.size()] : t->produce[rand32() % t->produce.size()];
        if (!data.object(item)) continue;
        if (t->harvestType == "DropOvernight" || t->harvestType == "DigUp") {
            int ox, oy;
            if (in && openSpot(*this, *in, rand32(), ox, oy)) {
                WorldObject o;
                o.x = ox;
                o.y = oy;
                o.itemId = item;
                in->objects.push_back(o);
            }
        } else {
            a.ready = item;
        }
    }
}

void Game::animalsUpdate(float dt) {
    if (!cur) return;
    FarmBuilding* b = buildingForInterior(cur->name);
    if (!b) return;
    for (auto& a : animals) {
        if (a.home != b->id) continue;
        if (a.heart > 0) a.heart -= dt;
        if (a.x < 0) {   // just arrived: somewhere to stand
            int ox, oy;
            if (!openSpot(*this, *cur, rand32(), ox, oy)) { ox = cur->map.width() / 2; oy = cur->map.height() / 2; }
            a.x = ox * TILE + 8.0f;
            a.y = oy * TILE + 14.0f;
        }
        a.think -= dt;
        if (a.think <= 0) {
            if (timeOfDay < 18 * 60 && rand32() % 3 == 0) {
                static const float d[4][2] = {{0, 1}, {1, 0}, {0, -1}, {-1, 0}};
                a.facing = (int)(rand32() % 4);
                a.vx = d[a.facing][0] * 14;
                a.vy = d[a.facing][1] * 14;
            } else {
                a.vx = a.vy = 0;
            }
            a.think = 1.0f + (rand32() % 200) / 100.0f;
        }
        if (a.vx || a.vy) {
            a.anim += dt;
            float nx = a.x + a.vx * dt, ny = a.y + a.vy * dt;
            int tx = (int)std::floor((nx + (a.vx > 0 ? 6 : a.vx < 0 ? -6 : 0)) / TILE);
            int ty = (int)std::floor((ny + (a.vy > 0 ? 1 : -4)) / TILE);
            bool hitFarmer = std::fabs(nx - px) < 12 && std::fabs(ny - py) < 10;
            if (!tileSolid(tx, ty) && !cur->map.warpAt(tx, ty) && !hitFarmer) { a.x = nx; a.y = ny; }
            else { a.vx = a.vy = 0; }
        }
    }
}

// A on an animal: pet it, or milk / shear it with the right tool in hand.
bool Game::animalInteract(int tx, int ty) {
    FarmBuilding* b = cur ? buildingForInterior(cur->name) : nullptr;
    if (!b) return false;
    float cx = tx * TILE + 8.0f, cy = ty * TILE + 8.0f;
    for (auto& a : animals) {
        if (a.home != b->id || a.x < 0) continue;
        const AnimalType* t = data.animalType(a.type);
        float r = t && t->w > 16 ? 18.0f : 11.0f;
        if (std::fabs(a.x - cx) > r || std::fabs((a.y - 8) - cy) > r) continue;
        const ToolDef* held = inv[selected].type == SLOT_TOOL ? data.tool(inv[selected].id) : nullptr;
        if (held && t && a.ready >= 0 && !t->harvestTool.empty() && held->name == t->harvestTool) {
            if (addItem(a.ready, 1)) {
                itemFlies(a.ready, a.x, a.y - 16);
                sfx(t->harvestTool == "Shears" ? "scissors" : "Milking");
                a.ready = -1;
                a.friendship = std::min(1000, a.friendship + 5);
                gainXp(SK_FARMING, 5);
            }
            return true;
        }
        if (!a.petted) {
            a.petted = true;
            a.friendship = std::min(1000, a.friendship + 15);
            gainXp(SK_FARMING, 5);
        }
        a.heart = 1.2f;
        a.vx = a.vy = 0;
        a.think = 1.5f;
        if (t && !t->sound.empty()) sfx(t->sound.c_str());
        int hearts = a.friendship / 200;
        std::string h;
        for (int i = 0; i < 5; i++) h += i < hearts ? "<3 " : "- ";
        std::string extra;
        if (a.ready >= 0 && t && !t->harvestTool.empty()) extra = "  (ready: use the " + t->harvestTool + ")";
        say(a.name + " the " + a.type + "  " + h + extra);
        return true;
    }
    return false;
}

// The feed hopper: hay in hand goes into the silos; otherwise fill the troughs.
bool Game::hopperInteract(int tx, int ty) {
    FarmBuilding* b = cur ? buildingForInterior(cur->name) : nullptr;
    const BuildingType* t = b ? typeOf(*b) : nullptr;
    if (!t || t->hopperX < 0) return false;
    if (tx != t->hopperX || (ty != t->hopperY && ty != t->hopperY - 1)) return false;
    Slot& s = inv[selected];
    if (s.type == SLOT_OBJECT && s.id == 178) {
        int room = hayCapacity() - hay;
        if (room <= 0) { say(hayCapacity() ? "Your silos are full." : "Build a silo to store hay."); return true; }
        int n = std::min(room, s.count);
        hay += n;
        s.count -= n;
        if (s.count <= 0) s = Slot();
        sfx("Ship");
        say("Stored " + std::to_string(n) + " hay. Silos: " + std::to_string(hay) + "/" + std::to_string(hayCapacity()));
        return true;
    }
    auto tr = troughTiles(*cur);
    b->troughs.resize(tr.size());
    int filled = 0;
    for (auto& h : b->troughs) {
        if (h) continue;
        if (hay > 0) { hay--; h = 1; filled++; }
        else if (countItem(178) > 0) { takeItem(178, 1); h = 1; filled++; }
    }
    if (filled) { sfx("shwip"); say("Put " + std::to_string(filled) + " hay in the troughs. Silos: " + std::to_string(hay)); }
    else say(tr.empty() ? "No troughs here." : hay == 0 && countItem(178) == 0 ? "No hay! Cut grass with a silo built, or buy hay from Marnie." : "The troughs are full.");
    return true;
}

// On the farm: walk into a building's door to go in.
bool Game::farmBuildingInteract(int tx, int ty) {
    if (!cur || !cur->isFarm) return false;
    FarmBuilding* b = buildingAtTile(tx, ty);
    if (!b) return false;
    const BuildingType* t = typeOf(*b);
    if (b->daysLeft > 0 && b->upgradingTo.empty()) {
        say("Under construction: " + std::to_string(b->daysLeft) + (b->daysLeft == 1 ? " day left." : " days left."));
        return true;
    }
    if (t && b->type == "Well") {
        for (auto& s : inv)
            if (s.type == SLOT_TOOL)
                if (const ToolDef* td = data.tool(s.id))
                    if (td->kind == TOOL_CAN) { water = canCapacity(); sfx("slosh"); say("Filled the watering can."); return true; }
        return true;
    }
    if (t && b->type == "Greenhouse" && !greenhouseFixed()) {
        say("The old greenhouse is in ruins. Fixing up the Community Center's pantry would restore it.");
        return true;
    }
    if (t && b->type == "Fish Pond") return pondInteract(*b);
    if (t && b->type == "Mill") return millInteract(*b);
    if (t && b->type == "Junimo Hut") return collectBuilding(*b, "The junimos haven't gathered anything yet.");
    if (t && b->type == "Gold Clock") { say("The Gold Clock. Time leaves your farm alone."); return true; }
    if (t && b->type == "Pet Bowl") return petBowlInteract();
    if (t && b->type.size() > 8 && b->type.compare(b->type.size() - 7, 7, "Obelisk") == 0) {
        struct { const char* type; const char* map; int x, y; } to[] = {
            {"Earth Obelisk", "Mountain", 31, 20}, {"Water Obelisk", "Beach", 20, 4},
            {"Desert Obelisk", "Desert", 35, 43}, {"Island Obelisk", "IslandSouth", 11, 11}};
        for (auto& o : to)
            if (b->type == o.type) {
                if (!hasMap(o.map)) { say("The obelisk hums, but that place wasn't converted."); return true; }
                sfx("wand");
                warpTo(o.map, o.x, o.y);
                return true;
            }
        return true;
    }
    if (t && b->type == "Silo") {
        say("Hay in your silos: " + std::to_string(hay) + "/" + std::to_string(hayCapacity()));
        return true;
    }
    if (t && !t->interior.empty() && tx == b->x + t->doorX && ty == b->y + t->doorY) {
        std::string in = interiorName(*b);
        if (!hasMap(t->interior)) { say("That building's map wasn't converted."); return true; }
        facing = FACE_UP;
        enterFrom(in, true);
        return true;
    }
    return false;
}

// Robin's house upgrades: a kitchen (10,000g + 450 wood), then a nursery (65,000g + 100 hardwood).
bool Game::startHouseUpgrade() {
    if (houseLevel >= 3) { say("Your house is as big as it gets."); return false; }
    if (houseUpgradeDays > 0) { say("Robin is already working on your house."); return false; }
    for (auto& b : buildings)
        if (b.daysLeft > 0) { say("Robin is already busy with a building."); return false; }
    int cost = houseLevel == 0 ? 10000 : houseLevel == 1 ? 65000 : 100000, mat = houseLevel == 0 ? 388 : 709,
        n = houseLevel == 0 ? 450 : houseLevel == 1 ? 100 : 0;   // the cellar is gold only
    if (money < cost) { say("Not enough money"); return false; }
    if (countItem(mat) < n) {
        const ObjectDef* o = data.object(mat);
        say("Need " + std::to_string(n) + " " + (o ? o->name : "materials"));
        return false;
    }
    money -= cost;
    takeItem(mat, n);
    houseUpgradeDays = 3;
    sfx("purchase");
    say("Robin will have your house upgraded in 3 days.");
    return true;
}

// The horse appears by a finished stable each morning.
void Game::horseMorning() {
    horseLoc.clear();
    for (auto& b : buildings)
        if (b.type == "Stable" && b.daysLeft <= 0) {
            horseLoc = "Farm";
            horseX = (b.x + 1) * TILE + 8.0f;
            horseY = (b.y + 2) * TILE + 14.0f;
            horseFacing = FACE_DOWN;
            return;
        }
}

bool Game::horseInteract(int tx, int ty) {
    if (horseLoc != cur->name || riding) return false;
    float cx = tx * TILE + 8.0f, cy = ty * TILE + 8.0f;
    if (std::fabs(horseX - cx) > 20 || std::fabs(horseY - 8 - cy) > 16) return false;
    riding = true;
    px = horseX;
    py = horseY;
    sfx("dwop");
    return true;
}

// Every farm has a greenhouse: in ruins until the Community Center pantry is
// done. It stands where the farm's map says (an imported farm brings its own).
void Game::ensureGreenhouse() {
    const BuildingType* t = data.buildingType("Greenhouse");
    if (!t || !farm) return;
    for (auto& b : buildings) if (b.type == "Greenhouse") return;
    FarmBuilding b;
    b.id = nextBuildingId++;
    b.type = "Greenhouse";
    const FarmType& ft = data.farmTypes[me.farmType >= 0 && me.farmType < (int)data.farmTypes.size() ? me.farmType : 0];
    b.x = ft.ghX;   // the map's GreenhouseLocation (25,10 on the farms that don't say)
    b.y = ft.ghY;
    int w = farm->map.width();
    for (int dy = 0; dy < t->h; dy++)
        for (int dx = 0; dx < t->w; dx++)
            if (farm->map.inBounds(b.x + dx, b.y + dy)) {
                size_t i = (size_t)(b.y + dy) * w + b.x + dx;
                farm->debris[i] = DEB_NONE;
                farm->grass[i] = 0;
            }
    for (size_t i = farm->trees.size(); i-- > 0;)
        if (farm->trees[i].x >= b.x && farm->trees[i].x < b.x + t->w && farm->trees[i].y >= b.y && farm->trees[i].y < b.y + t->h)
            farm->trees.erase(farm->trees.begin() + i);
    buildings.push_back(b);
}

void Game::startingCoop() {
    const BuildingType* t = data.buildingType("Coop");
    if (!t || !farm) return;
    FarmBuilding b;
    b.id = nextBuildingId++;
    b.type = "Coop";
    b.x = 54;   // where a new Meadowlands save has it
    b.y = 9;
    int w = farm->map.width();
    for (int dy = -1; dy <= t->h; dy++)
        for (int dx = 0; dx < t->w; dx++)
            if (farm->map.inBounds(b.x + dx, b.y + dy)) {
                size_t i = (size_t)(b.y + dy) * w + b.x + dx;
                farm->debris[i] = DEB_NONE;
                farm->grass[i] = 0;
            }
    for (size_t i = farm->trees.size(); i-- > 0;)
        if (farm->trees[i].x >= b.x && farm->trees[i].x < b.x + t->w && farm->trees[i].y >= b.y - 1 && farm->trees[i].y <= b.y + t->h)
            farm->trees.erase(farm->trees.begin() + i);
    buildings.push_back(b);
    static const char* kinds[2] = {"White Chicken", "Brown Chicken"};
    static const char* names[2] = {"Daisy", "Clover"};
    for (int i = 0; i < 2; i++) {
        if (!data.animalType(kinds[i])) continue;
        FarmAnimal an;
        an.type = kinds[i];
        an.name = names[i];
        an.home = b.id;
        an.x = -1;
        animals.push_back(an);
    }
}

// ---------------------------------------------------------------- things that happen in the night
// Rare farm events while you sleep (chances as the wiki lists them): the crop fairy, the witch over
// the coop, a meteorite, the strange capsule, a stone owl. The railroad earthquake on Summer 3, year 1.

bool Game::farmSpotFree(int x, int y, int w, int h) const {
    if (!farm) return false;
    const TileMap& m = farm->map;
    for (int ty = y; ty < y + h; ty++)
        for (int tx = x; tx < x + w; tx++) {
            if (!m.inBounds(tx, ty) || m.tile(L_BACK, tx, ty) == NO_TILE || m.tile(L_BUILDINGS, tx, ty) != NO_TILE) return false;
            uint8_t f = m.flags(tx, ty);
            if ((f & CF_WATER) || (f & CF_BLOCKED) || m.warpAt(tx, ty)) return false;
            size_t i = (size_t)ty * m.width() + tx;
            if (farm->soils[i].tilled || (i < farm->debris.size() && farm->debris[i] != DEB_NONE)) return false;
            for (auto& t : farm->trees) if (t.x == tx && t.y == ty) return false;
            for (auto& t : farm->fruitTrees) if (t.x == tx && t.y == ty) return false;
            for (auto& c : farm->clumps) if (tx >= c.x && tx < c.x + 2 && ty >= c.y && ty < c.y + 2) return false;
            for (auto& b : buildings)
                if (const BuildingType* bt = typeOf(b))
                    if (tx >= b.x && tx < b.x + bt->w && ty >= b.y - 1 && ty < b.y + bt->h + 1) return false;
            for (auto& b : data.layout.buildings)
                if (tx >= b.tileX - 1 && tx <= b.tileX + b.footW && ty >= b.tileY - 1 && ty <= b.tileY + b.footH + 1) return false;
            auto it = placed.find("Farm");
            if (it != placed.end())
                for (auto& p : it->second) if (p.x == tx && p.y == ty) return false;
        }
    return true;
}

bool Game::randomFarmSpot(int w, int h, int& ox, int& oy) {
    if (!farm) return false;
    for (int tries = 0; tries < 300; tries++) {
        int x = 3 + (int)(rand32() % (uint32_t)std::max(1, farm->map.width() - 6));
        int y = 8 + (int)(rand32() % (uint32_t)std::max(1, farm->map.height() - 12));
        if (farmSpotFree(x - 1, y - 1, w + 2, h + 2)) { ox = x; oy = y; return true; }
    }
    return false;
}

void Game::nightEvents() {
    if (!farm) return;
    // the strange capsule cracks open three nights after it lands
    if (mailReceived.count("Got_Capsule") && !mailReceived.count("s3d_capsuleOpen")) {
        int n = 1;
        while (n <= 3 && mailReceived.count("s3d_capsuleNight" + std::to_string(n))) n++;
        if (n <= 3) mailReceived.insert("s3d_capsuleNight" + std::to_string(n));
        else {
            mailReceived.insert("s3d_capsuleOpen");
            for (auto& kv : placed)
                for (auto& p : kv.second)
                    if (p.big && p.id == 96 && data.big(98)) p.id = 98;   // empty now... whatever was inside is gone
        }
    }
    if (year == 1 && season == SUMMER && day == 3 && !mailReceived.count("s3d_earthquake")) {
        mailReceived.insert("s3d_earthquake");
        morningNote = "There was an earthquake in the night... the way to the railroad is open.";
        return;
    }
    if (absoluteDay() < 5) return;
    uint32_t roll = rand32() % 1000;
    int x, y;
    if (roll < 10 && season != WINTER) {                    // crop fairy: 1%
        std::vector<size_t> growing;
        int fw = farm->map.width();
        for (size_t i = 0; i < farm->soils.size(); i++) {
            const Soil& s = farm->soils[i];
            const CropDef* c = s.seed >= 0 ? data.cropForSeed(s.seed) : nullptr;
            if (c && !s.dead && s.phase < c->phaseDays.size()) growing.push_back(i);
        }
        if (growing.empty()) return;
        size_t at = growing[rand32() % growing.size()];
        int cx = (int)(at % fw), cy = (int)(at / fw);
        for (size_t i : growing) {
            int tx = (int)(i % fw), ty = (int)(i / fw);
            if (std::abs(tx - cx) > 2 || std::abs(ty - cy) > 2) continue;
            Soil& s = farm->soils[i];
            s.phase = (uint8_t)data.cropForSeed(s.seed)->phaseDays.size();
            s.dayInPhase = 0;
        }
        morningNote = "A crop fairy visited in the night! Some of your crops grew all at once.";
    } else if (roll < 20) {                                 // the witch: 1%, needs a coop with hens in it
        for (auto& b : buildings) {
            const BuildingType* t = typeOf(b);
            if (!t || t->interior.find("Coop") == std::string::npos || b.daysLeft > 0) continue;
            bool lived = false;
            for (auto& a : animals) lived |= a.home == b.id;
            Location* in = lived ? loadLocation(interiorName(b)) : nullptr;
            int ox, oy;
            if (!in || !data.object(305) || !openSpot(*this, *in, rand32(), ox, oy)) continue;
            WorldObject o;
            o.x = ox; o.y = oy; o.itemId = 305;             // a void egg
            in->objects.push_back(o);
            morningNote = "You heard cackling over the farm in the night... something was left in the coop.";
            break;
        }
    } else if (roll < 30) {                                 // a meteorite: 1%
        if (randomFarmSpot(2, 2, x, y)) {
            Location::Clump c;
            c.x = (int16_t)x; c.y = (int16_t)y; c.kind = Location::CLUMP_METEOR; c.health = 20;
            farm->clumps.push_back(c);
            morningNote = "An explosion shook the house in the night! Something fell on the farm.";
        }
    } else if (roll < 38 && year > 1 && !mailReceived.count("Got_Capsule") && data.big(96)) {   // strange capsule: 0.8%
        if (randomFarmSpot(1, 1, x, y)) {
            Placed p;
            p.x = x; p.y = y; p.big = true; p.id = 96;
            placed["Farm"].push_back(p);
            mailReceived.insert("Got_Capsule");
            morningNote = "A strange sound woke you in the night... like something landing outside.";
        }
    } else if (roll < 43 && data.big(95)) {                 // stone owl: 0.5%
        if (randomFarmSpot(1, 1, x, y)) {
            Placed p;
            p.x = x; p.y = y; p.big = true; p.id = 95;
            placed["Farm"].push_back(p);
            morningNote = "You heard an owl hooting in the night...";
        }
    }
}

} // namespace sv
