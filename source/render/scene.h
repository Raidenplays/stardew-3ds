// Everything drawn on both screens, plus bottom-screen touch handling.
// Platform-independent: works with any Canvas.
#pragma once
#include <map>
#include <string>

#include "canvas.h"
#include "core/game.h"

static const int TOP_W = 400, TOP_H = 240, BOT_W = 320, BOT_H = 240;

enum Tab { TAB_ITEMS = 0, TAB_CRAFT, TAB_QUESTS, TAB_SOCIAL, TAB_MAP, TAB_OPTIONS, TAB_COUNT };

struct Ui {
    int tab = TAB_ITEMS;
    int picked = -1;          // inventory slot picked up for moving
    bool showSummary = false;
    bool quit = false;
    int mapCache = 0;         // 0 off, 1 on, 2 on + flipped (testing the faster map drawing)
    bool showPerf = false;    // performance readout (Options tab, or L + SELECT)
    std::string status;
    float statusTime = 0;

    // shop takeover
    std::string shop;         // empty = closed
    bool shopSell = false;
    bool shopUpgrade = false; // blacksmith: tool upgrades
    int shopScroll = 0;
    std::map<int, int> cart;  // itemId -> qty

    int socialScroll = 0;
    int craftSel = 0;         // index into GameData::recipes
    int craftScroll = 0;      // first row shown
    bool cooking = false;     // the craft tab shows kitchen recipes (in an upgraded farmhouse)
    int questSel = 0;         // journal: selected quest
    int questScroll = 0;
    int profSel = 0;          // level-up: highlighted profession card
    int dlgLine = 0;          // first wrapped line shown of a long dialogue page
    int dlgLines = 0;         // wrapped line count of the current page (set while drawing)

    int loadedSeason = -1;
    std::string loadedMap;
};

// Call once per frame after game.update(): reacts to game events.
// Returns true if textures should be dropped (map or season changed).
bool sceneAfterUpdate(sv::Game& g, Ui& ui, const std::string& savePath);

void drawTop(Canvas& c, sv::Game& g, Ui& ui, uint32_t timeMs);

// Time spent building each part of the top screen (performance readout).
// g_profClock is set by the platform; without it nothing is measured.
enum { PROF_MAP, PROF_OBJECTS, PROF_FRONT, PROF_WEATHER, PROF_HUD, PROF_COUNT };
extern float g_profMs[PROF_COUNT];
extern double (*g_profClock)();   // milliseconds
void drawBottom(Canvas& c, sv::Game& g, Ui& ui);
void touchBottom(sv::Game& g, Ui& ui, int x, int y, const std::string& savePath);
// Options that stay set between launches (settings.txt next to the save)
void loadUiSettings(Ui& ui, const std::string& path);
void saveUiSettings(const Ui& ui, const std::string& path);
void dialogueAdvance(sv::Game& g, Ui& ui);
void summaryAdvance(sv::Game& g, Ui& ui);    // A / tap on the morning screens (level-ups first)
void summaryMove(sv::Game& g, Ui& ui, int dir);   // left/right between profession cards
// While fishing, the touch screen feeds the game input (hold to reel, tap to hook).
void fishingTouch(const sv::Game& g, bool down, bool held, int x, int y, sv::Input& in);   // A pressed while someone is talking
std::string shopTitle(const std::string& id);
std::string placeName(const std::string& id);   // "Town" -> "Pelican Town"
// shared bits for other screens (title, character creation)
void drawFarmerPreview(Canvas& c, const sv::Game& g, const sv::Outfit& o, float feetX, float feetY, int facing,
                       float walkTime, float scale);
void uiFrame(Canvas& c, float x, float y, float w, float h, bool dark = false);
void uiSlot(Canvas& c, float x, float y, float w, float h, bool selected);
