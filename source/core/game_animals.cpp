// Farm buildings (Robin) and farm animals (Marnie): coops, barns, silos, sheds,
// wells; chickens, cows and the rest living inside, fed with hay, giving
// eggs overnight or milk / wool to the pail and shears.
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
        if (b.daysLeft > 0 || !b.upgradingTo.empty()) { say("Robin is already busy with a building."); return false; }
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
    b.daysLeft = std::max(1, t->days);
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
void Game::animalsNight() {
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

} // namespace sv
