// Everything drawn on both screens, plus bottom-screen touch handling.
// Platform-independent: works with any Canvas.
#pragma once
#include <map>
#include <string>

#include "canvas.h"
#include "core/game.h"

static const int TOP_W = 400, TOP_H = 240, BOT_W = 320, BOT_H = 240;

enum Tab { TAB_ITEMS = 0, TAB_CRAFT, TAB_SOCIAL, TAB_MAP, TAB_OPTIONS, TAB_COUNT };

struct Ui {
    int tab = TAB_ITEMS;
    int picked = -1;          // inventory slot picked up for moving
    bool showSummary = false;
    bool quit = false;
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
    int dlgLine = 0;          // first wrapped line shown of a long dialogue page
    int dlgLines = 0;         // wrapped line count of the current page (set while drawing)

    int loadedSeason = -1;
    std::string loadedMap;
};

// Call once per frame after game.update(): reacts to game events.
// Returns true if textures should be dropped (map or season changed).
bool sceneAfterUpdate(sv::Game& g, Ui& ui, const std::string& savePath);

void drawTop(Canvas& c, sv::Game& g, Ui& ui, uint32_t timeMs);
void drawBottom(Canvas& c, sv::Game& g, Ui& ui);
void touchBottom(sv::Game& g, Ui& ui, int x, int y, const std::string& savePath);
void dialogueAdvance(sv::Game& g, Ui& ui);
// While fishing, the touch screen feeds the game input (hold to reel, tap to hook).
void fishingTouch(const sv::Game& g, bool down, bool held, int x, int y, sv::Input& in);   // A pressed while someone is talking
std::string shopTitle(const std::string& id);
std::string placeName(const std::string& id);   // "Town" -> "Pelican Town"
