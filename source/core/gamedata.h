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
    Layout layout;
    std::string root;   // SD folder the data came from
};

} // namespace sv
