#include "game.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

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

Game::Game(const GameData& d, const TileMap& m) : data(d), map(m) {}

uint32_t Game::rand32() {
    // xorshift32
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

void Game::newGame(uint32_t seed) {
    rngState = seed ? seed : 1;
    size_t cells = (size_t)map.width() * map.height();
    soils.assign(cells, Soil());
    debrisMap.assign(cells, DEB_NONE);
    debrisVar.assign(cells, 0);
    for (auto& s : inv) s = Slot();
    shipped.clear();
    day = 1;
    season = SPRING;
    year = 1;
    money = 500;
    selected = 0;

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
    for (int y = 0; y < map.height(); y++)
        for (int x = 0; x < map.width(); x++)
            if ((map.flags(x, y) & (CF_DIGGABLE | CF_NOSPAWN)) == CF_DIGGABLE) diggable++;
    spawnDebris(diggable / 6, r);
    rngState = r;
    startDay();
}

void Game::spawnDebris(int count, uint32_t& r) {
    int w = map.width(), h = map.height();
    if (w == 0 || h == 0) return;
    for (int i = 0, tries = 0; i < count && tries < count * 20; tries++) {
        r ^= r << 13; r ^= r >> 17; r ^= r << 5;
        int x = (int)(r % (uint32_t)w);
        int y = (int)((r >> 8) % (uint32_t)h);
        uint8_t f = map.flags(x, y);
        if ((f & (CF_DIGGABLE | CF_NOSPAWN | CF_BLOCKED)) != CF_DIGGABLE) continue;
        size_t idx = (size_t)y * w + x;
        if (debrisMap[idx] || soils[idx].tilled || buildingAt(x, y)) continue;
        int doorDx = x - data.layout.spawnX, doorDy = y - data.layout.spawnY;
        if (doorDx * doorDx + doorDy * doorDy < 9) continue;   // keep the porch clear
        int k = (int)((r >> 20) % 10);
        if (season == WINTER && k < 5) k = 5;                  // no weeds in winter
        debrisMap[idx] = k < 5 ? DEB_WEED : (k < 8 ? DEB_STONE : DEB_TWIG);
        debrisVar[idx] = (uint8_t)((r >> 4) & 1);
        i++;
    }
}

void Game::startDay() {
    timeOfDay = DAY_START;
    clockAcc = 0;
    energy = MAX_ENERGY;
    px = data.layout.spawnX * TILE + TILE / 2.0f;
    py = data.layout.spawnY * TILE + TILE - 2.0f;
    facing = FACE_DOWN;
    toolTimer = 0;
    char buf[64];
    snprintf(buf, sizeof buf, "%s %d, Year %d%s", seasonName(season), day, year,
             raining ? " - Rain" : "");
    std::string s = buf;
    s[0] = (char)toupper((unsigned char)s[0]);
    say(s);
}

std::string Game::timeString() const {
    int t = timeOfDay % (24 * 60);
    int h = t / 60, m = t % 60;
    bool pm = h >= 12;
    int h12 = h % 12;
    if (h12 == 0) h12 = 12;
    char buf[16];
    snprintf(buf, sizeof buf, "%d:%02d %s", h12, m, pm ? "pm" : "am");
    return buf;
}

bool Game::buildingAt(int x, int y) const {
    for (auto& b : data.layout.buildings)
        if (x >= b.tileX && x < b.tileX + b.footW && y >= b.tileY && y < b.tileY + b.footH)
            return true;
    return false;
}

bool Game::tileSolid(int tx, int ty) const {
    if (!map.inBounds(tx, ty)) return true;
    uint8_t f = map.flags(tx, ty);
    if ((f & CF_BLOCKED) && !(f & CF_PASSABLE)) return true;
    if (buildingAt(tx, ty)) return true;
    size_t i = (size_t)ty * map.width() + tx;
    if (debrisMap[i] != DEB_NONE) return true;
    const Soil& s = soils[i];
    if (s.seed >= 0) {
        const CropDef* c = data.cropForSeed(s.seed);
        if (c && c->raised && s.phase > 0) return true;   // trellis crops block
    }
    return false;
}

bool Game::blockedPx(float x, float y) const {
    // small box around the feet
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
    if (!map.inBounds(x, y)) return false;
    const Soil& s = soil(x, y);
    if (s.seed < 0 || s.dead || s.regrowWait > 0) return false;
    const CropDef* c = data.cropForSeed(s.seed);
    return c && s.phase >= c->phaseDays.size();
}

int Game::cropFrame(int x, int y) const {
    const Soil& s = soil(x, y);
    const CropDef* c = data.cropForSeed(s.seed);
    if (!c) return 0;
    if (s.regrown) return s.regrowWait > 0 ? 7 : 6;        // bush without / with produce
    if (s.phase == 0) return s.variant & 1;                // two seed looks
    return std::min(s.phase + 1, 7);
}

bool Game::addItem(int objId, int count) {
    for (auto& s : inv)
        if (s.type == SLOT_OBJECT && s.id == objId && s.count < 999) {
            int room = 999 - s.count, n = std::min(room, count);
            s.count += n;
            count -= n;
            if (!count) return true;
        }
    for (auto& s : inv)
        if (s.empty()) {
            s.type = SLOT_OBJECT;
            s.id = objId;
            s.count = count;
            return true;
        }
    say("Inventory full");
    return false;
}

void Game::selectSlot(int i) {
    if (i >= 0 && i < INV_SIZE) selected = i;
}

void Game::swapSlots(int a, int b) {
    if (a < 0 || b < 0 || a >= INV_SIZE || b >= INV_SIZE || a == b) return;
    Slot& A = inv[a];
    Slot& B = inv[b];
    if (A.type == SLOT_OBJECT && B.type == SLOT_OBJECT && A.id == B.id) {
        int n = std::min(999 - B.count, A.count);
        B.count += n;
        A.count -= n;
        if (!A.count) A = Slot();
        return;
    }
    std::swap(A, B);
}

bool Game::trashSlot(int i) {
    if (i < 0 || i >= INV_SIZE || inv[i].type != SLOT_OBJECT) return false;
    inv[i] = Slot();
    return true;
}

std::vector<ShopEntry> Game::shopStock() const {
    std::vector<ShopEntry> out;
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

bool Game::buy(int seedId, int qty) {
    for (auto& e : shopStock())
        if (e.seedId == seedId) {
            int cost = e.price * qty;
            if (cost > money) {
                say("Not enough money");
                return false;
            }
            if (!addItem(seedId, qty)) return false;
            money -= cost;
            return true;
        }
    return false;
}

bool Game::tryInteract(int tx, int ty) {
    // Harvest
    if (cropReady(tx, ty)) {
        Soil& s = soils[ty * map.width() + tx];
        const CropDef* c = data.cropForSeed(s.seed);
        int n = c->harvestMin;
        if (!addItem(c->harvestId, n)) return true;
        const ObjectDef* o = data.object(c->harvestId);
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
    // Shipping bin
    for (auto& b : data.layout.bins)
        if (b.first == tx && b.second == ty) {
            Slot& sl = inv[selected];
            if (sl.type == SLOT_OBJECT) {
                const ObjectDef* o = data.object(sl.id);
                if (o && o->price > 0) {
                    shipped.push_back(sl);
                    say("Shipped " + std::to_string(sl.count) + " " + o->name);
                    sl = Slot();
                    return true;
                }
            }
            say("Hold something to ship");
            return true;
        }
    // Farmhouse door
    if (tx == data.layout.doorX && ty == data.layout.doorY) {
        goToBed();
        return true;
    }
    return false;
}

void Game::useSelected(int tx, int ty) {
    Slot& sl = inv[selected];
    if (sl.empty()) return;
    bool inMap = map.inBounds(tx, ty);
    size_t idx = inMap ? (size_t)ty * map.width() + tx : 0;

    if (sl.type == SLOT_TOOL) {
        const ToolDef* t = data.tool(sl.id);
        if (!t) return;
        if (energy <= 0) {
            say("Too tired...");
            return;
        }
        toolTimer = TOOL_TIME;
        toolKindAnim = t->kind;
        if (t->kind != TOOL_SCYTHE) energy = std::max(0, energy - ENERGY_PER_SWING);
        if (!inMap) return;
        Soil& s = soils[idx];
        uint8_t f = map.flags(tx, ty);
        switch (t->kind) {
            case TOOL_HOE:
                if ((f & CF_DIGGABLE) && !(f & CF_WATER) && !s.tilled && !debrisMap[idx] &&
                    !buildingAt(tx, ty) && map.tile(L_BUILDINGS, tx, ty) == NO_TILE) {
                    s = Soil();
                    s.tilled = true;
                    s.watered = raining;
                }
                break;
            case TOOL_CAN:
                if (f & CF_WATER) {
                    water = CAN_CAPACITY;
                    say("Watering can filled");
                } else if (s.tilled) {
                    if (water > 0) {
                        s.watered = true;
                        water--;
                    } else {
                        say("Watering can is empty");
                    }
                }
                break;
            case TOOL_AXE:
                if (debrisMap[idx] == DEB_TWIG) {
                    debrisMap[idx] = DEB_NONE;
                    addItem(OBJ_WOOD, 1);
                }
                break;
            case TOOL_PICKAXE:
                if (debrisMap[idx] == DEB_STONE) {
                    debrisMap[idx] = DEB_NONE;
                    addItem(OBJ_STONE, 1);
                } else if (s.tilled && s.seed < 0) {
                    s = Soil();
                } else if (s.dead) {
                    s = Soil();
                }
                break;
            case TOOL_SCYTHE:
                if (debrisMap[idx] == DEB_WEED) {
                    debrisMap[idx] = DEB_NONE;
                    if (rand32() % 2) addItem(OBJ_FIBER, 1);
                } else if (s.dead) {
                    s.seed = -1;
                    s.dead = false;
                }
                break;
            default:
                break;
        }
        return;
    }

    // Object in hand
    const CropDef* crop = data.cropForSeed(sl.id);
    if (crop) {
        if (!inMap) return;
        Soil& s = soils[idx];
        if (!s.tilled || s.seed >= 0) return;
        if (!(crop->seasonMask & (1 << season))) {
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
        if (--sl.count <= 0) sl = Slot();
        return;
    }
    const ObjectDef* o = data.object(sl.id);
    if (o && o->edibility > 0 && o->edibility != -300) {
        int gain = (int)std::ceil(o->edibility * 2.5f);
        energy = std::min(MAX_ENERGY, energy + gain);
        say("Ate " + o->name + " (+" + std::to_string(gain) + ")");
        if (--sl.count <= 0) sl = Slot();
    }
}

void Game::update(float dt, const Input& in) {
    if (toastTime > 0) toastTime -= dt;

    // Clock
    clockAcc += dt;
    while (clockAcc >= SECONDS_PER_10MIN) {
        clockAcc -= SECONDS_PER_10MIN;
        timeOfDay += 10;
        if (timeOfDay >= DAY_END) {
            passedOut = true;
            goToBed();
            return;
        }
    }

    if (in.prev) selected = (selected + HOTBAR - 1) % HOTBAR;
    if (in.next) selected = (selected + 1) % HOTBAR;

    if (toolTimer > 0) {
        toolTimer -= dt;
        moving = false;
        return;
    }

    // Movement
    float mx = in.mx, my = in.my;
    float len = std::sqrt(mx * mx + my * my);
    moving = len > 0.2f;
    if (moving) {
        if (len > 1) { mx /= len; my /= len; }
        if (std::fabs(mx) > std::fabs(my)) facing = mx > 0 ? FACE_RIGHT : FACE_LEFT;
        else facing = my > 0 ? FACE_DOWN : FACE_UP;
        float sp = (in.walk ? WALK_SPEED : RUN_SPEED) * dt;
        float nx = px + mx * sp, ny = py + my * sp;
        if (!blockedPx(nx, py)) px = nx;
        if (!blockedPx(px, ny)) py = ny;
        walkTime += dt * (in.walk ? 1.0f : 1.6f);
    } else {
        walkTime = 0;
    }

    if (in.action) {
        int tx, ty;
        targetTile(tx, ty);
        if (!tryInteract(tx, ty)) useSelected(tx, ty);
    }
}

void Game::goToBed() {
    // Shipping
    int earned = 0;
    for (auto& s : shipped) {
        const ObjectDef* o = data.object(s.id);
        if (o) earned += o->price * s.count;
    }
    shipped.clear();
    money += earned;
    lastEarnings = earned;

    // Calendar
    int oldSeason = season;
    day++;
    if (day > 28) {
        day = 1;
        season = (season + 1) % 4;
        if (season == SPRING) year++;
    }
    bool newSeason = season != oldSeason;

    // Crops grow overnight if they were watered.
    for (size_t i = 0; i < soils.size(); i++) {
        Soil& s = soils[i];
        if (s.seed >= 0 && !s.dead) {
            const CropDef* c = data.cropForSeed(s.seed);
            if (!c) {
                s.seed = -1;
            } else if (newSeason && !(c->seasonMask & (1 << season))) {
                s.dead = true;
            } else if (s.watered) {
                if (s.regrowWait > 0) {
                    s.regrowWait--;
                    if (s.regrowWait == 0) s.regrowWait = -1;
                } else if (s.phase < c->phaseDays.size()) {
                    s.dayInPhase++;
                    if (s.dayInPhase >= c->phaseDays[s.phase]) {
                        s.phase++;
                        s.dayInPhase = 0;
                    }
                }
            }
        }
        s.watered = false;
        // Bare tilled soil sometimes reverts, and always does in winter.
        if (s.tilled && s.seed < 0 && (season == WINTER || rand32() % 10 == 0)) s = Soil();
    }

    // Weather for the new day
    raining = (day > 3 && season != WINTER && rand32() % 100 < 18);
    if (raining)
        for (auto& s : soils)
            if (s.tilled) s.watered = true;

    uint32_t r = rand32();
    spawnDebris(6, r);

    water = CAN_CAPACITY;   // you top it up at home -- small convenience
    dayEnded = true;
    startDay();
    if (passedOut) energy = MAX_ENERGY / 2;
}

// ---------------------------------------------------------------- saving

static const uint32_t SAVE_MAGIC = 0x53563344;   // "SV3D"
static const uint32_t SAVE_VERSION = 1;

namespace {
struct W {
    FILE* f;
    void u32(uint32_t v) { fwrite(&v, 4, 1, f); }
    void i32(int32_t v) { fwrite(&v, 4, 1, f); }
    void f32(float v) { fwrite(&v, 4, 1, f); }
    void u8(uint8_t v) { fwrite(&v, 1, 1, f); }
};
struct R {
    FILE* f;
    bool ok = true;
    uint32_t u32() { uint32_t v = 0; ok &= fread(&v, 4, 1, f) == 1; return v; }
    int32_t i32() { int32_t v = 0; ok &= fread(&v, 4, 1, f) == 1; return v; }
    float f32() { float v = 0; ok &= fread(&v, 4, 1, f) == 1; return v; }
    uint8_t u8() { uint8_t v = 0; ok &= fread(&v, 1, 1, f) == 1; return v; }
};
} // namespace

bool Game::save(const std::string& path) const {
    std::string tmp = path + ".tmp";
    FILE* f = fopen(tmp.c_str(), "wb");
    if (!f) return false;
    W w{f};
    w.u32(SAVE_MAGIC);
    w.u32(SAVE_VERSION);
    w.i32(map.width());
    w.i32(map.height());
    w.i32(day); w.i32(season); w.i32(year);
    w.i32(money); w.i32(water); w.u8(raining);
    w.u32(rngState);
    w.i32(selected);
    for (auto& s : inv) { w.u8(s.type); w.i32(s.id); w.i32(s.count); }
    w.u32((uint32_t)shipped.size());
    for (auto& s : shipped) { w.u8(s.type); w.i32(s.id); w.i32(s.count); }
    for (size_t i = 0; i < soils.size(); i++) {
        const Soil& s = soils[i];
        uint8_t bits = (s.tilled ? 1 : 0) | (s.watered ? 2 : 0) | (s.dead ? 4 : 0) | (s.regrown ? 8 : 0);
        w.u8(bits);
        if (s.tilled) {   // crops only ever exist on tilled soil
            w.i32(s.seed); w.u8(s.phase); w.u8(s.dayInPhase);
            w.u8((uint8_t)s.regrowWait); w.u8(s.variant);
        }
        w.u8(debrisMap[i]);
        if (debrisMap[i]) w.u8(debrisVar[i]);
    }
    bool ok = ferror(f) == 0;
    fclose(f);
    if (!ok) { remove(tmp.c_str()); return false; }
    remove(path.c_str());
    return rename(tmp.c_str(), path.c_str()) == 0;
}

bool Game::load(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    R r{f};
    if (r.u32() != SAVE_MAGIC || r.u32() != SAVE_VERSION ||
        r.i32() != map.width() || r.i32() != map.height()) {
        fclose(f);
        return false;
    }
    size_t cells = (size_t)map.width() * map.height();
    soils.assign(cells, Soil());
    debrisMap.assign(cells, 0);
    debrisVar.assign(cells, 0);
    day = r.i32(); season = r.i32() & 3; year = r.i32();
    money = r.i32(); water = r.i32(); raining = r.u8() != 0;
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
    for (size_t i = 0; i < cells && r.ok; i++) {
        Soil& s = soils[i];
        uint8_t bits = r.u8();
        s.tilled = bits & 1; s.watered = bits & 2; s.dead = bits & 4; s.regrown = bits & 8;
        bool hasCrop = false;
        // crop block written if tilled or seed>=0; seed>=0 implies tilled in practice
        if (s.tilled) hasCrop = true;
        if (hasCrop) {
            s.seed = r.i32(); s.phase = r.u8(); s.dayInPhase = r.u8();
            s.regrowWait = (int8_t)r.u8(); s.variant = r.u8();
        }
        debrisMap[i] = r.u8();
        if (debrisMap[i]) debrisVar[i] = r.u8();
    }
    fclose(f);
    if (!r.ok) return false;
    startDay();
    return true;
}

} // namespace sv
