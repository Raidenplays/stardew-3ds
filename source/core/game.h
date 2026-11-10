// Game state and rules for the farm. No 3DS code in here.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "gamedata.h"
#include "tilemap.h"

namespace sv {

enum SlotType : uint8_t { SLOT_EMPTY = 0, SLOT_TOOL = 1, SLOT_OBJECT = 2 };

struct Slot {
    SlotType type = SLOT_EMPTY;
    int id = 0;        // tool index into GameData::tools, or object id
    int count = 0;
    bool empty() const { return type == SLOT_EMPTY; }
};

enum DebrisType : uint8_t { DEB_NONE = 0, DEB_WEED, DEB_STONE, DEB_TWIG };

struct Soil {
    bool tilled = false;
    bool watered = false;
    bool dead = false;
    bool regrown = false;   // regrowing crop that has been harvested at least once
    int seed = -1;          // seed object id, -1 = nothing planted
    uint8_t phase = 0;      // index into phaseDays; == size() means ready
    uint8_t dayInPhase = 0;
    int8_t regrowWait = -1; // >0: days until a regrowing crop is ready again
    uint8_t variant = 0;    // which seed sprite / visual variation
};

struct Input {
    float mx = 0, my = 0;   // movement, -1..1
    bool action = false;    // A: interact or use the selected item
    bool walk = false;      // hold B to walk instead of run
    bool prev = false, next = false;   // L / R
};

enum Facing : int { FACE_DOWN = 0, FACE_RIGHT = 1, FACE_UP = 2, FACE_LEFT = 3 };

static const int INV_SIZE = 36;
static const int HOTBAR = 12;
static const int MAX_ENERGY = 270;
static const int CAN_CAPACITY = 40;
static const int DAY_START = 6 * 60;     // 6:00 am
static const int DAY_END = 26 * 60;      // 2:00 am -> pass out
static const float SECONDS_PER_10MIN = 7.0f;

struct ShopEntry {
    int seedId;
    int price;
};

class Game {
public:
    explicit Game(const GameData& data, const TileMap& map);

    void newGame(uint32_t seed);
    void update(float dt, const Input& in);

    // Things the UI can trigger.
    void selectSlot(int i);
    void swapSlots(int a, int b);
    void goToBed();                      // ends the day
    std::vector<ShopEntry> shopStock() const;
    bool buy(int seedId, int qty);
    bool trashSlot(int i);

    bool save(const std::string& path) const;
    bool load(const std::string& path);

    // ---- read-only state for drawing ----
    const Soil& soil(int x, int y) const { return soils[y * map.width() + x]; }
    DebrisType debris(int x, int y) const { return (DebrisType)debrisMap[y * map.width() + x]; }
    uint8_t debrisVariant(int x, int y) const { return debrisVar[y * map.width() + x]; }
    bool cropReady(int x, int y) const;
    int cropFrame(int x, int y) const;   // 0..7 column in the crop sprite row
    bool buildingAt(int x, int y) const;
    void targetTile(int& tx, int& ty) const;
    int weekday() const { return (day - 1) % 7; }   // 0 = Mon
    std::string timeString() const;

    const GameData& data;
    const TileMap& map;

    // public for UI / save convenience
    float px = 0, py = 0;     // player feet, in map pixels
    int facing = FACE_DOWN;
    bool moving = false;
    float walkTime = 0;       // for walk animation
    float toolTimer = 0;      // > 0 while a tool swing is playing
    int toolKindAnim = 0;

    int day = 1, season = SPRING, year = 1;
    int timeOfDay = DAY_START;   // minutes since midnight
    float clockAcc = 0;
    int money = 500;
    int energy = MAX_ENERGY;
    int water = CAN_CAPACITY;
    bool raining = false;
    int selected = 0;
    Slot inv[INV_SIZE];
    std::vector<Slot> shipped;     // in the bin tonight

    // end-of-day report for the UI; consumed by it
    bool dayEnded = false;
    int lastEarnings = 0;
    bool passedOut = false;

    std::string toast;
    float toastTime = 0;

private:
    void useSelected(int tx, int ty);
    bool tryInteract(int tx, int ty);
    bool addItem(int objId, int count);
    bool blockedPx(float x, float y) const;
    bool tileSolid(int tx, int ty) const;
    void spawnDebris(int count, uint32_t& rng);
    void startDay();
    void say(const std::string& s);
    uint32_t rand32();

    std::vector<Soil> soils;
    std::vector<uint8_t> debrisMap;
    std::vector<uint8_t> debrisVar;
    uint32_t rngState = 1;
};

} // namespace sv
