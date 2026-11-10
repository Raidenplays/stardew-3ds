// Game state and rules. No 3DS code in here.
#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "gamedata.h"
#include "npc.h"
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

// Something placed on a map tile for the day: forage or an ore node.
struct WorldObject {
    int x = 0, y = 0;
    int itemId = 0;       // object id (also its sprite)
    bool ore = false;
};

// One map plus whatever lives on it.
struct Location {
    std::string name;
    TileMap map;
    bool isFarm = false;
    std::vector<Soil> soils;
    std::vector<uint8_t> debris, debrisVar;
    std::vector<WorldObject> objects;
    int spawnDay = -1;                 // absolute day the objects were spawned
    std::string file;                  // which .smap is loaded (seasonal variants differ)
    void initState();
};

// A villager's live state.
struct NpcState {
    const NpcDef* def = nullptr;
    std::string loc;
    float x = 0, y = 0;       // feet, map pixels
    int facing = 0;
    bool moving = false;
    float walkTime = 0;

    std::vector<SchedEntry> sched;
    size_t next = 0;          // next schedule entry to start

    struct Leg {
        std::string loc;      // map this leg is walked on
        int fx = 0, fy = 0;   // start tile
        int tx = 0, ty = 0;   // end tile
        bool warp = false;    // end of leg warps to (wloc, wx, wy)
        std::string wloc;
        int wx = 0, wy = 0;
    };
    std::vector<Leg> legs;
    size_t leg = 0;
    float legDone = 0;        // tiles walked on this leg (off-screen)
    std::vector<std::pair<int, int>> path;   // on-screen A* path
    size_t pathIdx = 0;
    int endFacing = 0;

    int friendship = 0;       // 250 per heart
    bool introduced = false;
    bool talkedToday = false;
    int giftsToday = 0, giftsWeek = 0;

    int hearts() const { return friendship / 250; }
    int tileX() const { return (int)(x / 16); }
    int tileY() const { return (int)((y - 4) / 16); }
};

struct DialogueBox {
    bool active = false;
    std::string npc;          // internal name ("" for plain messages)
    std::vector<DialoguePage> pages;
    size_t page = 0;
};

// One character in a cutscene ("farmer" or a villager's internal name).
struct EventActor {
    std::string name;
    float x = 0, y = 0;          // feet, map pixels
    int facing = 0;              // ours: 0 down 1 right 2 up 3 left
    bool visible = true;
    int frame = -1;              // showFrame override, -1 = normal
    std::vector<int> anim;       // animate frames
    int animMs = 100;
    bool animLoop = false;
    float animT = 0;
    float tx = 0, ty = 0;        // move target
    bool moving = false;
    int endFacing = 0;
    float walkTime = 0;
    float jumpT = -1;            // >= 0 while jumping
    float offX = 0, offY = 0;    // positionOffset
    int emote = -1;
    float emoteT = 0;
    std::string bubble;          // textAboveHead
    float bubbleT = 0;
    float speed = 40;            // px/s
};

struct Cutscene {
    bool active = false;
    std::string id, location;
    std::vector<std::string> cmds;   // the script, split on '/'
    size_t ip = 0;                   // next command
    float wait = 0;                  // pause timer
    bool waitMoves = false;          // a non-continue move is running
    std::vector<EventActor> actors;
    float camX = 0, camY = 0;        // camera centre, map pixels
    float panVX = 0, panVY = 0, panT = 0;
    float fade = 0;                  // 0 clear .. 1 black
    float fadeTarget = 0, fadeSpeed = 1.5f;
    bool skippable = false;
    bool forkFlag = false;
    int forkWant = -1;               // question forkN: which answer arms the fork
    bool questionPending = false;
    std::string retLoc;              // where to put the player afterwards
    float retX = 0, retY = 0;
    int retFacing = 0;
    std::string after;               // "newDay", "warpOut" ...
};

struct Input {
    float mx = 0, my = 0;   // movement, -1..1
    bool action = false;    // A: interact or use the selected item
    bool walk = false;      // hold B to walk instead of run
    bool prev = false, next = false;   // L / R
};

int oreNodeDrop(int node);   // ore node object id -> item it drops

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

    WorldObject* objectAt(int x, int y);
    std::string mapFileFor(const std::string& name) const;   // seasonal variant if there is one

    // Debug / preview tool: jump straight to a map tile.
    bool teleport(const std::string& name, int tx, int ty) { return warpTo(name, tx, ty); }

    // Dialogue (UI drives these while dialogue.active)
    void dialogueNext();
    void dialogueChoose(int i);
    const NpcState* npcByName(const std::string& name) const;
    bool isBirthday(const NpcDef& n) const { return n.birthSeason == season && n.birthDay == day; }
    ScheduleContext scheduleContext(const NpcState& n) const;

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
    int absoluteDay() const { return (year - 1) * 112 + season * 28 + day; }
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

    std::vector<NpcState> npcs;
    DialogueBox dialogue;
    Cutscene cutscene;
    std::set<std::string> eventsSeen;
    std::set<std::string> mailReceived;

    // Cutscenes
    bool startEvent(const std::string& location, const std::string& key, const std::string& script);
    bool checkEvents();                 // run the first eligible event here, if any
    bool eventConditionsMet(const std::string& key) const;
    void skipEvent();
    EventActor* eventActor(const std::string& name);

private:
    void eventUpdate(float dt);
    bool eventStep(const std::string& cmd);   // false = wait before the next command
    void endEvent();
    void npcsStartDay();
    void npcsUpdate(float dt);
    void npcPlan(NpcState& n, const SchedEntry& e);
    void npcFinishLeg(NpcState& n);
    void npcsEnterLocation();          // place villagers when the player arrives
    NpcState* npcAt(int tx, int ty);
    void talkTo(NpcState& n);
    void giveGift(NpcState& n, int slot);
    void showDialogue(const NpcState& n, const std::vector<DialoguePage>& pages);
    Location* loadLocation(const std::string& name, std::string* err = nullptr);
    bool hasMap(const std::string& name) const;
    void refreshSeasonMaps();
    void spawnObjects(Location& loc);
    bool canSpawnAt(const Location& loc, int x, int y, const std::string& terrain, bool vanilla) const;
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
