// Title screen (New farm / Continue) and character creation.
#pragma once
#include <functional>
#include <string>

#include "canvas.h"
#include "core/game.h"

enum TitleScreen { TS_TITLE = 0, TS_CREATE, TS_DONE_NEW, TS_DONE_CONTINUE };

// Fields on the creation screen, top to bottom.
enum CreateField {
    CF_NAME = 0, CF_FARM, CF_FAVORITE, CF_HAIR, CF_HAIR_COLOR, CF_SHIRT, CF_PANTS_COLOR, CF_FARM_TYPE, CF_OK,
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
    std::string note;            // e.g. "Give your farmer a name"
    // text entry: the 3DS app plugs in the system keyboard; returns the new text
    std::function<std::string(const std::string& hint, const std::string& current, int maxLen)> keyboard;
};

void titleInit(TitleUi& t, const sv::GameData& data, bool hasSave);
void drawTitleTop(Canvas& c, const sv::Game& g, TitleUi& t);
void drawTitleBottom(Canvas& c, const sv::Game& g, TitleUi& t);

enum TitleKey { TK_UP, TK_DOWN, TK_LEFT, TK_RIGHT, TK_A, TK_B, TK_START };
void titleKey(TitleUi& t, const sv::GameData& data, TitleKey k);
void titleTouch(TitleUi& t, const sv::GameData& data, int x, int y);
