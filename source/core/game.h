// Game state and rules. No 3DS code in here.
#pragma once
#include <cstdint>
#include <map>
#include <memory>
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
    uint8_t variant = 0;
};

// One map plus whatever lives on it.
struct Location {
    std::string name;
    TileMap map;
    bool isFarm = false;
    std::vector<Soil> soils;
    std::vector<uint8_t> debris, debrisVar;
    void initState();
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
    int itemId;
    int price;
};

class Game {
public:
    explicit Game(const GameData& data);

    bool newGame(uint32_t seed, std::string* err = nullptr);
    void update(float dt, const Input& in);

    // Things the UI can trigger.
    void selectSlot(int i);
    void swapSlots(int a, int b);
    bool goToBed();                      // false (with a toast) if not at home
    bool canSleepHere() const;
    std::vector<ShopEntry> shopStock(const std::string& shop) const;
    bool buy(const std::string& shop, int itemId, int qty);
    int sellPrice(const Slot& s) const;  // per item, 0 = shop won't take it
    bool sellSlot(int i);
    bool trashSlot(int i);
    bool eatSlot(int i);

    // Debug / preview tool: jump straight to a map tile.
    bool teleport(const std::string& name, int tx, int ty) { return warpTo(name, tx, ty); }

    bool save(const std::string& path) const;
    bool load(const std::string& path, std::string* err = nullptr);

    // ---- read-only state for drawing ----
    const TileMap& map() const { return cur->map; }
    const Location& location() const { return *cur; }
    const Location& farmLocation() const { return *farm; }
    const Soil& soil(int x, int y) const;
    DebrisType debris(int x, int y) const;
    uint8_t debrisVariant(int x, int y) const;
    bool cropReady(int x, int y) const;
    int cropFrame(int x, int y) const;   // 0..7 column in the crop sprite row
    bool buildingAt(int x, int y) const; // farm buildings, current location only
    void targetTile(int& tx, int& ty) const;
    int weekday() const { return (day - 1) % 7; }   // 0 = Mon
    std::string timeString() const;
    static std::string timeString(int minutes);

    const GameData& data;

    float px = 0, py = 0;     // player feet, in map pixels
    int facing = FACE_DOWN;
    bool moving = false;
    float walkTime = 0;
    float toolTimer = 0;
    int toolKindAnim = 0;
    float fade = 0;           // 1 = black, eases to 0 after a warp

    int day = 1, season = SPRING, year = 1;
    int timeOfDay = DAY_START;
    float clockAcc = 0;
    int money = 500;
    int energy = MAX_ENERGY;
    int water = CAN_CAPACITY;
    bool raining = false;
    int selected = 0;
    Slot inv[INV_SIZE];
    std::vector<Slot> shipped;

    // events for the UI; it clears them
    bool dayEnded = false;
    int lastEarnings = 0;
    bool passedOut = false;
    std::string openShop;     // set when the player talks to a shop counter

    std::string toast;
    float toastTime = 0;

private:
    Location* loadLocation(const std::string& name, std::string* err = nullptr);
    bool hasMap(const std::string& name) const;
    bool warpTo(const std::string& name, int tx, int ty);
    bool enterFrom(const std::string& name);   // arrive at name's warp back to cur
    bool runMapAction(const std::string& action, bool bumped);
    void useSelected(int tx, int ty);
    bool tryInteract(int tx, int ty);
    bool addItem(int objId, int count);
    bool blockedPx(float x, float y) const;
    bool tileSolid(int tx, int ty) const;
    void spawnDebris(int count, uint32_t& rng);
    void startDay();
    void wakeUp();
    void say(const std::string& s);
    uint32_t rand32();

    std::map<std::string, std::unique_ptr<Location>> locations;
    Location* farm = nullptr;
    Location* cur = nullptr;
    float warpCooldown = 0;
    uint32_t rngState = 1;
};

} // namespace sv
