// Villager helpers: schedules, routes between maps, A* on a map, dialogue text.
// Formats follow the public modding docs for schedule and dialogue data.
#pragma once
#include <string>
#include <utility>
#include <vector>

#include "gamedata.h"
#include "tilemap.h"

namespace sv {

struct SchedEntry {
    int time = 0;        // minutes since midnight
    std::string loc;
    int x = 0, y = 0;
    int facing = 0;      // ours: 0 down 1 right 2 up 3 left
    std::string anim;    // animation played on arrival (Data/animationDescriptions), or ""
};

struct ScheduleContext {
    int season = 0;      // 0..3
    int day = 1;         // 1..28
    int weekday = 0;     // 0 = Mon
    bool raining = false;
    int hearts = 0;
};

// Picks today's schedule key and parses it. Empty result = stays home.
std::vector<SchedEntry> resolveSchedule(const NpcDef& npc, const ScheduleContext& ctx);
int parseClock(const std::string& t);   // "1330" -> 13*60+30

// Shortest chain of map-to-map hops (BFS over the warp graph). Empty if
// from == to or no route; ok tells the two apart.
std::vector<GraphEdge> routeBetween(const GameData& data, const std::string& from,
                                    const std::string& to, bool* ok);

// 4-way A* on a map's passability. The goal tile may be solid (doors).
// Returns tiles to step through, excluding the start.
std::vector<std::pair<int, int>> findPath(const TileMap& m, int sx, int sy, int gx, int gy,
                                          int maxNodes = 30000);

struct DialogueChoice {
    std::string label;
    int points = 0;          // friendship change
    std::string responseKey; // dialogue key to show next ("" = none)
    std::string inlineReply; // for quick questions with the reply inline
};

struct DialoguePage {
    std::string text;
    int portrait = 0;        // 0 neutral 1 happy 2 sad 3 unique 4 love 5 angry, or explicit
    std::vector<DialogueChoice> choices;
};

std::vector<DialoguePage> parseDialogue(const std::string& raw, const std::string& farmerName);

// Which dialogue key to use when talking today ("" = none found).
std::string pickDialogueKey(const NpcDef& npc, const ScheduleContext& ctx, bool introduced);

Taste giftTaste(const GameData& data, const NpcDef& npc, int itemId);
int giftPoints(Taste t);

} // namespace sv
