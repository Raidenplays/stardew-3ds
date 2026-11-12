// Game data loaded from the converted SD-card files (data/*.dat, layout.txt).
// Pure C++ -- no 3DS dependencies, so it can be unit-tested on a PC.
#pragma once
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <vector>

#include "vfs.h"

namespace sv {

enum Season : int { SPRING = 0, SUMMER = 1, FALL = 2, WINTER = 3 };
const char* seasonName(int s);       // "spring"
const char* seasonShort(int s);      // "Spr."

struct CropDef {
    int seedId = -1;
    uint8_t seasonMask = 0;          // bit per Season
    std::vector<int> phaseDays;      // days spent in each growth phase
    int regrowDays = -1;
    int harvestId = -1;
    int spriteIndex = 0;             // row pair in TileSheets/crops
    bool raised = false;
    int harvestMin = 1;
    std::string texture = "TileSheets_crops";
    int totalDays() const;
};

struct ObjectDef {
    int id = -1;
    std::string name;
    int price = 0;
    int spriteIndex = 0;             // in Maps/springobjects (24 per row)
    int category = 0;
    int edibility = -300;
    std::string texture = "Maps_springobjects";
    std::string stringId;            // the game's own id ("24", "Rafseazz.RSVCP_Ridge_Cherry")
};

enum ToolKind : int { TOOL_NONE = 0, TOOL_HOE, TOOL_CAN, TOOL_AXE, TOOL_PICKAXE, TOOL_SCYTHE, TOOL_OTHER, TOOL_ROD, TOOL_WEAPON, TOOL_BOOTS, TOOL_HAT };

struct ToolDef {
    std::string key, className, name, texture;
    int upgrade = 0;
    int spriteIndex = 0;
    int price = -1;                  // Clint's upgrade price (gold) for this level
    std::string upgradeFrom;         // key of the tool this one upgrades
    ToolKind kind = TOOL_OTHER;
    int minDamage = 0, maxDamage = 0;   // weapons
};

// Data/Buildings: something Robin builds on the farm.
struct BuildingType {
    std::string name, texture, interior, occupants, upgradeFrom, builder = "Robin";
    int srcX = 0, srcY = 0, srcW = 0, srcH = 0;   // srcW 0: the whole texture
    int w = 1, h = 1, cost = 0, days = 0, hay = 0, maxOcc = 0;
    std::vector<std::pair<int, int>> mats;         // item id, amount
    int doorX = -1, doorY = -1;                    // human door (relative)
    int adX = -1, adY = -1, adW = 0, adH = 0;      // animal door
    int hopperX = -1, hopperY = -1;                // feed hopper inside
};

// Data/FishPondData, resolved per fish by the converter
struct FishPondDef {
    int fish = 0, maxPop = 10, spawn = -1;
    float minChance = 0.15f, maxChance = 0.95f;
    struct Out { int pop = 0; float chance = 1; int id = 0, mn = 1, mx = 1; };
    std::vector<Out> items;
};

// Data/FruitTrees
struct FruitTreeDef {
    int sapling = 0, fruit = 0;
    uint8_t seasonMask = 0;
    std::string texture;
    int row = 0;
};

// Data/FarmAnimals
struct AnimalType {
    std::string name, house, tex, babyTex, harvestedTex, harvestType, harvestTool, sound, requires, shopTex;
    int price = -1, w = 16, h = 16, daysToProduce = 1, daysToMature = 1;
    int shopX = 0, shopY = 0, shopW = 0, shopH = 0;
    std::vector<int> produce, deluxe;
};

// Data/Monsters: what a monster is like.
struct MonsterDef {
    std::string name;
    int health = 10, damage = 2, speed = 2, xp = 1;
    std::vector<std::pair<int, float>> drops;   // item id, chance
};

// Big craftables (machines, chests, scarecrows...): 16x32 sprites.
struct BigDef {
    int id = -1;
    std::string name;
    int price = 0;
    int spriteIndex = 0;             // 8 per row in TileSheets/Craftables
    std::string texture = "TileSheets_Craftables";
    std::string stringId;
};

struct Ingredient { int id; int count; bool big; };   // id < 0 (not big): any item of that category

struct Recipe {
    std::string name;
    std::vector<Ingredient> ingredients;
    bool outBig = false;
    int outId = 0, outCount = 1;
    std::string unlock;              // "default", "s Farming 2", "f Name 5", "l 2", "null"
    bool cooking = false;            // a kitchen recipe (Data/CookingRecipes)
};

// One way a machine can be used, flattened by the converter.
struct MachineRule {
    int machine = 0;                 // big craftable id
    std::vector<int> inputs;         // object ids accepted (empty = runs by itself)
    int inputCount = 1;
    int output = 0, outCount = 1;
    std::string flavor;              // "Wine", "Jelly"... output is "<input> <thing>"
    bool copyInput = false;          // output = the input item (crystalarium)
    int minutes = -1, days = -1;
    int fuel = -1, fuelCount = 0;
    bool autoRun = false;
};

struct TreeDef {
    int type = 0;
    int seed = -1;                   // object id that plants it
    std::string texture[4];          // per season
    float growthChance = 0.2f;
};

enum FishMotion : int { FM_MIXED = 0, FM_DART, FM_SMOOTH, FM_SINKER, FM_FLOATER };

struct FishDef {
    int id = 0;
    int difficulty = 30;             // 0..110
    FishMotion motion = FM_MIXED;
    int minSize = 1, maxSize = 10;   // inches
    std::vector<int> times;          // pairs of hhmm (600 1900)
    uint8_t seasonMask = 15;
    int weather = 0;                 // 0 any, 1 sun, 2 rain
    int minLevel = 0;
};

struct FishSpot {
    std::vector<int> items;          // one of these (random)
    float chance = 1.0f;
    uint8_t seasonMask = 15;
    int minLevel = 0;
    int precedence = 0;
    bool ignoreFishRules = false;
};

struct QuestDef {
    std::string id, type, title, description, objective;
    std::vector<std::string> target;   // split on spaces; item ids already converted
    std::vector<std::string> next;     // quests that start when this one is done
    int reward = 0;
};

struct ProfessionDef {
    int id = -1, skill = 0, level = 5, parent = -1;
    std::string key, name, description;
};

// The game's profession numbering (what the effects check).
enum Profession : int {
    PR_RANCHER = 0, PR_TILLER = 1, PR_ARTISAN = 4, PR_AGRICULTURIST = 5,
    PR_FISHER = 6, PR_ANGLER = 8,
    PR_FORESTER = 12, PR_GATHERER = 13, PR_LUMBERJACK = 14,
    PR_MINER = 18, PR_BLACKSMITH = 20, PR_GEMOLOGIST = 23,
};

struct FarmType { std::string key, name, map; };

struct MailTrigger {
    std::string id, when;            // "DayStarted" / "DayEnding"
    std::string condition;           // game query, "-" = always
    std::vector<std::string> actions;
};

struct BuildingDef {
    std::string name, texture;
    int srcX = 0, srcY = 0, srcW = 0, srcH = 0;
    int tileX = 0, tileY = 0, footW = 0, footH = 0;
};

enum Taste : int { TASTE_LOVE = 0, TASTE_LIKE, TASTE_DISLIKE, TASTE_HATE, TASTE_NEUTRAL, TASTE_COUNT };

struct NpcDef {
    std::string name, display;
    int birthSeason = -1, birthDay = 0;
    std::string homeMap;
    int homeX = 0, homeY = 0, homeDir = 0;
    bool social = true;
    std::vector<int> tastes[TASTE_COUNT];        // item ids, or negative category ids
    std::string tasteText[TASTE_COUNT];
    std::unordered_map<std::string, std::string> schedules;
    std::unordered_map<std::string, std::string> dialogue;
};

// A villager's animation at a schedule stop (Data/animationDescriptions):
// frame numbers on their 16x32 sheet, 4 per row.
struct NpcAnim {
    std::vector<int> intro, loop, outro;
};

// A way from one map to another (map-edge warp or door).
struct GraphEdge {
    int x, y;            // tile on the source map
    std::string target;
    int tx, ty;          // arrival tile
};

struct FurnitureDef {
    int id = 0;
    std::string name, type;
    int spriteW = 1, spriteH = 2;     // tiles
    int boxW = 1, boxH = 1;           // footprint, tiles
};

struct FurniturePlacement {
    std::string map;
    int id = 0;
    int x = 0, y = 0;                 // top-left of footprint
};

struct Outfit {
    int shirt = 0, pants = 0;
    uint8_t pantsR = 46, pantsG = 85, pantsB = 183;
    int hair = 0;
    uint8_t hairR = 138, hairG = 78, hairB = 38;
};

// Daily spawns: vanilla forage (Data/Locations) and Farm Type Manager areas.
struct ForageEntry { uint8_t seasonMask; int itemId; float chance; };
struct LocationForage { int minDaily = 1, maxDaily = 4, maxAtOnce = 6; std::vector<ForageEntry> items; };
struct SpawnRect { int x1, y1, x2, y2; };
struct SpawnArea {
    std::string id, location, kind;            // kind: "forage" or "ore"
    int minDaily = 0, maxDaily = 0;
    uint8_t seasonMask = 15;
    std::string terrain;                       // "all", "grass", "dirt", "diggable"...
    std::vector<SpawnRect> include, exclude;
    std::vector<std::pair<int, int>> items[4]; // per season: (item id, weight)
};

struct ShopItem {
    int itemId = -1;
    int price = 0;
    uint8_t seasonMask = 0;   // 0 = all year
    int minYear = 1;
    int kind = 0;             // 0 object, 1 big craftable, 2 hat, 3 furniture
    int tradeId = -1;         // -1 gold, -2 star tokens, -3 Qi coins, else the object you pay with
    int tradeAmt = 0;
    std::string noMail;       // sold only until this letter/flag is set
    std::string cond;         // the rest of the game's own condition, checked when the shop opens
};

struct Layout {
    std::vector<BuildingDef> buildings;   // drawn on the Farm
    int doorX = 64, doorY = 17;           // farmhouse door on the Farm
    int spawnX = 64, spawnY = 18;         // Farm wake-up spot if there's no house map
    std::vector<std::pair<int, int>> bins;
    int mailX = 68, mailY = 16;           // the farmhouse mailbox (house tile 9,4 in Data/Buildings)
    std::string bedMap = "FarmHouse";
    int bedX = 9, bedY = 4, bedW = 2, bedH = 3;
    std::string wakeMap = "FarmHouse";
    int wakeX = 9, wakeY = 8;
    std::string farmerName = "Farmer";
    Outfit outfit;
    std::vector<FurniturePlacement> furniture;
};

class GameData {
public:
    bool load(const std::string& root, std::string* err);

    const CropDef* cropForSeed(int seedId) const;
    const ObjectDef* object(int id) const;
    const ToolDef* toolByKind(ToolKind k) const;   // upgrade level 0 version
    const ToolDef* tool(int index) const;
    int toolUpgrade(int index) const;              // index of the next level, -1 = none
    int toolCount() const { return (int)tools.size(); }

    std::unordered_map<int, CropDef> crops;      // keyed by seed id
    std::unordered_map<int, ObjectDef> objects;
    std::vector<ToolDef> tools;
    std::unordered_map<std::string, std::vector<ShopItem>> shops;
    std::vector<NpcDef> npcs;
    std::vector<std::string> romance;   // who can be dated and married (Data/Characters CanBeRomanced)
    bool romanceable(const std::string& n) const {
        for (auto& r : romance) if (r == n) return true;
        return false;
    }
    std::unordered_map<std::string, NpcAnim> npcAnims;
    std::unordered_map<std::string, MonsterDef> monsters;
    std::vector<BuildingType> buildingTypes;
    std::vector<FruitTreeDef> fruitTrees;
    // the museum: what Gunther takes ('A' artifacts, 'M' minerals) and the rewards
    std::unordered_map<int, char> museumItems;
    struct MuseumReward {
        std::string key;
        char kind = '-';              // O object, R recipe, B big craftable, F furniture
        int id = 0, count = 1;
        std::vector<std::pair<std::string, int>> reqs;   // "id=96" 1, "arch" 15, "min" 11, "all" 40
    };
    std::vector<MuseumReward> museumRewards;
    std::vector<std::pair<std::string, std::string>> bundles;   // Data/Bundles: "Pantry/0" -> raw definition
    std::unordered_map<std::string, int> objectIds;             // the game's own item id -> ours
    int objectIdFor(std::string s) const {                       // "24", "(O)24", "Mod.Item" -> id, or -1
        if (s.rfind("(O)", 0) == 0) s = s.substr(3);
        auto it = objectIds.find(s);
        if (it != objectIds.end()) return it->second;
        return !s.empty() && (isdigit((unsigned char)s[0]) || s[0] == '-') ? atoi(s.c_str()) : -1;
    }
    std::unordered_map<int, std::string> rings;   // object id -> name (Data/Objects Type Ring)
    std::vector<FishPondDef> fishPonds;
    const FishPondDef* fishPond(int fish) const {
        for (auto& f : fishPonds) if (f.fish == fish) return &f;
        return nullptr;
    }
    const FruitTreeDef* fruitTreeForSapling(int id) const {
        for (auto& f : fruitTrees) if (f.sapling == id) return &f;
        return nullptr;
    }
    std::vector<AnimalType> animalTypes;
    const BuildingType* buildingType(const std::string& n) const {
        for (auto& b : buildingTypes) if (b.name == n) return &b;
        return nullptr;
    }
    const AnimalType* animalType(const std::string& n) const {
        for (auto& a : animalTypes) if (a.name == n) return &a;
        return nullptr;
    }
    struct GeodeDef { bool basics = false; std::vector<int> items; };   // basics: stone, ore, coal...
    std::unordered_map<int, GeodeDef> geodes;
    const MonsterDef* monster(const std::string& name) const {
        auto it = monsters.find(name);
        return it == monsters.end() ? nullptr : &it->second;
    }
    int toolByKey(const std::string& key) const {
        for (size_t i = 0; i < tools.size(); i++) if (tools[i].key == key) return (int)i;
        return -1;
    }
    const NpcAnim* npcAnim(const std::string& name) const {
        auto it = npcAnims.find(name);
        return it == npcAnims.end() ? nullptr : &it->second;
    }
    std::unordered_map<int, FurnitureDef> furniture;
    std::unordered_map<int, BigDef> bigs;
    std::vector<Recipe> recipes;
    std::vector<Recipe> cookRecipes;
    std::vector<MachineRule> machineRules;
    std::unordered_map<int, TreeDef> trees;   // by type
    std::unordered_map<int, FishDef> fish;
    std::unordered_map<std::string, QuestDef> quests;
    std::unordered_map<std::string, std::string> mail;   // key -> letter text
    std::vector<MailTrigger> mailTriggers;
    std::vector<ProfessionDef> professions;   // 30, by id
    std::vector<FarmType> farmTypes;          // Standard first
    const ProfessionDef* profession(int id) const {
        return id >= 0 && id < (int)professions.size() ? &professions[id] : nullptr;
    }
    const QuestDef* quest(const std::string& id) const;
    std::unordered_map<std::string, std::vector<FishSpot>> fishSpots;   // by location ("Default" = everywhere)
    const TreeDef* treeForSeed(int objId) const;
    const BigDef* big(int id) const;
    bool isMachine(int bigId) const;
    std::unordered_map<std::string, std::string> locationNames;   // from Data/Locations
    // Location music (Data/Locations Music / MusicDefault), data/music.dat
    struct MusicEntry { std::string track; int from = 0, to = 2600; };   // hhmm
    struct LocationMusic { std::string def; bool ignoreRain = false, townTheme = false; std::vector<MusicEntry> timed; };
    std::unordered_map<std::string, LocationMusic> music;
    std::unordered_map<std::string, LocationForage> forage;
    std::vector<SpawnArea> spawnAreas;
    std::vector<int> farmerBob;    // per body frame: pixels the body drops
    int bob(int frame) const { return frame >= 0 && frame < (int)farmerBob.size() ? farmerBob[frame] : 0; }
    std::vector<int> universalTastes[TASTE_COUNT];
    std::unordered_map<std::string, std::vector<GraphEdge>> graph;   // by source map
    const NpcDef* npc(const std::string& name) const;
    Layout layout;
    std::string root;   // SD folder the data came from
    Vfs vfs;            // base + enabled mod layers (set mods before load())
};

} // namespace sv
