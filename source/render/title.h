// Title screen (New farm / Continue) and character creation.
#pragma once
#include <functional>
#include <string>

#include "canvas.h"
#include "core/game.h"

enum TitleScreen { TS_SPLASH = 0, TS_TITLE, TS_CREATE, TS_DONE_NEW, TS_DONE_CONTINUE, TS_EXIT, TS_PADPRO };

// Fields on the creation screen, top to bottom.
enum CreateField {
    CF_NAME = 0, CF_FARM, CF_FAVORITE, CF_HAIR, CF_HAIR_COLOR, CF_SHIRT, CF_PANTS_COLOR, CF_FARM_TYPE, CF_PET, CF_OK,
    CF_COUNT
};

struct TitleUi {
    TitleScreen screen = TS_TITLE;
    bool hasSave = false;
    int cursor = 0;              // title: 0 new, 1 continue
    int field = CF_NAME;
    sv::Game::Character draft;
    int hairColor = 0, pantsColor = 0;
    float t = 0;                 // for the little walk animation
    float splashT = 0;           // seconds into the opening credits
    float introT = 1e9f;         // seconds into the title's pan up the sky (big = finished)
    bool official = false;       // drawing the game's own title sheet (4 buttons)
    bool creditsText = false;    // show the developer credit as text only (previews)
    std::string note;            // e.g. "Give your farmer a name"
    // Circle Pad Pro calibration (TS_PADPRO): the app fills these in every frame
    int padState = 0;            // 0 off, 1 looking, 2 connected
    int padStep = 0;             // 0 let go of the stick, 1 roll it round the edge, 2 saved
    float padX = 0, padY = 0;    // where the stick is, -1..1 (as measured so far)
    int padButtons = 0;          // bit 0 ZL, 1 ZR, 2 R
    bool padConfirm = false;     // A was pressed: the app takes the next step
    // text entry: the 3DS app plugs in the system keyboard; returns the new text
    std::function<std::string(const std::string& hint, const std::string& current, int maxLen)> keyboard;
    // sound effects (e.g. "logoClick"); empty until the engine has audio
    std::function<void(const char* cue)> sound;
};

void titleInit(TitleUi& t, const sv::GameData& data, bool hasSave);
void drawTitleTop(Canvas& c, const sv::Game& g, TitleUi& t);
// opening credits run for this long (on white, like the original), then the
// title scene fades in and pans up the sky for INTRO_SECONDS
static const float SPLASH_SECONDS = 11.2f;
static const float INTRO_SECONDS = 12.6f;
// jump straight to the settled title (A / START / touch during the intro)
void titleSkipIntro(TitleUi& t);
void titleTick(TitleUi& t, float dt);
void drawTitleBottom(Canvas& c, const sv::Game& g, TitleUi& t);

enum TitleKey { TK_UP, TK_DOWN, TK_LEFT, TK_RIGHT, TK_A, TK_B, TK_START };
void titleKey(TitleUi& t, const sv::GameData& data, TitleKey k);
void titleTouch(TitleUi& t, const sv::GameData& data, int x, int y);

// The title screen's sky (panorama, drifting clouds, treetops on the bottom screen).
void titleSky(Canvas& c, float w, float h, float t, bool bottom);
