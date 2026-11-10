// Game data loaded from the converted SD-card files (data/*.dat, layout.txt).
// Pure C++ -- no 3DS dependencies, so it can be unit-tested on a PC.
#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

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
    int totalDays() const;
};

struct ObjectDef {
    int id = -1;
    std::string name;
    int price = 0;
    int spriteIndex = 0;             // in Maps/springobjects (24 per row)
    int category = 0;
    int edibility = -300;
};

enum ToolKind : int { TOOL_NONE = 0, TOOL_HOE, TOOL_CAN, TOOL_AXE, TOOL_PICKAXE, TOOL_SCYTHE, TOOL_OTHER };

struct ToolDef {
    std::string key, className, name, texture;
    int upgrade = 0;
    int spriteIndex = 0;
    ToolKind kind = TOOL_OTHER;
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

// A way from one map to another (map-edge warp or door).
struct GraphEdge {
    int x, y;            // tile on the source map
    std::string target;
    int tx, ty;          // arrival tile
};

struct ShopItem {
    int itemId = -1;
    int price = 0;
    uint8_t seasonMask = 0;   // 0 = all year
    int minYear = 1;
};

struct Layout {
    std::vector<BuildingDef> buildings;   // drawn on the Farm
    int doorX = 64, doorY = 17;           // farmhouse door on the Farm
    int spawnX = 64, spawnY = 18;         // Farm wake-up spot if there's no house map
    std::vector<std::pair<int, int>> bins;
    std::string bedMap = "FarmHouse";
    int bedX = 9, bedY = 4, bedW = 2, bedH = 3;
    std::string wakeMap = "FarmHouse";
    int wakeX = 9, wakeY = 8;
    std::string farmerName = "Farmer";
};

class GameData {
public:
    bool load(const std::string& root, std::string* err);

    const CropDef* cropForSeed(int seedId) const;
    const ObjectDef* object(int id) const;
    const ToolDef* toolByKind(ToolKind k) const;   // upgrade level 0 version
    const ToolDef* tool(int index) const;
    int toolCount() const { return (int)tools.size(); }

    std::unordered_map<int, CropDef> crops;      // keyed by seed id
    std::unordered_map<int, ObjectDef> objects;
    std::vector<ToolDef> tools;
    std::unordered_map<std::string, std::vector<ShopItem>> shops;
    std::vector<NpcDef> npcs;
    std::vector<int> universalTastes[TASTE_COUNT];
    std::unordered_map<std::string, std::vector<GraphEdge>> graph;   // by source map
    const NpcDef* npc(const std::string& name) const;
    Layout layout;
    std::string root;   // SD folder the data came from
};

} // namespace sv
