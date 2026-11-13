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
    int mapCache = 1;         // 0 off, 1 on, 2 on + flipped: the map drawn as two pictures instead of tile by tile
    bool showPerf = false;    // performance readout (Options tab, or L + SELECT)
    bool padPro = false;      // look for a Circle Pad Pro (old 3DS)
    // lighter drawing for the old 3DS (Options tab)
    int fxWeather = 2;        // rain, snow, petals: 2 all of it, 1 half, 0 none
    bool fxCritters = true;   // butterflies
    bool fps30 = false;       // hold a steady 30 frames a second instead of chasing 60
    // A pointer for the bottom screen while a box, shop or letter is open: the D-pad or Circle Pad moves
    // it and A taps where it points, so those screens work without the stylus.
    float ptrX = 160, ptrY = 120;
    bool ptrOn = false;
    float quitAsk = 0;        // "tap again to quit" is showing (seconds left)
    bool quitNoSave = false;  // leaving by "Quit without saving"
    // the end of the day, on the top screen: 1 what was shipped (under a night sky), 2 the date scroll and the save
    int night = 0;
    float nightT = 0;
    bool nightSaved = false;
    int battery = -1;         // the console's battery in fifths (0-5), -1 = not known (set by the platform)
    int batteryPct = -1;      // and in percent when the console says
    bool charging = false;
    int padProState = 0;      // 0 off, 1 looking, 2 connected (set by the platform)
    int padCal[4] = {0, 0, 0, 0};   // centre x, y and range x, y from the title screen's calibration (0 = none)
    std::string status;
    float statusTime = 0;

    // shop takeover
    std::string shop;         // empty = closed
    bool shopSell = false;
    bool shopUpgrade = false; // blacksmith: tool upgrades
    int shopScroll = 0;
    std::map<int, int> cart;  // itemId -> qty

    int socialScroll = 0;
    bool collections = false; // the Social tab is showing Collections
    int colPage = 0;          // 0 shipped, 1 fish, 2 museum, 3 cooking, 4 achievements
    int colScroll = 0, colSel = -1;
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
// A screen that takes over the bottom screen is open (chest, shipping bin, shop, letter, bundles,
// museum): you stand still, and the pad drives the pointer.
bool uiModal(const sv::Game& g, const Ui& ui);

void drawTop(Canvas& c, sv::Game& g, Ui& ui, uint32_t timeMs);

// Time spent building each part of the top screen (performance readout).
// g_profClock is set by the platform; without it nothing is measured.
enum { PROF_MAP, PROF_OBJECTS, PROF_FRONT, PROF_WEATHER, PROF_HUD, PROF_COUNT };
extern float g_profMs[PROF_COUNT];
extern int g_mapRedraws;   // times a map picture was redrawn (perf readout)
extern double (*g_profClock)();   // milliseconds
void drawBottom(Canvas& c, sv::Game& g, Ui& ui);
void touchBottom(sv::Game& g, Ui& ui, int x, int y, const std::string& savePath);
// Options that stay set between launches (settings.txt next to the save)
void loadUiSettings(Ui& ui, const std::string& path);
void saveUiSettings(const Ui& ui, const std::string& path);
void dialogueAdvance(sv::Game& g, Ui& ui);
void nightAdvance(sv::Game& g, Ui& ui);   // A or a tap during the end-of-day screens
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
