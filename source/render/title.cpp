#include "title.h"

#include <algorithm>
#include <cstdio>

#include "scene.h"

using namespace sv;

static const uint32_t TXT = RGBA(0x3B, 0x1F, 0x0E), SOFT = RGBA(0x6B, 0x44, 0x23), BG = RGBA(0x2A, 0x1A, 0x10);
static const uint32_t SEL = RGBA(0xC8, 0x32, 0x1E), CREAM = RGBA(0xFF, 0xE8, 0xB0);

// The game's title sheet from your files (Minigames/TitleButtons): logo on
// top, then the New / Load / Co-op / Exit buttons, then their lit-up versions.
static const char* TITLE_TEX = "Minigames_TitleButtons";
static const int LOGO_W = 400, LOGO_H = 187, BTN_W = 74, BTN_H = 58, BTN_Y = 187;
static const float NEW_X = 82, LOAD_X = 164, BTNS_Y = 70;   // on the bottom screen

static bool officialTitle(Canvas& c) { return c.texWidth(TITLE_TEX) >= 400; }

// colour choices (picked by hand; the sprites are tinted by these)
struct Rgb { uint8_t r, g, b; };
static const Rgb HAIR_COLORS[] = {{138, 78, 38}, {60, 36, 20}, {20, 20, 22}, {214, 168, 92}, {240, 220, 150},
                                  {176, 64, 34}, {120, 120, 128}, {200, 90, 150}, {70, 110, 190}, {80, 150, 90}};
static const Rgb PANTS_COLORS[] = {{46, 85, 183}, {40, 40, 48}, {120, 80, 50}, {70, 110, 60}, {150, 40, 40},
                                   {200, 190, 160}, {90, 60, 130}, {60, 130, 150}};
static const int N_HAIR_COLORS = sizeof HAIR_COLORS / sizeof HAIR_COLORS[0];
static const int N_PANTS_COLORS = sizeof PANTS_COLORS / sizeof PANTS_COLORS[0];
static const int N_HAIRSTYLES = 32, N_SHIRTS = 112;

static void applyColors(TitleUi& t) {
    Outfit& o = t.draft.outfit;
    const Rgb& h = HAIR_COLORS[t.hairColor];
    o.hairR = h.r; o.hairG = h.g; o.hairB = h.b;
    const Rgb& p = PANTS_COLORS[t.pantsColor];
    o.pantsR = p.r; o.pantsG = p.g; o.pantsB = p.b;
}

void titleInit(TitleUi& t, const GameData& data, bool hasSave) {
    t.screen = TS_TITLE;
    t.hasSave = hasSave;
    t.cursor = hasSave ? 1 : 0;
    t.field = CF_NAME;
    t.draft = Game::Character();
    t.draft.outfit = data.layout.outfit;
    t.draft.name = "";
    t.draft.farm = "";
    t.hairColor = 0;
    t.pantsColor = 0;
    applyColors(t);
    t.note.clear();
}

// ---------------------------------------------------------------- drawing

void drawTitleTop(Canvas& c, const Game& g, TitleUi& t) {
    c.rect(0, 0, 400, 240, RGBA(0x7C, 0xB8, 0xE8));   // sky
    c.rect(0, 170, 400, 70, RGBA(0x5C, 0x9C, 0x3C));  // hill
    c.rect(0, 168, 400, 4, RGBA(0x4A, 0x82, 0x30));
    if (t.screen == TS_TITLE) {
        if (officialTitle(c)) {
            t.t += 1.0f / 60;
            float bob = (int)(t.t * 2) % 2 ? 1.0f : 0.0f;   // the logo breathes a little
            c.image(TITLE_TEX, 0, 0, LOGO_W, LOGO_H, 0, 2 + bob);
            // our own "3DS" sign hanging under the logo
            if (c.texWidth("UiBadge_3ds") > 0) c.image("UiBadge_3ds", 0, 0, 132, 62, 134, LOGO_H - 12 + bob);
            return;
        }
        uiFrame(c, 70, 34, 260, 64);
        c.text(200, 46, 0.8f, TXT, "stardew3ds", ALIGN_CENTER);
        c.text(200, 74, 0.45f, SOFT, "a farm for the bus ride", ALIGN_CENTER);
        drawFarmerPreview(c, g, g.me.outfit, 200, 196, FACE_DOWN, 0, 2.0f);
        return;
    }
    // creation: your farmer, big, turning around
    t.t += 1.0f / 60;
    int facing = ((int)(t.t / 1.5f)) % 4;
    static const int turn[4] = {FACE_DOWN, FACE_RIGHT, FACE_UP, FACE_LEFT};
    uiFrame(c, 130, 16, 140, 200);
    drawFarmerPreview(c, g, t.draft.outfit, 200, 196, turn[facing], 0, 3.0f);
    std::string who = t.draft.name.empty() ? "?" : t.draft.name;
    c.text(200, 22, 0.5f, TXT, who, ALIGN_CENTER);
    if (!t.draft.farm.empty()) c.text(200, 222, 0.4f, CREAM, t.draft.farm + " Farm", ALIGN_CENTER);
}

static const float ROW_X = 8, ROW_W = 304, ROW_H = 22, ROW_Y0 = 6;

static float rowY(int f) { return ROW_Y0 + f * (ROW_H + 2); }

static void arrows(Canvas& c, float y, bool on) {
    c.text(150, y + 3, 0.5f, on ? SEL : SOFT, "<", ALIGN_CENTER);
    c.text(298, y + 3, 0.5f, on ? SEL : SOFT, ">", ALIGN_CENTER);
}

void drawTitleBottom(Canvas& c, const Game& g, TitleUi& t) {
    c.rect(0, 0, 320, 240, BG);
    if (t.screen == TS_TITLE && officialTitle(c)) {
        c.rect(0, 0, 320, 240, RGBA(0x5C, 0x9C, 0xD8));
        c.rect(0, 150, 320, 90, RGBA(0x5C, 0x9C, 0x3C));
        c.rect(0, 148, 320, 4, RGBA(0x4A, 0x82, 0x30));
        // New, then Load (dimmed when there's no farm to load); the lit row when picked
        for (int i = 0; i < 2; i++) {
            bool on = t.cursor == i;
            int sx = i == 0 ? 0 : BTN_W;
            float dx = i == 0 ? NEW_X : LOAD_X;
            bool off = i == 1 && !t.hasSave;
            c.image(TITLE_TEX, sx, BTN_Y + (on && !off ? BTN_H : 0), BTN_W, BTN_H, dx, BTNS_Y, false,
                    off ? RGBA(0x90, 0x90, 0x90) : 0);
        }
        if (!t.hasSave) c.text(160, 200, 0.4f, CREAM, "No saved farm yet", ALIGN_CENTER);
        return;
    }
    if (t.screen == TS_TITLE) {
        const char* labels[2] = {"New farm", "Continue"};
        for (int i = 0; i < 2; i++) {
            bool on = t.cursor == i, off = i == 1 && !t.hasSave;
            float y = 60 + i * 64;
            uiSlot(c, 40, y, 240, 52, on && !off);
            c.text(160, y + 16, 0.6f, off ? SOFT : TXT, labels[i], ALIGN_CENTER);
        }
        if (!t.hasSave) c.text(160, 200, 0.4f, CREAM, "No saved farm yet", ALIGN_CENTER);
        return;
    }
    static const char* names[CF_COUNT] = {"Name", "Farm name", "Favourite thing", "Hair", "Hair colour",
                                          "Shirt", "Pants colour", "Farm type", ""};
    char buf[64];
    for (int f = 0; f < CF_COUNT; f++) {
        float y = rowY(f);
        bool on = t.field == f;
        if (f == CF_OK) {
            bool ready = !t.draft.name.empty() && !t.draft.farm.empty();
            uiFrame(c, 60, y + 2, 200, ROW_H + 8, !ready);
            c.text(160, y + 7, 0.55f, on ? SEL : TXT, "Start farming!", ALIGN_CENTER);
            continue;
        }
        uiSlot(c, ROW_X, y, ROW_W, ROW_H, on);
        c.text(ROW_X + 6, y + 3, 0.42f, TXT, names[f]);
        std::string val;
        uint32_t col = TXT;
        switch (f) {
            case CF_NAME: val = t.draft.name.empty() ? "tap to type" : t.draft.name; col = t.draft.name.empty() ? SOFT : TXT; break;
            case CF_FARM: val = t.draft.farm.empty() ? "tap to type" : t.draft.farm; col = t.draft.farm.empty() ? SOFT : TXT; break;
            case CF_FAVORITE: val = t.draft.favorite.empty() ? "tap to type" : t.draft.favorite; col = t.draft.favorite.empty() ? SOFT : TXT; break;
            case CF_HAIR: snprintf(buf, sizeof buf, "%d", t.draft.outfit.hair + 1); val = buf; arrows(c, y, on); break;
            case CF_SHIRT: snprintf(buf, sizeof buf, "%d", t.draft.outfit.shirt + 1); val = buf; arrows(c, y, on); break;
            case CF_HAIR_COLOR: {
                const Rgb& h = HAIR_COLORS[t.hairColor];
                c.rect(200, y + 4, 50, ROW_H - 8, RGBA(h.r, h.g, h.b));
                arrows(c, y, on);
                break;
            }
            case CF_PANTS_COLOR: {
                const Rgb& p = PANTS_COLORS[t.pantsColor];
                c.rect(200, y + 4, 50, ROW_H - 8, RGBA(p.r, p.g, p.b));
                arrows(c, y, on);
                break;
            }
            case CF_FARM_TYPE:
                val = t.draft.farmType < (int)g.data.farmTypes.size() ? g.data.farmTypes[t.draft.farmType].name : "?";
                arrows(c, y, on);
                break;
            default: break;
        }
        if (!val.empty()) {
            bool centred = f >= CF_HAIR;
            c.text(centred ? 224 : ROW_X + ROW_W - 6, y + 3, 0.42f, col, val, centred ? ALIGN_CENTER : ALIGN_RIGHT);
        }
    }
    if (!t.note.empty()) c.text(160, 228, 0.38f, CREAM, t.note, ALIGN_CENTER);
}

// ---------------------------------------------------------------- input

static void editText(TitleUi& t, int f) {
    if (!t.keyboard) return;
    std::string* s = f == CF_NAME ? &t.draft.name : f == CF_FARM ? &t.draft.farm : &t.draft.favorite;
    const char* hint = f == CF_NAME ? "Your name" : f == CF_FARM ? "Farm name" : "Favourite thing";
    *s = t.keyboard(hint, *s, f == CF_FAVORITE ? 20 : 16);
    t.note.clear();
}

static void change(TitleUi& t, const GameData& data, int dir) {
    Outfit& o = t.draft.outfit;
    auto wrap = [](int v, int n) { return ((v % n) + n) % n; };
    switch (t.field) {
        case CF_HAIR: o.hair = wrap(o.hair + dir, N_HAIRSTYLES); break;
        case CF_SHIRT: o.shirt = wrap(o.shirt + dir, N_SHIRTS); break;
        case CF_HAIR_COLOR: t.hairColor = wrap(t.hairColor + dir, N_HAIR_COLORS); break;
        case CF_PANTS_COLOR: t.pantsColor = wrap(t.pantsColor + dir, N_PANTS_COLORS); break;
        case CF_FARM_TYPE: t.draft.farmType = wrap(t.draft.farmType + dir, (int)std::max<size_t>(1, data.farmTypes.size())); break;
        default: break;
    }
    applyColors(t);
}

static void confirm(TitleUi& t) {
    if (t.draft.name.empty()) { t.note = "Give your farmer a name"; t.field = CF_NAME; return; }
    if (t.draft.farm.empty()) { t.note = "Name your farm"; t.field = CF_FARM; return; }
    t.screen = TS_DONE_NEW;
}

void titleKey(TitleUi& t, const GameData& data, TitleKey k) {
    if (t.screen == TS_TITLE) {
        if (k == TK_UP || k == TK_DOWN || k == TK_LEFT || k == TK_RIGHT) t.cursor = t.cursor ? 0 : (t.hasSave ? 1 : 0);
        if (k == TK_A || k == TK_START) {
            if (t.cursor == 1 && t.hasSave) t.screen = TS_DONE_CONTINUE;
            else t.screen = TS_CREATE;
        }
        return;
    }
    if (t.screen != TS_CREATE) return;
    switch (k) {
        case TK_UP: t.field = (t.field + CF_COUNT - 1) % CF_COUNT; break;
        case TK_DOWN: t.field = (t.field + 1) % CF_COUNT; break;
        case TK_LEFT: change(t, data, -1); break;
        case TK_RIGHT: change(t, data, 1); break;
        case TK_A:
            if (t.field <= CF_FAVORITE) editText(t, t.field);
            else if (t.field == CF_OK) confirm(t);
            else change(t, data, 1);
            break;
        case TK_START: confirm(t); break;
        case TK_B: t.screen = TS_TITLE; break;
    }
}

void titleTouch(TitleUi& t, const GameData& data, int x, int y) {
    if (t.screen == TS_TITLE) {
        // works for both layouts: the official buttons side by side, or the plain ones stacked
        for (int i = 0; i < 2; i++) {
            float bx = i == 0 ? NEW_X : LOAD_X;
            if (x >= bx && x < bx + BTN_W && y >= BTNS_Y && y < BTNS_Y + BTN_H) {
                t.cursor = i;
                titleKey(t, data, TK_A);
                return;
            }
        }
        for (int i = 0; i < 2; i++)
            if (x >= 40 && x < 280 && y >= 60 + i * 64 && y < 112 + i * 64) {
                t.cursor = i;
                titleKey(t, data, TK_A);
            }
        return;
    }
    if (t.screen != TS_CREATE) return;
    for (int f = 0; f < CF_COUNT; f++) {
        float ry = rowY(f);
        if (y < ry || y >= ry + ROW_H + (f == CF_OK ? 8 : 0)) continue;
        t.field = f;
        if (f == CF_OK) { confirm(t); return; }
        if (f <= CF_FAVORITE) { editText(t, f); return; }
        if (x < 170) change(t, data, -1);
        else if (x > 270) change(t, data, 1);
        return;
    }
}
