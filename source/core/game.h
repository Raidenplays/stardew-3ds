// Game state and rules. No 3DS code in here.
#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <functional>
#include <set>
#include <string>
#include <vector>

#include "gamedata.h"
#include "npc.h"
#include "tilemap.h"

namespace sv {

enum SlotType : uint8_t { SLOT_EMPTY = 0, SLOT_TOOL = 1, SLOT_OBJECT = 2, SLOT_BIG = 3 };

struct Slot {
    SlotType type = SLOT_EMPTY;
    int id = 0;        // tool index into GameData::tools, object id, or big craftable id
    int count = 0;
    int flavor = -1;   // artisan goods: the object it was made from (Blueberry -> Blueberry Wine)
    uint8_t quality = 0;   // 0 normal, 1 silver, 2 gold, 4 iridium (the game's numbers)
    bool empty() const { return type == SLOT_EMPTY; }
    bool stacksWith(const Slot& o) const {
        return type != SLOT_TOOL && type == o.type && id == o.id && flavor == o.flavor && quality == o.quality;
    }
};

enum Skill : int { SK_FARMING = 0, SK_FISHING, SK_FORAGING, SK_MINING, SK_COMBAT, SK_COUNT };

// Something the player put down: a machine, chest, scarecrow or sprinkler.
struct Placed {
    int x = 0, y = 0;
    bool big = true;           // big craftable (else a placeable object such as a sprinkler)
    int id = 0;
    // machines
    int rule = -1;             // index into GameData::machineRules while working/ready
    Slot held;                 // the finished (or cooking) output
    int input = -1;            // what went in (for flavored output)
    int64_t readyAt = -1;      // absolute minutes; <= now = ready to collect
    // chests
    std::vector<Slot> items;
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

// A wild tree (oak, maple, pine...). Stage 5 is fully grown.
struct Tree {
    int x = 0, y = 0;
    int type = 1;
    uint8_t stage = 5;
    int8_t health = 10;
    bool stump = false;
    float shake = 0;        // seconds left of the hit wobble
};

static const int TREE_GROWN = 5;

// A fruit tree: 28 days to grow up, then up to 3 fruit in its season.
struct FruitTree {
    int x = 0, y = 0;
    int sapling = 0;              // which (Data/FruitTrees key)
    int daysLeft = 28;            // until grown
    int fruit = 0;                // on the tree now (0-3)
    int8_t health = 6;            // axe hits left once grown
    float shake = 0;
};

// One map plus whatever lives on it.
struct Location {
    std::string name;
    TileMap map;
    bool isFarm = false;
    int mineLevel = 0;                 // UndergroundMine<n>: the floor number
    int chestX = -1, chestY = -1;      // a mine treasure chest on this floor
    std::vector<Soil> soils;
    std::vector<uint8_t> debris, debrisVar;
    std::vector<WorldObject> objects;
    std::vector<Tree> trees;
    // Bushes and grass come from the map's Paths layer markers (24/25/26 =
    // large/medium/small bush, 22 = grass, 36 = blue grass).
    struct Bush { int16_t x = 0, y = 0; uint8_t size = 0; float shake = 0; int pickedDay = -1; };   // size 0 small, 1 medium, 2 large
    std::vector<Bush> bushes;
    // Big 2x2 obstacles placed when the farm is made (Paths 19 log, 20 boulder,
    // 21 stump): they need an upgraded tool.
    struct Clump { int16_t x = 0, y = 0; uint8_t kind = 0; int8_t health = 0; float shake = 0; };
    enum { CLUMP_STUMP = 0, CLUMP_LOG = 1, CLUMP_BOULDER = 2, CLUMP_METEOR = 3 };
    std::vector<Clump> clumps;
    std::vector<FruitTree> fruitTrees;
    struct Monster {
        std::string kind;              // Data/Monsters name
        float x = 0, y = 0;            // feet, map pixels
        float vx = 0, vy = 0;          // own movement
        float kx = 0, ky = 0;          // knockback
        int hp = 1, damage = 1;
        float hurt = 0, anim = 0, think = 0;
        bool bug = false;              // flies straight, bounces off walls
        uint32_t tint = 0;             // slimes are grey on the sheet
        int ai = 0;                    // 0 slime hop, 1 bug, 2 bat (flies through walls), 3 dust spirit, 4 rock crab
        bool hidden = false;           // a rock crab still pretending to be a rock
    };
    std::vector<Monster> monsters;
    std::vector<uint8_t> grass;        // per tile: tufts 0-4 (low bits), bit 3 = blue grass
    struct Rustle { int cell; float t; };
    std::vector<Rustle> rustles;       // grass swaying after you walk through it
    bool nightTiles = false;           // NightTiles applied (lit windows)
    struct Light { int x, y, type; };  // the map's Light property: glows at night
    std::vector<Light> lights;
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
    uint32_t lastFact = 0;   // a talking prop: the fact it told last
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
    std::string endAnim;      // animation to start on reaching the stop
    const NpcAnim* anim = nullptr;   // playing now (standing at a stop)
    float animTime = 0;
    bool animOut = false;     // playing the outro before walking off

    int friendship = 0;       // 250 per heart
    bool introduced = false;
    bool talkedToday = false;
    int giftsToday = 0, giftsWeek = 0;

    int hearts() const { return friendship / 250; }
    int animFrame() const;
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
    float shakeT = 0;            // seconds of shaking left
    std::vector<std::pair<int, int>> route;   // advancedMove: tile steps still to walk
    bool routeLoop = false;
    size_t routeAt = 0;
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
    std::string special;             // a built-in question being asked ("cave", "catQuestion")
    std::string music = "continue"; // the script's track; "continue" = keep what's playing
    std::string aboveHead;           // itemAboveHead: "rod", "pan" or an object id held up
};

struct Input {
    float mx = 0, my = 0;   // movement, -1..1
    bool action = false;    // A: interact or use the selected item
    bool walk = false;      // hold B to walk instead of run
    bool prev = false, next = false;   // L / R
    bool hold = false;      // A is held down (charging upgraded tools)
    int sx = 0, sy = 0;     // the face buttons as a direction (A right, Y left, X up, B down): arcade shooting
};

enum FishState : int { FS_NONE = 0, FS_CHARGE, FS_CAST, FS_WAIT, FS_BITE, FS_REEL, FS_DONE };

struct Fishing {
    FishState state = FS_NONE;
    float t = 0;               // time in this state
    float power = 0;           // cast strength 0..1
    float waitFor = 0;
    int bobX = 0, bobY = 0;    // bobber tile
    int fish = -1;             // object id on the line
    // reeling meter, 0 = bottom .. 1 = top
    float fishPos = 0.3f, fishVel = 0, fishTarget = 0.3f, nextMove = 0;
    float barPos = 0.0f, barVel = 0, barSize = 0.2f;
    float progress = 0.3f;
    bool perfect = true;
    bool caught = false;
    int size = 0;
};

struct QuestState {
    std::string id;
    int progress = 0;
    bool done = false;        // finished, reward not collected yet
};

struct UpgradeOffer {
    int slot = -1;          // inventory slot of the tool
    int next = -1;          // tool index it becomes
    int price = 0;
    int bar = -1, bars = 5; // bar object id and count
};

int oreNodeDrop(int node);   // ore node object id -> item it drops

enum Facing : int { FACE_DOWN = 0, FACE_RIGHT = 1, FACE_UP = 2, FACE_LEFT = 3 };

static const int INV_SIZE = 36;
static const int CHEST_SIZE = 36;
static const int HOTBAR = 12;
static const int MAX_ENERGY = 270;
static const int CAN_CAPACITY = 40;
static const int DAY_START = 6 * 60;     // 6:00 am
static const int DAY_END = 26 * 60;      // 2:00 am -> pass out
static const float SECONDS_PER_10MIN = 7.0f;

// Something Robin built on the farm (or is building).
// Data/Festivals, as the converter indexed it
struct Festival {
    std::string key, name, loc, map[2];   // map[1]: the even-year layout
    int season = 0, day = 0, start = 900, end = 1400;
    int sx[2] = {-1, -1}, sy[2] = {-1, -1}, sdir[2] = {2, 2};
};

struct FarmBuilding {
    int id = 0;
    std::string type;             // Data/Buildings name
    int x = 0, y = 0;             // top-left of the footprint
    int daysLeft = 0;             // still under construction
    std::string upgradingTo;      // an upgrade under way
    std::vector<uint8_t> troughs; // hay in each trough inside (map order)
    // fish ponds
    int pondFish = -1, pondPop = 0, pondDays = 0;
    int pondOut = -1, pondOutN = 0;   // produce waiting in the bucket
    // mill (flour ready tomorrow) and junimo hut (what they harvested)
    std::vector<Slot> items, pending;
};

struct FarmAnimal {
    std::string type, name;
    int home = 0;                 // FarmBuilding id
    float x = 0, y = 0;           // feet, pixels in its building
    int age = 0, friendship = 0, daysSinceProduce = 0;
    bool petted = false, fed = true;
    int ready = -1;               // milk / wool waiting for the pail or shears
    int facing = 0;
    float vx = 0, vy = 0, think = 0, anim = 0, heart = 0;
};

// A Community Center bundle, parsed from its Data/Bundles line.
struct Bundle {
    std::string key, room, name;   // "Pantry/0", "Pantry", "Spring Crops"
    int index = 0;                 // the number after the slash (saves use it)
    struct Item { int id; int count; int quality; };   // id -1: gold (the vault)
    std::vector<Item> items;
    int needed = 0;                // how many of the items complete it
    std::string reward;            // "O 465 20", "BO 10 1"...
    int color = 0;
};

struct ShopEntry {
    int itemId;
    int price;
};

class Game {
public:
    explicit Game(const GameData& data);

    // Who you are (from character creation; saved with the game).
    struct Character {
        std::string name = "Farmer", farm = "Stardew", favorite = "";
        Outfit outfit;
        int farmType = 0;          // index into GameData::farmTypes
        int petKind = 1, petBreed = 0;   // the pet you'd like: 1 cat, 2 dog; which coat
    };
    Character me;
    const std::string& farmerName() const { return me.name; }

    bool newGame(uint32_t seed, std::string* err = nullptr);
    bool newGame(uint32_t seed, const Character& who, std::string* err = nullptr);
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
    // worn things: two rings, boots, a hat
    enum { EQ_RING1 = 0, EQ_RING2, EQ_BOOTS, EQ_HAT, EQ_COUNT };
    Slot equip[EQ_COUNT];
    int equipKind(const Slot& s) const;     // which EQ_ slot it goes in (rings: EQ_RING1), or -1
    bool wearSlot(int i);                   // from the bag; swaps with what's on
    bool takeOff(int e);
    bool wearing(const char* ringName) const;
    int defense() const;

    WorldObject* objectAt(int x, int y);
    std::string mapFileFor(const std::string& name) const;   // seasonal variant if there is one
    bool hasMap(const std::string& name) const;

    // Debug / preview tool: jump straight to a map tile.
    bool teleport(const std::string& name, int tx, int ty) { return warpTo(name, tx, ty); }

    // Dialogue (UI drives these while dialogue.active)
    void dialogueNext();
    void dialogueChoose(int i);
    const NpcState* npcByName(const std::string& name) const;
    bool isBirthday(const NpcDef& n) const { return n.birthSeason == season && n.birthDay == day; }
    ScheduleContext scheduleContext(const NpcState& n) const;

    // ---- skills, crafting, machines (game_craft.cpp) ----
    int skillXp[SK_COUNT] = {};
    int skillLevels[SK_COUNT] = {};       // levels you have; they go up while you sleep
    int skillLevel(int skill) const;      // current level (recipes, bar size...)
    int xpLevel(int skill) const;         // level your XP has reached (applied at night)
    void applyLevelUps();                 // called when the day ends
    std::set<int> professions;
    bool has(int profession) const { return professions.count(profession) > 0; }
    // the two professions offered for a level-up (empty unless level 5 or 10)
    std::vector<int> professionChoices(int skill, int level) const;
    void chooseProfession(int id);
    static int xpForLevel(int level);     // total xp needed to reach level (1..10)
    void gainXp(int skill, int xp);
    bool recipeKnown(const Recipe& r) const;
    int countOf(const Ingredient& in) const;
    bool canCraft(const Recipe& r) const;
    bool craft(const Recipe& r);
    std::string itemName(const Slot& s) const;
    int itemPrice(const Slot& s) const;   // per item, with profession bonuses
    int itemBasePrice(const Slot& s) const;
    bool addSlot(Slot s);                 // stacks; false = no room (nothing added)
    bool addItem(int objId, int count, int quality = 0);
    bool giveTool(const std::string& key);   // by Data/Tools key (BambooPole, Pan...)
    void awardPrize(const std::string& what); // event awardFestivalPrize / gifts
    std::map<std::string, std::vector<Placed>> placed;   // by location name
    const std::vector<Placed>& placedHere() const;
    Placed* placedAt(int x, int y);
    const Placed* placedAt(int x, int y) const;
    bool isChest(const Placed& p) const;
    bool machineReady(const Placed& p) const;
    float machineProgress(const Placed& p) const;   // 0..1 while working
    int64_t nowMinutes() const { return (int64_t)absoluteDay() * 1440 + timeOfDay; }
    // chest UI
    bool chestOpen = false;
    int chestX = 0, chestY = 0;
    Placed* openChest();
    void chestTransfer(bool toChest, int index);   // whole stack
    void closeChest();
    // the shipping bin: the same screen; what's inside sells overnight, and you can take things back out
    bool binOpen = false;
    Placed binBox;
    float binLid = 0;          // 0 shut .. 1 open (opens as you walk up)
    int binX = 0, binY = 0;
    void openBin(int x, int y);
    // backpack: 12 slots to start, 24 and 36 from Pierre's
    int invSlots = 12;
    float packAsk = 0;         // seconds left to confirm buying the next pack

    // ---- mail (game_mail.cpp) ----
    std::vector<std::string> mailbox;        // unread letters, oldest first
    std::set<std::string> mailTomorrow;      // arrives next morning
    std::set<std::string> triggersApplied;
    long long totalEarned = 0;               // for "money earned" conditions
    void deliverMorningMail();               // dated letters + DayStarted triggers
    void runTriggers(const std::string& when);
    bool checkCondition(const std::string& query) const;
    // reading: the letter at the front of the mailbox
    struct Letter {
        std::string key, text, title;
        std::vector<std::pair<int, int>> items;   // object id, count
        int money = 0;
        std::string quest, recipe;               // added when read
        bool cooking = false;
    };
    bool letterOpen = false;
    Letter letter;
    bool openMail();                         // false = mailbox empty
    void takeLetterItems();
    void closeLetter();
    static Letter parseLetter(const std::string& key, const std::string& raw, const std::string& farmer);

    // ---- quests (game_quests.cpp) ----
    std::vector<QuestState> quests;          // the journal
    std::set<std::string> questsFinished;    // collected, never offered again
    bool addQuest(const std::string& id);
    int questGoal(const QuestDef& q) const;  // e.g. villagers to meet; 0 = no counter
    void questItem(int itemId, int count);   // harvested / caught / mined / made
    void questCrafted(bool big, int id);
    void questEntered(const std::string& location);
    void questMet();                         // introduced to someone new
    bool questDelivered(const std::string& npc, int itemId);   // true = quest took it
    bool collectQuest(size_t i);
    bool hasUnseenQuest = false;             // journal badge
    // level-ups waiting for the end-of-day screen: (skill, new level)
    std::vector<std::pair<int, int>> levelUps;

    // ---- fishing (game_fish.cpp) ----
    Fishing fishing;
    int pickFish();                 // what bites here right now (-1 = nothing)
    bool fishingActive() const { return fishing.state != FS_NONE; }

    // ---- tool upgrades (game_tools.cpp) ----
    std::vector<UpgradeOffer> upgradeOffers() const;
    bool startUpgrade(const UpgradeOffer& o);
    int upgradeTool = -1;           // tool index waiting at the blacksmith
    int upgradeReadyDay = 0;        // absolute day it's ready
    bool collectUpgrade();          // at the blacksmith
    int canCapacity() const;
    Tree* treeAt(int x, int y);
    const Tree* treeAt(int x, int y) const;
    int toolLevel(ToolKind k) const;   // best owned level of that tool kind
    bool charging = false;          // holding A with an upgraded hoe / can
    float chargeTime = 0;
    int chargeLevel() const;
    void chargedTiles(int level, std::vector<std::pair<int, int>>& out) const;

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
    bool windy = false;
    bool stormy = false;     // rain with lightning
    bool snowing = false;    // winter days with snow
    float lightning = 0;     // flash brightness (render), counts down
    float nextLightning = 6; // seconds to the next strike on a stormy day      // spring/fall "debris" days: petals and leaves blow across the screen
    // Sound cues (Stardew's names) the frontend plays then clears.
    std::vector<std::string> sounds;

    // Little visual effects (they don't change the game): bits flying off
    // what a tool hits, leaves dropping from a shaken tree, a felled tree
    // toppling over. Positions are world pixels.
    struct Effect {
        enum Kind { BIT, FALLING_TREE, ITEM } kind = BIT;
        int item = -1;                        // ITEM: object id (drawn like its icon)
        std::string tex;
        int sx = 0, sy = 0, sw = 0, sh = 0;   // source rect
        float x = 0, y = 0, vx = 0, vy = 0;   // BIT: centre; FALLING_TREE: trunk base
        float groundY = 0;                    // BIT: lands here
        float rot = 0, vrot = 0, t = 0, life = 1;
        int dir = 1;                          // FALLING_TREE: +1 falls right, -1 left
    };
    std::vector<Effect> effects;
    int lastGrassCell = -1;
    // n bits of a sprite (each `piece` px square) burst out from world (x, y)
    void burst(const std::string& tex, int sx, int sy, int sw, int sh, float x, float y, int n, int piece = 4);
    void updateEffects(float dt);
    bool isDark() const;               // lamps on / windows lit
    void updateNightTiles();
    // an item pops out of (x, y) world px, bounces, then flies into the farmer
    void itemFlies(int objId, float x, float y, int count = 1);
    // "Wood x3" pop-ups at the bottom left (same item stacks onto one)
    struct Notice { int id = 0; int count = 0; float t = 0; };
    std::vector<Notice> notices;
    void sfx(const char* cue) { sounds.emplace_back(cue); }
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
    // New farm: play the arrival cutscene from your Data/Events (the bus stop
    // event marked for day 0). Returns false if there isn't one.
    bool startOpening();
    bool checkEvents();                 // run the first eligible event here, if any
    bool eventConditionsMet(const std::string& key) const;
    void skipEvent();
    EventActor* eventActor(const std::string& name);
    void npcsUpdate(float dt);         // villagers walk their schedules (public for tools)

    int bushBerry(const Location::Bush& b) const;   // salmonberry / blackberry ready on it, or -1
    // Your horse (once there's a stable): waits by the stable each morning; A to ride, A again to get off
    std::string horseLoc;                   // "" = no horse
    float horseX = 0, horseY = 0;
    int horseFacing = 0;
    bool riding = false;
    void horseMorning();
    bool horseInteract(int tx, int ty);
    // The museum (Gunther)
    std::set<int> museumDonated;
    bool museumOpen = false;
    bool museumWants(const Slot& s) const;
    bool museumDonate(int slot);
    void museumRewardsCheck(bool give);       // give (or, for an imported save, just mark) earned rewards
    bool placeFurnitureInHouse(int id);       // a furniture reward finds a free spot in your house
    // Romance: dating (a bouquet at 8 hearts), engaged (the mermaid's pendant at 10), married
    std::string spouse, engagedTo;
    std::set<std::string> dating;
    int weddingDay = -1;
    int friendshipCap(const NpcState& n) const;
    bool romanceGift(NpcState& n, int slot);    // bouquet / pendant handled; true if it was one
    void marriageTalk(NpcState& n, std::vector<DialoguePage>& pages);
    // The farmhouse: 0 one room, 1 kitchen, 2 nursery (maps FarmHouse, FarmHouse1, FarmHouse2)
    int houseLevel = 0;
    int houseUpgradeDays = 0;               // Robin's working on the next level
    bool canCookHere() const { return cur && cur->name == "FarmHouse" && houseLevel >= 1; }
    std::vector<FurniturePlacement> furniture;   // in your house (and anywhere else): starts from the layout
    const FurniturePlacement* bedIn(const std::string& map) const;
    bool houseDoor(int level, int& x, int& y) const;   // the farmhouse map's door for that upgrade level
    bool startHouseUpgrade();
    // Farm buildings and animals
    std::vector<FarmBuilding> buildings;
    std::vector<FarmAnimal> animals;
    int hay = 0;                             // in the silos
    int nextBuildingId = 1;
    std::string placing;                     // a building waiting for a spot on the farm
    int placeX = 0, placeY = 0;
    float placeRepeat = 0;
    const BuildingType* typeOf(const FarmBuilding& b) const { return data.buildingType(b.type); }
    FarmBuilding* buildingById(int id);
    FarmBuilding* buildingAtTile(int tx, int ty);
    std::string interiorName(const FarmBuilding& b) const;
    FarmBuilding* buildingForInterior(const std::string& loc);
    int hayCapacity() const;
    bool canAfford(const BuildingType& t, std::string* why) const;
    bool startBuilding(const std::string& type);   // Robin: pay, then place it (or upgrade)
    bool placeOk(int x, int y) const;
    void updatePlacing(const Input& in, float dt);
    bool buildingSatisfies(const std::string& have, const std::string& need) const;
    int roomFor(const AnimalType& a) const;        // building id with space, or 0
    bool buyAnimal(const std::string& type);
    bool buyAnimalTool(const std::string& key);
    void animalsNight();
    void animalsUpdate(float dt);
    bool animalInteract(int tx, int ty);
    bool hopperInteract(int tx, int ty);
    bool farmBuildingInteract(int tx, int ty);
    bool pondInteract(FarmBuilding& b);
    void pondsNight();
    bool millInteract(FarmBuilding& b);
    bool collectBuilding(FarmBuilding& b, const char* empty);
    void junimosHarvest();
    void nightEvents();
    // ---- the rest of the map actions (game_world.cpp) ----
    std::map<std::string, std::string> mapStrings;
    bool mapStringsLoaded = false;
    std::set<std::string> garbageToday;
    const std::string* mapString(const std::string& key);
    void showMessage(const std::string& text);
    bool worldAction(const std::string& verb, const std::string& rest);
    bool worldChoice(const std::string& what, const std::string& key);
    void worldMorning();
    bool ccDone() const;
    // the travelling cart (Forest, Fridays and Sundays), the beach bridge, the cellar, secret notes, Joja
    std::vector<ShopEntry> cartStock;
    bool cartHere() const { return weekday() == 4 || weekday() == 6; }
    void cartMorning();
    void applyMapFixes();                    // things you've repaired, on the map you're on
    void stockCellar();
    bool caskInteract(Placed& p);
    void casksAge();
    void secretNoteChance(int skill);
    bool readSecretNote(int slot);
    void jojaDesk();
    int cellarX = -1, cellarY = -1;          // the way down, in the farmhouse
    // ---- festivals (game_festival.cpp) ----
    std::vector<Festival> festivals;
    bool festivalsLoaded = false;
    int festivalActive = -1;                 // index into festivals while you're at one
    bool festivalDone = false;               // been (or it's over) today
    std::map<std::string, std::string> festLines;
    float eggHunt = 0;                       // seconds left in the egg hunt
    int eggsFound = 0;
    void loadFestivals();
    const Festival* festivalToday();
    bool festivalWarp(const std::string& name);
    void enterFestival(const Festival& f);
    bool festivalTalk(NpcState& n);
    void festivalMain();
    void festivalUpdate(float dt);
    void endFestival();
    void festivalAfterScene();
    void festivalOutcome();
    void grandpaEvaluation();
    void caveNight();
    // ---- the arcade machines (game_arcade.cpp) ----
    struct Arcade {
        int game = 0;              // 0 off, 1 Journey of the Prairie King, 2 Junimo Kart
        float t = 0, pause = 0;
        int lives = 3, score = 0, stage = 1;
        // prairie king (a 192x192 field)
        float px = 96, py = 96, cool = 0, spawn = 1, timer = 40;
        struct Shot { float x, y, vx, vy; };
        struct Foe { float x, y; int kind, hp; };
        std::vector<Shot> shots;
        std::vector<Foe> foes;
        // junimo kart
        std::vector<int> track;    // rail height per 16px (0 = a gap)
        float dist = 0, cartY = 0, vy = 0;
    } arcade;
    int junimoBest = 0;
    void arcadeStart(int which);
    void arcadeEnd(bool won);
    void arcadeUpdate(float dt, const Input& in);
    // ---- special orders (game_orders.cpp) ----
    struct OrderDef {
        std::string key, requester, name, text;
        int days = 7, money = 0, season = -1;
        bool repeatable = false;
        struct Objective { std::string kind, target, text; int count = 1; std::set<int> items; };
        std::vector<Objective> objectives;
    };
    std::vector<OrderDef> orderDefs;
    bool ordersLoaded = false;
    std::string orderKey;                  // the one you've taken
    int orderDays = 0;
    std::vector<int> orderProgress;
    void loadOrders();
    const OrderDef* orderDef(const std::string& key);
    bool ordersUnlocked() const;
    std::vector<const OrderDef*> ordersOffered();
    std::string orderStatus();
    void ordersBoard();
    bool ordersChoice(const std::string& what, const std::string& key);
    void orderEvent(const std::string& kind, int item, int count, const std::string& target);
    bool orderWants(const std::string& npc, int item);
    bool orderCheckDone();
    void ordersNight();
    // ---- collections and achievements (game_collect.cpp) ----
    std::map<int, int> shippedEver, fishCaught, cookedEver;   // item -> how many, ever
    std::set<int> craftedEver;
    struct Achievement { int id = 0; std::string name, desc; bool earned = false, fresh = false; };
    std::map<int, std::pair<std::string, std::string>> achNames;
    bool achNamesLoaded = false;
    std::vector<int> collectionItems(int page) const;   // 0 shipped, 1 fish, 2 museum, 3 cooking
    int collectionCount(int page, int item) const;
    std::vector<Achievement> achievements();
    void achievementsMorning();
    // ---- children (game_kids.cpp) ----
    struct Child {
        std::string name;
        bool girl = false, dark = false;
        int daysOld = 0, friendship = 0;
        bool talked = false;
        float x = 0, y = 0, tx = 0, ty = 0;
        int facing = 0, state = 0;   // 0 still, 1 moving, 2 asleep
        float timer = 0, anim = 0, heart = 0;
    };
    std::vector<Child> kids;
    int babyDue = -1;                // nights until the baby arrives
    bool babyAsk = false;            // your spouse wants to talk this morning
    int kidStage(const Child& k) const;
    std::string gameLine(const std::string& key, const std::string& fallback, const std::string& a0, const std::string& a1);
    bool spouseIsMale();
    void ensureChildBeds();
    bool childBed(int idx, int& bx, int& by) const;
    bool kidsSpouseTalk(NpcState& n);
    void kidPlace(Child& k, int idx);
    bool canHaveBaby() const;
    void kidsNight();
    void kidsMorning();
    void kidsAsk();
    bool kidsChoice(const std::string& what, const std::string& key);
    void kidsUpdate(float dt);
    bool kidInteract(int tx, int ty);
    NpcState* npcByNameMut(const std::string& name) { return const_cast<NpcState*>(npcByName(name)); }
    // ---- your pet (game_pet.cpp) ----
    struct Pet {
        int type = 0;              // 1 cat, 2 dog, 3 turtle
        int breed = 0;             // which coat
        std::string name;
        int friendship = 0;        // 0..1000
        bool petted = false;
        int gift = -1;             // something it brought you (object id)
        std::string loc = "Farm";
        float x = 0, y = 0, tx = 0, ty = 0;
        int facing = 0, state = 0; // 0 sitting, 1 walking, 2 asleep
        float timer = 0, anim = 0, heart = 0;
    };
    std::vector<Pet> pets;
    bool bowlFull = false;
    struct PetGift { int kind, item; float weight; };
    std::vector<PetGift> petGifts;
    bool petGiftsLoaded = false;
    std::function<std::string(const std::string& hint, const std::string& current, int maxLen)> keyboard;   // the system keyboard
    void adoptPet(int type, int breed = -1);
    bool buyPet(int type, int breed);
    void petHome(Pet& pet, int idx);
    void petTokens();
    int petGiftFor(const Pet& pet);
    void ensurePetBowl();
    void petMorning();
    void petUpdate(float dt);
    bool petInteract(int tx, int ty);
    bool petChoice(const std::string& what, const std::string& key);
    bool petBowlInteract();
    bool visitFestival();                    // (previews, tests) start today and walk into its festival
    bool hasEvent(const std::string& loc, const std::string& id) const;
    bool farmSpotFree(int x, int y, int w, int h) const;
    bool randomFarmSpot(int w, int h, int& ox, int& oy);
    std::string morningNote;   // what the night left behind: shown when you wake up
    std::vector<std::pair<int, int>> troughTiles(const Location& loc) const;
    int countItem(int id) const;
    bool applyImport(const std::string& path, std::string* summary);   // a PC save via tools/saveimport.py
    // Community Center
    std::vector<std::pair<std::string, std::string>> bundleDefs;   // empty: the game data's; an imported save brings its own
    std::map<int, std::vector<uint8_t>> bundleGiven;                // bundle index -> each item slot given
    std::vector<Bundle> bundles() const;
    static std::vector<std::string> bundleRooms();
    int bundleGivenCount(const Bundle& b) const;
    bool bundleDone(const Bundle& b) const { return bundleGivenCount(b) >= b.needed; }
    bool roomDone(const std::string& room) const;
    int bundleHave(const Bundle::Item& it) const;   // how many you could give
    bool bundleGive(int bundleIndex, int slot);      // donate one item slot of a bundle
    int bundleView = -1;                             // -1 closed, 0 rooms, 1 a room's bundles, 2 one bundle
    std::string bundleRoom;
    int bundleSel = -1;
    int fruitTreeAt(int tx, int ty) const;           // index into cur->fruitTrees, or -1
    bool plantFruitTree(int tx, int ty);
    bool shakeFruitTree(int tx, int ty);
    bool chopFruitTree(int tx, int ty);
    void fruitTreesNight(Location& loc, bool anySeason);
    void growSoils(Location& loc, bool newSeason, bool anySeason);
    bool greenhouseFixed() const { return mailReceived.count("ccPantry") > 0; }
    bool inGreenhouse() const { return cur && cur->name.rfind("Greenhouse", 0) == 0; }
    bool cropLand() const { return cur && (cur->isFarm || inGreenhouse()); }   // where you can hoe and plant
    void ensureGreenhouse();
    void notify(const std::string& s) { say(s); }
    void takeItem(int id, int n);
    // The Mines
    int mineDeepest = 0;                     // deepest floor reached
    int health = 100, maxHealth = 100;       // knocked out at 0
    float speedBoost = 0, warriorBoost = 0;   // Savage / Warrior ring, seconds left
    float invuln = 0;                        // after a hit: a moment you can't be hurt again
    static int mineLevelOf(const std::string& name);   // "UndergroundMine12" -> 12, else 0
    void enterMine(int level);               // 0 = back up to the entrance
    std::string pendingChoice;               // a question the game asked ("elevator")
private:
    void eventUpdate(float dt);
    bool eventStep(const std::string& cmd);   // false = wait before the next command
    void endEvent();
    void npcsStartDay();
    void npcPlan(NpcState& n, const SchedEntry& e);
    void startAnim(NpcState& n);
    void npcFinishLeg(NpcState& n);
    void npcsEnterLocation();          // place villagers when the player arrives
    NpcState* npcAt(int tx, int ty);
    void talkTo(NpcState& n);
    void giveGift(NpcState& n, int slot);
    void showDialogue(const NpcState& n, const std::vector<DialoguePage>& pages);
    Location* loadLocation(const std::string& name, std::string* err = nullptr);
    void refreshSeasonMaps();
    void spawnObjects(Location& loc);
    bool canSpawnAt(const Location& loc, int x, int y, const std::string& terrain, bool vanilla) const;
    // door = through a door (plays the door sound, like the game); map-edge walks are silent
    bool warpTo(const std::string& name, int tx, int ty, bool door = false);
    bool enterFrom(const std::string& name, bool door = false);   // arrive at name's warp back to cur
    bool runMapAction(const std::string& action, bool bumped);
    void useSelected(int tx, int ty);
    bool tryInteract(int tx, int ty);
    bool placeSelected(int tx, int ty);
    bool interactPlaced(Placed& p);
    bool pickUpPlaced(int tx, int ty);
    void machinesDayStart();
    void sprinklersWater();
    void treesFromMap(Location& loc);
    void fishingUpdate(float dt, const Input& in);
    void completeQuest(QuestState& q);
    void startFishing();
    void treesGrow();
    bool chopTree(int tx, int ty, int axeLevel);
    int bushAt(int tx, int ty) const;        // index into location().bushes, or -1
    bool debrisFromMap(uint32_t& r);
    void mineGenerate(Location& loc);
    bool mineInteract(int tx, int ty);
    void mineRockBroken(int tx, int ty, int node);
    void mineSpawnMonsters(Location& loc);
    void monstersUpdate(float dt);
    void swingWeapon(const ToolDef& w);
    void hurtPlayer(int damage, float fromX, float fromY);
    void openElevator();
    bool crackGeode();          // Clint opens the geode you're holding (25g)       // a new farm's debris from the map's markers
    int clumpAt(int tx, int ty) const;
    bool shakeBush(int tx, int ty);      // index into cur->clumps covering the tile, or -1
    bool hitClump(int tx, int ty, ToolKind kind, int level);
    int grassAt(int tx, int ty) const;       // tufts on that tile (0 = none)
    bool cutGrass(int tx, int ty);           // scythe / hoe; true if there was grass
    void growGrass();                        // overnight on the farm
    bool plantTree(int tx, int ty);
    void startMachine(Placed& p, int ruleIdx, int input);
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
