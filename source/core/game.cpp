#include "game.h"
#include "saves.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>

namespace sv {

// Well-known object ids (Data/Objects).
static const int OBJ_WOOD = 388;
static const int OBJ_STONE = 390;
static const int OBJ_FIBER = 771;
static const int OBJ_PARSNIP_SEEDS = 472;

static const int TILE = 16;
static const int ENERGY_PER_SWING = 2;
static const float WALK_SPEED = 40.0f;   // px/s at native scale
static const float RUN_SPEED = 75.0f;
static const float TOOL_TIME = 0.30f;

void Location::initState() {
    size_t cells = (size_t)map.width() * map.height();
    soils.assign(cells, Soil());
    debris.assign(cells, DEB_NONE);
    debrisVar.assign(cells, 0);
    grass.assign(cells, 0);
    floors.assign(cells, 0);
    bushes.clear();
    rustles.clear();
}

Game::Game(const GameData& d) : data(d) {
    me.name = d.layout.farmerName;   // until character creation says otherwise
    me.outfit = d.layout.outfit;
}

uint32_t Game::rand32() {
    uint32_t x = rngState ? rngState : 0x9E3779B9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rngState = x;
    return x;
}

void Game::say(const std::string& s) {
    toast = s;
    toastTime = 2.5f;
}

// ------------------------------------------------------------ locations

Location* Game::loadLocation(const std::string& name, std::string* err) {
    auto it = locations.find(name);
    if (it != locations.end()) return it->second.get();
    std::unique_ptr<Location> loc(new Location());
    loc->name = name;
    loc->file = mapFileFor(name);
    if (!loc->map.load(loc->file, err)) return nullptr;
    loc->map.name = name;
    loc->isFarm = (name == "Farm");

    loc->mineLevel = mineLevelOf(name);
    loc->initState();
    if (loc->map.outdoors) treesFromMap(*loc);
    if (loc->mineLevel) mineGenerate(*loc);
    {   // Light: "x y type x y type ..."
        const std::string& l = loc->map.prop("Light");
        std::vector<int> v;
        const char* p = l.c_str();
        char* end;
        while (*p) {
            long n = strtol(p, &end, 10);
            if (end == p) { p++; continue; }
            v.push_back((int)n);
            p = end;
        }
        for (size_t i = 0; i + 2 < v.size(); i += 3) loc->lights.push_back({v[i], v[i + 1], v[i + 2]});
    }
    Location* out = loc.get();
    locations[name] = std::move(loc);
    return out;
}

std::string Game::mapFileFor(const std::string& name) const {
    // your farm type decides which map "Farm" is
    if (name == "Farm" && me.farmType > 0 && me.farmType < (int)data.farmTypes.size()) {
        const std::string& m = data.farmTypes[me.farmType].map;
        std::string v = "maps/" + m + "@" + seasonName(season) + ".smap";
        if (data.vfs.exists(v)) return data.vfs.path(v);
        if (data.vfs.exists("maps/" + m + ".smap")) return data.vfs.path("maps/" + m + ".smap");
    }
    if (int lv = mineLevelOf(name)) {   // a mine floor: one of Maps/Mines/1..60, 120 at the bottom
        int n = lv == 120 ? 120 : (lv - 1) % 40 + 1;
        if (lv > 120 && n % 10 == 0) n = n == 40 ? 39 : n + 1;   // no rest floors in the Skull Cavern
        std::string p = "maps/Mines_" + std::to_string(n) + ".smap";
        if (!data.vfs.exists(p)) p = "maps/Mines_" + std::to_string(n % 10 == 0 ? 10 : 1) + ".smap";
        return data.vfs.path(p);
    }
    if (name == "FarmHouse" && houseLevel > 0) {   // the upgraded house
        std::string up = "maps/FarmHouse" + std::to_string(std::min(houseLevel, 2)) + ".smap";
        if (data.vfs.exists(up)) return data.vfs.path(up);
    }
    size_t at = name.find('@');      // a farm building's inside: "Coop@3" uses maps/Coop
    if (at != std::string::npos) return mapFileFor(name.substr(0, at));
    std::string variant = "maps/" + name + "@" + seasonName(season) + ".smap";
    if (data.vfs.exists(variant)) return data.vfs.path(variant);
    return data.vfs.path("maps/" + name + ".smap");
}

void Game::refreshSeasonMaps() {
    for (auto& kv : locations) {
        Location& l = *kv.second;
        std::string want = mapFileFor(l.name);
        if (want == l.file) continue;
        TileMap m;
        if (!m.load(want, nullptr)) continue;
        bool sameSize = m.width() == l.map.width() && m.height() == l.map.height();
        m.name = l.name;
        l.map = std::move(m);
        l.file = want;
        if (!sameSize) l.initState();   // crops can't survive a resized map
        l.spawnDay = -1;
    }
}

bool Game::hasMap(const std::string& name) const {
    return locations.count(name) || data.vfs.exists("maps/" + name + ".smap");
}

bool Game::warpTo(const std::string& name, int tx, int ty, bool door) {
    if (cur && festivalWarp(name)) return true;
    if (islandGate(name)) return true;
    {   // the desert during its festival, the beach on night market nights
        std::string today = mapForToday(name);
        if (today != name) return warpTo(today, tx, ty, door);
    }
    if (name == "Farm" && cur)   // leaving a farm building: out its own door
        if (FarmBuilding* fb = buildingForInterior(cur->name))
            if (const BuildingType* bt = typeOf(*fb)) {
                tx = fb->x + bt->doorX;
                ty = fb->y + bt->doorY + 1;
                facing = FACE_DOWN;
            }
    Location* dest = loadLocation(name);
    if (!dest) {
        say("Can't go there yet (" + name + " not converted)");
        return false;
    }
    Location* prev = cur;
    if (prev)
        for (auto& n : npcs)
            if (n.leg < n.legs.size() && n.legs[n.leg].loc == prev->name) {
                n.legs[n.leg].fx = n.tileX();
                n.legs[n.leg].fy = n.tileY();
                n.legDone = 0;
                n.path.clear();
                n.pathIdx = 0;
            }
    if (door && prev && prev != dest) sfx("doorClose");
    if (riding && !dest->map.outdoors) riding = false;   // the horse waits outside
    cur = dest;
    // Only the farm and where you are stay in memory.
    if (prev && prev != farm && prev != cur && prev->name.find('@') == std::string::npos && prev->name != "IslandWest") {   // farm buildings and the island farm stay too
        std::string old = prev->name;   // copy: erase destroys the key's owner
        locations.erase(old);
    }

    if (cur != prev) caveNight();   // Demetrius's cave fills up while you're away
    applyMapFixes();
    const TileMap& m = cur->map;
    tx = std::max(0, std::min(tx, m.width() - 1));
    ty = std::max(0, std::min(ty, m.height() - 1));
    // If the arrival tile is solid, step to the nearest open one.
    if (tileSolid(tx, ty)) {
        bool found = false;
        for (int r = 1; r <= 3 && !found; r++)
            for (int dy = -r; dy <= r && !found; dy++)
                for (int dx = -r; dx <= r && !found; dx++)
                    if (!tileSolid(tx + dx, ty + dy)) {
                        tx += dx;
                        ty += dy;
                        found = true;
                    }
    }
    px = tx * TILE + TILE / 2.0f;
    py = ty * TILE + TILE - 2.0f;
    fade = 1.0f;
    warpCooldown = 0.35f;
    toolTimer = 0;
    spawnObjects(*cur);
    islandWalnuts();
    islandFossils();
    npcsEnterLocation();
    questEntered(name);
    checkEvents();
    return true;
}

bool Game::enterFrom(const std::string& name, bool door) {
    std::string from = cur ? cur->name : "";
    Location* dest = loadLocation(name);
    if (!dest) return warpTo(name, 0, 0, door);
    // Arrive just inside the warp that leads back where we came from.
    const Warp* back = dest->map.warpTo(from);
    if (back) {
        int tx = back->x, ty = back->y;
        if (ty >= dest->map.height() - 1) ty = dest->map.height() - 2;
        else ty -= 1;
        if (tx < 0) tx = 0;
        if (tx >= dest->map.width()) tx = dest->map.width() - 1;
        if (ty < 0) ty = 0;
        facing = FACE_UP;
        return warpTo(name, tx, ty, door);
    }
    return warpTo(name, dest->map.width() / 2, dest->map.height() / 2, door);
}

// Parses tile Action strings from the maps.
bool Game::runMapAction(const std::string& action, bool bumped) {
    if (action.empty()) return false;
    std::istringstream ss(action);
    std::string verb;
    ss >> verb;
    if (verb == "Warp" || verb == "LockedDoorWarp" || verb == "MagicWarp") {
        int x = 0, y = 0;
        std::string target;
        ss >> x >> y >> target;
        if (target.empty()) return false;
        if (verb == "LockedDoorWarp") {
            int open = 0, close = 2600;
            ss >> open >> close;
            int now = (timeOfDay / 60) * 100 + timeOfDay % 60;
            if (now < open || now >= close) {
                if (!bumped || toastTime <= 0)
                    say("Closed. Open " + timeString((open / 100) * 60 + open % 100) + " to " +
                        timeString((close / 100) * 60 + close % 100));
                return true;
            }
        }
        warpTo(target, x, y, verb != "MagicWarp");   // doors click; magic warps don't
        return true;
    }
    if (bumped) return false;   // everything else needs a button press
    if (verb == "Mine") {            // the ladder down at the mine entrance
        std::string which;
        ss >> which;
        if (!which.empty()) { say("That part of the mine isn't in yet."); return true; }
        enterMine(1);
        return true;
    }
    if (verb == "Gunther") {          // the museum counter
        museumOpen = true;
        return true;
    }
    if (verb == "MineElevator") {
        if (mineDeepest < 5) say("The elevator stops every 5 floors. Reach floor 5 first.");
        else openElevator();
        return true;
    }
    if (verb == "OpenShop") {
        std::string id;
        ss >> id;
        if (id == "Blacksmith" && crackGeode()) return true;   // holding a geode: Clint opens it
        openShop = id.empty() ? "SeedShop" : id;
        if (openShop == "Blacksmith") collectUpgrade();
        return true;
    }
    if (verb == "WizardBook") {   // the Wizard's book of buildings
        openShop = "Wizard";
        return true;
    }
    if (verb == "BuyBackpack") {
        if (invSlots >= INV_SIZE) { say("You already have the biggest backpack."); return true; }
        int cost = invSlots < 24 ? 2000 : 10000;
        const char* nm = invSlots < 24 ? "Large Pack (24 slots)" : "Deluxe Pack (36 slots)";
        if (packAsk <= 0) {
            say(std::string(nm) + ": " + std::to_string(cost) + "g. Press A again to buy.");
            packAsk = 4.0f;
        } else if (money < cost) {
            say("Not enough money");
        } else {
            money -= cost;
            invSlots = invSlots < 24 ? 24 : 36;
            packAsk = 0;
            sfx("purchase");
            say(std::string("Bought the ") + (invSlots == 24 ? "Large Pack!" : "Deluxe Pack!"));
        }
        return true;
    }
    if (verb == "Mailbox") {
        if (!openMail()) say("No new mail");
        return true;
    }
    if (verb == "Buy") {        // "Buy General", "Buy Fish"
        std::string what;
        ss >> what;
        openShop = what == "Fish" && data.shops.count("FishShop") ? "FishShop" : "SeedShop";
        return true;
    }
    {
        std::string rest;
        std::getline(ss, rest);
        if (worldAction(verb, rest)) return true;
    }
    return false;
}

// ------------------------------------------------------------ state access

static const Soil kNoSoil;

const Soil& Game::soil(int x, int y) const {
    if (!cur->map.inBounds(x, y)) return kNoSoil;
    return cur->soils[(size_t)y * cur->map.width() + x];
}
DebrisType Game::debris(int x, int y) const {
    if (!cur->map.inBounds(x, y)) return DEB_NONE;
    return (DebrisType)cur->debris[(size_t)y * cur->map.width() + x];
}
uint8_t Game::debrisVariant(int x, int y) const {
    if (!cur->map.inBounds(x, y)) return 0;
    return cur->debrisVar[(size_t)y * cur->map.width() + x];
}

// ------------------------------------------------------------ new game / days

bool Game::newGame(uint32_t seed, const Character& who, std::string* err) {
    me = who;
    if (me.farmType < 0 || me.farmType >= (int)data.farmTypes.size()) me.farmType = 0;
    return newGame(seed, err);
}

bool Game::newGame(uint32_t seed, std::string* err) {
    rngState = seed ? seed : 1;
    maxEnergy = MAX_ENERGY;
    maxHealth = 100;
    houseLevel = houseUpgradeDays = 0;
    furniture = data.layout.furniture;
    museumDonated.clear();
    museumOpen = false;
    for (auto& e : equip) e = Slot();
    invSlots = 12;
    binOpen = false;
    pets.clear();
    bowlFull = false;
    kids.clear();
    shippedEver.clear(); fishCaught.clear(); cookedEver.clear(); craftedEver.clear();
    orderKey.clear(); orderProgress.clear(); orderDays = 0;
    babyDue = -1;
    babyAsk = false;
    spouse.clear();
    engagedTo.clear();
    dating.clear();
    weddingDay = -1;
    bundleDefs.clear();
    bundleGiven.clear();
    bundleView = -1;
    buildings.clear();
    animals.clear();
    hay = 0;
    nextBuildingId = 1;
    placing.clear();
    quests.clear();
    questsFinished.clear();
    levelUps.clear();
    for (int i = 0; i < SK_COUNT; i++) { skillXp[i] = 0; skillLevels[i] = 0; }
    mailbox.clear();
    mailTomorrow.clear();
    triggersApplied.clear();
    totalEarned = 0;
    letterOpen = false;
    professions.clear();
    eventsSeen.clear();
    mailReceived.clear();
    cutscene = Cutscene();
    npcs.clear();
    dialogue = DialogueBox();
    locations.clear();
    farm = loadLocation("Farm", err);
    if (!farm) return false;
    cur = farm;
    for (auto& s : inv) s = Slot();
    shipped.clear();
    day = 1;
    season = SPRING;
    year = 1;
    money = 500;
    selected = 0;
    raining = false;

    int slot = 0;
    const ToolKind order[] = {TOOL_AXE, TOOL_HOE, TOOL_CAN, TOOL_PICKAXE, TOOL_SCYTHE};
    for (ToolKind k : order) {
        const ToolDef* t = data.toolByKind(k);
        if (!t) continue;
        inv[slot].type = SLOT_TOOL;
        inv[slot].id = (int)(t - &data.tools[0]);
        inv[slot].count = 1;
        slot++;
    }
    if (data.cropForSeed(OBJ_PARSNIP_SEEDS)) {
        inv[slot].type = SLOT_OBJECT;
        inv[slot].id = OBJ_PARSNIP_SEEDS;
        inv[slot].count = 15;
    }

    // A new farm is overgrown.
    uint32_t r = rngState;
    int diggable = 0;
    const TileMap& m = farm->map;
    for (int y = 0; y < m.height(); y++)
        for (int x = 0; x < m.width(); x++)
            if ((m.flags(x, y) & (CF_DIGGABLE | CF_NOSPAWN)) == CF_DIGGABLE) diggable++;
    // the map marks where the debris starts (Paths 13-15 weeds, 16-17 stones, 18 twigs);
    // maps without markers get it scattered at random
    if (!debrisFromMap(r)) spawnDebris(diggable / 6, r);
    rngState = r;
    ensureGreenhouse();
    // the first two quests of a new farm
    addQuest("9");
    addQuest("6");
    hasUnseenQuest = true;
    deliverMorningMail();
    startDay();
    wakeUp();
    return true;
}

void Game::spawnDebris(int count, uint32_t& r) {
    const TileMap& m = farm->map;
    int w = m.width(), h = m.height();
    if (w == 0 || h == 0) return;
    for (int i = 0, tries = 0; i < count && tries < count * 20; tries++) {
        r ^= r << 13; r ^= r >> 17; r ^= r << 5;
        int x = (int)(r % (uint32_t)w);
        int y = (int)((r >> 8) % (uint32_t)h);
        uint8_t f = m.flags(x, y);
        if ((f & (CF_DIGGABLE | CF_NOSPAWN | CF_BLOCKED)) != CF_DIGGABLE) continue;
        size_t idx = (size_t)y * w + x;
        if (farm->debris[idx] || farm->soils[idx].tilled) continue;
        bool onClump = false;
        for (auto& c : farm->clumps)
            if (x >= c.x - 1 && x < c.x + 3 && y >= c.y - 1 && y < c.y + 3) onClump = true;
        if (onClump) continue;
        bool onBuilding = false;
        for (auto& b : data.layout.buildings)
            if (x >= b.tileX && x < b.tileX + b.footW && y >= b.tileY - 1 && y < b.tileY + b.footH + 1)
                onBuilding = true;
        if (onBuilding) continue;
        int doorDx = x - data.layout.doorX, doorDy = y - data.layout.doorY;
        if (doorDx * doorDx + doorDy * doorDy < 12) continue;   // keep the porch clear
        int k = (int)((r >> 20) % 10);
        if (season == WINTER && k < 5) k = 5;
        farm->debris[idx] = k < 5 ? DEB_WEED : (k < 8 ? DEB_STONE : DEB_TWIG);
        farm->debrisVar[idx] = (uint8_t)((r >> 4) & 1);
        i++;
    }
}

// One night for a location's soil: crops grow (if watered), die out of season, dry out.
void Game::growSoils(Location& loc, bool newSeason, bool anySeason) {
    for (auto& s : loc.soils) {
        if (s.seed >= 0 && !s.dead) {
            const CropDef* c = data.cropForSeed(s.seed);
            if (!c) {
                s.seed = -1;
            } else if (newSeason && !anySeason && !(c->seasonMask & (1 << season))) {
                s.dead = true;
            } else if (s.watered) {
                if (s.regrowWait > 0) {
                    s.regrowWait--;
                    if (s.regrowWait == 0) s.regrowWait = -1;
                } else if (s.phase < c->phaseDays.size()) {
                    s.dayInPhase++;
                    // agriculturist: crops sometimes put on an extra day of growth
                    if (has(PR_AGRICULTURIST) && rand32() % 10 == 0) s.dayInPhase++;
                    if (s.dayInPhase >= c->phaseDays[s.phase]) {
                        s.phase++;
                        s.dayInPhase = 0;
                    }
                }
            }
        }
        s.watered = false;
        if (s.tilled && s.seed < 0 && !anySeason && (season == WINTER || rand32() % 10 == 0)) s = Soil();
    }

}

void Game::startDay() {
    setDialogueToken("%farm%", me.farm);
    setDialogueToken("%farm", me.farm);         // the game's own form ("This is %farm farm.")
    setDialogueToken("%spouse", spouse.empty() ? "your spouse" : spouse);
    setDialogueToken("%pet", pets.empty() ? "your pet" : pets[0].name);
    setDialogueToken("%name%", me.name);
    setDialogueToken("%spouse%", spouse.empty() ? "your spouse" : spouse);
    setDialogueToken("%pet%", pets.empty() ? "your pet" : pets[0].name);
    setDialogueToken("%year%", std::to_string(year));
    setDialogueToken("%season%", seasonName(season));
    npcsStartDay();
    festivalActive = -1;
    festivalDone = false;
    worldMorning();
    cartMorning();
    petMorning();
    kidsMorning();
    achievementsMorning();
    if (const Festival* fe = festivalToday())
        if (morningNote.empty()) {
            int h = fe->start / 100;
            morningNote = "Today is the " + fe->name + "! It starts at " + std::to_string(h % 12 == 0 ? 12 : h % 12) +
                          (h < 12 ? "am" : "pm") + (fe->loc == "Town" ? " in town." : fe->loc == "Beach" ? " on the beach." : " in the forest.");
        }
    timeOfDay = DAY_START;
    clockAcc = 0;
    energy = maxEnergy;
    health = maxHealth;
    invuln = 0;
    toolTimer = 0;
    char buf[64];
    snprintf(buf, sizeof buf, "%s %d, Year %d%s", seasonName(season), day, year,
             raining ? " - Rain" : "");
    std::string s = buf;
    s[0] = (char)toupper((unsigned char)s[0]);
    say(s);
}

void Game::wakeUp() {
    const Layout& L = data.layout;
    if (hasMap(L.wakeMap)) {
        facing = FACE_DOWN;
        int wx = L.wakeX, wy = L.wakeY;
        if (const FurniturePlacement* bed = bedIn(L.wakeMap)) {   // just below your bed
            auto fd = data.furniture.find(bed->id);
            wx = bed->x;
            wy = bed->y + (fd != data.furniture.end() ? fd->second.boxH : 3);
        }
        warpTo(L.wakeMap, wx, wy);
    } else {
        cur = farm;
        facing = FACE_DOWN;
        warpTo("Farm", L.spawnX, L.spawnY);
    }
}

std::string Game::timeString(int minutes) {
    int t = minutes % (24 * 60);
    int h = t / 60, m = t % 60;
    bool pm = h >= 12;
    int h12 = h % 12;
    if (h12 == 0) h12 = 12;
    char buf[16];
    snprintf(buf, sizeof buf, "%d:%02d %s", h12, m, pm ? "pm" : "am");
    return buf;
}
std::string Game::timeString() const { return timeString(timeOfDay); }

bool Game::buildingAt(int x, int y) const {
    for (auto& fp : furniture) {
        if (fp.map != cur->name) continue;
        int fx = fp.x, fy = fp.y;
        auto it = data.furniture.find(fp.id);
        if (it != data.furniture.end() && (it->second.type == "rug" || it->second.type == "painting" || it->second.type == "window"))
            continue;   // walk over rugs; wall hangings are on the wall
        int bw = it != data.furniture.end() ? it->second.boxW : 1;
        int bh = it != data.furniture.end() ? it->second.boxH : 1;
        if (x >= fx && x < fx + bw && y >= fy && y < fy + bh) return true;
    }
    if (!cur->isFarm) return false;
    if (const_cast<Game*>(this)->buildingAtTile(x, y)) return true;
    for (auto& b : data.layout.buildings)
        if (x >= b.tileX && x < b.tileX + b.footW && y >= b.tileY && y < b.tileY + b.footH)
            return true;
    return false;
}

bool Game::tileSolid(int tx, int ty) const {
    const TileMap& m = cur->map;
    if (!m.inBounds(tx, ty)) return true;
    uint8_t f = m.flags(tx, ty);
    if ((f & CF_BLOCKED) && !(f & CF_PASSABLE)) return true;
    if (buildingAt(tx, ty)) return true;
    size_t i = (size_t)ty * m.width() + tx;
    if (cur->debris[i] != DEB_NONE) return true;
    for (auto& o : cur->objects)
        if (o.x == tx && o.y == ty) return true;
    if (const Placed* p = placedAt(tx, ty))
        if (!(!p->big && p->id == FENCE_GATE && p->input == 1)) return true;   // an open gate lets you through
    for (auto& t : cur->trees)
        if (t.x == tx && t.y == ty && (t.stage > 0 || t.stump)) return true;
    if (bushAt(tx, ty) >= 0) return true;
    if (!cur->clumps.empty() && clumpAt(tx, ty) >= 0) return true;
    if (!cur->fruitTrees.empty() && fruitTreeAt(tx, ty) >= 0) return true;
    for (auto& n : npcs)   // someone who never leaves their spot (Raiden and his 3DS) is in the way
        if (n.def && n.loc == cur->name && n.tileX() == tx && n.tileY() == ty && n.def->dialogue.count("fact_0")) return true;
    if (cur->mineLevel && m.tile(L_BUILDINGS, tx, ty) != NO_TILE) return true;   // a ladder that turned up
    if (cur->mineLevel && tx == cur->chestX && ty == cur->chestY) return true;
    const Soil& s = cur->soils[i];
    if (s.seed >= 0) {
        const CropDef* c = data.cropForSeed(s.seed);
        if (c && c->raised && s.phase > 0) return true;
    }
    return false;
}

bool Game::blockedPx(float x, float y) const {
    const float hw = 5, top = 6, bot = 1;
    float xs[2] = {x - hw, x + hw};
    float ys[2] = {y - top, y - bot};
    for (float cx : xs)
        for (float cy : ys)
            if (tileSolid((int)std::floor(cx / TILE), (int)std::floor(cy / TILE))) return true;
    return false;
}

void Game::targetTile(int& tx, int& ty) const {
    tx = (int)std::floor(px / TILE);
    ty = (int)std::floor((py - 4) / TILE);
    switch (facing) {
        case FACE_DOWN: ty++; break;
        case FACE_UP: ty--; break;
        case FACE_LEFT: tx--; break;
        case FACE_RIGHT: tx++; break;
    }
}

bool Game::cropReady(int x, int y) const {
    if (!cur->map.inBounds(x, y)) return false;
    const Soil& s = soil(x, y);
    if (s.seed < 0 || s.dead || s.regrowWait > 0) return false;
    const CropDef* c = data.cropForSeed(s.seed);
    return c && s.phase >= c->phaseDays.size();
}

int Game::cropFrame(int x, int y) const {
    const Soil& s = soil(x, y);
    const CropDef* c = data.cropForSeed(s.seed);
    if (!c) return 0;
    if (s.regrown) return s.regrowWait > 0 ? 7 : 6;
    if (s.phase == 0) return s.variant & 1;
    return std::min(s.phase + 1, 7);
}

// ------------------------------------------------------------ inventory

bool Game::addItem(int objId, int count, int quality) {
    Slot sl;
    sl.type = SLOT_OBJECT;
    sl.id = objId;
    sl.count = count;
    sl.quality = (uint8_t)quality;
    if (!addSlot(sl)) return false;
    for (auto& n : notices)
        if (n.id == objId && n.t > 0) {
            n.count += count;
            n.t = 3.5f;
            return true;
        }
    Notice n;
    n.id = objId;
    n.count = count;
    n.t = 3.5f;
    notices.push_back(n);
    if (notices.size() > 5) notices.erase(notices.begin());
    return true;
}

void Game::itemFlies(int objId, float x, float y, int count) {
    for (int i = 0; i < std::min(count, 4); i++) {
        if (effects.size() > 80) return;
        Effect e;
        e.kind = Effect::ITEM;
        e.item = objId;
        e.x = x;
        e.y = y - 6;
        e.vx = (float)((int)(rand32() % 81) - 40);
        e.vy = -90.0f - (float)(rand32() % 40);
        e.groundY = y + 4 + (float)(rand32() % 6);
        e.life = 2.0f;
        effects.push_back(e);
    }
}

void Game::selectSlot(int i) {
    if (i >= 0 && i < INV_SIZE) selected = i;
}

void Game::closeChest() {
    if (binOpen) {
        shipped.clear();
        for (auto& s : binBox.items)
            if (!s.empty()) shipped.push_back(s);
        binOpen = false;
        sfx("doorCreakReverse");
    }
    chestOpen = false;
}

void Game::openBin(int x, int y) {
    binBox = Placed();
    binBox.items.assign(36, Slot());
    for (size_t i = 0; i < shipped.size() && i < 36; i++) binBox.items[i] = shipped[i];
    binX = x;
    binY = y;
    binOpen = chestOpen = true;
    sfx("doorCreak");
}

void Game::swapSlots(int a, int b) {
    if (a < 0 || b < 0 || a >= INV_SIZE || b >= INV_SIZE || a == b) return;
    if (b >= invSlots) return;   // a bigger backpack unlocks those
    Slot& A = inv[a];
    Slot& B = inv[b];
    if (A.stacksWith(B)) {
        int n = std::min(999 - B.count, A.count);
        B.count += n;
        A.count -= n;
        if (!A.count) A = Slot();
        return;
    }
    std::swap(A, B);
}

bool Game::trashSlot(int i) {
    if (i < 0 || i >= INV_SIZE || inv[i].empty() || inv[i].type == SLOT_TOOL) return false;
    inv[i] = Slot();
    return true;
}

int Game::equipKind(const Slot& s) const {
    if (s.type == SLOT_OBJECT) return data.rings.count(s.id) ? EQ_RING1 : -1;
    if (s.type != SLOT_TOOL) return -1;
    const ToolDef* t = data.tool(s.id);
    return !t ? -1 : t->kind == TOOL_BOOTS ? EQ_BOOTS : t->kind == TOOL_HAT ? EQ_HAT : -1;
}

bool Game::wearSlot(int i) {
    if (i < 0 || i >= INV_SIZE) return false;
    int e = equipKind(inv[i]);
    if (e < 0) return false;
    if (e == EQ_RING1 && !equip[EQ_RING1].empty() && equip[EQ_RING2].empty()) e = EQ_RING2;
    Slot on = inv[i];
    on.count = 1;
    Slot old = equip[e];
    if (inv[i].count > 1) {            // one ring off a stack: the old one needs its own room
        if (!old.empty() && !addSlot(old)) { say("No room in your bag."); return false; }
        inv[i].count--;
    } else {
        inv[i] = old;
    }
    equip[e] = on;
    sfx("crit");
    say("Wearing: " + itemName(on));
    return true;
}

bool Game::takeOff(int e) {
    if (e < 0 || e >= EQ_COUNT || equip[e].empty()) return false;
    if (!addSlot(equip[e])) { say("No room in your bag."); return false; }
    equip[e] = Slot();
    sfx("shwip");
    return true;
}

bool Game::wearing(const char* ringName) const {
    for (int e = EQ_RING1; e <= EQ_RING2; e++) {
        if (equip[e].empty()) continue;
        auto it = data.rings.find(equip[e].id);
        if (it != data.rings.end() && it->second == ringName) return true;
    }
    return false;
}

int Game::defense() const {
    int d = 0;
    if (!equip[EQ_BOOTS].empty())
        if (const ToolDef* t = data.tool(equip[EQ_BOOTS].id)) d += t->minDamage;
    if (wearing("Topaz Ring")) d += 1;
    if (wearing("Crabshell Ring")) d += 5;
    return d;
}

bool Game::eatSlot(int i) {
    if (i < 0 || i >= INV_SIZE) return false;
    Slot& sl = inv[i];
    const ObjectDef* o = sl.type == SLOT_OBJECT ? data.object(sl.id) : nullptr;
    if (o && sl.id == 79) return readSecretNote(i);
    if (o && sl.id == 434) {   // a Stardrop: more energy for good, and a full bar
        maxEnergy = std::min(MAX_ENERGY + STARDROP_ENERGY * MAX_STARDROPS, maxEnergy + STARDROP_ENERGY);
        energy = maxEnergy;
        say("Your maximum energy level has increased.");
        sfx("stardrop");
        if (--sl.count <= 0) sl = Slot();
        return true;
    }
    if (!o || o->edibility <= 0 || o->edibility == -300) return false;
    int gain = (int)std::ceil(o->edibility * 2.5f);
    energy = std::min(maxEnergy, energy + gain);
    say("Ate " + o->name + " (+" + std::to_string(gain) + ")");
    sfx("eat");
    if (--sl.count <= 0) sl = Slot();
    return true;
}

// ------------------------------------------------------------ shops

static int hatIndex(const GameData& d, int id) {
    std::string key = "Hat_" + std::to_string(id);
    for (size_t i = 0; i < d.tools.size(); i++)
        if (d.tools[i].key == key) return (int)i;
    return -1;
}

std::vector<ShopEntry> Game::shopStock(const std::string& shop) const {
    std::vector<ShopEntry> out;
    if (shop == "Traveler") return cartStock;   // today's cart
    auto it = data.shops.find(shop);
    if (it != data.shops.end()) {
        for (auto& si : it->second) {
            if (si.seasonMask && !(si.seasonMask & (1 << season))) continue;
            if (year < si.minYear) continue;
            if (!si.noMail.empty() && mailReceived.count(si.noMail)) continue;
            if (!si.cond.empty() && !gameQuery(si.cond)) continue;
            ShopEntry e{si.itemId + si.kind * SHOP_KIND, 0, si.kind, si.itemId, si.tradeId};
            int base = 0;
            if (si.kind == 0) { const ObjectDef* o = data.object(si.itemId); if (!o) continue; base = o->price * 2; }
            else if (si.kind == 1) { if (!data.bigs.count(si.itemId)) continue; }
            else if (si.kind == 2) { if (hatIndex(data, si.itemId) < 0) continue; }
            else if (!data.furniture.count(si.itemId)) continue;
            e.price = si.tradeId >= 0 ? std::max(1, si.tradeAmt) : si.price > 0 ? si.price : base;
            if (e.price <= 0) continue;
            bool dup = false;
            for (auto& o : out) dup |= o.itemId == e.itemId;
            if (!dup) out.push_back(e);
        }
        return out;
    }
    // No shop data converted: seeds for this season, Pierre-style pricing.
    for (auto& kv : data.crops) {
        const CropDef& c = kv.second;
        if (!(c.seasonMask & (1 << season))) continue;
        const ObjectDef* o = data.object(c.seedId);
        if (!o || o->price <= 0) continue;
        out.push_back({c.seedId, o->price * 2});
    }
    std::sort(out.begin(), out.end(),
              [](const ShopEntry& a, const ShopEntry& b) { return a.price < b.price; });
    return out;
}

bool Game::buy(const std::string& shop, int itemId, int qty) {
    if (qty <= 0) return false;
    for (auto& e : shopStock(shop))
        if (e.itemId == itemId) {
            int cost = e.price * qty;
            std::string unit;
            if (cost > shopFunds(shop, &unit)) {
                say("Not enough " + unit);
                return false;
            }
            if (e.kind == 0) { if (!addItem(e.realId, qty)) return false; }
            else if (e.kind == 1) {
                Slot b; b.type = SLOT_BIG; b.id = e.realId; b.count = qty;
                if (!addSlot(b)) return false;
            } else if (e.kind == 2) { if (!giveTool("Hat_" + std::to_string(e.realId))) return false; }
            else if (!placeFurnitureInHouse(e.realId)) { say("There's no room for it in your house."); return false; }
            else say("It's been delivered to your house.");
            if (e.tradeId == -1) money -= cost;
            else if (e.tradeId == -2) starTokens -= cost;
            else if (e.tradeId == -3) clubCoins -= cost;
            else takeItem(e.tradeId, cost);
            sfx("purchase");
            return true;
        }
    return false;
}

// The game's own conditions ("DAY_OF_WEEK Monday, PLAYER_HEARTS Current Krobus 10"): every clause must hold.
bool Game::gameQuery(const std::string& cond) const {
    auto words = [](const std::string& s) {
        std::vector<std::string> out;
        std::string cur;
        bool quoted = false;
        for (char ch : s) {
            if (ch == '"') { quoted = !quoted; continue; }
            if (ch == ' ' && !quoted) { if (!cur.empty()) out.push_back(cur); cur.clear(); }
            else cur += ch;
        }
        if (!cur.empty()) out.push_back(cur);
        return out;
    };
    auto num = [](const std::vector<std::string>& a, size_t i, int dflt = 0) { return i < a.size() ? atoi(a[i].c_str()) : dflt; };
    // a roll that comes out the same all day (or all week) for the same key
    auto roll = [&](const std::string& span, const std::string& key) {
        uint32_t h = (uint32_t)(span == "week" ? absoluteDay() / 7 : span == "season" ? absoluteDay() / 28 : absoluteDay()) * 2654435761u;
        for (char ch : key) h = (h ^ (uint8_t)ch) * 16777619u;
        h ^= h >> 15; h *= 0x2c1b3c6du; h ^= h >> 12;
        return (h & 0xFFFFFF) / (double)0x1000000;
    };
    size_t at = 0;
    while (at <= cond.size()) {
        size_t end = at;                                 // split on commas outside quotes
        for (bool q = false; end < cond.size() && (q || cond[end] != ','); end++) q ^= cond[end] == '"';
        std::vector<std::string> a = words(cond.substr(at, end - at));
        at = end + 1;
        if (a.empty()) continue;
        bool neg = a[0][0] == '!';
        std::string q = a[0].substr(neg ? 1 : 0);
        a.erase(a.begin());
        if (!a.empty() && (a[0] == "Current" || a[0] == "Host" || a[0] == "Any" || a[0] == "Target") && q.rfind("PLAYER_", 0) == 0) a.erase(a.begin());
        bool ok = false;
        if (q == "DAY_OF_WEEK") {
            static const char* const DAYS[] = {"sunday", "monday", "tuesday", "wednesday", "thursday", "friday", "saturday"};
            for (auto& d : a) {
                std::string l = d;
                for (auto& ch : l) ch = (char)tolower((unsigned char)ch);
                ok |= l == DAYS[day % 7] || l.substr(0, 3) == std::string(DAYS[day % 7]).substr(0, 3);
            }
        } else if (q == "DAY_OF_MONTH") {
            for (auto& d : a) ok |= d == "odd" ? day % 2 == 1 : d == "even" ? day % 2 == 0 : atoi(d.c_str()) == day;
        } else if (q == "YEAR") ok = year >= num(a, 0, 1);
        else if (q == "DAYS_PLAYED") ok = absoluteDay() >= num(a, 0);
        else if (q == "SEASON") { static const char* const S[] = {"spring", "summer", "fall", "winter"}; for (auto& d : a) ok |= d == S[season % 4]; }
        else if (q == "PLAYER_HAS_ACHIEVEMENT") ok = mailReceived.count("ach:" + (a.empty() ? std::string() : a[0])) != 0;
        else if (q == "PLAYER_HAS_ALL_ACHIEVEMENTS") { ok = true; for (auto& ach : const_cast<Game*>(this)->achievements()) ok &= ach.earned; }
        else if (q == "MINE_LOWEST_LEVEL_REACHED") ok = mineDeepest >= num(a, 0);
        else if (q == "PLAYER_HAS_MAIL") ok = !a.empty() && mailReceived.count(a[0]) != 0;
        else if (q == "PLAYER_HAS_SEEN_EVENT") ok = !a.empty() && eventsSeen.count(a[0]) != 0;
        else if (q == "PLAYER_HEARTS") {
            if (a.size() >= 2 && a[0] == "Any") { for (auto& n : npcs) ok |= n.hearts() >= num(a, 1); }
            else if (const NpcState* n = a.empty() ? nullptr : npcByName(a[0])) ok = n->hearts() >= num(a, 1);
        } else if (q == "PLAYER_NPC_RELATIONSHIP") ok = !spouse.empty() || !engagedTo.empty();
        else if (q == "PLAYER_BASE_FARMING_LEVEL" || q == "PLAYER_FARMING_LEVEL") ok = skillLevels[SK_FARMING] >= num(a, 0);
        else if (q == "PLAYER_BASE_FISHING_LEVEL" || q == "PLAYER_FISHING_LEVEL") ok = skillLevels[SK_FISHING] >= num(a, 0);
        else if (q == "PLAYER_BASE_MINING_LEVEL" || q == "PLAYER_MINING_LEVEL") ok = skillLevels[SK_MINING] >= num(a, 0);
        else if (q == "PLAYER_BASE_FORAGING_LEVEL" || q == "PLAYER_FORAGING_LEVEL") ok = skillLevels[SK_FORAGING] >= num(a, 0);
        else if (q == "PLAYER_BASE_COMBAT_LEVEL" || q == "PLAYER_COMBAT_LEVEL") ok = skillLevels[SK_COMBAT] >= num(a, 0);
        else if (q == "PLAYER_FARMHOUSE_UPGRADE") ok = houseLevel >= num(a, 0);
        else if (q == "MUSEUM_DONATIONS") ok = (int)museumDonated.size() >= num(a, 0);
        else if (q == "WORLD_STATE_FIELD") ok = a.size() >= 2 && a[0] == "GoldenWalnutsFound" && walnutsFound >= num(a, 1);
        else if (q == "PLAYER_HAS_TOWN_KEY") ok = mailReceived.count("HasTownKey") != 0;
        else if (q == "IS_COMMUNITY_CENTER_COMPLETE") ok = ccDone();
        else if (q == "IS_JOJA_MART_COMPLETE") ok = mailReceived.count("JojaMember") && mailReceived.count("ccVault") && !ccDone();
        else if (q == "PLAYER_HAS_CRAFTING_RECIPE" || q == "PLAYER_HAS_COOKING_RECIPE") {
            std::string name;
            for (auto& w : a) name += (name.empty() ? "" : " ") + w;
            for (auto& r : data.recipes) ok |= r.name == name && recipeKnown(r);
        } else if (q == "PLAYER_HAS_ITEM") {
            std::string id = a.empty() ? "" : a[0];
            if (id.rfind("(T)", 0) == 0 || id.rfind("(W)", 0) == 0) {      // a tool, by its key
                for (int i = 0; i < invSlots; i++)
                    if (inv[i].type == SLOT_TOOL) { const ToolDef* t = data.tool(inv[i].id); ok |= t && t->key.find(id.substr(3)) != std::string::npos; }
            } else ok = countItem(atoi(id.rfind("(O)", 0) == 0 ? id.c_str() + 3 : id.c_str())) >= std::max(1, num(a, 1, 1));
        } else if (q == "RANDOM") ok = roll("day", cond) < atof(a.empty() ? "0" : a[0].c_str());
        else if (q == "SYNCED_RANDOM") ok = a.size() >= 3 && roll(a[0], a[1]) < atof(a[2].c_str());
        else if (q == "SYNCED_CHOICE") {                 // span key min max values...
            if (a.size() >= 5) {
                int lo = num(a, 2), hi = num(a, 3), pick = lo + (int)(roll(a[0], a[1]) * (hi - lo + 1));
                for (size_t i = 4; i < a.size(); i++) ok |= atoi(a[i].c_str()) == pick;
            }
        } else if (q == "ANY") { for (auto& sub : a) ok |= gameQuery(sub); }
        else if (q == "PLAYER_STAT") { auto st = a.empty() ? stats.end() : stats.find(a[0]); ok = (st != stats.end() ? st->second : 0) >= num(a, 1); }
        else ok = false;                                 // multiplayer, conversation topics: never true on a 3DS
        if (ok == neg) return false;
    }
    return true;
}

int Game::shopFunds(const std::string& shop, std::string* unit) const {
    int trade = -1;
    auto it = data.shops.find(shop);
    if (shop != "Traveler" && it != data.shops.end() && !it->second.empty()) trade = it->second.front().tradeId;
    if (trade == -2) { if (unit) *unit = "star tokens"; return starTokens; }
    if (trade == -3) { if (unit) *unit = "Qi coins"; return clubCoins; }
    if (trade >= 0) {
        const ObjectDef* o = data.object(trade);
        if (unit) *unit = o ? o->name + "s" : "items";
        return countItem(trade);
    }
    if (unit) *unit = "money";
    return money;
}

std::string Game::shopEntryName(const ShopEntry& e) const {
    if (e.kind == 1) { auto b = data.bigs.find(e.realId); return b != data.bigs.end() ? b->second.name : "?"; }
    if (e.kind == 2) { const ToolDef* t = data.tool(hatIndex(data, e.realId)); return t ? t->name : "Hat"; }
    if (e.kind == 3) { auto f = data.furniture.find(e.realId); return f != data.furniture.end() ? f->second.name : "Furniture"; }
    const ObjectDef* o = data.object(e.realId ? e.realId : e.itemId);
    return o ? o->name : "?";
}

int Game::sellPrice(const Slot& s) const {
    if (s.type == SLOT_TOOL || s.empty()) return 0;
    return itemPrice(s);
}

bool Game::sellSlot(int i) {
    if (i < 0 || i >= INV_SIZE) return false;
    int p = sellPrice(inv[i]);
    if (p <= 0) return false;
    money += p * inv[i].count;
    say("Sold for " + std::to_string(p * inv[i].count) + "g");
    sfx("purchaseClick");
    inv[i] = Slot();
    return true;
}

// ------------------------------------------------------------ actions

const FurniturePlacement* Game::bedIn(const std::string& map) const {
    for (auto& fp : furniture) {
        if (fp.map != map) continue;
        auto fd = data.furniture.find(fp.id);
        if (fd != data.furniture.end() && fd->second.type == "bed") return &fp;
    }
    return nullptr;
}

bool Game::houseDoor(int level, int& x, int& y) const {
    std::string p = level <= 0 ? "maps/FarmHouse.smap" : "maps/FarmHouse" + std::to_string(std::min(level, 2)) + ".smap";
    if (!data.vfs.exists(p)) return false;
    TileMap m;
    if (!m.load(data.vfs.path(p), nullptr)) return false;
    const Warp* w = m.warpTo("Farm");
    if (!w) return false;
    x = w->x;
    y = w->y;
    return true;
}

bool Game::canSleepHere() const { return cur && cur->name == data.layout.bedMap; }

bool Game::tryInteract(int tx, int ty) {
    if (mineInteract(tx, ty)) return true;
    if (farmBuildingInteract(tx, ty) || hopperInteract(tx, ty) || animalInteract(tx, ty)) return true;
    if (cur->name == "Forest" && cartHere() && tx >= 26 && tx <= 31 && ty >= 10 && ty <= 14) {
        if (timeOfDay >= 20 * 60) say("The merchant has packed up for the night.");
        else openShop = "Traveler";
        return true;
    }
    if (cur->name == "FarmHouse" && cellarX >= 0 && tx == cellarX && ty == cellarY) {
        sfx("stairsdown");
        Location* c = loadLocation("Cellar");
        const Warp* w = c ? c->map.warpTo("FarmHouse") : nullptr;
        warpTo("Cellar", w ? w->x : 3, w ? w->y + 1 : 3, false);
        return true;
    }
    if (petInteract(tx, ty)) return true;
    if (kidInteract(tx, ty)) return true;
    if (!cur->fruitTrees.empty() && shakeFruitTree(tx, ty)) return true;
    if (NpcState* n = npcAt(tx, ty)) {
        if (inv[selected].type == SLOT_OBJECT) giveGift(*n, selected);
        else talkTo(*n);
        return true;
    }
    if (Placed* p = placedAt(tx, ty)) {
        if (!p->big && p->id == FENCE_GATE) {   // swing the gate
            p->input = p->input == 1 ? -1 : 1;
            sfx("doorClose");
            return true;
        }
        if (interactPlaced(*p)) return true;
    }
    if (shakeBush(tx, ty)) return true;
    if (pickWalnut(tx, ty)) return true;
    if (WorldObject* o = objectAt(tx, ty)) {
        if (!o->ore) {
            int id = o->itemId;
            if (addItem(id, 1)) {
                sfx("pickUpItem");
                gainXp(SK_FORAGING, 7);
                questItem(id, 1);
                if (has(PR_GATHERER) && rand32() % 5 == 0 && addItem(id, 1)) say("Found two!");
                const ObjectDef* od = data.object(id);
                say("+1 " + (od ? od->name : std::string("item")));
                cur->objects.erase(cur->objects.begin() + (o - &cur->objects[0]));
            }
            return true;
        }
    }
    if (cropLand()) {
        if (cropReady(tx, ty)) {
            Soil& s = cur->soils[(size_t)ty * cur->map.width() + tx];
            const CropDef* c = data.cropForSeed(s.seed);
            int n = c->harvestMin;
            int lvl = skillLevel(SK_FARMING);
            float gold = 0.2f * (lvl / 10.0f) + 0.01f, silver = std::min(0.75f, gold * 2.0f);
            float roll = (rand32() % 10000) / 10000.0f;
            int q = roll < gold ? 2 : roll < silver ? 1 : 0;
            if (!addItem(c->harvestId, n, q)) return true;
            itemFlies(c->harvestId, tx * TILE + 8.0f, ty * TILE + 6.0f, n);   // pops out of the soil to you
            sfx("harvest");
            const ObjectDef* o = data.object(c->harvestId);
            if (o) gainXp(SK_FARMING, (int)std::lround(16.0 * std::log(0.018 * std::max(0, o->price) + 1.0)));
            questItem(c->harvestId, n);
            if (cur->name == "IslandWest") walnutChance(0, 12);
            if (o) say("+" + std::to_string(n) + " " + o->name);
            if (c->regrowDays > 0) {
                s.regrowWait = (int8_t)c->regrowDays;
                s.regrown = true;
            } else {
                s.seed = -1;
                s.phase = 0;
                s.dayInPhase = 0;
            }
            return true;
        }
        for (auto& b : data.layout.bins)
            if (b.first == tx && b.second == ty) {
                openBin(tx, ty);
                return true;
            }
        if (tx == data.layout.mailX && ty == data.layout.mailY) {
            if (!openMail()) say("No new mail");
            return true;
        }
        if (tx == data.layout.doorX && ty == data.layout.doorY) {
            if (hasMap("FarmHouse")) enterFrom("FarmHouse", true);
            else goToBed();
            return true;
        }
    }
    const Layout& L = data.layout;
    int bx = L.bedX, by = L.bedY, bw = L.bedW, bh = L.bedH;
    if (const FurniturePlacement* bed = bedIn(cur->name)) {   // the bed where it actually is
        auto fd = data.furniture.find(bed->id);
        bx = bed->x; by = bed->y;
        if (fd != data.furniture.end()) { bw = fd->second.boxW; bh = fd->second.boxH; }
    }
    if (cur->name == L.bedMap && tx >= bx && tx < bx + bw && ty >= by && ty < by + bh) {
        goToBed();
        return true;
    }
    if (runMapAction(cur->map.action(tx, ty), false)) return true;
    {   // some things answer from a few steps away (over a fence)
        static const int D[4][2] = {{0, 1}, {1, 0}, {0, -1}, {-1, 0}};
        for (int k = 1; k <= 3; k++) {
            const std::string& far = cur->map.action(tx + D[facing & 3][0] * k, ty + D[facing & 3][1] * k);
            if (far.rfind("NPCSpeechMessageNoRadius", 0) == 0 && runMapAction(far, false)) return true;
        }
    }
    if (lastInteract(tx, ty)) return true;
    if (cur->name.rfind("CommunityCenter", 0) == 0) {   // the junimos' bundles
        bool can = mailReceived.count("canReadJunimoText") || eventsSeen.count("112");
        for (auto& kv : bundleGiven) can |= !kv.second.empty();   // an older save already working on them
        if (!can && hasEvent("WizardHouse", "112")) {
            say("A golden scroll, written in a script you can't read...");
            if (!mailReceived.count("seenJunimoNote")) {
                mailReceived.insert("seenJunimoNote");
                mailTomorrow.insert("wizardJunimoNote");
            }
            return true;
        }
        bundleView = 0;
        sfx("bigSelect");
        return true;
    }
    return false;
}

void Game::useSelected(int tx, int ty) {
    Slot& sl = inv[selected];
    if (sl.empty()) return;
    const TileMap& m = cur->map;
    bool inMap = m.inBounds(tx, ty);
    size_t idx = inMap ? (size_t)ty * m.width() + tx : 0;

    if (sl.type == SLOT_TOOL) {
        const ToolDef* t = data.tool(sl.id);
        if (!t) return;
        if (t->kind == TOOL_BOOTS || t->kind == TOOL_HAT) { wearSlot(selected); return; }
        if (energy <= 0) {
            say("Too tired...");
            return;
        }
        toolTimer = TOOL_TIME;
        toolKindAnim = t->kind;
        if (t->kind != TOOL_SCYTHE && t->kind != TOOL_WEAPON) energy = std::max(0, energy - ENERGY_PER_SWING);
        if (t->kind == TOOL_WEAPON) { swingWeapon(*t); return; }
        if (!inMap) return;
        if ((t->kind == TOOL_PICKAXE || t->kind == TOOL_AXE) && pickUpPlaced(tx, ty)) return;
        if (t->kind == TOOL_PICKAXE && floorAt(tx, ty) >= 0) {   // lift a path or floor
            int kind = floorAt(tx, ty);
            cur->floors[(size_t)ty * cur->map.width() + tx] = 0;
            addItem(FLOOR_ITEMS[kind], 1);
            sfx("hammer");
            return;
        }
        if (t->kind == TOOL_AXE && !cur->fruitTrees.empty() && chopFruitTree(tx, ty)) return;
        if ((t->kind == TOOL_PICKAXE || t->kind == TOOL_AXE || t->kind == TOOL_HOE) && !cur->clumps.empty() &&
            clumpAt(tx, ty) >= 0) {
            if (t->kind == TOOL_HOE) sfx("woodyHit");
            else hitClump(tx, ty, t->kind, t->upgrade);
            return;
        }
        Soil& s = cur->soils[idx];
        uint8_t& deb = cur->debris[idx];
        uint8_t f = m.flags(tx, ty);
        switch (t->kind) {
            case TOOL_HOE:
                if (cropLand() && (f & CF_DIGGABLE) && !(f & CF_WATER) && !s.tilled && !deb &&
                    !buildingAt(tx, ty) && m.tile(L_BUILDINGS, tx, ty) == NO_TILE) {
                    cutGrass(tx, ty);
                    s = Soil();
                    s.tilled = true;
                    s.watered = raining;
                    sfx("hoeHit");
                    burst("TerrainFeatures_hoeDirt", 0, 0, 16, 16, tx * TILE + 8.0f, ty * TILE + 10.0f, 5, 3);
                } else {
                    sfx("woodyHit");
                }
                break;
            case TOOL_CAN:
                if (f & CF_WATER) {
                    water = canCapacity();
                    sfx("slosh");
                    say("Watering can filled");
                } else if (s.tilled) {
                    if (water > 0) {
                        s.watered = true;
                        water--;
                        sfx("wateringCan");
                    } else {
                        say("Watering can is empty");
                    }
                }
                break;
            case TOOL_AXE:
                if (chopTree(tx, ty, t->upgrade)) break;
                sfx("axchop");
                if (deb == DEB_TWIG) {
                    int v = debrisVariant(tx, ty) & 1, idx = v ? 295 : 294;   // Maps/springobjects twigs
                    burst("Maps_springobjects", (idx % 24) * 16, (idx / 24) * 16, 16, 16, tx * TILE + 8.0f, ty * TILE + 10.0f, 7);
                    deb = DEB_NONE;
                    if (addItem(OBJ_WOOD, 1)) itemFlies(OBJ_WOOD, tx * TILE + 8.0f, ty * TILE + 10.0f);
                    gainXp(SK_FORAGING, 1);
                }
                break;
            case TOOL_PICKAXE:
                sfx("hammer");
                if (WorldObject* o = objectAt(tx, ty)) {
                    if (o->ore) {
                        sfx("stoneCrack");
                        if (const ObjectDef* nd = data.object(o->itemId))
                            burst(nd->texture, (nd->spriteIndex % 24) * 16, (nd->spriteIndex / 24) * 16, 16, 16,
                                  tx * TILE + 8.0f, ty * TILE + 10.0f, 7);
                        int node = o->itemId;
                        int drop = oreNodeDrop(o->itemId);
                        int n = drop == 390 ? 1 : 1 + (int)(rand32() % 2);
                        if (has(PR_MINER) && drop != 390) n++;
                        cur->objects.erase(cur->objects.begin() + (o - &cur->objects[0]));
                        if (addItem(drop, n)) itemFlies(drop, tx * TILE + 8.0f, ty * TILE + 10.0f, n);
                        gainXp(SK_MINING, 5);
                        if (cur->mineLevel > 300) walnutChance(2, 25);
                        qiBeanChance(20);
                        questItem(drop, n);
                        mineRockBroken(tx, ty, node);
                        const ObjectDef* od = data.object(drop);
                        if (od) say("+" + std::to_string(n) + " " + od->name);
                    }
                } else if (deb == DEB_STONE) {
                    int v = debrisVariant(tx, ty) & 1, idx = v ? 450 : 343;   // Maps/springobjects stones
                    burst("Maps_springobjects", (idx % 24) * 16, (idx / 24) * 16, 16, 16, tx * TILE + 8.0f, ty * TILE + 10.0f, 7);
                    deb = DEB_NONE;
                    sfx("stoneCrack");
                    if (addItem(OBJ_STONE, 1)) itemFlies(OBJ_STONE, tx * TILE + 8.0f, ty * TILE + 10.0f);
                    gainXp(SK_MINING, 1);
                } else if ((s.tilled && s.seed < 0) || s.dead) {
                    s = Soil();
                }
                break;
            case TOOL_SCYTHE: {
                sfx("swordswipe");
                // the blade sweeps the tile in front and the two beside it
                bool cut = false;
                for (int k = -1; k <= 1; k++) {
                    int gx = tx + ((facing == FACE_UP || facing == FACE_DOWN) ? k : 0);
                    int gy = ty + ((facing == FACE_LEFT || facing == FACE_RIGHT) ? k : 0);
                    cut |= cutGrass(gx, gy);
                }
                if (cut) sfx("cut");
                if (deb == DEB_WEED) {
                    int v = debrisVariant(tx, ty) & 1, idx = v ? 675 : 674;   // Maps/springobjects weeds
                    burst("Maps_springobjects", (idx % 24) * 16, (idx / 24) * 16, 16, 16, tx * TILE + 8.0f, ty * TILE + 10.0f, 6);
                    deb = DEB_NONE;
                    sfx("cut");
                    if (rand32() % 2 && addItem(OBJ_FIBER, 1)) itemFlies(OBJ_FIBER, tx * TILE + 8.0f, ty * TILE + 10.0f);
                } else if (s.dead) {
                    s.seed = -1;
                    s.dead = false;
                }
                break;
            }
            default:
                break;
        }
        return;
    }

    if (plantFruitTree(tx, ty)) return;
    if (plantTree(tx, ty)) return;
    if (placeSelected(tx, ty)) return;
    const CropDef* crop = sl.type == SLOT_OBJECT ? data.cropForSeed(sl.id) : nullptr;
    if (crop) {
        if (!inMap || !cropLand()) return;
        Soil& s = cur->soils[idx];
        if (!s.tilled || s.seed >= 0) return;
        if (!inGreenhouse() && !(crop->seasonMask & (1 << season))) {
            say("Can't plant this now");
            return;
        }
        s.seed = sl.id;
        s.phase = 0;
        s.dayInPhase = 0;
        s.regrowWait = -1;
        s.regrown = false;
        s.dead = false;
        s.variant = (uint8_t)(rand32() & 1);
        sfx("dirtyHit");
        if (--sl.count <= 0) sl = Slot();
        return;
    }
    eatSlot(selected);
}

void Game::burst(const std::string& tex, int sx, int sy, int sw, int sh, float x, float y, int n, int piece) {
    if (effects.size() > 80) return;
    for (int i = 0; i < n; i++) {
        Effect e;
        e.tex = tex;
        e.sw = e.sh = piece;
        e.sx = sx + (int)(rand32() % (uint32_t)std::max(1, sw - piece));
        e.sy = sy + (int)(rand32() % (uint32_t)std::max(1, sh - piece));
        float a = (float)(rand32() % 1000) / 1000.0f * 3.14159f;   // upward half-circle
        float sp = 40.0f + (float)(rand32() % 60);
        e.x = x + (float)((int)(rand32() % 9) - 4);
        e.y = y - 4;
        e.vx = std::cos(a) * sp;
        e.vy = -std::sin(a) * sp - 30.0f;
        e.groundY = y + 6 + (float)(rand32() % 6);
        e.vrot = (float)((int)(rand32() % 21) - 10);
        e.life = 0.6f + (float)(rand32() % 30) / 100.0f;
        effects.push_back(e);
    }
}

bool Game::isDark() const {
    int start = season == WINTER ? 17 * 60 + 30 : season == FALL ? 18 * 60 + 30 : 19 * 60;
    return timeOfDay >= start || raining && timeOfDay >= 17 * 60;
}

void Game::updateNightTiles() {
    if (!cur) return;
    bool dark = isDark();
    if (dark == cur->nightTiles) return;
    const std::string& list = cur->map.prop(dark ? "NightTiles" : "DayTiles");
    if (!list.empty()) cur->map.applyTileSwaps(list);
    cur->nightTiles = dark;
}

void Game::updateEffects(float dt) {
    updateNightTiles();
    for (auto& n : notices) n.t -= dt;
    notices.erase(std::remove_if(notices.begin(), notices.end(), [](const Notice& n) { return n.t <= 0; }), notices.end());
    if (cur) {
        for (auto& r : cur->rustles) r.t -= dt;
        cur->rustles.erase(std::remove_if(cur->rustles.begin(), cur->rustles.end(),
                                          [](const Location::Rustle& r) { return r.t <= 0; }), cur->rustles.end());
        for (auto& b : cur->bushes) if (b.shake > 0) b.shake -= dt;
        for (auto& b : cur->clumps) if (b.shake > 0) b.shake -= dt;
        for (auto& b : cur->fruitTrees) if (b.shake > 0) b.shake -= dt;
    }
    for (auto& e : effects) {
        e.t += dt;
        if (e.kind == Effect::FALLING_TREE) continue;   // angle comes from t when drawn
        if (e.kind == Effect::ITEM && e.t > 0.55f) {    // pulled in to the farmer
            float tx = px, ty = py - 14;
            float dx = tx - e.x, dy = ty - e.y, d = std::sqrt(dx * dx + dy * dy);
            float sp = 120.0f + (e.t - 0.55f) * 500.0f;
            if (d < 4 || d < sp * dt) { e.t = e.life; continue; }
            e.x += dx / d * sp * dt;
            e.y += dy / d * sp * dt;
            continue;
        }
        e.vy += 420.0f * dt;
        e.x += e.vx * dt;
        e.y += e.vy * dt;
        e.rot += e.vrot * dt;
        if (e.y > e.groundY) {   // land and skid to a stop
            e.y = e.groundY;
            e.vy = e.vy > 60 ? -e.vy * 0.3f : 0;
            e.vx *= 0.5f;
            e.vrot *= 0.5f;
        }
    }
    effects.erase(std::remove_if(effects.begin(), effects.end(), [](const Effect& e) { return e.t >= e.life; }),
                  effects.end());
}

void Game::update(float dt, const Input& in) {
    if (toastTime > 0) toastTime -= dt;
    if (arcade.game) { arcadeUpdate(dt, in); return; }
    if (packAsk > 0) packAsk -= dt;
    petUpdate(dt);
    kidsUpdate(dt);
    if (weddingToday && !cutscene.active && !dialogue.active && cur) {
        weddingToday = false;
        if (!startWedding()) say("You married " + spouse + "! They've moved into your house.");
    }
    if (!morningNote.empty() && !cutscene.active && timeOfDay >= DAY_START + 10) { say(morningNote); morningNote.clear(); }
    {   // the shipping bin's lid swings open as you come near
        bool near = binOpen;
        if (cur && cur->isFarm)
            for (auto& b : data.layout.bins)
                near |= std::fabs(px - (b.first * TILE + 8)) < 28 && std::fabs(py - (b.second * TILE + 8)) < 30;
        binLid = std::max(0.0f, std::min(1.0f, binLid + (near ? dt : -dt) * 5.0f));
    }
    if (lightning > 0) lightning = std::max(0.0f, lightning - dt * 2.5f);
    if (stormy && raining && cur && cur->map.outdoors && !cutscene.active) {
        nextLightning -= dt;
        if (nextLightning <= 0) {
            lightning = 1.0f;
            sfx("thunder");
            nextLightning = 8.0f + (float)(rand32() % 1400) / 100.0f;
        }
    }
    updateEffects(dt);
    for (auto& t : cur->trees)
        if (t.shake > 0) t.shake -= dt;
    if (fade > 0) fade = std::max(0.0f, fade - dt * 3.0f);
    if (warpCooldown > 0) warpCooldown -= dt;
    if (cutscene.active) {           // cutscenes drive everything (and stop the clock)
        moving = false;
        eventUpdate(dt);
        return;
    }
    if (dialogue.active) {           // the world waits while someone's talking
        moving = false;   // the UI advances pages (it knows about long-page scrolling)
        return;
    }
    if (bundleView >= 0 || museumOpen) {   // looking at the bundles / the museum
        moving = false;
        return;
    }
    if (!placing.empty()) {          // choosing where Robin builds
        moving = false;
        updatePlacing(in, dt);
        return;
    }

    if (festivalActive < 0) clockAcc += dt;   // time stands still at a festival
    while (clockAcc >= SECONDS_PER_10MIN) {
        clockAcc -= SECONDS_PER_10MIN;
        timeOfDay += 10;
        if (timeOfDay >= DAY_END) {
            passedOut = true;
            goToBed();
            return;
        }
    }

    if (festivalActive < 0) npcsUpdate(dt);
    else festivalUpdate(dt);
    monstersUpdate(dt);
    animalsUpdate(dt);
    if (!cur) return;   // knocked out mid-update can't leave us nowhere, but be safe

    if (in.prev) selected = (selected + HOTBAR - 1) % HOTBAR;
    if (in.next) selected = (selected + 1) % HOTBAR;

    if (toolTimer > 0) {
        toolTimer -= dt;
        moving = false;
        return;
    }

    if (fishingActive()) {
        fishingUpdate(dt, in);
        return;
    }
    if (in.action) {   // fishing rod: start charging a cast
        const Slot& sel = inv[selected];
        const ToolDef* st = sel.type == SLOT_TOOL ? data.tool(sel.id) : nullptr;
        if (st && st->kind == TOOL_ROD) {
            int tx, ty;
            targetTile(tx, ty);
            if (!tryInteract(tx, ty)) {
                if (energy <= 0) say("Too tired...");
                else startFishing();
            }
            return;
        }
    }

    // Upgraded hoe / watering can: hold A to charge, let go to swing.
    {
        const Slot& sel = inv[selected];
        const ToolDef* st = sel.type == SLOT_TOOL ? data.tool(sel.id) : nullptr;
        bool chargeable = st && (st->kind == TOOL_HOE || st->kind == TOOL_CAN) && st->upgrade > 0;
        if (charging) {
            moving = false;
            if (!chargeable) {
                charging = false;
            } else if (in.hold) {
                chargeTime += dt;
                return;
            } else {
                int level = chargeLevel();
                charging = false;
                int tx, ty;
                targetTile(tx, ty);
                if (level == 0) {
                    useSelected(tx, ty);
                } else {
                    int before = energy;
                    std::vector<std::pair<int, int>> tiles;
                    chargedTiles(level, tiles);
                    for (auto& t : tiles) useSelected(t.first, t.second);
                    energy = std::max(0, before - ENERGY_PER_SWING * (level + 1));
                }
                return;
            }
        } else if (in.action && chargeable) {
            int tx, ty;
            targetTile(tx, ty);
            if (!tryInteract(tx, ty)) {
                charging = true;
                chargeTime = 0;
            }
            return;
        }
    }

    float mx = in.mx, my = in.my;
    float len = std::sqrt(mx * mx + my * my);
    moving = len > 0.2f;
    // a second stick points you: you can back away from something and still face it
    bool aimed = in.ax || in.ay;
    if (aimed) facing = in.ax > 0 ? FACE_RIGHT : in.ax < 0 ? FACE_LEFT : in.ay > 0 ? FACE_DOWN : FACE_UP;
    if (moving) {
        if (len > 1) { mx /= len; my /= len; }
        if (aimed) {}
        else if (std::fabs(mx) > std::fabs(my)) facing = mx > 0 ? FACE_RIGHT : FACE_LEFT;
        else facing = my > 0 ? FACE_DOWN : FACE_UP;
        float sp = (in.walk ? WALK_SPEED : RUN_SPEED) * (riding ? 1.6f : 1.0f) * (speedBoost > 0 ? 1.25f : 1.0f) * dt;
        float nx = px + mx * sp, ny = py + my * sp;

        // Map-edge and path warps: stepping onto a warp tile, even off the edge.
        if (warpCooldown <= 0) {
            // test the leading edge of the feet box, since walls stop the centre short
            float ex = nx + (mx > 0.3f ? 6.0f : mx < -0.3f ? -6.0f : 0.0f);
            float ey = ny + (my > 0.3f ? 0.5f : my < -0.3f ? -7.0f : -3.0f);
            int wx = (int)std::floor(ex / TILE), wy = (int)std::floor(ey / TILE);
            if (const Warp* w = cur->map.warpAt(wx, wy)) {
                Warp copy = *w;
                warpTo(copy.target, copy.tx, copy.ty);
                return;
            }
        }
        bool movedX = false, movedY = false;
        {   // move in small steps so a slow frame still stops flush against a wall, slide along
            // walls, and round corners: walking straight at a corner eases you past it
            float remX = nx - px, remY = ny - py;
            int steps = std::max(1, (int)std::ceil(std::max(std::fabs(remX), std::fabs(remY)) / 2.0f));
            float sx = remX / steps, sy = remY / steps;
            for (int k = 0; k < steps; k++) {
                bool okX = sx != 0 && !blockedPx(px + sx, py);
                bool okY = sy != 0 && !blockedPx(px, py + sy);
                if (okX) { px += sx; movedX = true; }
                if (okY && !blockedPx(px, py + sy)) { py += sy; movedY = true; }
                if (sx != 0 && !okX && sy == 0) {          // blocked going sideways: a gap just above or below?
                    for (int off = 1; off <= 5; off++) {
                        if (!blockedPx(px + sx, py - off) && !blockedPx(px, py - off)) { py -= std::min((float)off, std::fabs(sx)); break; }
                        if (!blockedPx(px + sx, py + off) && !blockedPx(px, py + off)) { py += std::min((float)off, std::fabs(sx)); break; }
                    }
                } else if (sy != 0 && !okY && sx == 0) {   // blocked going up or down: a gap to one side?
                    for (int off = 1; off <= 5; off++) {
                        if (!blockedPx(px - off, py + sy) && !blockedPx(px - off, py)) { px -= std::min((float)off, std::fabs(sy)); break; }
                        if (!blockedPx(px + off, py + sy) && !blockedPx(px + off, py)) { px += std::min((float)off, std::fabs(sy)); break; }
                    }
                }
            }
        }
        {   // walking through grass sets it swaying (and rustles)
            int cx = (int)std::floor(px / TILE), cy = (int)std::floor((py - 2) / TILE);
            int cell = cur->map.inBounds(cx, cy) ? cy * cur->map.width() + cx : -1;
            if (cell != lastGrassCell && cell >= 0 && grassAt(cx, cy)) {
                bool already = false;
                for (auto& r : cur->rustles) if (r.cell == cell) { r.t = 0.5f; already = true; }
                if (!already) cur->rustles.push_back({cell, 0.5f});
                sfx("grassyStep");
            }
            lastGrassCell = cell;
        }
        if ((nx != px && !movedX) || (ny != py && !movedY)) {   // bumped into a bush: it shakes
            int bx, by;
            targetTile(bx, by);
            int b = bushAt(bx, by);
            if (b >= 0 && cur->bushes[b].shake <= 0) {
                cur->bushes[b].shake = 0.4f;
                sfx("leafrustle");
            }
        }
        walkTime += dt * (in.walk ? 1.0f : 1.6f);

        // Walking into a door opens it.
        if (!movedX && !movedY && warpCooldown <= 0) {
            int tx, ty;
            targetTile(tx, ty);
            if (cur->isFarm && tx == data.layout.doorX && ty == data.layout.doorY) {
                tryInteract(tx, ty);
                return;
            }
            if (cur->isFarm)   // a farm building's door
                if (FarmBuilding* fb = buildingAtTile(tx, ty))
                    if (const BuildingType* bt = typeOf(*fb))
                        if (!bt->interior.empty() && tx == fb->x + bt->doorX && ty == fb->y + bt->doorY &&
                            (fb->daysLeft <= 0 || !fb->upgradingTo.empty())) {
                            farmBuildingInteract(tx, ty);
                            return;
                        }
            if (runMapAction(cur->map.action(tx, ty), true)) return;
        }
    } else {
        walkTime = 0;
    }

    if (riding) {   // the horse goes where you go
        horseLoc = cur->name;
        horseX = px;
        horseY = py;
        horseFacing = facing;
    }
    if (in.action) {
        int tx, ty;
        targetTile(tx, ty);
        if (riding) {   // A on horseback: get off (or talk / open things in front)
            if (!tryInteract(tx, ty)) { riding = false; sfx("dwop"); }
            return;
        }
        if (horseInteract(tx, ty)) return;
        if (!tryInteract(tx, ty)) useSelected(tx, ty);
    }
}

bool Game::goToBed() {
    if (!passedOut && hasMap(data.layout.bedMap) && !canSleepHere()) {
        say("Go home to sleep");
        return false;
    }
    int earned = 0;
    for (auto& s : shipped) {
        earned += itemPrice(s) * s.count;
        if (s.type == SLOT_OBJECT) {
            shippedEver[s.id] += s.count;
            orderEvent("ship", s.id, s.count, "");
            orderEvent("shipvalue", s.id, itemPrice(s) * s.count, "");
        }
    }
    shipped.clear();
    money += earned;
    totalEarned += earned;
    runTriggers("DayEnding");
    lastEarnings = earned;

    int oldSeason = season;
    day++;
    if (day > 28) {
        day = 1;
        season = (season + 1) % 4;
        if (season == SPRING) year++;
    }
    bool newSeason = season != oldSeason;
    if (newSeason) refreshSeasonMaps();

    if (!engagedTo.empty() && weddingDay >= 0 && absoluteDay() >= weddingDay) {   // the wedding day
        spouse = engagedTo;
        dating.erase(spouse);
        engagedTo.clear();
        weddingDay = -1;
        weddingToday = true;   // the ceremony in the town square, as soon as you're up
    }
    if (houseUpgradeDays > 0 && --houseUpgradeDays == 0) {
        int ox, oy, nx, ny;
        if (houseDoor(houseLevel, ox, oy) && houseDoor(houseLevel + 1, nx, ny))
            for (auto& fp : furniture)
                if (fp.map == "FarmHouse") { fp.x += nx - ox; fp.y += ny - oy; }
        houseLevel++;
        locations.erase("FarmHouse");   // the bigger house loads next time
        say(houseLevel == 1 ? "Robin finished your house: you have a kitchen now!" : houseLevel == 3 ? "Robin dug out your cellar. The casks are ready." : "Robin finished your house upgrade!");
        if (houseLevel == 3) stockCellar();
    }
    growSoils(*farm, newSeason, false);
    riding = false;
    horseMorning();
    fruitTreesNight(*farm, false);
    for (auto& b : buildings)   // the greenhouse grows anything, any time of year
        if (b.type == "Greenhouse" && b.daysLeft <= 0)
            if (Location* gh = loadLocation(interiorName(b))) {
                growSoils(*gh, newSeason, true);
                fruitTreesNight(*gh, true);
            }

    {   // the island farm: summer all year
        auto iw = locations.find("IslandWest");
        if (iw != locations.end()) growSoils(*iw->second, newSeason, true);
    }

    bool stormedLastNight = stormy && raining;
    raining = (day > 3 && season != WINTER && rand32() % 100 < 18);
    windy = !raining && day > 2 && (season == SPRING || season == FALL) && rand32() % 100 < 15;
    stormy = raining && rand32() % 100 < (season == SUMMER ? 40 : 15);
    snowing = season == WINTER && day > 1 && rand32() % 100 < 25;
    if (raining)
        for (auto& s : farm->soils)
            if (s.tilled) s.watered = true;

    specialMorning(stormedLastNight);
    applyLevelUps();
    sprinklersWater();
    machinesDayStart();
    treesGrow();
    growGrass();
    animalsNight();
    morningNote.clear();
    nightEvents();
    kidsNight();
    ordersNight();

    uint32_t r = rand32();
    spawnDebris(6, r);

    water = canCapacity();
    dayEnded = true;
    deliverMorningMail();
    startDay();
    wakeUp();
    if (passedOut) energy = maxEnergy / 2;
    return true;
}

// ---------------------------------------------------------------- saving

static const uint32_t SAVE_MAGIC = 0x53563344;   // "SV3D"
static const uint32_t SAVE_VERSION = 36;   // 2: villager friendships, 3: events seen + mail,
                                          // 4: skills, flavors, placed machines/chests
                                          // 5: tool at the blacksmith
                                          // 6: farm trees
                                          // 7: quests
                                          // 8: skill levels (applied at night)
                                          // 9: mailbox
                                          // 10: professions
                                          // 11: your character + farm type
                                          // 12: farm grass
                                          // 13: big stumps, logs and boulders
                                          // 14: deepest mine floor
                                          // 15: farm buildings and animals
                                          // 16: fruit trees, soil inside buildings (greenhouse)
                                          // 17: Community Center bundles
                                          // 18: item quality
                                          // 19: house upgrades
                                          // 20: dating, engagement, marriage
                                          // 21: furniture
                                          // 22: museum donations
                                          // 23: fish ponds
                                          // 24: rings, boots, hat
                                          // 25: backpack size
                                          // 26: what the mill and junimo huts hold
                                          // 27: your pet
                                          // 28: several pets, their gifts
                                          // 29: children
                                          // 30: collections
                                          // 31: special orders
                                          // 32: the island farm's soil, arcade best

namespace {
struct W {
    FILE* f;
    void u32(uint32_t v) { fwrite(&v, 4, 1, f); }
    void i32(int32_t v) { fwrite(&v, 4, 1, f); }
    void u8(uint8_t v) { fwrite(&v, 1, 1, f); }
    void str(const std::string& s) {
        uint16_t len = (uint16_t)std::min<size_t>(s.size(), 65535);
        fwrite(&len, 2, 1, f);
        fwrite(s.data(), 1, len, f);
    }
    void slot(const Slot& s) { u8(s.type); i32(s.id); i32(s.count); i32(s.flavor); }
};
struct R {
    FILE* f;
    bool ok = true;
    uint32_t u32() { uint32_t v = 0; ok &= fread(&v, 4, 1, f) == 1; return v; }
    int32_t i32() { int32_t v = 0; ok &= fread(&v, 4, 1, f) == 1; return v; }
    uint8_t u8() { uint8_t v = 0; ok &= fread(&v, 1, 1, f) == 1; return v; }
    std::string str() {
        uint16_t len = 0;
        ok &= fread(&len, 2, 1, f) == 1;
        std::string s(ok ? len : 0, ' ');
        if (ok && len) ok &= fread(&s[0], 1, len, f) == len;
        return s;
    }
    Slot slot() { Slot s; s.type = (SlotType)u8(); s.id = i32(); s.count = i32(); s.flavor = i32(); return s; }
};
} // namespace

bool Game::save(const std::string& path) const {
    if (!farm) return false;
    std::string tmp = path + ".tmp";
    FILE* f = fopen(tmp.c_str(), "wb");
    if (!f) return false;
    W w{f};
    const TileMap& m = farm->map;
    w.u32(SAVE_MAGIC);
    w.u32(SAVE_VERSION);
    w.i32(me.farmType);   // v11: needed before the farm map loads
    w.i32(m.width());
    w.i32(m.height());
    w.i32(day); w.i32(season); w.i32(year);
    w.i32(money); w.i32(water);
    w.u8((uint8_t)(raining | (stormy << 1) | (snowing << 2) | (windy << 3)));   // weather bits
    w.u32(rngState);
    w.i32(selected);
    for (auto& s : inv) { w.u8(s.type); w.i32(s.id); w.i32(s.count); }
    w.u32((uint32_t)shipped.size());
    for (auto& s : shipped) { w.u8(s.type); w.i32(s.id); w.i32(s.count); }
    for (size_t i = 0; i < farm->soils.size(); i++) {
        const Soil& s = farm->soils[i];
        uint8_t bits = (s.tilled ? 1 : 0) | (s.watered ? 2 : 0) | (s.dead ? 4 : 0) | (s.regrown ? 8 : 0);
        w.u8(bits);
        if (s.tilled) {
            w.i32(s.seed); w.u8(s.phase); w.u8(s.dayInPhase);
            w.u8((uint8_t)s.regrowWait); w.u8(s.variant);
        }
        w.u8(farm->debris[i]);
        if (farm->debris[i]) w.u8(farm->debrisVar[i]);
    }
    w.u32((uint32_t)npcs.size());
    for (auto& n : npcs) {
        const std::string& nm = n.def->name;
        w.u8((uint8_t)std::min<size_t>(nm.size(), 255));
        fwrite(nm.data(), 1, std::min<size_t>(nm.size(), 255), f);
        w.i32(n.friendship);
        w.u8(n.introduced ? 1 : 0);
        w.u8((uint8_t)n.giftsWeek);
    }
    for (const std::set<std::string>* set : {&eventsSeen, &mailReceived}) {
        w.u32((uint32_t)set->size());
        for (auto& id : *set) {
            uint16_t len = (uint16_t)std::min<size_t>(id.size(), 65535);
            fwrite(&len, 2, 1, f);
            fwrite(id.data(), 1, len, f);
        }
    }
    // v4
    for (int i = 0; i < SK_COUNT; i++) w.i32(skillXp[i]);
    for (auto& s : inv) w.i32(s.flavor);
    for (auto& s : shipped) w.i32(s.flavor);
    w.u32((uint32_t)placed.size());
    for (auto& kv : placed) {
        w.str(kv.first);
        w.u32((uint32_t)kv.second.size());
        for (auto& p : kv.second) {
            w.i32(p.x); w.i32(p.y); w.u8(p.big); w.i32(p.id);
            w.i32(p.rule); w.i32(p.input);
            w.u32((uint32_t)(p.readyAt & 0xFFFFFFFF)); w.i32((int32_t)(p.readyAt >> 32));
            w.slot(p.held);
            w.u32((uint32_t)p.items.size());
            for (auto& it : p.items) w.slot(it);
        }
    }
    w.i32(upgradeTool);
    w.i32(upgradeReadyDay);
    for (int i = 0; i < SK_COUNT; i++) w.i32(skillLevels[i]);
    w.u32((uint32_t)mailbox.size());
    for (auto& k : mailbox) w.str(k);
    w.u32((uint32_t)mailTomorrow.size());
    for (auto& k : mailTomorrow) w.str(k);
    w.u32((uint32_t)triggersApplied.size());
    for (auto& k : triggersApplied) w.str(k);
    w.u32((uint32_t)(totalEarned & 0xFFFFFFFF));
    w.i32((int32_t)(totalEarned >> 32));
    w.u32((uint32_t)professions.size());
    for (int p : professions) w.i32(p);
    w.str(me.name); w.str(me.farm); w.str(me.favorite);
    w.i32(me.farmType);
    const Outfit& o = me.outfit;
    w.i32(o.shirt); w.i32(o.pants); w.i32(o.hair);
    w.u8(o.pantsR); w.u8(o.pantsG); w.u8(o.pantsB); w.u8(o.hairR); w.u8(o.hairG); w.u8(o.hairB);
    w.u32((uint32_t)quests.size());
    for (auto& q : quests) { w.str(q.id); w.i32(q.progress); w.u8(q.done); }
    w.u32((uint32_t)questsFinished.size());
    for (auto& id : questsFinished) w.str(id);
    w.u32((uint32_t)farm->trees.size());
    for (auto& t : farm->trees) {
        w.i32(t.x); w.i32(t.y); w.i32(t.type);
        w.u8(t.stage); w.u8((uint8_t)t.health); w.u8(t.stump);
    }
    uint32_t ng = 0;
    for (uint8_t g : farm->grass) ng += g != 0;
    w.u32(ng);
    for (size_t i = 0; i < farm->grass.size(); i++)
        if (farm->grass[i]) { w.u32((uint32_t)i); w.u8(farm->grass[i]); }
    w.u32((uint32_t)farm->clumps.size());
    for (auto& c : farm->clumps) { w.i32(c.x); w.i32(c.y); w.u8(c.kind); w.u8((uint8_t)c.health); }
    w.i32(mineDeepest);
    w.i32(hay);
    w.i32(nextBuildingId);
    w.u32((uint32_t)buildings.size());
    for (auto& b : buildings) {
        w.i32(b.id); w.str(b.type); w.i32(b.x); w.i32(b.y); w.i32(b.daysLeft); w.str(b.upgradingTo);
        w.u32((uint32_t)b.troughs.size());
        for (uint8_t t : b.troughs) w.u8(t);
        // what's lying inside (eggs...)
        auto it = locations.find(interiorName(b));
        std::vector<WorldObject> none;
        const std::vector<WorldObject>& objs = it != locations.end() ? it->second->objects : none;
        w.u32((uint32_t)objs.size());
        for (auto& o : objs) { w.i32(o.x); w.i32(o.y); w.i32(o.itemId); }
        // soil and fruit trees inside (the greenhouse)
        const Location* in = it != locations.end() ? it->second.get() : nullptr;
        uint32_t nt = 0;
        if (in) for (auto& s : in->soils) nt += s.tilled;
        w.u32(nt);
        if (in)
            for (size_t i = 0; i < in->soils.size(); i++) {
                const Soil& s = in->soils[i];
                if (!s.tilled) continue;
                w.u32((uint32_t)i); w.u8((s.watered ? 2 : 0) | (s.dead ? 4 : 0) | (s.regrown ? 8 : 0));
                w.i32(s.seed); w.u8(s.phase); w.u8(s.dayInPhase); w.u8((uint8_t)s.regrowWait);
            }
        w.u32(in ? (uint32_t)in->fruitTrees.size() : 0);
        if (in) for (auto& t : in->fruitTrees) { w.i32(t.x); w.i32(t.y); w.i32(t.sapling); w.i32(t.daysLeft); w.i32(t.fruit); }
    }
    w.u32((uint32_t)animals.size());
    for (auto& a : animals) {
        w.str(a.type); w.str(a.name); w.i32(a.home); w.i32(a.age); w.i32(a.friendship);
        w.i32(a.daysSinceProduce); w.i32(a.ready); w.u8(a.fed ? 1 : 0);
    }
    w.u32((uint32_t)farm->fruitTrees.size());
    for (auto& t : farm->fruitTrees) { w.i32(t.x); w.i32(t.y); w.i32(t.sapling); w.i32(t.daysLeft); w.i32(t.fruit); }
    w.u32((uint32_t)bundleDefs.size());
    for (auto& kv : bundleDefs) { w.str(kv.first); w.str(kv.second); }
    w.u32((uint32_t)bundleGiven.size());
    for (auto& kv : bundleGiven) {
        w.i32(kv.first);
        w.u32((uint32_t)kv.second.size());
        for (uint8_t v : kv.second) w.u8(v);
    }
    w.i32(houseLevel);
    w.i32(houseUpgradeDays);
    w.str(spouse);
    w.str(engagedTo);
    w.i32(weddingDay);
    w.u32((uint32_t)dating.size());
    for (auto& d : dating) w.str(d);
    w.u32((uint32_t)furniture.size());
    for (auto& fp : furniture) { w.str(fp.map); w.i32(fp.id); w.i32(fp.x); w.i32(fp.y); }
    w.u32((uint32_t)museumDonated.size());
    for (int id : museumDonated) w.i32(id);
    w.i32(me.petKind); w.i32(me.petBreed);
    {   // the island farm
        auto iw = locations.find("IslandWest");
        const Location* in = iw != locations.end() ? iw->second.get() : nullptr;
        uint32_t nt = 0;
        if (in) for (auto& s : in->soils) nt += s.tilled;
        w.u32(nt);
        if (in)
            for (size_t i = 0; i < in->soils.size(); i++) {
                const Soil& s = in->soils[i];
                if (!s.tilled) continue;
                w.u32((uint32_t)i); w.u8((s.watered ? 2 : 0) | (s.dead ? 4 : 0) | (s.regrown ? 8 : 0));
                w.i32(s.seed); w.u8(s.phase); w.u8(s.dayInPhase); w.u8((uint8_t)s.regrowWait);
            }
        w.i32(junimoBest);
    }
    w.i32(walnuts); w.i32(walnutsFound); w.i32(clubCoins);   // v33
    for (int k = 0; k < 4; k++) w.i32(walnutPools[k]);
    w.u32((uint32_t)walnutsGot.size());
    for (auto& k : walnutsGot) w.str(k);
    w.u32((uint32_t)stats.size());                            // v34
    for (auto& kv : stats) { w.str(kv.first); w.i32(kv.second); }
    w.i32(maxEnergy); w.i32(maxHealth);                       // v35
    {                                                         // v36: paths and floors, per location
        uint32_t nf = 0;
        for (auto& kv : locations)
            for (uint8_t f : kv.second->floors) nf += f != 0;
        w.u32(nf);
        for (auto& kv : locations) {
            int lw = kv.second->map.width();
            for (size_t i = 0; i < kv.second->floors.size(); i++)
                if (kv.second->floors[i]) { w.str(kv.first); w.i32((int)(i % lw)); w.i32((int)(i / lw)); w.u8(kv.second->floors[i]); }
        }
    }
    w.str(orderKey); w.i32(orderDays);
    w.u32((uint32_t)orderProgress.size());
    for (int v : orderProgress) w.i32(v);
    for (auto* m : {&shippedEver, &fishCaught, &cookedEver}) {
        w.u32((uint32_t)m->size());
        for (auto& kv : *m) { w.i32(kv.first); w.i32(kv.second); }
    }
    w.u32((uint32_t)craftedEver.size());
    for (int id : craftedEver) w.i32(id);
    w.i32(babyDue); w.u8(babyAsk);
    w.u32((uint32_t)kids.size());
    for (auto& k : kids) { w.str(k.name); w.u8(k.girl); w.u8(k.dark); w.i32(k.daysOld); w.i32(k.friendship); w.u8(k.talked); }
    w.u8(bowlFull);
    w.u32((uint32_t)pets.size());
    for (auto& p : pets) { w.i32(p.type); w.i32(p.breed); w.str(p.name); w.i32(p.friendship); w.u8(p.petted); w.i32(p.gift); }
    for (auto& b : buildings)
        for (auto* v : {&b.items, &b.pending}) {
            w.u32((uint32_t)v->size());
            for (auto& s : *v) w.slot(s);
        }
    w.i32(invSlots);
    for (auto& e : equip) w.slot(e);
    for (auto& b : buildings) { w.i32(b.pondFish); w.i32(b.pondPop); w.i32(b.pondDays); w.i32(b.pondOut); w.i32(b.pondOutN); }
    // item quality: inventory, shipped, then each chest / machine in order
    for (auto& s : inv) w.u8(s.quality);
    w.u32((uint32_t)shipped.size());
    for (auto& s : shipped) w.u8(s.quality);
    for (auto& kv : placed)
        for (auto& p : kv.second) {
            w.u8(p.held.quality);
            w.u32((uint32_t)p.items.size());
            for (auto& s : p.items) w.u8(s.quality);
        }
    bool ok = ferror(f) == 0;
    fclose(f);
    if (!ok) { remove(tmp.c_str()); return false; }
    remove(path.c_str());
    if (rename(tmp.c_str(), path.c_str()) != 0) return false;
    writeSaveInfo(path, *this);   // who and when, for the Load screen
    return true;
}

bool Game::load(const std::string& path, std::string* err) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    R r{f};
    uint32_t version = 0;
    if (r.u32() != SAVE_MAGIC || (version = r.u32()) < 1 || version > SAVE_VERSION) {
        fclose(f);
        return false;
    }
    maxEnergy = MAX_ENERGY;
    maxHealth = 100;
    me = Character();
    me.name = data.layout.farmerName;
    me.outfit = data.layout.outfit;
    if (version >= 11) me.farmType = r.i32();
    if (me.farmType < 0 || me.farmType >= (int)data.farmTypes.size()) me.farmType = 0;
    locations.clear();
    farm = loadLocation("Farm", err);
    if (!farm) { fclose(f); return false; }
    cur = farm;
    const TileMap& m = farm->map;
    if (r.i32() != m.width() || r.i32() != m.height()) {
        fclose(f);
        return false;
    }
    day = r.i32(); season = r.i32() & 3; year = r.i32();
    money = r.i32(); water = r.i32();
    {
        uint8_t wb = r.u8();
        raining = wb & 1;
        stormy = (wb & 2) != 0;
        snowing = (wb & 4) != 0;
        windy = (wb & 8) != 0;
    }
    rngState = r.u32();
    selected = r.i32();
    if (selected < 0 || selected >= INV_SIZE) selected = 0;
    for (auto& s : inv) { s.type = (SlotType)r.u8(); s.id = r.i32(); s.count = r.i32(); }
    uint32_t ns = r.u32();
    shipped.clear();
    for (uint32_t i = 0; i < ns && r.ok && i < 4096; i++) {
        Slot s; s.type = (SlotType)r.u8(); s.id = r.i32(); s.count = r.i32();
        shipped.push_back(s);
    }
    for (size_t i = 0; i < farm->soils.size() && r.ok; i++) {
        Soil& s = farm->soils[i];
        uint8_t bits = r.u8();
        s.tilled = bits & 1; s.watered = bits & 2; s.dead = bits & 4; s.regrown = bits & 8;
        if (s.tilled) {
            s.seed = r.i32(); s.phase = r.u8(); s.dayInPhase = r.u8();
            s.regrowWait = (int8_t)r.u8(); s.variant = r.u8();
        }
        farm->debris[i] = r.u8();
        if (farm->debris[i]) farm->debrisVar[i] = r.u8();
    }
    npcs.clear();
    dialogue = DialogueBox();
    npcsStartDay();   // creates the villager list
    if (version >= 2 && r.ok) {
        uint32_t nn = r.u32();
        for (uint32_t i = 0; i < nn && r.ok && i < 1000; i++) {
            uint8_t len = r.u8();
            std::string nm(len, ' ');
            if (len && fread(&nm[0], 1, len, f) != len) r.ok = false;
            int fr = r.i32();
            uint8_t intro = r.u8(), gw = r.u8();
            for (auto& n : npcs)
                if (n.def->name == nm) {
                    n.friendship = fr;
                    n.introduced = intro != 0;
                    n.giftsWeek = gw;
                }
        }
    }
    eventsSeen.clear();
    mailReceived.clear();
    cutscene = Cutscene();
    if (version >= 3 && r.ok) {
        for (std::set<std::string>* set : {&eventsSeen, &mailReceived}) {
            uint32_t cnt = r.u32();
            for (uint32_t i = 0; i < cnt && r.ok && i < 100000; i++) {
                uint16_t len = 0;
                if (fread(&len, 2, 1, f) != 1) { r.ok = false; break; }
                std::string id(len, ' ');
                if (len && fread(&id[0], 1, len, f) != len) { r.ok = false; break; }
                set->insert(id);
            }
        }
    }
    for (auto& x : skillXp) x = 0;
    placed.clear();
    chestOpen = false;
    if (version >= 4 && r.ok) {
        for (auto& x : skillXp) x = std::max<int>(0, r.i32());
        for (auto& s : inv) s.flavor = r.i32();
        for (auto& s : shipped) s.flavor = r.i32();
        uint32_t nl = r.u32();
        for (uint32_t i = 0; i < nl && r.ok && i < 10000; i++) {
            std::string name = r.str();
            uint32_t np = r.u32();
            auto& v = placed[name];
            for (uint32_t j = 0; j < np && r.ok && j < 100000; j++) {
                Placed p;
                p.x = r.i32(); p.y = r.i32(); p.big = r.u8() != 0; p.id = r.i32();
                p.rule = r.i32(); p.input = r.i32();
                uint32_t lo = r.u32();
                int32_t hi = r.i32();
                p.readyAt = ((int64_t)hi << 32) | lo;
                p.held = r.slot();
                uint32_t ni = r.u32();
                for (uint32_t k = 0; k < ni && r.ok && k < 1000; k++) p.items.push_back(r.slot());
                if (p.rule >= (int)data.machineRules.size()) p.rule = -1;
                v.push_back(p);
            }
        }
    }
    upgradeTool = -1;
    charging = false;
    if (version >= 5 && r.ok) {
        upgradeTool = r.i32();
        upgradeReadyDay = r.i32();
        if (upgradeTool >= data.toolCount()) upgradeTool = -1;
    }
    quests.clear();
    questsFinished.clear();
    levelUps.clear();
    for (int i = 0; i < SK_COUNT; i++) skillLevels[i] = xpLevel(i);   // older saves
    if (version >= 8 && r.ok)
        for (int i = 0; i < SK_COUNT; i++) skillLevels[i] = std::max(0, std::min<int>(10, r.i32()));
    mailbox.clear();
    mailTomorrow.clear();
    triggersApplied.clear();
    totalEarned = 0;
    letterOpen = false;
    if (version >= 9 && r.ok) {
        uint32_t n = r.u32();
        for (uint32_t i = 0; i < n && r.ok && i < 1000; i++) mailbox.push_back(r.str());
        n = r.u32();
        for (uint32_t i = 0; i < n && r.ok && i < 1000; i++) mailTomorrow.insert(r.str());
        n = r.u32();
        for (uint32_t i = 0; i < n && r.ok && i < 10000; i++) triggersApplied.insert(r.str());
        uint32_t lo = r.u32();
        int32_t hi = r.i32();
        totalEarned = ((long long)hi << 32) | lo;
    }
    professions.clear();
    if (version >= 10 && r.ok) {
        uint32_t n = r.u32();
        for (uint32_t i = 0; i < n && r.ok && i < 30; i++) professions.insert(r.i32());
    }
    if (version >= 11 && r.ok) {
        me.name = r.str(); me.farm = r.str(); me.favorite = r.str();
        int ft = r.i32();
        (void)ft;   // already read from the header
        Outfit& o = me.outfit;
        o.shirt = r.i32(); o.pants = r.i32(); o.hair = r.i32();
        o.pantsR = r.u8(); o.pantsG = r.u8(); o.pantsB = r.u8(); o.hairR = r.u8(); o.hairG = r.u8(); o.hairB = r.u8();
    }
    if (version >= 7 && r.ok) {
        uint32_t nq = r.u32();
        for (uint32_t i = 0; i < nq && r.ok && i < 1000; i++) {
            QuestState q;
            q.id = r.str();
            q.progress = r.i32();
            q.done = r.u8() != 0;
            if (data.quest(q.id)) quests.push_back(q);
        }
        uint32_t nf = r.u32();
        for (uint32_t i = 0; i < nf && r.ok && i < 100000; i++) questsFinished.insert(r.str());
    }
    if (version >= 6 && r.ok) {
        uint32_t nt = r.u32();
        farm->trees.clear();
        for (uint32_t i = 0; i < nt && r.ok && i < 100000; i++) {
            Tree t;
            t.x = r.i32(); t.y = r.i32(); t.type = r.i32();
            t.stage = r.u8(); t.health = (int8_t)r.u8(); t.stump = r.u8() != 0;
            farm->trees.push_back(t);
        }
    }
    if (version >= 12 && r.ok) {
        uint32_t ng = r.u32();
        std::fill(farm->grass.begin(), farm->grass.end(), 0);
        for (uint32_t i = 0; i < ng && r.ok && i < 1000000; i++) {
            uint32_t idx = r.u32();
            uint8_t v = r.u8();
            if (idx < farm->grass.size()) farm->grass[idx] = v;
        }
    } else {   // older saves: the map's grass, but not where you've already dug, built or placed things
        int w = farm->map.width();
        for (size_t i = 0; i < farm->grass.size(); i++) {
            int x = (int)(i % w), y = (int)(i / w);
            if (farm->soils[i].tilled || farm->debris[i]) farm->grass[i] = 0;
            for (auto& t : farm->trees) if (t.x == x && t.y == y) farm->grass[i] = 0;
        }
        auto pl = placed.find("Farm");
        if (pl != placed.end())
            for (auto& p : pl->second)
                if (p.x >= 0 && p.y >= 0 && p.y * w + p.x < (int)farm->grass.size()) farm->grass[p.y * w + p.x] = 0;
    }
    if (version >= 13 && r.ok) {
        uint32_t nc = r.u32();
        farm->clumps.clear();
        for (uint32_t i = 0; i < nc && r.ok && i < 10000; i++) {
            Location::Clump c;
            c.x = (int16_t)r.i32(); c.y = (int16_t)r.i32();
            c.kind = r.u8(); c.health = (int8_t)r.u8();
            farm->clumps.push_back(c);
        }
    } else {   // older saves: the map's clumps, where nothing else has been put since
        int w = farm->map.width();
        auto& cs = farm->clumps;
        auto pl = placed.find("Farm");
        for (size_t k = cs.size(); k-- > 0;) {
            bool clear = true;
            for (int dy = 0; dy < 2; dy++)
                for (int dx = 0; dx < 2; dx++) {
                    int x = cs[k].x + dx, y = cs[k].y + dy;
                    if (!farm->map.inBounds(x, y)) continue;
                    size_t i = (size_t)y * w + x;
                    if (farm->soils[i].tilled || farm->debris[i]) clear = false;
                    for (auto& t : farm->trees) if (t.x == x && t.y == y) clear = false;
                    if (pl != placed.end())
                        for (auto& p : pl->second) if (p.x == x && p.y == y) clear = false;
                }
            if (!clear) cs.erase(cs.begin() + k);
        }
    }
    mineDeepest = version >= 14 && r.ok ? r.i32() : 0;
    buildings.clear();
    animals.clear();
    hay = 0;
    nextBuildingId = 1;
    if (version >= 15 && r.ok) {
        hay = r.i32();
        nextBuildingId = r.i32();
        uint32_t nb = r.u32();
        for (uint32_t i = 0; i < nb && r.ok && i < 500; i++) {
            FarmBuilding b;
            b.id = r.i32(); b.type = r.str(); b.x = r.i32(); b.y = r.i32(); b.daysLeft = r.i32(); b.upgradingTo = r.str();
            uint32_t nt = r.u32();
            for (uint32_t k = 0; k < nt && r.ok && k < 64; k++) b.troughs.push_back(r.u8());
            uint32_t no = r.u32();
            std::vector<WorldObject> objs;
            for (uint32_t k = 0; k < no && r.ok && k < 500; k++) {
                WorldObject o;
                o.x = r.i32(); o.y = r.i32(); o.itemId = r.i32();
                objs.push_back(o);
            }
            buildings.push_back(b);
            Location* in = !objs.empty() || version >= 16 ? loadLocation(interiorName(b)) : nullptr;
            if (in) in->objects = objs;
            if (version >= 16 && r.ok) {
                uint32_t ns = r.u32();
                for (uint32_t k = 0; k < ns && r.ok && k < 100000; k++) {
                    uint32_t i = r.u32();
                    uint8_t bits = r.u8();
                    Soil s;
                    s.tilled = true; s.watered = bits & 2; s.dead = bits & 4; s.regrown = bits & 8;
                    s.seed = r.i32(); s.phase = r.u8(); s.dayInPhase = r.u8(); s.regrowWait = (int8_t)r.u8();
                    if (in && i < in->soils.size()) in->soils[i] = s;
                }
                uint32_t nf = r.u32();
                for (uint32_t k = 0; k < nf && r.ok && k < 10000; k++) {
                    FruitTree t;
                    t.x = r.i32(); t.y = r.i32(); t.sapling = r.i32(); t.daysLeft = r.i32(); t.fruit = r.i32();
                    if (in) in->fruitTrees.push_back(t);
                }
            }
        }
        uint32_t na = r.u32();
        for (uint32_t i = 0; i < na && r.ok && i < 2000; i++) {
            FarmAnimal a;
            a.type = r.str(); a.name = r.str(); a.home = r.i32(); a.age = r.i32(); a.friendship = r.i32();
            a.daysSinceProduce = r.i32(); a.ready = r.i32(); a.fed = r.u8() != 0;
            a.x = -1;
            animals.push_back(a);
        }
    }
    farm->fruitTrees.clear();
    if (version >= 16 && r.ok) {
        uint32_t nf = r.u32();
        for (uint32_t k = 0; k < nf && r.ok && k < 10000; k++) {
            FruitTree t;
            t.x = r.i32(); t.y = r.i32(); t.sapling = r.i32(); t.daysLeft = r.i32(); t.fruit = r.i32();
            farm->fruitTrees.push_back(t);
        }
    }
    bundleDefs.clear();
    bundleGiven.clear();
    bundleView = -1;
    if (version >= 17 && r.ok) {
        uint32_t nd = r.u32();
        for (uint32_t k = 0; k < nd && r.ok && k < 1000; k++) {
            std::string a = r.str();
            std::string b = r.str();
            bundleDefs.push_back({a, b});
        }
        uint32_t ng = r.u32();
        for (uint32_t k = 0; k < ng && r.ok && k < 1000; k++) {
            int idx = r.i32();
            uint32_t n = r.u32();
            std::vector<uint8_t> v;
            for (uint32_t j = 0; j < n && r.ok && j < 64; j++) v.push_back(r.u8());
            bundleGiven[idx] = v;
        }
    }
    houseLevel = houseUpgradeDays = 0;
    if (version >= 19 && r.ok) {
        houseLevel = r.i32();
        houseUpgradeDays = r.i32();
        locations.erase("FarmHouse");
    }
    spouse.clear();
    engagedTo.clear();
    weddingDay = -1;
    dating.clear();
    if (version >= 20 && r.ok) {
        spouse = r.str();
        engagedTo = r.str();
        weddingDay = r.i32();
        uint32_t nd = r.u32();
        for (uint32_t k = 0; k < nd && r.ok && k < 100; k++) dating.insert(r.str());
    }
    furniture = data.layout.furniture;
    museumDonated.clear();
    museumOpen = false;
    for (auto& e : equip) e = Slot();
    invSlots = INV_SIZE;   // saves from before backpacks keep all their room
    pets.clear();
    bowlFull = false;
    kids.clear();
    shippedEver.clear(); fishCaught.clear(); cookedEver.clear(); craftedEver.clear();
    orderKey.clear(); orderProgress.clear(); orderDays = 0;
    babyDue = -1;
    babyAsk = false;
    if (version >= 21 && r.ok) {
        furniture.clear();
        uint32_t nf = r.u32();
        for (uint32_t k = 0; k < nf && r.ok && k < 5000; k++) {
            FurniturePlacement fp;
            fp.map = r.str(); fp.id = r.i32(); fp.x = r.i32(); fp.y = r.i32();
            furniture.push_back(fp);
        }
        if (version >= 22 && r.ok) {
            uint32_t nm = r.u32();
            for (uint32_t k = 0; k < nm && r.ok && k < 1000; k++) museumDonated.insert(r.i32());
        }
        if (version >= 27 && r.ok) {
            { int pk = r.i32(); me.petKind = pk >= 1 && pk <= 3 ? pk : 1; } me.petBreed = std::max(0, std::min(4, (int)r.i32()));
            if (version >= 32) {
                uint32_t ns = r.u32();
                Location* in = ns ? loadLocation("IslandWest") : nullptr;
                for (uint32_t k = 0; k < ns && r.ok && k < 100000; k++) {
                    uint32_t i = r.u32();
                    uint8_t bits = r.u8();
                    Soil s;
                    s.tilled = true; s.watered = bits & 2; s.dead = bits & 4; s.regrown = bits & 8;
                    s.seed = r.i32(); s.phase = r.u8(); s.dayInPhase = r.u8(); s.regrowWait = (int8_t)r.u8();
                    if (in && i < in->soils.size()) in->soils[i] = s;
                }
                junimoBest = r.i32();
            }
            walnuts = walnutsFound = clubCoins = 0;
            walnutsGot.clear();
            for (int k = 0; k < 4; k++) walnutPools[k] = 0;
            if (version >= 33) {
                walnuts = r.i32(); walnutsFound = r.i32(); clubCoins = r.i32();
                for (int k = 0; k < 4; k++) walnutPools[k] = r.i32();
                uint32_t nw = r.u32();
                for (uint32_t k = 0; k < nw && r.ok && k < 1000; k++) walnutsGot.insert(r.str());
            }
            stats.clear();
            if (version >= 34) {
                uint32_t ns2 = r.u32();
                for (uint32_t k = 0; k < ns2 && r.ok && k < 500; k++) { std::string key = r.str(); stats[key] = r.i32(); }
            }
            if (version >= 35) {
                maxEnergy = std::max((int)MAX_ENERGY, std::min(MAX_ENERGY + STARDROP_ENERGY * MAX_STARDROPS, (int)r.i32()));
                maxHealth = std::max(100, std::min(250, (int)r.i32()));
                energy = std::min(energy, maxEnergy);
            }
            if (version >= 36) {
                uint32_t nf = r.u32();
                for (uint32_t k = 0; k < nf && r.ok && k < 200000; k++) {
                    std::string ln = r.str();
                    int fx = r.i32(), fy = r.i32();
                    uint8_t kind = r.u8();
                    Location* L = hasMap(ln) ? loadLocation(ln) : nullptr;
                    if (L && L->map.inBounds(fx, fy) && kind <= 13) L->floors[(size_t)fy * L->map.width() + fx] = kind;
                }
            }
            if (version >= 31) {
                orderKey = r.str(); orderDays = r.i32();
                uint32_t n = r.u32();
                for (uint32_t k = 0; k < n && r.ok && k < 32; k++) orderProgress.push_back(r.i32());
            }
            if (version >= 30) {
                for (auto* m : {&shippedEver, &fishCaught, &cookedEver}) {
                    uint32_t n = r.u32();
                    for (uint32_t k = 0; k < n && r.ok && k < 5000; k++) { int id = r.i32(); (*m)[id] = r.i32(); }
                }
                uint32_t nc = r.u32();
                for (uint32_t k = 0; k < nc && r.ok && k < 5000; k++) craftedEver.insert(r.i32());
            }
            if (version >= 29) {
                babyDue = r.i32(); babyAsk = r.u8() != 0;
                uint32_t nk = r.u32();
                for (uint32_t k = 0; k < nk && r.ok && k < 4; k++) {
                    Child c;
                    c.name = r.str(); c.girl = r.u8() != 0; c.dark = r.u8() != 0; c.daysOld = r.i32(); c.friendship = r.i32(); c.talked = r.u8() != 0;
                    kids.push_back(c);
                }
            }
            if (version >= 28) {
                bowlFull = r.u8() != 0;
                uint32_t np = r.u32();
                for (uint32_t k = 0; k < np && r.ok && k < 16; k++) {
                    Pet p;
                    p.type = r.i32(); p.breed = r.i32(); p.name = r.str(); p.friendship = r.i32(); p.petted = r.u8() != 0; p.gift = r.i32();
                    pets.push_back(p);
                }
            } else {
                Pet p;
                p.type = r.i32(); p.breed = r.i32(); p.name = r.str(); p.friendship = r.i32(); p.petted = r.u8() != 0;
                bowlFull = r.u8() != 0;
                if (p.type) pets.push_back(p);
            }
        }
        if (version >= 26 && r.ok)
            for (auto& b : buildings)
                for (auto* v : {&b.items, &b.pending}) {
                    uint32_t n = r.u32();
                    for (uint32_t k = 0; k < n && r.ok && k < 100; k++) v->push_back(r.slot());
                }
        if (version >= 25 && r.ok) invSlots = std::max(12, std::min((int)INV_SIZE, (int)r.i32()));
        if (version >= 24 && r.ok)
            for (auto& e : equip) e = r.slot();
        if (version >= 23 && r.ok)
            for (auto& b : buildings) {
                b.pondFish = r.i32(); b.pondPop = r.i32(); b.pondDays = r.i32();
                b.pondOut = r.i32(); b.pondOutN = r.i32();
            }
    } else if (houseLevel > 0) {   // older saves: the starting furniture, moved into the bigger house's first room
        int ox, oy, nx, ny;
        if (houseDoor(0, ox, oy) && houseDoor(houseLevel, nx, ny))
            for (auto& fp : furniture)
                if (fp.map == "FarmHouse") { fp.x += nx - ox; fp.y += ny - oy; }
    }
    if (version >= 18 && r.ok) {
        for (auto& s : inv) s.quality = r.u8();
        uint32_t ns2 = r.u32();
        for (uint32_t k = 0; k < ns2 && r.ok; k++) {
            uint8_t q = r.u8();
            if (k < shipped.size()) shipped[k].quality = q;
        }
        for (auto& kv : placed)
            for (auto& p : kv.second) {
                p.held.quality = r.u8();
                uint32_t ni = r.u32();
                for (uint32_t k = 0; k < ni && r.ok; k++) {
                    uint8_t q = r.u8();
                    if (k < p.items.size()) p.items[k].quality = q;
                }
            }
    }
    for (auto& c : farm->clumps)     // nothing grows under them
        for (int dy = 0; dy < 2; dy++)
            for (int dx = 0; dx < 2; dx++)
                if (farm->map.inBounds(c.x + dx, c.y + dy)) farm->grass[(size_t)(c.y + dy) * farm->map.width() + c.x + dx] = 0;
    fclose(f);
    if (!r.ok) return false;
    // saves from before events could hand out items: Willy's rod was never given
    if (eventsSeen.count("739330") && toolLevel(TOOL_ROD) < 0) awardPrize("rod");
    ensureGreenhouse();
    riding = false;
    horseMorning();
    // conversions before build 64 had no scythe (1.6 keeps it with the weapons): hand one over once
    if (!eventsSeen.count("s3d_scythe") && data.toolByKind(TOOL_SCYTHE)) {
        bool have = false;
        for (auto& s : inv)
            if (s.type == SLOT_TOOL)
                if (const ToolDef* t = data.tool(s.id)) have |= t->kind == TOOL_SCYTHE;
        for (auto& kv : placed)
            for (auto& p : kv.second)
                for (auto& s : p.items)
                    if (s.type == SLOT_TOOL)
                        if (const ToolDef* t = data.tool(s.id)) have |= t->kind == TOOL_SCYTHE;
        if (have || giveTool(data.toolByKind(TOOL_SCYTHE)->key)) eventsSeen.insert("s3d_scythe");
    }
    startDay();
    wakeUp();
    return true;
}

} // namespace sv
