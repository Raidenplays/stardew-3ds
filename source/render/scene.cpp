#include "scene.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace sv;

static const int TILE = 16;

// ---------------------------------------------------------------- palette
static const uint32_t COL_PANEL = RGBA(0xF9, 0xD8, 0x96);
static const uint32_t COL_PANEL_DARK = RGBA(0xDC, 0xA4, 0x56);
static const uint32_t COL_TAB_OFF = RGBA(0xC9, 0x8E, 0x4A);
static const uint32_t COL_BORDER = RGBA(0x85, 0x3D, 0x1A);
static const uint32_t COL_SLOT_EDGE = RGBA(0xB9, 0x86, 0x4A);
static const uint32_t COL_TEXT = RGBA(0x3B, 0x1F, 0x0E);
static const uint32_t COL_TEXT_SOFT = RGBA(0x6B, 0x44, 0x23);
static const uint32_t COL_SLOT = RGBA(0xFC, 0xE6, 0xB4);
static const uint32_t COL_SLOT_ON = RGBA(0xFF, 0xF1, 0xC9);
static const uint32_t COL_SEL = RGBA(0xC8, 0x32, 0x1E);
static const uint32_t COL_PICK = RGBA(0x2F, 0x6F, 0xC0);
static const uint32_t COL_GREEN = RGBA(0x3E, 0x7A, 0x2C);
static const uint32_t COL_GREEN_DARK = RGBA(0x23, 0x43, 0x1A);
static const uint32_t COL_ENERGY = RGBA(0x4C, 0xC8, 0x3C);
static const uint32_t COL_ENERGY_BG = RGBA(0x40, 0x20, 0x10);
static const uint32_t COL_WATER = RGBA(0x40, 0x90, 0xF0);
static const uint32_t COL_BG = RGBA(0x2A, 0x1A, 0x10);
static const uint32_t COL_WHITE = RGBA(0xFF, 0xFF, 0xFF);
static const uint32_t COL_CREAM = RGBA(0xFF, 0xE8, 0xB0);

static const uint32_t COL_SLOT_TMP = RGBA(0xFC, 0xE6, 0xB4);   // == COL_SLOT
static const uint32_t COL_SLOT_ON_TMP = RGBA(0xFF, 0xF1, 0xC9);

// The game's own UI frames, from LooseSprites/Cursors in your converted files.
// If that texture isn't there, boxes fall back to flat colours.
static const char* UI_TEX = "LooseSprites_Cursors";
struct Frame { int x, y, w, h, l, t, r, b; };
static const Frame FRAME_MENU = {384, 373, 18, 18, 5, 6, 5, 5};   // riveted menu box
static const Frame FRAME_SLOT = {293, 360, 24, 24, 4, 4, 4, 4};   // light inset box
static const uint32_t UI_FILL = RGBA(0xFF, 0xD2, 0x84);

static uint32_t mulColor(uint32_t a, uint32_t b) {
    if (!b) return a;
    auto ch = [&](int sh) { return (((a >> sh) & 0xFF) * ((b >> sh) & 0xFF) / 255) << sh; };
    return ch(24) | ch(16) | ch(8) | (a & 0xFF);
}

// 9-slice: corners as they are, edges tiled, middle filled with the sheet's colour.
static void frame9(Canvas& c, const Frame& f, float x, float y, float w, float h, uint32_t tint = 0) {
    x = std::floor(x); y = std::floor(y); w = std::floor(w); h = std::floor(h);
    int mw = f.w - f.l - f.r, mh = f.h - f.t - f.b;
    c.rect(x + f.l, y + f.t, w - f.l - f.r, h - f.t - f.b, mulColor(UI_FILL, tint));
    for (float ex = x + f.l; ex < x + w - f.r; ex += mw) {   // top + bottom edges
        int seg = (int)std::min((float)mw, x + w - f.r - ex);
        c.image(UI_TEX, f.x + f.l, f.y, seg, f.t, ex, y, false, tint);
        c.image(UI_TEX, f.x + f.l, f.y + f.h - f.b, seg, f.b, ex, y + h - f.b, false, tint);
    }
    for (float ey = y + f.t; ey < y + h - f.b; ey += mh) {   // left + right edges
        int seg = (int)std::min((float)mh, y + h - f.b - ey);
        c.image(UI_TEX, f.x, f.y + f.t, f.l, seg, x, ey, false, tint);
        c.image(UI_TEX, f.x + f.w - f.r, f.y + f.t, f.r, seg, x + w - f.r, ey, false, tint);
    }
    c.image(UI_TEX, f.x, f.y, f.l, f.t, x, y, false, tint);
    c.image(UI_TEX, f.x + f.w - f.r, f.y, f.r, f.t, x + w - f.r, y, false, tint);
    c.image(UI_TEX, f.x, f.y + f.h - f.b, f.l, f.b, x, y + h - f.b, false, tint);
    c.image(UI_TEX, f.x + f.w - f.r, f.y + f.h - f.b, f.r, f.b, x + w - f.r, y + h - f.b, false, tint);
}

static bool gameUi(Canvas& c) { return c.texWidth(UI_TEX) >= 410; }

static void box(Canvas& c, float x, float y, float w, float h, uint32_t fill, uint32_t border,
                float bw = 2) {
    static const uint32_t PANEL = RGBA(0xF9, 0xD8, 0x96), PANEL_DARK = RGBA(0xDC, 0xA4, 0x56),
                          TAB_OFF = RGBA(0xC9, 0x8E, 0x4A);
    if (gameUi(c) && w >= 12 && h >= 12) {
        if (fill == PANEL) { frame9(c, FRAME_MENU, x, y, w, h); return; }
        if (fill == PANEL_DARK) { frame9(c, FRAME_MENU, x, y, w, h, RGBA(0xE6, 0xC8, 0xA6)); return; }
        if (fill == TAB_OFF) { frame9(c, FRAME_MENU, x, y, w, h, RGBA(0xC8, 0xA8, 0x88)); return; }
        if (fill == COL_SLOT_TMP || fill == COL_SLOT_ON_TMP) {
            frame9(c, FRAME_SLOT, x, y, w, h, fill == COL_SLOT_ON_TMP ? 0 : RGBA(0xF2, 0xE6, 0xD2));
            uint32_t hl = RGBA(0xC8, 0x32, 0x1E), pick = RGBA(0x2F, 0x6F, 0xC0);
            if (border == hl || border == pick) {   // selection outline on top
                c.rect(x, y, w, 2, border); c.rect(x, y + h - 2, w, 2, border);
                c.rect(x, y, 2, h, border); c.rect(x + w - 2, y, 2, h, border);
            }
            return;
        }
    }
    c.rect(x, y, w, h, border);
    c.rect(x + bw, y + bw, w - 2 * bw, h - 2 * bw, fill);
}

// ---------------------------------------------------------------- icons
static void objectIcon(Canvas& c, int spriteIndex, float x, float y,
                       const std::string& tex = "Maps_springobjects") {
    int cols = std::max(1, c.texWidth(tex) / 16);
    if (tex == "Maps_springobjects") cols = 24;
    c.image(tex, (spriteIndex % cols) * 16, (spriteIndex / cols) * 16, 16, 16, x, y);
}

static void itemIcon(Canvas& c, const ObjectDef* o, float x, float y) {
    if (o) objectIcon(c, o->spriteIndex, x, y, o->texture);
}

// Big craftables: 16x32, 8 per row. srcY/h pick part of it (slots show the middle).
static void bigSprite(Canvas& c, const Game& g, int id, int frame, float x, float y, int srcY = 0, int h = 32) {
    const BigDef* b = g.data.big(id);
    if (!b) return;
    int idx = b->spriteIndex + frame;
    int cols = std::max(1, c.texWidth(b->texture) / 16);
    if (c.texWidth(b->texture) == 0) cols = 8;
    c.image(b->texture, (idx % cols) * 16, (idx / cols) * 32 + srcY, 16, h, x, y);
}

static void slotIcon(Canvas& c, const Game& g, const Slot& s, float x, float y) {
    if (s.type == SLOT_BIG) {
        bigSprite(c, g, s.id, 0, x, y, 10, 16);
    } else if (s.type == SLOT_OBJECT) {
        itemIcon(c, g.data.object(s.id), x, y);
        if (s.quality) {   // the quality star from Cursors (silver, gold, iridium)
            int sx = s.quality == 1 ? 338 : 346, sy = s.quality >= 4 ? 392 : 400;
            if (c.texWidth("LooseSprites_Cursors") > 0) c.image("LooseSprites_Cursors", sx, sy, 8, 8, x - 1, y + 9);
            else c.rect(x, y + 11, 4, 4, s.quality == 1 ? RGBA(0xC0, 0xC0, 0xD0) : s.quality == 2 ? RGBA(0xF0, 0xC8, 0x30) : RGBA(0xB0, 0x60, 0xE0));
        }
    } else if (s.type == SLOT_TOOL) {
        const ToolDef* td = g.data.tool(s.id);
        if (!td) return;
        int w = c.texWidth(td->texture);
        int cols = std::max(1, w / 16);
        c.image(td->texture, (td->spriteIndex % cols) * 16, (td->spriteIndex / cols) * 16, 16, 16, x, y);
    }
}

static std::string slotName(const Game& g, const Slot& s) {
    return s.empty() ? "" : g.itemName(s);
}

std::string placeName(const std::string& id) {
    static const char* names[][2] = {
        {"Town", "Pelican Town"}, {"BusStop", "Bus Stop"}, {"FarmHouse", "Farmhouse"},
        {"SeedShop", "Pierre's General Store"}, {"Forest", "Cindersap Forest"},
        {"Mountain", "The Mountain"}, {"Beach", "The Beach"}, {"Saloon", "Stardrop Saloon"},
        {"ScienceHouse", "Carpenter's Shop"}, {"AnimalShop", "Marnie's Ranch"},
        {"Blacksmith", "Blacksmith"}, {"CommunityCenter", "Community Center"},
        {"JojaMart", "JojaMart"}, {"Railroad", "Railroad"}, {"Backwoods", "Backwoods"},
    };
    for (auto& n : names)
        if (id == n[0]) return n[1];
    // mod locations: "Custom_Ridgeside_RidgesideVillage" -> "Ridgeside Village"
    std::string s = id;
    for (const char* pre : {"Custom_Ridgeside_", "Custom_", "RSV_"})
        if (s.rfind(pre, 0) == 0) { s = s.substr(strlen(pre)); break; }
    std::string out;
    for (size_t i = 0; i < s.size(); i++) {
        char ch = s[i];
        if (ch == '_') { out += ' '; continue; }
        if (i > 0 && isupper((unsigned char)ch) && islower((unsigned char)s[i - 1])) out += ' ';
        out += ch;
    }
    return out;
}

static std::string placeName(const Game& g, const std::string& id) {
    auto it = g.data.locationNames.find(id);
    return it != g.data.locationNames.end() ? it->second : placeName(id);
}

std::string shopTitle(const std::string& id) {
    if (id == "SeedShop") return "Pierre's General Store";
    if (id == "Blacksmith") return "Blacksmith";
    if (id == "Carpenter") return "Carpenter's Shop";
    if (id == "AnimalShop") return "Marnie's Ranch";
    if (id == "Saloon") return "The Stardrop Saloon";
    if (id == "FishShop") return "Fish Shop";
    return id;
}

// ---------------------------------------------------------- hoe dirt edges
// Neighbour sum (up=100, right=10, down=500, left=1000) -> cell in hoeDirt.
static int hoeDirtIndex(int sum) {
    switch (sum) {
        case 0: return 0;
        case 10: return 15;
        case 100: return 13;
        case 1000: return 12;
        case 500: return 4;
        case 1010: return 11;
        case 1100: return 9;
        case 1500: return 8;
        case 600: return 1;
        case 1600: return 2;
        case 610: return 5;
        case 1610: return 6;
        case 510: return 3;
        case 1110: return 10;
        case 110: return 14;
        case 1510: return 7;
    }
    return 0;
}


// The player: body, pants, shirt, arms, hair. (x, y) = feet on screen.
// The farmer is layered: body, pants (tinted), shirt, arms, hair (tinted).
// (sx, sy) is the feet; scale > 1 draws it bigger, pixel-sharp.
static void drawFarmerAs(Canvas& c, const Game& g, const Outfit& o, float sx, float sy, int facing, bool moving,
                         float walkTime, int frameOverride, float k = 1.0f, bool carrying = false) {
    float dx = std::floor(sx - 8 * k), dy = std::floor(sy - 30 * k);
    static const int dirRow[4] = {0, 1, 2, 1};   // down, right, up, left(=right flipped)
    bool flip = facing == FACE_LEFT;
    int step = 0;
    if (moving) {
        static const int cycle[4] = {0, 1, 0, 2};
        step = cycle[(int)(walkTime * 8) & 3];
    }
    int frame = frameOverride >= 0 ? frameOverride : dirRow[facing & 3] * 6 + step;
    int fx = (frame % 6) * 16, fy = (frame / 6) * 32;
    int bob = g.data.bob(frame);
    const std::string base = "Characters_Farmer_farmer_base";
    auto img = [&](const std::string& t, int x, int y, int w, int h, float ox, float oy, bool fl, uint32_t tint) {
        if (k == 1.0f) c.image(t, x, y, w, h, dx + ox, dy + oy, fl, tint);
        else c.imageScaled(t, x, y, w, h, dx + ox * k, dy + oy * k, k, fl, tint);
    };
    img(base, fx, fy, 16, 32, 0, 0, flip, 0);
    if (c.texWidth("Characters_Farmer_pants") > 0) {
        int px0 = (o.pants % 10) * 192, py0 = (o.pants / 10) * 688;
        img("Characters_Farmer_pants", px0 + fx, py0 + fy, 16, 32, 0, 0, flip, RGBA(o.pantsR, o.pantsG, o.pantsB));
    }
    if (c.texWidth("Characters_Farmer_shirts") > 0) {
        static const int shirtRow[4] = {0, 1, 3, 2};
        int shx = (o.shirt % 16) * 8, shy = (o.shirt / 16) * 32 + shirtRow[facing & 3] * 8;
        img("Characters_Farmer_shirts", shx, shy, 8, 8, 4, 15 + bob, false, 0);
    }
    // arms: the normal set at +96; when carrying something, the raised set at +192 (walk rows only)
    bool carryArms = carrying && frame < 24 && c.texWidth(base) >= 288;
    if (c.texWidth(base) >= 192) img(base, fx + (carryArms ? 192 : 96), fy, 16, 32, 0, 0, flip, 0);
    int hs = o.hair;
    img("Characters_Farmer_hairstyles", (hs % 8) * 16, (hs / 8) * 96 + dirRow[facing & 3] * 32, 16, 32, 0, bob, flip,
        RGBA(o.hairR, o.hairG, o.hairB));
}

void drawFarmerPreview(Canvas& c, const Game& g, const Outfit& o, float feetX, float feetY, int facing,
                       float walkTime, float scale) {
    drawFarmerAs(c, g, o, feetX, feetY, facing, walkTime > 0, walkTime, -1, scale);
}

void uiFrame(Canvas& c, float x, float y, float w, float h, bool dark) {
    box(c, x, y, w, h, dark ? COL_PANEL_DARK : COL_PANEL, COL_BORDER, 2);
}

void uiSlot(Canvas& c, float x, float y, float w, float h, bool selected) {
    box(c, x, y, w, h, selected ? COL_SLOT_ON : COL_SLOT, selected ? COL_SEL : COL_SLOT_EDGE, selected ? 2 : 1);
}

static void drawFarmer(Canvas& c, const Game& g, float sx, float sy, int facing, bool moving, float walkTime,
                       int frameOverride) {
    drawFarmerAs(c, g, g.me.outfit, sx, sy, facing, moving, walkTime, frameOverride);
}

// Swinging a tool (hoe, axe, pickaxe, watering can): the farmer's raise /
// swing / hit poses from farmer_base, with the tool from TileSheets/tools.
// Each tool there is a row of 16x32 views starting at its Data/Tools
// SpriteIndex (21 columns of 16px): 0 held up, 1 front, 2 side, 3 front low,
// 4 from behind; its menu icon sits elsewhere (MenuSpriteIndex).
static const float TOOL_SWING = 0.30f;   // matches TOOL_TIME in game.cpp
static bool swingTool(const Game& g, const ToolDef*& td) {
    if (g.toolTimer <= 0 || g.inv[g.selected].type != SLOT_TOOL) return false;
    td = g.data.tool(g.inv[g.selected].id);
    return td && (td->kind == TOOL_HOE || td->kind == TOOL_AXE || td->kind == TOOL_PICKAXE || td->kind == TOOL_CAN);
}

// Fishing: the rod (its 16x16 picture from TileSheets/tools, a diagonal
// rod) turned to the pose -- held back over the shoulder while charging,
// swung forward on the cast, held out while waiting. Returns the rod tip.
static void rodPose(const Game& g, float sx, float sy, float& hx, float& hy, float& dir) {
    const Fishing& f = g.fishing;
    int facing = g.facing & 3;
    bool left = facing == FACE_LEFT;
    // direction the rod points (radians, screen coords: 0 = right, -pi/2 = up)
    float back, out;
    if (facing == FACE_DOWN) { back = -1.75f; out = 1.15f; hx = sx + 3; hy = sy - 14; }
    else if (facing == FACE_UP) { back = 1.2f; out = -1.6f; hx = sx + 3; hy = sy - 18; }
    else { back = -2.0f; out = -0.35f; hx = sx + (left ? -5.0f : 5.0f); hy = sy - 16; }
    float k = 1.0f;
    if (f.state == FS_CHARGE) k = 0.0f;
    else if (f.state == FS_CAST) k = std::min(1.0f, f.t / 0.18f);
    dir = back + (out - back) * k;
    if (left && facing != FACE_UP && facing != FACE_DOWN) dir = 3.14159f - dir;   // mirror for facing left
}

static void rodTip(const Game& g, float sx, float sy, float& tx, float& ty) {
    float hx, hy, dir;
    rodPose(g, sx, sy, hx, hy, dir);
    tx = hx + std::cos(dir) * 17.0f;
    ty = hy + std::sin(dir) * 17.0f;
}

static void drawFishingFarmer(Canvas& c, const Game& g, float sx, float sy) {
    const Fishing& f = g.fishing;
    int facing = g.facing & 3;
    bool charging = f.state == FS_CHARGE || (f.state == FS_CAST && f.t < 0.1f);
    int frame = facing == FACE_DOWN ? (charging ? 66 : 68) : facing == FACE_UP ? (charging ? 36 : 38) : (charging ? 30 : 33);
    const Slot& s = g.inv[g.selected];
    const ToolDef* td = s.type == SLOT_TOOL ? g.data.tool(s.id) : nullptr;
    float hx, hy, dir;
    rodPose(g, sx, sy, hx, hy, dir);
    auto rod = [&]() {
        if (!td) return;
        int cols = std::max(1, c.texWidth(td->texture) / 16);
        int ix = (td->spriteIndex % cols) * 16, iy = (td->spriteIndex / cols) * 16;
        // the picture's rod runs from the bottom-left (handle) up to the top-right (tip): -45 degrees
        c.imageRotated(td->texture, ix, iy, 16, 16, 2, 14, hx, hy, dir + 0.785f);
    };
    if (facing == FACE_UP) rod();
    drawFarmer(c, g, sx, sy, g.facing, false, 0, frame);
    if (facing != FACE_UP) rod();
}

static void drawPlayerWithTool(Canvas& c, const Game& g, float sx, float sy) {
    if (g.fishingActive() && g.fishing.state != FS_DONE) {
        drawFishingFarmer(c, g, sx, sy);
        return;
    }
    const ToolDef* td = nullptr;
    if (!swingTool(g, td)) {
        // holding an item or machine: carried over the head, bobbing with each step
        const Slot& s = g.inv[g.selected];
        bool carry = g.toolTimer <= 0 && !g.charging && !g.fishingActive() && (s.type == SLOT_OBJECT || s.type == SLOT_BIG);
        drawFarmerAs(c, g, g.me.outfit, sx, sy, g.facing, g.moving, g.walkTime, -1, 1.0f, carry);
        if (carry) {
            int frame = (g.facing & 3) == FACE_UP ? 12 : (g.facing & 3) == FACE_DOWN ? 0 : 6;
            if (g.moving) {
                static const int cycle[4] = {0, 1, 0, 2};
                frame += cycle[(int)(g.walkTime * 8) & 3];
            }
            float bob = (float)g.data.bob(frame);
            Slot one = s;
            one.count = 1;
            if (s.type == SLOT_BIG) slotIcon(c, g, one, std::floor(sx - 8), std::floor(sy - 49 + bob));
            else slotIcon(c, g, one, std::floor(sx - 8), std::floor(sy - 41 + bob));
        }
        return;
    }
    float k = 1.0f - g.toolTimer / TOOL_SWING;                 // 0 -> 1 through the swing
    int phase = k < 0.40f ? 0 : k < 0.60f ? 1 : 2;             // raise, swing, hit
    bool can = td->kind == TOOL_CAN;
    int facing = g.facing & 3;
    // farmer_base frames (body + arms) for each facing and phase
    static const int DOWN[3] = {66, 67, 68}, SIDE[3] = {30, 32, 33}, UP[3] = {36, 37, 38};
    static const int CAN_DOWN[3] = {54, 54, 55}, CAN_SIDE[3] = {58, 58, 59}, CAN_UP[3] = {36, 36, 36};
    int frame = facing == FACE_DOWN ? (can ? CAN_DOWN : DOWN)[phase]
              : facing == FACE_UP ? (can ? CAN_UP : UP)[phase]
              : (can ? CAN_SIDE : SIDE)[phase];
    // the tool's first view (Data/Tools SpriteIndex); older tools.dat files only
    // have the menu icon, which is 26 cells further on (23 for the watering can)
    int tw = c.texWidth(td->texture);
    int cols = std::max(1, tw / 16);
    int base = td->spriteIndex - (can ? 23 : 26);
    int bx = (base % cols) * 16, by = (base / cols) * 16;
    bool flip = facing == FACE_LEFT;
    float dir = flip ? -1.0f : 1.0f;
    auto tool = [&](int view, float ox, float oy, float rot) {
        // ox/oy: where the tool's grip (bottom middle of the 16x32 view) goes, from the feet
        if (rot == 0.0f) c.image(td->texture, bx + view * 16, by, 16, 32, std::floor(sx + ox - 8), std::floor(sy + oy - 32), flip);
        else c.imageRotated(td->texture, bx + view * 16, by, 16, 32, 8, 30, sx + ox, sy + oy, rot * dir);
    };
    if (facing == FACE_UP) {   // tool behind the farmer (the can stays in front, out of sight)
        if (phase < 2 && !can) tool(4, 0, phase == 0 ? -18 : -10, 0);
        drawFarmer(c, g, sx, sy, g.facing, false, 0, frame);
        return;
    }
    drawFarmer(c, g, sx, sy, g.facing, false, 0, frame);
    if (can) {
        int view = facing == FACE_DOWN ? 1 : 2;
        if (facing == FACE_DOWN) tool(1, 0, phase == 2 ? 4 : 2, 0);   // held out in front, low
        else tool(view, dir * 6, phase == 2 ? -4 : -8, phase == 2 ? 0.6f : 0.0f);
        return;
    }
    if (facing == FACE_DOWN) {
        if (phase == 0) tool(0, 0, -20, 0);          // held up over the head
        else if (phase == 1) tool(1, 0, 0, 0);       // coming down in front
        else tool(3, 0, 12, 0);                      // hits the ground at the feet
    } else {
        if (phase == 0) tool(2, dir * -3, -18, -0.5f);   // raised behind the head
        else if (phase == 1) tool(2, dir * 4, -16, 0.6f);
        else tool(2, dir * 6, -12, 1.6f);                 // out in front, level
    }
}

// ================================================================ top screen
struct DrawItem {
    float sortY;
    int kind;   // 0 debris, 1 crop, 2 building, 3 player, 4 villager, 5 furniture, 6 cutscene actor,
                // 7 object, 8 placed, 9 tree, 10 grass, 11 bush
    int a, b;
};

float g_profMs[PROF_COUNT];
double (*g_profClock)() = nullptr;
static double profNow() { return g_profClock ? g_profClock() : 0.0; }
static double g_profMark = 0;
static void profEnd(int section) {   // time since the last mark goes to `section`
    double t = profNow();
    g_profMs[section] = (float)(t - g_profMark);
    g_profMark = t;
}

static float g_camX = 0, g_camY = 0;   // the camera drawWorld used this frame
static bool g_useMapCache = false;
static void worldCamera(const Game&, float& x, float& y) { x = g_camX; y = g_camY; }

static void drawWorld(Canvas& c, Game& g, uint32_t timeMs) {
    g_profMark = profNow();
    const TileMap& m = g.map();
    int mapW = m.width() * TILE, mapH = m.height() * TILE;
    const Cutscene& cs = g.cutscene;
    float camX = g.px - TOP_W / 2.0f, camY = g.py - 16 - TOP_H / 2.0f;
    if (!g.placing.empty() && g.location().isFarm)   // choosing a building spot: look there
        if (const BuildingType* pt = g.data.buildingType(g.placing)) {
            camX = (g.placeX + pt->w / 2.0f) * TILE - TOP_W / 2.0f;
            camY = (g.placeY + pt->h / 2.0f) * TILE - TOP_H / 2.0f;
        }
    if (cs.active) {
        // events park the camera far off the map to show a black screen
        if (cs.camX < -20 * TILE || cs.camY < -20 * TILE || cs.camX > mapW + 20 * TILE ||
            cs.camY > mapH + 20 * TILE)
            return;
        camX = cs.camX - TOP_W / 2.0f;
        camY = cs.camY - TOP_H / 2.0f;
    }
    camX = std::max(0.0f, std::min(camX, (float)(mapW - TOP_W)));
    camY = std::max(0.0f, std::min(camY, (float)(mapH - TOP_H)));
    if (mapW < TOP_W) camX = (mapW - TOP_W) / 2.0f;
    if (mapH < TOP_H) camY = (mapH - TOP_H) / 2.0f;
    camX = std::floor(camX);
    camY = std::floor(camY);
    g_camX = camX;
    g_camY = camY;

    int x0 = std::max(0, (int)std::floor(camX / TILE) - 1), y0 = std::max(0, (int)std::floor(camY / TILE) - 1);
    int x1 = std::min(m.width() - 1, (int)((camX + TOP_W) / TILE) + 1);
    int y1 = std::min(m.height() - 1, (int)((camY + TOP_H) / TILE) + 3);

    std::vector<std::string> tsTex(m.tilesets.size());
    std::vector<int> tsCols(m.tilesets.size());
    for (size_t i = 0; i < m.tilesets.size(); i++) {
        tsTex[i] = m.tilesets[i].textureFor(g.season);
        tsCols[i] = m.tilesets[i].columns > 0 ? m.tilesets[i].columns : c.texWidth(tsTex[i]) / 16;
    }

    // each tilesheet looked up once per frame, not once per tile
    std::vector<const void*> tsHandle(tsTex.size());
    for (size_t i = 0; i < tsTex.size(); i++) tsHandle[i] = c.texLookup(tsTex[i]);
    // Tiles in one layer never overlap, so they can be drawn in any order:
    // grouping them by sheet and by area of the sheet keeps the GPU on one
    // texture page for long runs instead of switching for nearly every tile.
    struct TileDraw { uint32_t key; int16_t dx, dy; uint16_t sx, sy; uint8_t ts; };
    static std::vector<TileDraw> tileDraws;
    auto drawLayer = [&](int layer, float depth) {
        float sh = c.shift(depth);
        tileDraws.clear();
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++) {
                uint16_t t = m.tile(layer, x, y);
                if (t == NO_TILE) continue;
                int ts = m.tileset(layer, x, y);
                int cols = tsCols[ts];
                if (cols <= 0) continue;
                t = m.animatedTile(ts, t, timeMs);
                TileDraw d;
                d.sx = (uint16_t)((t % cols) * 16);
                d.sy = (uint16_t)((t / cols) * 16);
                d.ts = (uint8_t)ts;
                d.dx = (int16_t)(x * TILE - camX);
                d.dy = (int16_t)(y * TILE - camY);
                d.key = ((uint32_t)ts << 24) | ((uint32_t)(d.sy >> 8) << 12) | (uint32_t)(d.sx >> 8);   // sheet, 256px block
                tileDraws.push_back(d);
            }
        std::sort(tileDraws.begin(), tileDraws.end(), [](const TileDraw& a, const TileDraw& b) { return a.key < b.key; });
        for (const TileDraw& d : tileDraws)
            c.imageTex(tsHandle[d.ts], tsTex[d.ts], d.sx, d.sy, 16, 16, d.dx + sh, d.dy);
    };

    // Map cache: Back + Buildings drawn once into an offscreen picture around the
    // camera; each frame shows it as one image, plus the animated tiles live.
    static struct { const TileMap* m = nullptr; std::string name; uint32_t rev = 0; int season = -1; int ox = 0, oy = 0; bool ok = false; } mc;
    bool cached = g_useMapCache && c.cacheSupported();
    if (!cached) mc.ok = false;
    if (cached) {
        const int CS = Canvas::CACHE_SIZE;
        if (!mc.ok || mc.m != &m || mc.name != m.name || mc.rev != m.rev || mc.season != g.season || camX < mc.ox ||
            camY < mc.oy || camX + TOP_W > mc.ox + CS || camY + TOP_H > mc.oy + CS) {
            mc.m = &m;
            mc.name = m.name;
            mc.rev = m.rev;
            mc.season = g.season;
            mc.ox = (int)std::floor((camX - (CS - TOP_W) / 2) / TILE) * TILE;
            mc.oy = (int)std::floor((camY - (CS - TOP_H) / 2) / TILE) * TILE;
            mc.ok = true;
            int cx0 = std::max(0, mc.ox / TILE), cy0 = std::max(0, mc.oy / TILE);
            int cx1 = std::min(m.width() - 1, (mc.ox + CS) / TILE - 1), cy1 = std::min(m.height() - 1, (mc.oy + CS) / TILE - 1);
            c.cacheBegin();
            for (int layer : {L_BACK, L_BUILDINGS}) {
                tileDraws.clear();
                for (int y = cy0; y <= cy1; y++)
                    for (int x = cx0; x <= cx1; x++) {
                        uint16_t t = m.tile(layer, x, y);
                        if (t == NO_TILE) continue;
                        int ts = m.tileset(layer, x, y);
                        int cols = tsCols[ts];
                        if (cols <= 0 || m.isAnimated(ts, t)) continue;
                        if (layer == L_BUILDINGS) {   // over a moving Back tile: drawn live with it
                            uint16_t bt = m.tile(L_BACK, x, y);
                            if (bt != NO_TILE && m.isAnimated(m.tileset(L_BACK, x, y), bt)) continue;
                        }
                        TileDraw d;
                        d.sx = (uint16_t)((t % cols) * 16);
                        d.sy = (uint16_t)((t / cols) * 16);
                        d.ts = (uint8_t)ts;
                        d.dx = (int16_t)(x * TILE - mc.ox);
                        d.dy = (int16_t)(y * TILE - mc.oy);
                        d.key = ((uint32_t)ts << 24) | ((uint32_t)(d.sy >> 8) << 12) | (uint32_t)(d.sx >> 8);
                        tileDraws.push_back(d);
                    }
                std::sort(tileDraws.begin(), tileDraws.end(), [](const TileDraw& a, const TileDraw& b) { return a.key < b.key; });
                for (const TileDraw& d : tileDraws) c.imageTex(tsHandle[d.ts], tsTex[d.ts], d.sx, d.sy, 16, 16, d.dx, d.dy);
            }
            c.cacheEnd();
        }
        c.cacheDraw((int)camX - mc.ox, (int)camY - mc.oy, TOP_W, TOP_H, c.shift(0.0f), 0);
        // the moving tiles (water...), and Buildings tiles sitting on moving Back tiles
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++) {
                bool backMoves = false;
                for (int layer : {L_BACK, L_BUILDINGS}) {
                    uint16_t t = m.tile(layer, x, y);
                    if (t == NO_TILE) continue;
                    int ts = m.tileset(layer, x, y);
                    int cols = tsCols[ts];
                    if (cols <= 0) continue;
                    bool moves = m.isAnimated(ts, t);
                    if (layer == L_BACK) backMoves = moves;
                    if (!moves && !(layer == L_BUILDINGS && backMoves)) continue;
                    t = m.animatedTile(ts, t, timeMs);
                    c.imageTex(tsHandle[ts], tsTex[ts], (t % cols) * 16, (t / cols) * 16, 16, 16, x * TILE - camX + c.shift(0.0f),
                               y * TILE - camY);
                }
            }
    } else {
        drawLayer(L_BACK, 0.0f);   // the whole world sits at one depth: objects split across
                                   // layers (walls/roofs, trunks/leaves) must not come apart in 3D
    }

    // Tilled soil
    if (g.cropLand()) {
        bool wetHalf = c.texWidth("TerrainFeatures_hoeDirt") >= 128;
        auto tilled = [&](int x, int y) { return m.inBounds(x, y) && g.soil(x, y).tilled; };
        float sh = c.shift(0.0f);
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++) {
                const Soil& s = g.soil(x, y);
                if (!s.tilled) continue;
                int sum = (tilled(x, y - 1) ? 100 : 0) + (tilled(x + 1, y) ? 10 : 0) +
                          (tilled(x, y + 1) ? 500 : 0) + (tilled(x - 1, y) ? 1000 : 0);
                int idx = hoeDirtIndex(sum);
                int sx = (idx % 4) * 16, sy = (idx / 4) * 16;
                float dx = x * TILE - camX + sh, dy = y * TILE - camY;
                c.image("TerrainFeatures_hoeDirt", sx, sy, 16, 16, dx, dy);
                if (s.watered) {
                    if (wetHalf) c.image("TerrainFeatures_hoeDirt", sx + 64, sy, 16, 16, dx, dy);
                    else c.image("TerrainFeatures_hoeDirt", sx, sy, 16, 16, dx, dy, false, RGBA(0x90, 0x80, 0x80));
                }
            }
    }

    if (!cached) drawLayer(L_BUILDINGS, 0.0f);
    profEnd(PROF_MAP);

    static std::vector<DrawItem> items;
    items.clear();
    const Location& here = g.location();
    bool hasGrass = !here.grass.empty();
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            if (g.debris(x, y) != DEB_NONE) items.push_back({(float)(y * TILE + 15), 0, x, y});
            if (g.soil(x, y).seed >= 0) items.push_back({(float)(y * TILE + 14), 1, x, y});
            if (hasGrass && here.grass[(size_t)y * m.width() + x]) items.push_back({(float)(y * TILE + 10), 10, x, y});
        }
    for (size_t i = 0; i < here.bushes.size(); i++) {
        const Location::Bush& b = here.bushes[i];
        if (b.x >= x0 - 3 && b.x <= x1 && b.y >= y0 && b.y <= y1 + 3) items.push_back({(float)((b.y + 1) * TILE - 2), 11, (int)i, 0});
    }
    for (size_t i = 0; i < here.monsters.size(); i++) items.push_back({here.monsters[i].y, 13, (int)i, 0});
    if (here.mineLevel && here.chestX >= 0) items.push_back({(float)(here.chestY * TILE + 15), 18, here.chestX, here.chestY});
    if (!g.riding && g.horseLoc == here.name) items.push_back({g.horseY, 19, 0, 0});
    if (here.isFarm)
        for (size_t i = 0; i < g.buildings.size(); i++) {
            const BuildingType* bt = g.typeOf(g.buildings[i]);
            if (bt) items.push_back({(float)((g.buildings[i].y + bt->h) * TILE - 1), 14, (int)i, 0});
        }
    for (size_t i = 0; i < here.fruitTrees.size(); i++) {
        const FruitTree& t = here.fruitTrees[i];
        if (t.x >= x0 - 2 && t.x <= x1 + 2 && t.y >= y0 && t.y <= y1 + 5) items.push_back({(float)(t.y * TILE + 14), 17, (int)i, 0});
    }
    const FarmBuilding* inside = g.buildingForInterior(here.name);
    if (inside) {
        for (size_t i = 0; i < g.animals.size(); i++)
            if (g.animals[i].home == inside->id && g.animals[i].x >= 0) items.push_back({g.animals[i].y, 15, (int)i, 0});
        if (const BuildingType* bt = g.typeOf(*inside))
            if (bt->hopperX >= 0) items.push_back({(float)(bt->hopperY * TILE + 15), 16, bt->hopperX, bt->hopperY});
    }
    for (size_t i = 0; i < here.clumps.size(); i++) {
        const Location::Clump& b = here.clumps[i];
        if (b.x >= x0 - 2 && b.x <= x1 && b.y >= y0 - 2 && b.y <= y1) items.push_back({(float)((b.y + 2) * TILE - 2), 12, (int)i, 0});
    }
    const void* grassTex = c.texLookup("TerrainFeatures_grass");
    if (g.location().isFarm)
        for (size_t i = 0; i < g.data.layout.buildings.size(); i++) {
            auto& b = g.data.layout.buildings[i];
            items.push_back({(float)((b.tileY + b.footH) * TILE), 2, (int)i, 0});
        }
    for (size_t i = 0; i < g.location().objects.size(); i++) {
        const WorldObject& o = g.location().objects[i];
        if (o.x >= x0 && o.x <= x1 && o.y >= y0 && o.y <= y1) items.push_back({(float)(o.y * TILE + 14), 7, (int)i, 0});
    }
    for (size_t i = 0; i < g.location().trees.size(); i++) {
        const Tree& t = g.location().trees[i];
        if (t.x >= x0 - 2 && t.x <= x1 + 2 && t.y >= y0 && t.y <= y1 + 6)
            items.push_back({(float)(t.y * TILE + 14), 9, (int)i, 0});
    }
    {
        const std::vector<Placed>& pl = g.placedHere();
        for (size_t i = 0; i < pl.size(); i++)
            if (pl[i].x >= x0 && pl[i].x <= x1 && pl[i].y >= y0 && pl[i].y <= y1)
                items.push_back({(float)(pl[i].y * TILE + 15), 8, (int)i, 0});
    }
    if (cs.active) {
        for (size_t i = 0; i < cs.actors.size(); i++)
            if (cs.actors[i].visible) items.push_back({cs.actors[i].y, 6, (int)i, 0});
    } else {
        items.push_back({g.py, 3, 0, 0});
        for (size_t i = 0; i < g.npcs.size(); i++)
            if (g.npcs[i].loc == g.location().name) items.push_back({g.npcs[i].y, 4, (int)i, 0});
    }
    for (size_t i = 0; i < g.furniture.size(); i++) {
        const FurniturePlacement& fp = g.furniture[i];
        if (fp.map != g.location().name) continue;
        auto fd = g.data.furniture.find(fp.id);
        int bh = fd != g.data.furniture.end() ? fd->second.boxH : 1;
        bool flat = fd != g.data.furniture.end() && fd->second.type == "rug";   // rugs lie under everything
        items.push_back({flat ? -1000.0f : (float)((fp.y + bh) * TILE) - 1, 5, (int)i, 0});
    }
    std::stable_sort(items.begin(), items.end(),
                     [](const DrawItem& a, const DrawItem& b) { return a.sortY < b.sortY; });

    for (auto& it : items) {
        switch (it.kind) {
            case 0: {
                static const int weeds[2] = {674, 675}, stones[2] = {343, 450}, twigs[2] = {294, 295};
                int v = g.debrisVariant(it.a, it.b) & 1;
                DebrisType d = g.debris(it.a, it.b);
                int idx = d == DEB_WEED ? weeds[v] : d == DEB_STONE ? stones[v] : twigs[v];
                objectIcon(c, idx, it.a * TILE - camX, it.b * TILE - camY);
                break;
            }
            case 1: {
                const Soil& s = g.soil(it.a, it.b);
                const CropDef* cd = g.data.cropForSeed(s.seed);
                if (!cd) break;
                int frame = g.cropFrame(it.a, it.b);
                int sx = (cd->spriteIndex % 2) * 128 + frame * 16, sy = (cd->spriteIndex / 2) * 32;
                c.image(cd->texture, sx, sy, 16, 32, it.a * TILE - camX,
                        it.b * TILE - 16 - camY, ((it.a + it.b) & 1) != 0,
                        s.dead ? RGBA(0x90, 0x70, 0x40) : 0);
                break;
            }
            case 2: {
                auto& b = g.data.layout.buildings[it.a];
                float dx = b.tileX * TILE + (b.footW * TILE - b.srcW) / 2.0f - camX;
                float dy = (b.tileY + b.footH) * TILE - b.srcH - camY;
                if (c.texWidth(b.texture) > 0) {
                    c.image(b.texture, b.srcX, b.srcY, b.srcW, b.srcH, dx, dy);
                } else if (b.name == "mailbox") {   // converted before the mailbox texture was included
                    c.rect(dx + 7, dy + 14, 2, 18, RGBA(0x6A, 0x3A, 0x18));
                    c.rect(dx + 2, dy + 6, 12, 9, RGBA(0x4A, 0x6A, 0xB8));
                    c.rect(dx + 2, dy + 6, 12, 2, RGBA(0x6A, 0x8A, 0xD8));
                    if (!g.mailbox.empty()) c.rect(dx + 13, dy + 2, 2, 6, RGBA(0xD8, 0x30, 0x30));   // flag up
                }
                break;
            }
            case 3: {
                if (g.riding) {   // on horseback: the horse, then you a little higher
                    int f = g.moving ? 1 + ((int)(g.walkTime * 9) % 6) : 0;
                    int row = g.facing == FACE_DOWN ? 0 : g.facing == FACE_RIGHT ? 1 : g.facing == FACE_UP ? 2 : 1;
                    c.image("Animals_horse", f * 32, row * 32, 32, 32, std::floor(g.px - camX - 16), std::floor(g.py - camY - 30),
                            g.facing == FACE_LEFT);
                    drawPlayerWithTool(c, g, g.px - camX, g.py - camY - 10);
                    break;
                }
                drawPlayerWithTool(c, g, g.px - camX, g.py - camY);
                const ToolDef* td = nullptr;
                const ToolDef* wd = g.inv[g.selected].type == SLOT_TOOL ? g.data.tool(g.inv[g.selected].id) : nullptr;
                if (g.toolTimer > 0 && wd && (wd->kind == TOOL_WEAPON || (wd->kind == TOOL_SCYTHE && wd->texture != "TileSheets_tools"))) {
                    // the blade sweeps a quarter circle across the way you face
                    float k = 1.0f - g.toolTimer / TOOL_SWING;
                    static const float base[4] = {1.5708f, 0.0f, -1.5708f, 3.14159f};
                    int f = g.facing & 3;
                    float a = base[f] + (f == FACE_LEFT ? 1.2f : -1.2f) + (f == FACE_LEFT ? -2.4f : 2.4f) * k;
                    float hx = g.px - camX + (f == FACE_RIGHT ? 4 : f == FACE_LEFT ? -4 : 0), hy = g.py - camY - 12;
                    int cols = std::max(1, c.texWidth(wd->texture) / 16);
                    // the sprite points up-right (45 deg): turn it so it points along a
                    c.imageRotated(wd->texture, (wd->spriteIndex % cols) * 16, (wd->spriteIndex / cols) * 16, 16, 16, 2, 14,
                                   hx, hy, a + 0.785f);
                } else if (g.toolTimer > 0 && g.inv[g.selected].type == SLOT_TOOL && !swingTool(g, td)) {
                    int tx, ty;   // tools without a swing yet (scythe, rod...): icon at the target
                    g.targetTile(tx, ty);
                    slotIcon(c, g, g.inv[g.selected], tx * TILE - camX, ty * TILE - camY - 6);
                }
                break;
            }
            case 6: {
                const EventActor& a = cs.actors[it.a];
                float jump = a.jumpT >= 0 ? -std::sin(a.jumpT / 0.45f * 3.14159f) * 8 : 0;
                float ax = a.x + a.offX - camX, ay = a.y + a.offY - camY + jump;
                int frame = a.frame;
                if (!a.anim.empty()) {
                    size_t k = (size_t)(a.animT * 1000 / a.animMs);
                    frame = a.animLoop ? a.anim[k % a.anim.size()] : a.anim[std::min(k, a.anim.size() - 1)];
                }
                if (a.name == "farmer") {
                    if (!cs.aboveHead.empty()) {
                        // holding a gift up: both arms raised (farmer_base frame 57), the item over the head
                        drawFarmer(c, g, ax, ay, FACE_DOWN, false, 0, 57);
                        Slot s;
                        const std::string& h = cs.aboveHead;
                        for (size_t ti = 0; ti < g.data.tools.size(); ti++) {
                            const std::string& k = g.data.tools[ti].key;
                            if ((h == "rod" && k == "BambooPole") || (h == "pan" && k == "Pan")) {
                                s.type = SLOT_TOOL;
                                s.id = (int)ti;
                            }
                        }
                        if (s.type != SLOT_TOOL && !h.empty() && isdigit((unsigned char)h[0])) {
                            s.type = SLOT_OBJECT;
                            s.id = atoi(h.c_str());
                        }
                        s.count = 1;
                        if (s.type != SLOT_EMPTY) slotIcon(c, g, s, std::floor(ax - 8), std::floor(ay - 50));
                        break;
                    }
                    drawFarmer(c, g, ax, ay, a.facing, a.moving, a.walkTime, frame);
                } else {
                    int f = frame >= 0 ? frame : (a.facing & 3) * 4 + (a.moving ? ((int)(a.walkTime * 6)) & 3 : 0);
                    c.image("Characters_" + a.name, (f % 4) * 16, (f / 4) * 32, 16, 32, std::floor(ax - 8),
                            std::floor(ay - 30));
                }
                if (a.emoteT > 0 && a.emote >= 0) {
                    int ef = a.emote + ((int)((1.0f - a.emoteT) * 8) & 3);
                    c.image("TileSheets_emotes", (ef % 4) * 16, (ef / 4) * 16, 16, 16, std::floor(ax - 8),
                            std::floor(ay - 50));
                }
                if (a.bubbleT > 0 && !a.bubble.empty()) {
                    float tw = c.textWidth(a.bubble, 0.42f) + 10;
                    box(c, ax - tw / 2, ay - 52, tw, 18, RGBA(0xFF, 0xFF, 0xFF), RGBA(0x60, 0x40, 0x30), 1);
                    c.text(ax, ay - 50, 0.42f, COL_TEXT, a.bubble, ALIGN_CENTER);
                }
                break;
            }
            case 7: {
                const WorldObject& o = g.location().objects[it.a];
                const ObjectDef* od = g.data.object(o.itemId);
                if (od) itemIcon(c, od, o.x * TILE - camX, o.y * TILE - camY);
                else objectIcon(c, o.itemId, o.x * TILE - camX, o.y * TILE - camY);
                break;
            }
            case 5: {
                const FurniturePlacement& fp = g.furniture[it.a];
                auto fd = g.data.furniture.find(fp.id);
                if (fd == g.data.furniture.end()) break;
                int sw = fd->second.spriteW * 16, sh = fd->second.spriteH * 16;
                int sx = (fp.id % 32) * 16, sy = (fp.id / 32) * 16;
                float bottom = (fp.y + fd->second.boxH) * TILE;
                c.image("TileSheets_furniture", sx, sy, sw, sh, fp.x * TILE - camX, bottom - sh - camY);
                break;
            }
            case 9: {
                const Tree& t = g.location().trees[it.a];
                auto td = g.data.trees.find(t.type);
                if (td == g.data.trees.end()) break;
                const std::string& tex = td->second.texture[g.season & 3];
                float bx = t.x * TILE - camX, by = (t.y + 1) * TILE - camY;
                float wob = t.shake > 0 ? std::sin(t.shake * 60.0f) * 1.5f : 0.0f;
                if (t.stump) {
                    c.image(tex, 32, 96, 16, 32, bx, by - 32);
                } else if (t.stage >= TREE_GROWN) {
                    c.image(tex, 32, 96, 16, 32, bx, by - 32);   // roots: the stump under the trunk
                    c.image(tex, 0, 0, 48, 96, bx - 16 + wob, by - 96 + 8);
                } else if (t.stage >= 3) {
                    c.image(tex, 0, 96, 16, 32, bx + wob, by - 32);
                } else {
                    static const int sx[3] = {32, 0, 16};
                    c.image(tex, sx[t.stage], 128, 16, 16, bx + wob, by - 16);
                }
                break;
            }
            case 8: {
                const Placed& p = g.placedHere()[it.a];
                float x = p.x * TILE - camX, y = p.y * TILE - camY;
                if (!p.big) {
                    const ObjectDef* o = g.data.object(p.id);
                    itemIcon(c, o, x, y);
                    break;
                }
                bool working = p.rule >= 0 && !g.machineReady(p);
                float wob = working && ((timeMs / 120) % 8 == 0) ? 1.0f : 0.0f;
                bigSprite(c, g, p.id, 0, x + wob, y - 16);
                if (g.machineReady(p)) {   // bubble with the finished item
                    float bob = std::sin(timeMs / 250.0f) * 1.5f;
                    float bx = x - 2, by = y - 34 + bob;
                    c.rect(bx, by, 20, 20, RGBA(0xFF, 0xFF, 0xFF, 0xD0));
                    c.rect(bx + 8, by + 20, 4, 3, RGBA(0xFF, 0xFF, 0xFF, 0xD0));
                    slotIcon(c, g, p.held, bx + 2, by + 2);
                } else if (working && !g.data.machineRules[p.rule].autoRun) {
                    float f = g.machineProgress(p);
                    c.rect(x + 1, y - 20, 14, 3, RGBA(0x30, 0x20, 0x10, 0xC0));
                    c.rect(x + 2, y - 19, 12 * f, 1, RGBA(0x7C, 0xD0, 0x42));
                }
                break;
            }
            case 10: {   // 1-4 grass tufts, swaying for a moment after you walk through
                uint8_t v = here.grass[(size_t)it.b * m.width() + it.a];
                int n = v & 7;
                int row = (v & 8) ? 160 : g.season == SUMMER ? 20 : g.season == FALL ? 40 : g.season == WINTER ? 80 : 0;
                float sway = 0;
                int cell = it.b * m.width() + it.a;
                for (auto& r : here.rustles)
                    if (r.cell == cell) sway = std::sin(r.t * 30.0f) * 2.0f * (r.t / 0.5f);
                float tx = it.a * TILE - camX, ty = it.b * TILE - camY;
                for (int k = 0; k < n; k++) {
                    uint32_t h = (uint32_t)(cell * 4 + k) * 2654435761u;
                    int variant = (h >> 13) % 3;
                    float ox = (k & 1) * 7 - 3 + (float)((h >> 5) % 3), oy = (k >> 1) * 6 - 12;
                    float s = k & 1 ? sway : -sway;
                    c.imageTex(grassTex, "TerrainFeatures_grass", variant * 15, row, 15, 20, std::floor(tx + ox + s), ty + oy);
                }
                break;
            }
            case 14: {   // a farm building (see-through while Robin works on it)
                const FarmBuilding& fb = g.buildings[it.a];
                const BuildingType* bt = g.typeOf(fb);
                if (fb.type == "Fish Pond" && c.texWidth(bt->texture) >= 80) {   // water, frame, fish, bucket
                    float px = fb.x * TILE - camX, py = fb.y * TILE - camY;
                    uint32_t tint = fb.daysLeft > 0 ? RGBA(0xFF, 0xFF, 0xFF, 0x70) : 0;
                    c.image(bt->texture, 0, 80, 80, 80, std::floor(px), std::floor(py), false,
                            fb.daysLeft > 0 ? RGBA(0x50, 0x90, 0xD0, 0x70) : RGBA(0x50, 0x90, 0xD0));
                    for (int k = 0; k < std::min(fb.pondPop, 10) && fb.daysLeft <= 0; k++) {   // fish shadows drifting
                        float t = timeMs * 0.0004f + k * 1.7f;
                        float fx = px + 40 + std::sin(t + k) * 22, fy = py + 38 + std::cos(t * 0.7f + k * 2.3f) * 18;
                        c.rect(std::floor(fx) - 4, std::floor(fy) - 1, 8, 3, RGBA(0x08, 0x20, 0x40, 0xC0));
                    }
                    c.image(bt->texture, 0, 0, 80, 80, std::floor(px), std::floor(py), false, tint);
                    if (fb.pondOut >= 0) itemIcon(c, g.data.object(fb.pondOut), px + 58, py + 44);
                    if (fb.daysLeft > 0)
                        c.text(px + 40, py, 0.4f, COL_CREAM, std::to_string(fb.daysLeft) + (fb.daysLeft == 1 ? " day" : " days"), ALIGN_CENTER);
                    break;
                }
                int sw = bt->srcW ? bt->srcW : c.texWidth(bt->texture), sh = bt->srcH ? bt->srcH : c.texHeight(bt->texture);
                float dx = fb.x * TILE + (bt->w * TILE - sw) / 2.0f - camX, dy = (fb.y + bt->h) * TILE - sh - camY;
                bool building = fb.daysLeft > 0 && fb.upgradingTo.empty();
                int sy0 = fb.type == "Greenhouse" && !g.greenhouseFixed() ? 0 : bt->srcY;   // the ruins sit above
                if (sw > 0) c.image(bt->texture, bt->srcX, sy0, sw, sh, std::floor(dx), std::floor(dy), false,
                                    building ? RGBA(0xFF, 0xFF, 0xFF, 0x70) : 0);
                else c.rect(fb.x * TILE - camX, fb.y * TILE - camY, bt->w * TILE, bt->h * TILE, RGBA(0x8A, 0x5A, 0x30));
                if (building) {   // scaffolding hint
                    c.rect(fb.x * TILE - camX, (fb.y + bt->h) * TILE - 4 - camY, bt->w * TILE, 4, RGBA(0x9A, 0x6A, 0x3A));
                    c.text(fb.x * TILE + bt->w * 8 - camX, fb.y * TILE - camY, 0.4f, COL_CREAM,
                           std::to_string(fb.daysLeft) + (fb.daysLeft == 1 ? " day" : " days"), ALIGN_CENTER);
                }
                break;
            }
            case 15: {   // a farm animal
                const FarmAnimal& a = g.animals[it.a];
                const AnimalType* at = g.data.animalType(a.type);
                if (!at) break;
                bool baby = a.age < at->daysToMature && !at->babyTex.empty();
                const std::string& tex = baby ? at->babyTex : at->tex;
                int w = at->w, h = at->h;
                if (baby && w > 16 && c.texWidth(tex) > 0 && c.texWidth(tex) < 4 * w) { w = c.texWidth(tex) / 4; h = w; }
                int row = a.facing == 0 ? 0 : a.facing == 2 ? 2 : 1;
                int frame = (a.vx || a.vy) ? ((int)(a.anim * 6) & 3) : 0;
                float ax = std::floor(a.x - camX), ay = std::floor(a.y - camY);
                c.rect(ax - w / 3.0f, ay - 2, w * 2 / 3.0f, 3, RGBA(0, 0, 0, 0x40));
                c.image(tex, frame * w, row * h, w, h, ax - w / 2.0f, ay - h + 2, a.facing == 3);
                if (a.heart > 0) {   // a little heart when petted
                    float hy = ay - h - 4 - (1.2f - a.heart) * 6;
                    c.rect(ax - 3, hy, 2, 2, RGBA(0xE0, 0x30, 0x40));
                    c.rect(ax + 1, hy, 2, 2, RGBA(0xE0, 0x30, 0x40));
                    c.rect(ax - 3, hy + 1, 6, 2, RGBA(0xE0, 0x30, 0x40));
                    c.rect(ax - 2, hy + 3, 4, 1, RGBA(0xE0, 0x30, 0x40));
                    c.rect(ax - 1, hy + 4, 2, 1, RGBA(0xE0, 0x30, 0x40));
                }
                if (a.ready >= 0) c.rect(ax - 1, ay - h - 2, 3, 3, RGBA(0xFF, 0xF0, 0x80));   // produce waiting
                break;
            }
            case 17: {   // a fruit tree: 48x80 cells, 4 sapling sizes then the 4 seasons
                const FruitTree& t = here.fruitTrees[it.a];
                const FruitTreeDef* d = g.data.fruitTreeForSapling(t.sapling);
                if (!d) break;
                bool greenhouse = here.name.rfind("Greenhouse", 0) == 0;
                int cell = t.daysLeft > 21 ? 0 : t.daysLeft > 14 ? 1 : t.daysLeft > 7 ? 2 : t.daysLeft > 0 ? 3
                         : 4 + (greenhouse ? (int)SUMMER : g.season);
                float wob = t.shake > 0 ? std::sin(t.shake * 60.0f) * 1.5f : 0.0f;
                float tx = std::floor(t.x * TILE + 8 - 24 - camX + wob), ty = (t.y + 1) * TILE - 80 - camY;
                c.image(d->texture, cell * 48, d->row * 80, 48, 80, tx, ty);
                if (t.daysLeft <= 0 && t.fruit > 0) {   // the fruit hanging in the leaves
                    static const float fx[3] = {8, 24, 16}, fy[3] = {20, 12, 32};
                    const ObjectDef* fo = g.data.object(d->fruit);
                    for (int k = 0; k < t.fruit && k < 3; k++) itemIcon(c, fo, tx + fx[k], ty + fy[k]);
                }
                break;
            }
            case 19: {   // your horse, waiting
                int row = g.horseFacing == FACE_DOWN ? 0 : g.horseFacing == FACE_UP ? 2 : 1;
                c.rect(std::floor(g.horseX - camX - 10), std::floor(g.horseY - camY - 2), 20, 3, RGBA(0, 0, 0, 0x40));
                c.image("Animals_horse", 0, row * 32, 32, 32, std::floor(g.horseX - camX - 16), std::floor(g.horseY - camY - 30),
                        g.horseFacing == FACE_LEFT);
                break;
            }
            case 18:   // a mine treasure chest
                bigSprite(c, g, 130, 0, it.a * TILE - camX, (it.b - 1) * TILE - camY);
                break;
            case 16:   // the feed hopper
                bigSprite(c, g, 99, 0, it.a * TILE - camX, (it.b - 1) * TILE - camY);
                break;
            case 13: {   // a monster
                const Location::Monster& mo = here.monsters[it.a];
                float mx = std::floor(mo.x - camX), my = std::floor(mo.y - camY);
                bool flash = mo.hurt > 0 && ((int)(mo.hurt * 30) & 1);
                c.rect(mx - 6, my - 2, 12, 3, RGBA(0, 0, 0, 0x50));   // shadow
                if (mo.bug) {
                    int row = std::fabs(mo.vx) > std::fabs(mo.vy) ? (mo.vx > 0 ? 1 : 3) : (mo.vy > 0 ? 0 : 2);
                    int f = (int)(mo.anim * 10) & 3;
                    c.image("Characters_Monsters_Bug", f * 16, row * 16, 16, 16, mx - 8, my - 22, false,
                            flash ? RGBA(0xFF, 0x60, 0x60) : 0);
                } else if (mo.ai >= 2) {   // bats, dust spirits, rock crabs: 16x24 frames on their own sheets
                    std::string tex = "Characters_Monsters_" + mo.kind;
                    for (auto& ch : tex) if (ch == ' ') ch = '_';
                    int f = mo.ai == 2 ? ((int)(mo.anim * 10) & 3) : mo.ai == 3 ? ((int)(mo.anim * 8) & 3)
                          : mo.hidden ? 0 : 1 + ((int)(mo.anim * 6) % 3);
                    float lift = mo.ai == 2 ? 14 + std::sin(mo.anim * 6) * 2 : 0;   // bats fly
                    c.image(tex, f * 16, 0, 16, 24, mx - 8, my - 22 - lift, false, flash ? RGBA(0xFF, 0x60, 0x60) : 0);
                } else {
                    // a slime bounces while it hops; grey on the sheet, tinted per kind
                    int f = mo.think > 0.4f && (std::fabs(mo.vx) + std::fabs(mo.vy) > 5) ? ((int)(mo.anim * 8) & 3) : ((int)(mo.anim * 3) & 1);
                    c.image("Characters_Monsters_Green_Slime", f * 16, 0, 16, 24, mx - 8, my - 22, false,
                            flash ? RGBA(0xFF, 0xFF, 0xFF) : mo.tint);
                    c.rect(mx - 3, my - 9, 2, 2, RGBA(0x20, 0x20, 0x20));   // eyes
                    c.rect(mx + 1, my - 9, 2, 2, RGBA(0x20, 0x20, 0x20));
                }
                break;
            }
            case 12: {   // big stump / log / boulder: 32x32 on Maps/springobjects
                const Location::Clump& b = here.clumps[it.a];
                int idx = b.kind == 1 ? 602 : b.kind == 2 ? 672 : 600;
                float wob = b.shake > 0 ? std::sin(b.shake * 70.0f) * 1.0f : 0.0f;
                c.image("Maps_springobjects", (idx % 24) * 16, (idx / 24) * 16, 32, 32, std::floor(b.x * TILE - camX + wob),
                        b.y * TILE - camY);
                break;
            }
            case 11: {   // bushes from TileSheets/bushes: small 16x32, medium 32x32, large 48x48
                const Location::Bush& b = here.bushes[it.a];
                int sx, sy, sw, sh;
                int season = g.season & 3;
                if (b.size == 2) {
                    sw = sh = 48;
                    sx = season == FALL ? 48 : 0;
                    sy = season == WINTER ? 176 : 128;
                } else if (b.size == 1) {
                    sw = sh = 32;
                    static const int mx[4] = {0, 64, 0, 96}, my[4] = {96, 96, 64, 96};
                    sx = mx[season];
                    sy = my[season];
                    int berry = g.bushBerry(b);
                    if (berry == 296) { sx = 32; sy = 0; }        // salmonberries
                    if (berry == 410) { sx = 32; sy = 64; }       // blackberries
                } else {
                    sw = 16;
                    sh = 32;
                    sx = season * 32 + ((b.x + b.y) & 1) * 16;
                    sy = 224;
                }
                float wob = b.shake > 0 ? std::sin(b.shake * 60.0f) * 1.0f : 0.0f;
                c.image("TileSheets_bushes", sx, sy, sw, sh, std::floor(b.x * TILE - camX + wob), (b.y + 1) * TILE - sh - camY);
                break;
            }
            case 4: {
                const NpcState& n = g.npcs[it.a];
                int step = n.moving ? ((int)(n.walkTime * 6)) & 3 : 0;
                int frame = (n.facing & 3) * 4 + step;
                int af = n.moving ? -1 : n.animFrame();
                if (af >= 0) frame = af;
                c.image("Characters_" + n.def->name, (frame % 4) * 16, (frame / 4) * 32, 16, 32,
                        std::floor(n.x - 8 - camX), std::floor(n.y - 30 - camY));
                break;
            }
        }
    }

    // effects: bits flying off what a tool hit, a felled tree toppling
    for (const Game::Effect& e : g.effects) {
        if (e.kind == Game::Effect::ITEM) {
            const ObjectDef* od = g.data.object(e.item);
            float ix = std::floor(e.x - camX - 8), iy = std::floor(e.y - camY - 8);
            if (od) itemIcon(c, od, ix, iy);
            else objectIcon(c, e.item, ix, iy);
            continue;
        }
        if (e.kind == Game::Effect::FALLING_TREE) {
            float k = std::min(1.0f, e.t / 1.0f);
            float ang = e.dir * 1.5708f * k * k;      // speeds up as it goes over
            c.imageRotated(e.tex, e.sx, e.sy, e.sw, e.sh, 24, 88, e.x - camX, e.y - camY, ang);
        } else {
            c.image(e.tex, e.sx, e.sy, e.sw, e.sh, std::floor(e.x - camX - e.sw / 2.0f), std::floor(e.y - camY - e.sh / 2.0f));
        }
    }

    if (inside) {   // hay lying in the troughs
        auto tr = g.troughTiles(here);
        for (size_t i = 0; i < tr.size() && i < inside->troughs.size(); i++)
            if (inside->troughs[i]) itemIcon(c, g.data.object(178), tr[i].first * TILE - camX, tr[i].second * TILE - 2 - camY);
    }
    if (!g.placing.empty() && here.isFarm)
        if (const BuildingType* pt = g.data.buildingType(g.placing)) {
            bool ok = g.placeOk(g.placeX, g.placeY);
            int sw = pt->srcW ? pt->srcW : c.texWidth(pt->texture), sh = pt->srcH ? pt->srcH : c.texHeight(pt->texture);
            if (sw > 0)
                c.image(pt->texture, pt->srcX, pt->srcY, sw, sh, std::floor(g.placeX * TILE + (pt->w * TILE - sw) / 2.0f - camX),
                        std::floor((g.placeY + pt->h) * TILE - sh - camY), false, RGBA(0xFF, 0xFF, 0xFF, 0x80));
            for (int dy = 0; dy < pt->h; dy++)
                for (int dx = 0; dx < pt->w; dx++)
                    c.rect((g.placeX + dx) * TILE + 1 - camX, (g.placeY + dy) * TILE + 1 - camY, TILE - 2, TILE - 2,
                           ok ? RGBA(0x40, 0xE0, 0x40, 0x60) : RGBA(0xE0, 0x30, 0x30, 0x60));
        }
    profEnd(PROF_OBJECTS);
    drawLayer(L_FRONT, 0.0f);
    drawLayer(L_ALWAYSFRONT, 0.0f);
    // butterflies on fine spring/summer days (TileSheets/critters: 4-frame flaps, 20 columns)
    if (m.outdoors && !g.raining && !g.windy && (g.season == SPRING || g.season == SUMMER) &&
        g.timeOfDay < 18 * 60 && c.texWidth("TileSheets_critters") >= 320) {
        static const int kinds[6] = {128, 132, 136, 148, 152, 156};
        int n = std::min(16, m.width() * m.height() / 350);
        for (int i = 0; i < n; i++) {
            uint32_t h = (uint32_t)(i + 7) * 2654435761u ^ (uint32_t)m.width() * 40503u;
            float ax = (float)((h >> 4) % (uint32_t)(m.width() * TILE)), ay = (float)((h >> 15) % (uint32_t)(m.height() * TILE));
            float t = timeMs / 1000.0f + i * 3.7f;
            float x = ax + std::sin(t * 0.37f) * 60 + std::sin(t * 1.3f) * 14 - camX;
            float y = ay + std::sin(t * 0.29f + 1.0f) * 40 + std::sin(t * 2.1f) * 6 - camY;
            if (x < -16 || y < -16 || x > TOP_W || y > TOP_H) continue;
            static const int flap[6] = {0, 1, 2, 3, 2, 1};
            int idx = kinds[(h >> 9) % 6] + flap[(int)(t * 12) % 6];
            bool left = std::cos(t * 0.37f) < 0;
            c.image("TileSheets_critters", (idx % 20) * 16, (idx / 20) * 16, 16, 16, std::floor(x), std::floor(y), left);
        }
    }
    profEnd(PROF_FRONT);

    if (g.fishingActive() && !cs.active) {
        const Fishing& f = g.fishing;
        float pxs = std::floor(g.px - camX), pys = std::floor(g.py - camY);
        if (f.state == FS_CHARGE) {   // horizontal power bar over the head: red -> yellow -> green
            float bx = pxs - 18, by = pys - 60;
            box(c, bx, by, 36, 8, RGBA(0xFF, 0xE0, 0xA8), RGBA(0x8A, 0x3A, 0x10), 1);
            float p = f.power;
            uint32_t col = p < 0.33f ? RGBA(0xE0, 0x30, 0x10) : p < 0.66f ? RGBA(0xF8, 0xC0, 0x20) : RGBA(0x70, 0xE0, 0x30);
            c.rect(bx + 2, by + 2, 32 * p, 4, col);
        } else if (f.state != FS_DONE || f.caught) {
            float tipX, tipY;
            rodTip(g, pxs, pys, tipX, tipY);
            float bx = f.bobX * TILE + 8 - camX, by = f.bobY * TILE + 8 - camY;
            if (f.state == FS_CAST) {   // bobber flying out in an arc
                float k = std::min(1.0f, f.t / 0.5f);
                bx = tipX + (bx - tipX) * k;
                by = tipY + (by - tipY) * k - std::sin(k * 3.14159f) * 28;
            }
            bool dip = (f.state == FS_WAIT && ((int)(f.t * 2.2f) & 1)) || f.state == FS_BITE || f.state == FS_REEL;
            c.line(tipX, tipY, bx, by - 3, RGBA(0xF0, 0xF0, 0xF0, 0xB0), 1);
            if (c.texWidth("TileSheets_bobbers") > 0) {
                c.image("TileSheets_bobbers", 0, dip ? 16 : 0, 16, 16, std::floor(bx - 8), std::floor(by - 10));
            } else {
                c.rect(bx - 2, by - 2, 4, 4, RGBA(0xE0, 0x30, 0x30));
                c.rect(bx - 2, by, 4, 2, RGBA(0xF8, 0xF8, 0xF8));
            }
            if (f.state == FS_WAIT && f.t < 0.3f && c.texWidth("TileSheets_rain") >= 64) {   // splash where it lands
                int fr = 1 + std::min(2, (int)(f.t / 0.1f));
                c.image("TileSheets_rain", fr * 16, 0, 16, 16, std::floor(bx - 8), std::floor(by - 10));
            }
            if (f.state == FS_BITE) {
                c.rect(pxs - 5, pys - 58, 10, 16, RGBA(0xFF, 0xFF, 0xFF, 0xE0));
                c.text(pxs, pys - 58, 0.55f, RGBA(0xD0, 0x20, 0x20), "!", ALIGN_CENTER);
            }
        }
    }
    if (g.charging && !cs.active) {   // charged hoe / can: show the area and a meter
        std::vector<std::pair<int, int>> tiles;
        int lv = g.chargeLevel();
        g.chargedTiles(lv, tiles);
        for (auto& t : tiles)
            c.rect(t.first * TILE - camX, t.second * TILE - camY, 16, 16,
                   RGBA(0xFF, 0xFF, 0xFF, 0x50));
        const ToolDef* td = g.data.tool(g.inv[g.selected].id);
        int max = td ? std::max(1, td->upgrade) : 1;
        float bx = std::floor(g.px - 12 - camX), by = std::floor(g.py - 44 - camY);
        c.rect(bx, by, 24, 5, RGBA(0x30, 0x20, 0x10, 0xD0));
        c.rect(bx + 1, by + 1, 22.0f * lv / max, 3, RGBA(0xFF, 0xD0, 0x40));
    }
    const Slot& sel = g.inv[g.selected];
    if (!sel.empty() && g.toolTimer <= 0 && !cs.active && !g.charging && !g.fishingActive()) {
        int tx, ty;
        g.targetTile(tx, ty);
        float cx = tx * TILE - camX, cy = ty * TILE - camY;
        uint32_t col = RGBA(0xFF, 0xFF, 0xFF, 0x60);
        c.rect(cx, cy, 16, 1, col);
        c.rect(cx, cy + 15, 16, 1, col);
        c.rect(cx, cy, 1, 16, col);
        c.rect(cx + 15, cy, 1, 16, col);
    }
}

static void drawWeatherAndLight(Canvas& c, Game& g, uint32_t timeMs) {
    if (!g.map().outdoors) return;
    if (g.raining) {
        // the game's raindrops (TileSheets/rain): a falling streak, then a 3-frame splash
        bool sprite = c.texWidth("TileSheets_rain") >= 64;
        float sh = c.shift(2.0f);
        const int N = g.stormy ? 90 : 60;
        for (int i = 0; i < N; i++) {
            uint32_t h0 = (uint32_t)(i + 1) * 2654435761u;
            uint32_t period = 520 + (h0 >> 20) % 360;               // ms per drop cycle
            uint32_t tt = timeMs + (h0 >> 4) % period;
            uint32_t cycle = tt / period, ph = tt % period;
            uint32_t h = (cycle * 40503u + (uint32_t)i * 2246822519u) * 2654435761u;
            float x0 = (float)(h % 480) - 20 + sh, y0 = (float)((h >> 9) % 220) - 40;
            uint32_t fallMs = period - 240;
            int row = (h >> 3) & 1;
            if (ph < fallMs) {
                float k = ph / (float)fallMs;
                float x = x0 + 30 - k * 30, y = y0 + k * 60;
                if (sprite) c.image("TileSheets_rain", 0, row * 16, 16, 16, x, y);
                else c.line(x + 8, y, x + 4, y + 10, RGBA(0xA0, 0xB8, 0xFF, 0x90), 1.0f);
            } else if (sprite) {
                int f = 1 + std::min(2, (int)((ph - fallMs) / 80));
                c.image("TileSheets_rain", f * 16, row * 16, 16, 16, x0, y0 + 60);
            }
        }
        c.rect(0, 0, TOP_W, TOP_H, RGBA(0x20, 0x30, 0x50, g.stormy ? 0x48 : 0x30));   // wet, grey light
    }
    if (g.snowing) {
        // soft flakes drifting down and swaying
        float sh = c.shift(2.0f);
        for (int i = 0; i < 70; i++) {
            uint32_t h = (uint32_t)(i + 1) * 2654435761u;
            float speed = 0.012f + ((h >> 22) & 63) * 0.0002f;
            float x = std::fmod((float)((h >> 5) % 440) + std::sin(timeMs * 0.0015f + i) * 10.0f - g.px * 0.6f, 440.0f);
            float y = std::fmod((float)((h >> 13) % 280) + timeMs * speed - g.py * 0.6f, 280.0f);
            if (x < 0) x += 440.0f;
            if (y < 0) y += 280.0f;
            x -= 20;
            y -= 20;
            int sz = 1 + ((h >> 3) % 3 == 0);
            c.rect(x + sh, y, (float)sz + 1, (float)sz + 1, RGBA(0xFF, 0xFF, 0xFF, 0xE0));
            if (sz > 1) c.rect(x + sh - 1, y + 1, (float)sz + 3, (float)sz - 1, RGBA(0xFF, 0xFF, 0xFF, 0x70));
        }
    }
    if (g.windy && g.season != WINTER) {
        // blowing petals (spring) / leaves (fall): Cursors row 1184/1200/1216, 4-frame flutter
        int row = g.season == SPRING ? 1184 : g.season == SUMMER ? 1200 : 1216;
        float sh = c.shift(2.0f);
        float camX = g.px, camY = g.py;
        const int N = 36;
        for (int i = 0; i < N; i++) {
            uint32_t h = (uint32_t)(i + 1) * 2654435761u;
            float speed = 0.012f + (h >> 24) * 0.00008f;              // px per ms, drifting left
            float fall = 0.006f + ((h >> 16) & 255) * 0.00003f;       // and slowly down
            float x = std::fmod((float)((h >> 5) % 1000) - timeMs * speed - camX * 0.9f, 460.0f);
            float y = std::fmod((float)((h >> 11) % 1000) + timeMs * fall - camY * 0.9f, 280.0f);
            if (x < 0) x += 460.0f;
            if (y < 0) y += 280.0f;
            x -= 30;
            y -= 20;
            y += std::sin(timeMs * 0.003f + i) * 6.0f;                // flutter
            int frame = ((int)(timeMs / 90) + i) % 11;   // an 11-frame tumble (Cursors x 352-512)
            c.image("LooseSprites_Cursors", 352 + frame * 16, row, 16, 16, x + sh, y);
        }
    }
    if (g.lightning > 0) c.rect(0, 0, TOP_W, TOP_H, RGBA(0xFF, 0xFF, 0xFF, (uint8_t)(g.lightning * 200)));
    int t = g.timeOfDay;
    if (t > 18 * 60) {
        float k = std::min(1.0f, (t - 18 * 60) / 120.0f);
        c.rect(0, 0, TOP_W, TOP_H, RGBA(0x10, 0x10, 0x40, (uint8_t)(k * 120)));
    }
    // the map's lamps (its Light property) glow once it's dark
    if (g.isDark() && !g.location().lights.empty()) {
        float camX, camY;
        worldCamera(g, camX, camY);
        for (const auto& L : g.location().lights) {
            // 1 lantern, 2 window (others use the lantern glow: the big sheets cost too much GPU memory)
            const char* tex = L.type == 2 ? "LooseSprites_Lighting_windowLight" : "LooseSprites_Lighting_lantern";
            int tw = c.texWidth(tex), th = c.texHeight(tex);
            if (tw <= 0) continue;
            float scale = 72.0f / tw;   // about 4.5 tiles across
            float x = L.x * TILE + 8 - camX - tw * scale / 2, y = L.y * TILE + 8 - camY - th * scale / 2;
            if (x > TOP_W || y > TOP_H || x + tw * scale < 0 || y + th * scale < 0) continue;
            c.imageScaled(tex, 0, 0, tw, th, x, y, scale, false, RGBA(0xFF, 0xD8, 0x90, 0x90));
        }
    }
}

static void countText(Canvas& c, float rightX, float y, int count);

static void drawHud(Canvas& c, Game& g) {
    float hs = c.shift(3.0f);
    static const char* days[7] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
    char buf[48];
    if (!g.placing.empty()) {   // choosing where Robin builds: just the instructions
        box(c, 40 + hs, 6, TOP_W - 80, 34, COL_PANEL, COL_BORDER);
        c.text(TOP_W / 2.0f + hs, 9, 0.5f, COL_TEXT, "Where should the " + g.placing + " go?", ALIGN_CENTER);
        c.text(TOP_W / 2.0f + hs, 24, 0.4f, COL_TEXT_SOFT, "Move: Circle Pad / D-Pad   A: build   L: cancel", ALIGN_CENTER);
        return;
    }
    if (gameUi(c)) {
        // the game's own clock dial and money box from your Cursors sheet
        const float DW = 71, DH = 43;
        float x = TOP_W - DW - 4 + hs, y = 3;
        c.image(UI_TEX, 333, 431, (int)DW, (int)DH, x, y);
        snprintf(buf, sizeof buf, "%s. %d", days[g.weekday()], g.day);
        c.text(x + 47, y + 2, 0.4f, COL_TEXT, buf, ALIGN_CENTER);
        c.text(x + 48, y + 27, 0.34f, COL_TEXT, g.timeString(), ALIGN_CENTER);
        // the dial's hand (from your Cursors sheet) turns through the day:
        // on the sun (down) at 6am, round through the left, on the moon (up) at 2am
        {
            float k = std::max(0.0f, std::min(1.0f, (g.timeOfDay - DAY_START) / (float)(DAY_END - DAY_START)));
            float ang = 3.14159265f * (1.0f + k);   // clockwise from straight down
            c.imageRotated(UI_TEX, 324, 477, 7, 19, 3.5f, 17.0f, x + 21, y + 21, ang);
        }
        // weather and season icons in the dial's middle row
        int wx = 341;                                   // sunny
        if (g.raining) wx = g.stormy ? 377 : 365;       // storm / rain
        else if (g.snowing) wx = 401;                   // snow
        else if (g.windy) wx = g.season == FALL ? 389 : 353;   // falling leaves / spring breeze
        c.image(UI_TEX, wx, 421, 12, 8, x + 29, y + 17);
        c.image(UI_TEX, 406, 441 + 8 * (g.season & 3), 12, 8, x + 54, y + 17);
        c.image(UI_TEX, 340, 472, 65, 17, x + 4, y + DH + 1);
        snprintf(buf, sizeof buf, "%d", g.money);
        c.text(x + 62, y + DH + 3, 0.4f, COL_TEXT, buf, ALIGN_RIGHT);
    } else {
        float x = TOP_W - 104 + hs, y = 4;
        box(c, x, y, 100, 52, COL_PANEL, COL_BORDER);
        snprintf(buf, sizeof buf, "%s %d", days[g.weekday()], g.day);
        c.text(x + 50, y + 3, 0.5f, COL_TEXT, buf, ALIGN_CENTER);
        c.text(x + 50, y + 18, 0.5f, COL_TEXT, g.timeString(), ALIGN_CENTER);
        snprintf(buf, sizeof buf, "%dg", g.money);
        c.text(x + 94, y + 34, 0.45f, COL_TEXT, buf, ALIGN_RIGHT);
        c.text(x + 6, y + 34, 0.45f, COL_TEXT, seasonShort(g.season));
    }

    if (!g.mailbox.empty() && !g.letterOpen) {   // you've got mail
        static uint32_t t = 0;
        t++;
        float bob = (t / 8) % 2 ? 1.0f : 0.0f;
        float mx = TOP_W - 30 + hs, my = 70 + bob;
        c.rect(mx, my, 20, 14, RGBA(0x5A, 0x24, 0x0C));
        c.rect(mx + 1, my + 1, 18, 12, RGBA(0xFA, 0xEE, 0xD2));
        c.line(mx + 1, my + 1, mx + 10, my + 8, RGBA(0x8A, 0x4A, 0x20), 1);
        c.line(mx + 19, my + 1, mx + 10, my + 8, RGBA(0x8A, 0x4A, 0x20), 1);
        if (g.mailbox.size() > 1) countText(c, mx + 22, my + 8, (int)g.mailbox.size());
    }
    float ex = TOP_W - 16 + hs, ey = TOP_H - 74;
    box(c, ex - 2, ey - 2, 14, 72, COL_PANEL, COL_BORDER);
    c.rect(ex + 1, ey + 1, 8, 66, COL_ENERGY_BG);
    float k = (float)g.energy / MAX_ENERGY;
    uint32_t ec = k > 0.5f ? COL_ENERGY : (k > 0.2f ? RGBA(0xF0, 0xC0, 0x20) : RGBA(0xE0, 0x30, 0x20));
    c.rect(ex + 1, ey + 1 + 66 * (1 - k), 8, 66 * k, ec);
    c.text(ex + 5, ey + 66, 0.4f, COL_TEXT, "E", ALIGN_CENTER);
    // health: in the mines, or while hurt
    bool showHealth = g.location().mineLevel > 0 || g.health < g.maxHealth;
    if (showHealth) {
        float hx = TOP_W - 34 + hs, hy = TOP_H - 74;
        box(c, hx - 2, hy - 2, 14, 72, COL_PANEL, COL_BORDER);
        c.rect(hx + 1, hy + 1, 8, 66, COL_ENERGY_BG);
        float hk = std::max(0.0f, (float)g.health / g.maxHealth);
        bool flash = g.invuln > 0.9f && ((int)(g.invuln * 20) & 1);
        c.rect(hx + 1, hy + 1 + 66 * (1 - hk), 8, 66 * hk, flash ? RGBA(0xFF, 0xFF, 0xFF) : RGBA(0xE0, 0x30, 0x30));
        c.text(hx + 5, hy + 66, 0.4f, COL_TEXT, "H", ALIGN_CENTER);
    }

    const Slot& sel = g.inv[g.selected];
    if (sel.type == SLOT_TOOL) {
        const ToolDef* td = g.data.tool(sel.id);
        if (td && td->kind == TOOL_CAN) {
            float wx = TOP_W - (showHealth ? 52 : 34) + hs, wy = TOP_H - 74;
            box(c, wx - 2, wy - 2, 14, 72, COL_PANEL, COL_BORDER);
            float wk = (float)g.water / g.canCapacity();
            c.rect(wx + 1, wy + 1 + 66 * (1 - wk), 8, 66 * wk, COL_WATER);
        }
    }
    if (!sel.empty()) {
        std::string n = slotName(g, sel);
        if (sel.type == SLOT_OBJECT) n += " x" + std::to_string(sel.count);
        c.text(6 + hs, TOP_H - 18, 0.45f, COL_WHITE, n);
    }
    // pickup pop-ups, newest at the bottom: slide in, sit, slide out
    {
        float y = TOP_H - 44;
        for (int i = (int)g.notices.size() - 1; i >= 0; i--) {
            const Game::Notice& n = g.notices[i];
            const ObjectDef* od = g.data.object(n.id);
            std::string label = od ? od->name : std::string("Item");
            if (n.count > 1) label += " x" + std::to_string(n.count);
            float w = 26 + c.textWidth(label, 0.42f) + 6;
            float in = std::min(1.0f, (3.5f - n.t) / 0.15f), out = std::min(1.0f, n.t / 0.3f);
            float x = 4 - (1.0f - std::min(in, out)) * (w + 8) + hs;
            box(c, x, y, w, 22, COL_PANEL, COL_BORDER);
            if (od) itemIcon(c, od, x + 4, y + 3);
            else objectIcon(c, n.id, x + 4, y + 3);
            c.text(x + 24, y + 4, 0.42f, COL_TEXT, label);
            y -= 25;
        }
    }
    if (g.toastTime > 0 && !g.toast.empty()) {
        // centred in the space left of the clock
        float cx = (TOP_W - 108) / 2.0f + hs;
        box(c, cx - 140, 6, 280, 22, COL_PANEL, COL_BORDER);
        c.text(cx, 9, 0.5f, COL_TEXT, g.toast, ALIGN_CENTER);
    }
}

void drawTop(Canvas& c, Game& g, Ui& ui, uint32_t timeMs) {
    g_useMapCache = ui.mapCache != 0;
    c.cacheFlipY = ui.mapCache == 2;
    c.rect(0, 0, TOP_W, TOP_H, RGBA(0, 0, 0));
    drawWorld(c, g, timeMs);
    drawWeatherAndLight(c, g, timeMs);
    profEnd(PROF_WEATHER);
    if (g.cutscene.active) {
        if (g.cutscene.fade > 0)
            c.rect(0, 0, TOP_W, TOP_H, RGBA(0, 0, 0, (uint8_t)(std::min(1.0f, g.cutscene.fade) * 255)));
        // letterbox bars, so it reads as a cutscene
        c.rect(0, 0, TOP_W, 12, RGBA(0, 0, 0));
        c.rect(0, TOP_H - 12, TOP_W, 12, RGBA(0, 0, 0));
        return;
    }
    drawHud(c, g);
    profEnd(PROF_HUD);
    if (g.fade > 0) c.rect(0, 0, TOP_W, TOP_H, RGBA(0, 0, 0, (uint8_t)(g.fade * 255)));
}

// ============================================================ bottom screen
static const int SLOT = 24, HOT_X = 10, HOT_Y = 3;
static const int TAB_Y = 31, TAB_H = 20, PANEL_Y = 51;

static bool hit(int tx, int ty, float x, float y, float w, float h) {
    return tx >= x && ty >= y && tx < x + w && ty < y + h;
}

// Stack counts: light digits with a dark outline so they read over any icon.
static void countText(Canvas& c, float rightX, float y, int count) {
    std::string n = std::to_string(count);
    const uint32_t edge = RGBA(0x2A, 0x14, 0x08);
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++)
            if (dx || dy) c.text(rightX + dx, y + dy, 0.35f, edge, n, ALIGN_RIGHT);
    c.text(rightX, y, 0.35f, RGBA(0xFF, 0xF4, 0xD8), n, ALIGN_RIGHT);
}

static void slotBox(Canvas& c, const Game& g, int i, float x, float y, bool picked) {
    bool sel = i == g.selected;
    uint32_t edge = picked ? COL_PICK : (sel ? COL_SEL : COL_SLOT_EDGE);
    box(c, x, y, SLOT, SLOT, sel ? COL_SLOT_ON : COL_SLOT, edge, (sel || picked) ? 2 : 1);
    const Slot& s = g.inv[i];
    if (s.empty()) return;
    slotIcon(c, g, s, x + 4, y + 4);
    if (s.type != SLOT_TOOL && s.count > 1)
        countText(c, x + SLOT - 2, y + SLOT - 10, s.count);
}

static void slotPos(int i, float& x, float& y) {
    if (i < HOTBAR) {
        x = HOT_X + i * (SLOT + 1);
        y = HOT_Y;
    } else {
        int j = i - HOTBAR;
        x = HOT_X + (j % HOTBAR) * (SLOT + 1);
        y = PANEL_Y + 18 + (j / HOTBAR) * (SLOT + 1);
    }
}

static int slotAt(int tx, int ty, bool backpackVisible) {
    for (int i = 0; i < INV_SIZE; i++) {
        if (i >= HOTBAR && !backpackVisible) break;
        float x, y;
        slotPos(i, x, y);
        if (hit(tx, ty, x, y, SLOT, SLOT)) return i;
    }
    return -1;
}

static void drawHotbarAndTabs(Canvas& c, Game& g, Ui& ui) {
    box(c, HOT_X - 3, HOT_Y - 2, 12 * (SLOT + 1) + 5, SLOT + 4, COL_BORDER, COL_BORDER, 0);
    for (int i = 0; i < HOTBAR; i++) {
        float x, y;
        slotPos(i, x, y);
        slotBox(c, g, i, x, y, ui.picked == i);
    }
    static const char* names[TAB_COUNT] = {"Items", "Craft", "Quests", "Social", "Map", "Options"};
    float tw = (BOT_W - 8 - 2 * (TAB_COUNT - 1)) / (float)TAB_COUNT;
    for (int i = 0; i < TAB_COUNT; i++) {
        float x = 4 + i * (tw + 2);
        c.rect(x, TAB_Y, tw, TAB_H, COL_BORDER);
        c.rect(x + 2, TAB_Y + 2, tw - 4, TAB_H - 2, i == ui.tab ? COL_PANEL : COL_TAB_OFF);
        c.text(x + tw / 2, TAB_Y + 3, 0.48f, COL_TEXT, names[i], ALIGN_CENTER);
    }
    box(c, 4, PANEL_Y, BOT_W - 8, BOT_H - PANEL_Y - 4, COL_PANEL, COL_BORDER);
    c.rect(4 + 2 + ui.tab * (tw + 2), PANEL_Y, tw - 4, 2, COL_PANEL);   // open tab joins the panel
}

static void drawItemsTab(Canvas& c, Game& g, Ui& ui) {
    c.text(HOT_X, PANEL_Y + 3, 0.45f, COL_TEXT, "Backpack");
    c.text(BOT_W - HOT_X, PANEL_Y + 4, 0.36f, COL_TEXT_SOFT, "tap an item, then a slot to move it", ALIGN_RIGHT);
    for (int i = HOTBAR; i < INV_SIZE; i++) {
        float x, y;
        slotPos(i, x, y);
        slotBox(c, g, i, x, y, ui.picked == i);
    }
    const Slot& sel = g.inv[ui.picked >= 0 ? ui.picked : g.selected];
    float iy = PANEL_Y + 72;
    box(c, 10, iy, 300, 36, COL_SLOT, COL_SLOT_EDGE, 1);
    std::string name = slotName(g, sel);
    if (sel.type != SLOT_TOOL && sel.count > 1) name += "  x" + std::to_string(sel.count);
    c.text(16, iy + 3, 0.5f, COL_TEXT, name.empty() ? "Empty slot" : name);
    std::string info;
    if (sel.type == SLOT_OBJECT) {
        const CropDef* cd = g.data.cropForSeed(sel.id);
        if (cd) {
            static const char* sn[4] = {"spring", "summer", "fall", "winter"};
            std::string seasons;
            for (int s = 0; s < 4; s++)
                if (cd->seasonMask & (1 << s)) seasons += (seasons.empty() ? "" : "/") + std::string(sn[s]);
            info = "Plant in " + seasons + ". " + std::to_string(cd->totalDays()) + " days to grow.";
        }
        int p = g.sellPrice(sel);
        if (p > 0) info += (info.empty() ? "" : "  ") + std::string("Sells ") + std::to_string(p) + "g";
    } else if (sel.type == SLOT_TOOL) {
        const ToolDef* td = g.data.tool(sel.id);
        info = td && td->kind == TOOL_ROD ? "Hold A to cast, A when it bites, hold A to reel."
                                          : "Press A to use. Hold A to keep going.";
    } else if (sel.type == SLOT_BIG) {
        info = "Press A to put it down. Pickaxe picks it back up.";
    }
    c.text(16, iy + 19, 0.38f, COL_TEXT_SOFT, info);

    static const char* labels[3] = {"Trash", "Eat", "Sleep"};
    for (int i = 0; i < 3; i++) {
        float bx = 10 + i * 102;
        box(c, bx, iy + 42, 96, 26, COL_PANEL_DARK, COL_BORDER);
        c.text(bx + 48, iy + 47, 0.5f, COL_TEXT, labels[i], ALIGN_CENTER);
    }
    char buf[80];
    snprintf(buf, sizeof buf, "%s %d, Year %d   %dg   Energy %d/%d", seasonShort(g.season), g.day,
             g.year, g.money, g.energy, MAX_ENERGY);
    c.text(BOT_W / 2, iy + 76, 0.4f, COL_TEXT_SOFT, buf, ALIGN_CENTER);
    static const char* sk[SK_COUNT] = {"Farm", "Fish", "Forage", "Mine", "Combat"};
    std::string skills;
    for (int i = 0; i < SK_COUNT; i++)
        skills += std::string(i ? "   " : "") + sk[i] + " " + std::to_string(g.skillLevel(i));
    c.text(BOT_W / 2, iy + 91, 0.38f, COL_TEXT_SOFT, skills, ALIGN_CENTER);
}

static void touchItemsTab(Game& g, Ui& ui, int tx, int ty) {
    float iy = PANEL_Y + 72;
    int target = ui.picked >= 0 ? ui.picked : g.selected;
    if (hit(tx, ty, 10, iy + 42, 96, 26)) {
        if (g.trashSlot(target)) ui.picked = -1;
    } else if (hit(tx, ty, 112, iy + 42, 96, 26)) {
        if (g.eatSlot(target) && g.inv[target].empty()) ui.picked = -1;
    } else if (hit(tx, ty, 214, iy + 42, 96, 26)) {
        g.goToBed();
    }
}

// ---------------------------------------------------------------- crafting
static const int CR_COLS = 11, CR_ROWS = 3, CR_CELL = 26, CR_X = 17, CR_Y = PANEL_Y + 18;

static void recipeIcon(Canvas& c, const Game& g, const Recipe& r, float x, float y) {
    Slot s;
    s.type = r.outBig ? SLOT_BIG : SLOT_OBJECT;
    s.id = r.outId;
    s.count = 1;
    slotIcon(c, g, s, x, y);
}

static std::string categoryName(int cat) {
    switch (cat) {
        case -4: return "Any fish";
        case -5: return "Any egg";
        case -6: return "Any milk";
        case -75: return "Any vegetable";
        case -79: return "Any fruit";
        case -80: return "Any flower";
        case -81: return "Any forage";
        default: return "Any item";
    }
}

static void drawCraftTab(Canvas& c, Game& g, Ui& ui) {
    if (!g.canCookHere()) ui.cooking = false;
    const auto& rs = ui.cooking ? g.data.cookRecipes : g.data.recipes;
    int known = 0;
    for (auto& r : rs) known += g.recipeKnown(r);
    char buf[64];
    snprintf(buf, sizeof buf, "%d of %d recipes", known, (int)rs.size());
    c.text(HOT_X, PANEL_Y + 3, 0.45f, COL_TEXT, ui.cooking ? "Cooking" : "Crafting");
    if (g.canCookHere()) {   // the kitchen: switch between crafting and cooking
        box(c, 112, PANEL_Y + 2, 76, 15, COL_PANEL_DARK, COL_BORDER, 1);
        c.text(150, PANEL_Y + 2, 0.36f, COL_TEXT, ui.cooking ? "Crafting" : "Cook", ALIGN_CENTER);
    }
    c.text(BOT_W - HOT_X, PANEL_Y + 4, 0.38f, COL_TEXT_SOFT, buf, ALIGN_RIGHT);
    if (rs.empty()) {
        c.text(BOT_W / 2, PANEL_Y + 60, 0.42f, COL_TEXT_SOFT, "No recipes - re-run the converter", ALIGN_CENTER);
        return;
    }
    int rows = ((int)rs.size() + CR_COLS - 1) / CR_COLS;
    ui.craftScroll = std::max(0, std::min(ui.craftScroll, rows - CR_ROWS));
    ui.craftSel = std::max(0, std::min(ui.craftSel, (int)rs.size() - 1));
    for (int r = 0; r < CR_ROWS; r++)
        for (int col = 0; col < CR_COLS; col++) {
            int i = (ui.craftScroll + r) * CR_COLS + col;
            if (i >= (int)rs.size()) break;
            float x = CR_X + col * CR_CELL, y = CR_Y + r * CR_CELL;
            bool k = g.recipeKnown(rs[i]);
            bool can = k && g.canCraft(rs[i]);
            box(c, x, y, CR_CELL - 2, CR_CELL - 2, i == ui.craftSel ? COL_SLOT_ON : COL_SLOT,
                i == ui.craftSel ? COL_SEL : (can ? COL_GREEN : COL_SLOT_EDGE), i == ui.craftSel || can ? 2 : 1);
            recipeIcon(c, g, rs[i], x + 4, y + 4);
            if (!k) c.rect(x + 1, y + 1, CR_CELL - 4, CR_CELL - 4, RGBA(0x3B, 0x1F, 0x0E, 0xB0));
        }
    // scroll arrows
    box(c, BOT_W - 15, CR_Y, 10, 36, COL_PANEL_DARK, COL_BORDER, 1);
    box(c, BOT_W - 15, CR_Y + 40, 10, 36, COL_PANEL_DARK, COL_BORDER, 1);
    c.text(BOT_W - 10, CR_Y + 10, 0.4f, COL_TEXT, "^", ALIGN_CENTER);
    c.text(BOT_W - 10, CR_Y + 50, 0.4f, COL_TEXT, "v", ALIGN_CENTER);

    const Recipe& r = rs[ui.craftSel];
    bool k = g.recipeKnown(r);
    float iy = CR_Y + CR_ROWS * CR_CELL + 2;
    box(c, 10, iy, 300, 236 - iy - 4, COL_SLOT, COL_SLOT_EDGE, 1);
    std::string title = k ? r.name : "???";
    if (k && r.outCount > 1) title += "  x" + std::to_string(r.outCount);
    c.text(16, iy + 3, 0.5f, COL_TEXT, title);
    if (!k) {
        std::string how = "Learn it later";
        if (r.unlock.size() > 2 && r.unlock[0] == 's') how = "Reach " + r.unlock.substr(2) ;
        else if (r.unlock.size() > 2 && r.unlock[0] == 'f') how = "Befriend " + r.unlock.substr(2) + " hearts";
        else if (r.unlock.size() > 2 && r.unlock[0] == 'l') how = "Total skill level " + r.unlock.substr(2);
        c.text(16, iy + 20, 0.4f, COL_TEXT_SOFT, how);
        return;
    }
    float ix = 16, iy2 = iy + 20;
    for (auto& in : r.ingredients) {
        int have = g.countOf(in);
        Slot s;
        s.type = in.big ? SLOT_BIG : SLOT_OBJECT;
        s.id = in.id;
        s.count = 1;
        std::string nm;
        if (in.id >= 0 || in.big) slotIcon(c, g, s, ix, iy2);
        nm = (in.id >= 0 || in.big) ? g.itemName(s) : categoryName(in.id);
        snprintf(buf, sizeof buf, "%d/%d", std::min(have, 999), in.count);
        c.text(ix + 18, iy2 + 2, 0.38f, have >= in.count ? COL_GREEN : COL_SEL, buf);
        c.text(ix + 62, iy2 + 2, 0.36f, COL_TEXT_SOFT, nm.size() > 12 ? nm.substr(0, 11) + "." : nm);
        ix += 120;
        if (ix > 200) { ix = 16; iy2 += 18; }
    }
    bool can = g.canCraft(r);
    box(c, 226, iy + 2, 80, 20, can ? RGBA(0x7C, 0xB3, 0x42) : COL_TAB_OFF, COL_BORDER, 2);
    c.text(266, iy + 5, 0.48f, COL_TEXT, "Craft", ALIGN_CENTER);
}

static void touchCraftTab(Game& g, Ui& ui, int tx, int ty) {
    if (g.canCookHere() && hit(tx, ty, 112, PANEL_Y + 2, 76, 15)) {
        ui.cooking = !ui.cooking;
        ui.craftSel = 0;
        ui.craftScroll = 0;
        return;
    }
    const auto& rs = ui.cooking && g.canCookHere() ? g.data.cookRecipes : g.data.recipes;
    if (rs.empty()) return;
    int rows = ((int)rs.size() + CR_COLS - 1) / CR_COLS;
    if (hit(tx, ty, BOT_W - 17, CR_Y, 14, 38)) { ui.craftScroll = std::max(0, ui.craftScroll - 1); return; }
    if (hit(tx, ty, BOT_W - 17, CR_Y + 38, 14, 40)) {
        ui.craftScroll = std::min(std::max(0, rows - CR_ROWS), ui.craftScroll + 1);
        return;
    }
    for (int r = 0; r < CR_ROWS; r++)
        for (int col = 0; col < CR_COLS; col++) {
            int i = (ui.craftScroll + r) * CR_COLS + col;
            if (i < (int)rs.size() && hit(tx, ty, CR_X + col * CR_CELL, CR_Y + r * CR_CELL, CR_CELL, CR_CELL)) {
                if (i == ui.craftSel && g.recipeKnown(rs[i])) g.craft(rs[i]);   // tap again to craft
                ui.craftSel = i;
                return;
            }
        }
    float iy = CR_Y + CR_ROWS * CR_CELL + 2;
    if (hit(tx, ty, 226, iy + 2, 80, 20) && ui.craftSel < (int)rs.size()) g.craft(rs[ui.craftSel]);
}

// ---------------------------------------------------------------- chest
static const float CH_Y = 30, INV_Y2 = 136;

static void chestSlotPos(bool chest, int i, float& x, float& y) {
    x = HOT_X + (i % 12) * (SLOT + 1);
    y = (chest ? CH_Y : INV_Y2) + 14 + (i / 12) * (SLOT + 1);
}

static void drawChest(Canvas& c, Game& g) {
    Placed* p = g.openChest();
    c.rect(0, 0, BOT_W, BOT_H, COL_BG);
    if (!p) return;
    box(c, 4, 4, BOT_W - 8, 22, COL_PANEL_DARK, COL_BORDER);
    const BigDef* b = g.data.big(p->id);
    c.text(12, 7, 0.5f, COL_TEXT, b ? b->name : "Chest");
    box(c, BOT_W - 64, 6, 56, 18, COL_PANEL, COL_BORDER, 1);
    c.text(BOT_W - 36, 8, 0.42f, COL_TEXT, "Close", ALIGN_CENTER);
    box(c, 4, CH_Y, BOT_W - 8, 102, COL_PANEL, COL_BORDER);
    box(c, 4, INV_Y2, BOT_W - 8, 100, COL_PANEL, COL_BORDER);
    c.text(HOT_X, CH_Y + 3, 0.36f, COL_TEXT_SOFT, "In the chest - tap to take");
    c.text(HOT_X, INV_Y2 + 3, 0.36f, COL_TEXT_SOFT, "Backpack - tap to store");
    for (int i = 0; i < (int)p->items.size() && i < 36; i++) {
        float x, y;
        chestSlotPos(true, i, x, y);
        box(c, x, y, SLOT, SLOT, COL_SLOT, COL_SLOT_EDGE, 1);
        const Slot& s = p->items[i];
        if (s.empty()) continue;
        slotIcon(c, g, s, x + 4, y + 4);
        if (s.count > 1) countText(c, x + SLOT - 2, y + SLOT - 10, s.count);
    }
    for (int i = 0; i < INV_SIZE; i++) {
        float x, y;
        chestSlotPos(false, i, x, y);
        slotBox(c, g, i, x, y, false);
    }
}

static void touchChest(Game& g, int tx, int ty) {
    if (hit(tx, ty, BOT_W - 64, 6, 56, 18)) { g.closeChest(); return; }
    for (int i = 0; i < 36; i++) {
        float x, y;
        chestSlotPos(true, i, x, y);
        if (hit(tx, ty, x, y, SLOT, SLOT)) { g.chestTransfer(false, i); return; }
        chestSlotPos(false, i, x, y);
        if (hit(tx, ty, x, y, SLOT, SLOT)) { g.chestTransfer(true, i); return; }
    }
}

// Mini map: each block of cells becomes one coloured square.
static void drawMapTab(Canvas& c, Game& g) {
    const TileMap& m = g.map();
    float ax = 10, ay = PANEL_Y + 22, aw = 300, ah = 128;
    box(c, ax - 2, ay - 2, aw + 4, ah + 4, RGBA(0x50, 0x40, 0x30), COL_BORDER, 2);
    // biggest whole-pixel scale that fits; merge cells only for huge maps
    int bs = 1;
    float px = std::floor(std::min(aw / m.width(), ah / m.height()));
    while (px < 2) {
        bs++;
        px = std::floor(std::min(aw / (m.width() / bs), ah / (m.height() / bs)));
    }
    px = std::min(px, 8.0f);
    float ox = ax + (aw - (m.width() / bs) * px) / 2, oy = ay + (ah - (m.height() / bs) * px) / 2;
    for (int by = 0; by < m.height() / bs; by++)
        for (int bx = 0; bx < m.width() / bs; bx++) {
            int cx = bx * bs + bs / 2, cy = by * bs + bs / 2;
            uint8_t f = m.flags(cx, cy);
            uint32_t col;
            if (f & CF_WATER) col = RGBA(0x4F, 0x9B, 0xD1);
            else if (m.tile(L_BUILDINGS, cx, cy) != NO_TILE && !(f & CF_PASSABLE)) col = RGBA(0x8A, 0x5A, 0x3A);
            else if ((f & CF_BLOCKED) && !(f & CF_PASSABLE)) col = RGBA(0x5A, 0x48, 0x38);
            else if (f & CF_DIGGABLE) col = RGBA(0x7F, 0xB4, 0x5C);
            else if (m.tile(L_BACK, cx, cy) == NO_TILE) col = RGBA(0x20, 0x18, 0x10);
            else col = RGBA(0xD9, 0xC0, 0x8A);
            c.rect(ox + bx * px, oy + by * px, px, px, col);
        }
    std::string here;
    for (auto& n : g.npcs) {
        if (n.loc != g.location().name) continue;
        float nx = ox + (n.x / TILE / bs) * px, ny = oy + ((n.y - 4) / TILE / bs) * px;
        c.rect(nx - 2, ny - 2, 5, 5, RGBA(0xFF, 0xFF, 0xFF));
        c.rect(nx - 1, ny - 1, 3, 3, RGBA(0x2F, 0x6F, 0xC0));
        here += (here.empty() ? "" : ", ") + n.def->display;
    }
    float dx = ox + (g.px / TILE / bs) * px, dy = oy + ((g.py - 4) / TILE / bs) * px;
    c.rect(dx - 3, dy - 3, 6, 6, COL_WHITE);
    c.rect(dx - 2, dy - 2, 4, 4, COL_SEL);

    c.text(10, PANEL_Y + 4, 0.5f, COL_TEXT, placeName(g, g.location().name));
    c.text(BOT_W - 10, PANEL_Y + 5, 0.42f, COL_TEXT_SOFT, g.timeString(), ALIGN_RIGHT);
    std::string exits;
    for (auto& w : m.warps)
        if (exits.find(placeName(g, w.target)) == std::string::npos)
            exits += (exits.empty() ? "" : ", ") + placeName(g, w.target);
    c.text(10, ay + ah + 6, 0.38f, COL_TEXT_SOFT, exits.empty() ? "No exits found" : "Exits: " + exits);
    if (!here.empty()) c.text(10, ay + ah + 19, 0.38f, COL_PICK, "Here: " + here);
}

static void drawOptionsTab(Canvas& c, Game& g, Ui& ui) {
    const char* labels[3] = {"Save", "Sleep (ends the day)", "Save & quit"};
    for (int i = 0; i < 3; i++) {
        box(c, 40, PANEL_Y + 10 + i * 40, 240, 32, COL_PANEL_DARK, COL_BORDER);
        c.text(160, PANEL_Y + 17 + i * 40, 0.55f, COL_TEXT, labels[i], ALIGN_CENTER);
    }
    box(c, 14, PANEL_Y + 130, 142, 22, COL_PANEL_DARK, COL_BORDER);
    c.text(85, PANEL_Y + 134, 0.42f, COL_TEXT, ui.showPerf ? "Perf info: on" : "Perf info: off", ALIGN_CENTER);
    static const char* mcText[3] = {"Fast map: off", "Fast map: on", "Fast map: flip"};
    box(c, 164, PANEL_Y + 130, 142, 22, COL_PANEL_DARK, COL_BORDER);
    c.text(235, PANEL_Y + 134, 0.42f, COL_TEXT, mcText[ui.mapCache % 3], ALIGN_CENTER);
    if (ui.statusTime > 0) c.text(160, PANEL_Y + 156, 0.5f, COL_TEXT, ui.status, ALIGN_CENTER);
    c.text(160, PANEL_Y + 172, 0.38f, COL_TEXT_SOFT, "The game also saves every time you sleep.", ALIGN_CENTER);
    (void)g;
}

void loadUiSettings(Ui& ui, const std::string& path) {
    FILE* f = fopen(path.c_str(), "r");
    if (!f) return;
    char line[128];
    while (fgets(line, sizeof line, f)) {
        int v;
        if (sscanf(line, "fastMap=%d", &v) == 1) ui.mapCache = std::max(0, std::min(2, v));
        if (sscanf(line, "perfInfo=%d", &v) == 1) ui.showPerf = v != 0;
    }
    fclose(f);
}

void saveUiSettings(const Ui& ui, const std::string& path) {
    FILE* f = fopen(path.c_str(), "w");
    if (!f) return;
    fprintf(f, "fastMap=%d\nperfInfo=%d\n", ui.mapCache, ui.showPerf ? 1 : 0);
    fclose(f);
}

static std::string settingsPath(const std::string& savePath) {
    size_t s = savePath.find_last_of('/');
    return (s == std::string::npos ? std::string() : savePath.substr(0, s + 1)) + "settings.txt";
}

static void touchOptionsTab(Game& g, Ui& ui, int tx, int ty, const std::string& savePath) {
    if (hit(tx, ty, 14, PANEL_Y + 130, 142, 22)) { ui.showPerf = !ui.showPerf; saveUiSettings(ui, settingsPath(savePath)); return; }
    if (hit(tx, ty, 164, PANEL_Y + 130, 142, 22)) {
        ui.mapCache = (ui.mapCache + 1) % 3;
        saveUiSettings(ui, settingsPath(savePath));
        return;
    }
    for (int i = 0; i < 3; i++)
        if (hit(tx, ty, 40, PANEL_Y + 10 + i * 40, 240, 32)) {
            if (i == 1) {
                g.goToBed();
            } else {
                ui.status = g.save(savePath) ? "Saved." : "Save FAILED (SD card?)";
                ui.statusTime = 3;
                if (i == 2) ui.quit = true;
            }
        }
}


// --------------------------------------------------------------- text helpers
static std::vector<std::string> wrapText(Canvas& c, const std::string& s, float scale, float maxW) {
    std::vector<std::string> lines;
    std::string line, word;
    auto flush = [&]() {
        if (word.empty()) return;
        std::string test = line.empty() ? word : line + " " + word;
        if (!line.empty() && c.textWidth(test, scale) > maxW) {
            lines.push_back(line);
            line = word;
        } else {
            line = test;
        }
        word.clear();
    };
    for (char ch : s) {
        if (ch == ' ') flush();
        else word += ch;
    }
    flush();
    if (!line.empty()) lines.push_back(line);
    return lines;
}

static void heart(Canvas& c, float x, float y, uint32_t col) {
    c.rect(x + 1, y, 2, 1, col);
    c.rect(x + 4, y, 2, 1, col);
    c.rect(x, y + 1, 7, 2, col);
    c.rect(x + 1, y + 3, 5, 1, col);
    c.rect(x + 2, y + 4, 3, 1, col);
    c.rect(x + 3, y + 5, 1, 1, col);
}

// --------------------------------------------------------------- social tab
static const int SOC_ROWS = 5, SOC_H = 36, SOC_Y = PANEL_Y + 4;

static std::vector<int> socialOrder(const Game& g) {
    std::vector<int> idx;
    for (size_t i = 0; i < g.npcs.size(); i++)
        if (g.npcs[i].def->social) idx.push_back((int)i);
    std::stable_sort(idx.begin(), idx.end(), [&](int a, int b) {
        if (g.npcs[a].friendship != g.npcs[b].friendship) return g.npcs[a].friendship > g.npcs[b].friendship;
        return g.npcs[a].def->display < g.npcs[b].def->display;
    });
    return idx;
}

static void drawSocialTab(Canvas& c, Game& g, Ui& ui) {
    auto order = socialOrder(g);
    ui.socialScroll = std::max(0, std::min(ui.socialScroll, (int)order.size() - SOC_ROWS));
    for (int r = 0; r < SOC_ROWS; r++) {
        int k = ui.socialScroll + r;
        if (k >= (int)order.size()) break;
        const NpcState& n = g.npcs[order[k]];
        float y = SOC_Y + r * SOC_H;
        box(c, 8, y, 278, SOC_H - 3, COL_SLOT, COL_SLOT_EDGE, 1);
        box(c, 11, y + 4, 24, 24, RGBA(0xE7, 0xC3, 0x83), COL_SLOT_EDGE, 1);
        c.image("Characters_" + n.def->name, 0, 2, 16, 16, 15, y + 8);
        c.text(40, y + 2, 0.46f, COL_TEXT, n.def->display);
        for (int h = 0; h < 10; h++)
            heart(c, 40 + h * 10, y + 20, h < n.hearts() ? RGBA(0xE0, 0x43, 0x2B) : RGBA(0xE3, 0xC6, 0x96));
        char buf[40];
        snprintf(buf, sizeof buf, "Gifts %d/2", std::min(n.giftsWeek, 2));
        c.text(280, y + 3, 0.38f, COL_TEXT, buf, ALIGN_RIGHT);
        if (g.isBirthday(*n.def)) c.text(280, y + 17, 0.38f, COL_SEL, "Birthday!", ALIGN_RIGHT);
        else c.text(280, y + 17, 0.38f, n.talkedToday ? COL_GREEN : COL_TEXT_SOFT,
                    n.talkedToday ? "Talked today" : (n.introduced ? "" : "Not met"), ALIGN_RIGHT);
    }
    if (order.empty()) c.text(160, 120, 0.45f, COL_TEXT_SOFT, "No villagers in your files yet.", ALIGN_CENTER);
    box(c, 290, SOC_Y, 24, 80, COL_PANEL_DARK, COL_BORDER, 1);
    c.text(302, SOC_Y + 30, 0.5f, COL_TEXT, "^", ALIGN_CENTER);
    box(c, 290, SOC_Y + 96, 24, 80, COL_PANEL_DARK, COL_BORDER, 1);
    c.text(302, SOC_Y + 126, 0.5f, COL_TEXT, "v", ALIGN_CENTER);
}

static void touchSocialTab(Ui& ui, int tx, int ty) {
    if (hit(tx, ty, 290, SOC_Y, 24, 80)) ui.socialScroll -= SOC_ROWS - 1;
    if (hit(tx, ty, 290, SOC_Y + 96, 24, 80)) ui.socialScroll += SOC_ROWS - 1;
}

// --------------------------------------------------------------- dialogue takeover
static const int DLG_LINES = 7;

static void drawDialogue(Canvas& c, Game& g, Ui& ui) {
    c.rect(0, 0, BOT_W, BOT_H, COL_BG);
    const DialogueBox& d = g.dialogue;
    if (d.page >= d.pages.size()) return;
    const DialoguePage& p = d.pages[d.page];
    const NpcDef* def = g.data.npc(d.npc);

    float boxW = def ? 210 : 312;
    box(c, 4, 4, boxW, 132, COL_PANEL, COL_BORDER, 3);
    auto lines = wrapText(c, p.text, 0.5f, boxW - 18);
    ui.dlgLines = (int)lines.size();
    for (int i = 0; i < DLG_LINES && ui.dlgLine + i < (int)lines.size(); i++)
        c.text(12, 10 + i * 16, 0.5f, COL_TEXT, lines[ui.dlgLine + i]);
    bool lastPart = ui.dlgLine + DLG_LINES >= (int)lines.size();

    if (def) {
        box(c, 218, 4, 98, 98, RGBA(0xE7, 0xC3, 0x83), COL_BORDER, 3);
        int idx = std::max(0, p.portrait);
        c.image("Portraits_" + def->name, (idx % 2) * 64, (idx / 2) * 64, 64, 64, 235, 21);
        box(c, 218, 106, 98, 30, COL_PANEL_DARK, COL_BORDER, 3);
        c.text(267, 112, 0.5f, COL_TEXT, def->display, ALIGN_CENTER);
    }

    if (lastPart && !p.choices.empty()) {
        for (size_t i = 0; i < p.choices.size() && i < 3; i++) {
            float y = 142 + i * 31;
            box(c, 4, y, 312, 28, i == 0 ? COL_SLOT_ON : COL_SLOT, i == 0 ? COL_SEL : COL_SLOT_EDGE, i == 0 ? 2 : 1);
            c.text(14, y + 6, 0.48f, COL_TEXT, p.choices[i].label);
        }
    } else {
        c.text(boxW - 8, 118, 0.42f, COL_TEXT_SOFT, lastPart ? "A  next" : "A  more", ALIGN_RIGHT);
        c.text(BOT_W / 2, 200, 0.42f, COL_CREAM, "Tap anywhere or press A", ALIGN_CENTER);
    }
}

void dialogueAdvance(Game& g, Ui& ui) {
    if (!g.dialogue.active) return;
    if (ui.dlgLine + DLG_LINES < ui.dlgLines) {
        ui.dlgLine += DLG_LINES;
        return;
    }
    const DialogueBox& d = g.dialogue;
    if (d.page < d.pages.size() && !d.pages[d.page].choices.empty()) return;   // must pick
    ui.dlgLine = 0;
    ui.dlgLines = 0;
    g.dialogueNext();
}

static void touchDialogue(Game& g, Ui& ui, int tx, int ty) {
    const DialogueBox& d = g.dialogue;
    if (d.page < d.pages.size() && !d.pages[d.page].choices.empty() && ui.dlgLine + DLG_LINES >= ui.dlgLines) {
        for (int i = 0; i < 3; i++)
            if (hit(tx, ty, 4, 142 + i * 31, 312, 28)) {
                ui.dlgLine = 0;
                g.dialogueChoose(i);
                return;
            }
        return;
    }
    dialogueAdvance(g, ui);
}

// -------------------------------------------------------------- shop takeover
static const int SHOP_ROWS = 6, ROW_H = 24, LIST_Y = 58;

static int cartTotal(Game& g, Ui& ui) {
    int t = 0;
    for (auto& e : g.shopStock(ui.shop)) {
        auto it = ui.cart.find(e.itemId);
        if (it != ui.cart.end()) t += it->second * e.price;
    }
    return t;
}

static std::vector<int> sellableSlots(Game& g) {
    std::vector<int> out;
    for (int i = 0; i < INV_SIZE; i++)
        if (g.sellPrice(g.inv[i]) > 0) out.push_back(i);
    return out;
}

// Robin: buildings you can order now (new ones, and upgrades of ones you have).
static std::vector<const BuildingType*> buildOffers(const Game& g) {
    std::vector<const BuildingType*> out;
    for (auto& t : g.data.buildingTypes) {
        if (t.cost <= 0) continue;   // the greenhouse isn't for sale
        if (t.upgradeFrom.empty()) { out.push_back(&t); continue; }
        for (auto& b : g.buildings)
            if (b.type == t.upgradeFrom && b.daysLeft <= 0) { out.push_back(&t); break; }
    }
    return out;
}

// Marnie: animals for sale, then the milk pail and shears.
struct AnimalOffer { const AnimalType* a; std::string tool; };
static std::vector<AnimalOffer> animalOffers(const Game& g) {
    std::vector<AnimalOffer> out;
    for (auto& a : g.data.animalTypes)
        if (a.price > 0 && !a.house.empty()) out.push_back({&a, ""});
    for (const char* t : {"MilkPail", "Shears"})
        if (g.data.toolByKey(t) >= 0) out.push_back({nullptr, t});
    return out;
}

static const int OFFER_ROWS = 5, OFFER_H = 30;

static void drawShopExtra(Canvas& c, Game& g, Ui& ui) {
    char buf[96];
    if (ui.shop == "Carpenter") {
        auto offers = buildOffers(g);
        bool house = g.houseLevel < 2;   // the house upgrade sits on top of the list
        int total = (int)offers.size() + (house ? 1 : 0);
        ui.shopScroll = std::max(0, std::min(ui.shopScroll, total - OFFER_ROWS));
        for (int r = 0; r < OFFER_ROWS; r++) {
            int i = ui.shopScroll + r - (house ? 1 : 0);
            float y = LIST_Y + r * OFFER_H;
            if (i == -1) {
                box(c, 4, y, BOT_W - 8, OFFER_H - 2, COL_SLOT, COL_SLOT_EDGE, 1);
                c.text(10, y + 1, 0.45f, COL_TEXT, g.houseLevel == 0 ? "House upgrade: kitchen" : "House upgrade: nursery");
                bool ok = g.houseUpgradeDays == 0 && (g.houseLevel == 0 ? g.money >= 10000 && g.countItem(388) >= 450
                                                                         : g.money >= 65000 && g.countItem(709) >= 100);
                c.text(10, y + 15, 0.34f, ok ? COL_GREEN : COL_TEXT_SOFT,
                       g.houseUpgradeDays > 0 ? "Robin is working on it" : g.houseLevel == 0 ? "10000g  450 Wood" : "65000g  100 Hardwood");
                box(c, 252, y + 4, 58, 20, ok ? COL_GREEN : RGBA(0x80, 0x70, 0x60), COL_GREEN_DARK, 1);
                c.text(281, y + 6, 0.42f, COL_CREAM, "Build", ALIGN_CENTER);
                continue;
            }
            if (i >= (int)offers.size()) break;
            const BuildingType& t = *offers[i];
            box(c, 4, y, BOT_W - 8, OFFER_H - 2, COL_SLOT, COL_SLOT_EDGE, 1);
            c.text(10, y + 1, 0.45f, COL_TEXT, t.name + (t.upgradeFrom.empty() ? "" : " (upgrade)"));
            std::string mats = std::to_string(t.cost) + "g";
            for (auto& m : t.mats) {
                const ObjectDef* o = g.data.object(m.first);
                mats += "  " + std::to_string(m.second) + " " + (o ? o->name : "?");
            }
            bool ok = g.canAfford(t, nullptr);
            c.text(10, y + 15, 0.34f, ok ? COL_GREEN : COL_TEXT_SOFT, mats);
            box(c, 252, y + 4, 58, 20, ok ? COL_GREEN : RGBA(0x80, 0x70, 0x60), COL_GREEN_DARK, 1);
            c.text(281, y + 6, 0.42f, COL_CREAM, "Build", ALIGN_CENTER);
        }
        snprintf(buf, sizeof buf, "Hay in silos: %d/%d", g.hay, g.hayCapacity());
        c.text(8, 214, 0.36f, COL_CREAM, buf);
    } else {
        auto offers = animalOffers(g);
        ui.shopScroll = std::max(0, std::min(ui.shopScroll, (int)offers.size() - OFFER_ROWS));
        for (int r = 0; r < OFFER_ROWS; r++) {
            int i = ui.shopScroll + r;
            if (i >= (int)offers.size()) break;
            const AnimalOffer& o = offers[i];
            float y = LIST_Y + r * OFFER_H;
            box(c, 4, y, BOT_W - 8, OFFER_H - 2, COL_SLOT, COL_SLOT_EDGE, 1);
            bool ok;
            if (o.a) {
                if (!o.a->shopTex.empty() && o.a->shopW > 0)
                    c.image(o.a->shopTex, o.a->shopX, o.a->shopY, o.a->shopW, o.a->shopH, 8, y + 6);
                c.text(46, y + 1, 0.45f, COL_TEXT, o.a->name);
                bool room = g.roomFor(*o.a) != 0;
                ok = room && g.money >= o.a->price;
                snprintf(buf, sizeof buf, "%dg  %s", o.a->price,
                         room ? "has room" : ("needs a " + (o.a->requires.empty() ? o.a->house : o.a->requires)).c_str());
                c.text(46, y + 15, 0.34f, ok ? COL_GREEN : COL_TEXT_SOFT, buf);
            } else {
                int ti = g.data.toolByKey(o.tool);
                Slot ts; ts.type = SLOT_TOOL; ts.id = ti; ts.count = 1;
                slotIcon(c, g, ts, 10, y + 5);
                const ToolDef* td = g.data.tool(ti);
                c.text(46, y + 1, 0.45f, COL_TEXT, td ? td->name : o.tool);
                ok = g.money >= 1000;
                c.text(46, y + 15, 0.34f, ok ? COL_GREEN : COL_TEXT_SOFT, "1000g");
            }
            box(c, 252, y + 4, 58, 20, ok ? COL_GREEN : RGBA(0x80, 0x70, 0x60), COL_GREEN_DARK, 1);
            c.text(281, y + 6, 0.42f, COL_CREAM, "Buy", ALIGN_CENTER);
        }
        c.text(8, 214, 0.36f, COL_CREAM, "Animals live in your coop or barn.");
    }
}

static bool touchShopExtra(Game& g, Ui& ui, int tx, int ty) {
    for (int r = 0; r < OFFER_ROWS; r++) {
        if (!hit(tx, ty, 252, LIST_Y + r * OFFER_H + 4, 58, 20)) continue;
        int i = ui.shopScroll + r;
        if (ui.shop == "Carpenter") {
            auto offers = buildOffers(g);
            if (g.houseLevel < 2) {
                if (i == 0) { g.startHouseUpgrade(); return true; }
                i--;
            }
            if (i >= 0 && i < (int)offers.size() && g.startBuilding(offers[i]->name) && !g.placing.empty()) {
                ui.shop.clear();   // off to the farm to pick a spot
                ui.cart.clear();
            }
        } else {
            auto offers = animalOffers(g);
            if (i < (int)offers.size()) {
                if (offers[i].a) g.buyAnimal(offers[i].a->name);
                else g.buyAnimalTool(offers[i].tool);
            }
        }
        return true;
    }
    return false;
}

static void drawShop(Canvas& c, Game& g, Ui& ui) {
    c.rect(0, 0, BOT_W, BOT_H, COL_BG);
    box(c, 4, 4, BOT_W - 8, 32, COL_PANEL, COL_BORDER);
    c.text(10, 8, 0.52f, COL_TEXT, shopTitle(ui.shop));
    char buf[64];
    snprintf(buf, sizeof buf, "%dg", g.money);
    c.text(BOT_W - 44, 10, 0.5f, COL_TEXT, buf, ALIGN_RIGHT);
    box(c, BOT_W - 38, 7, 28, 26, COL_PANEL_DARK, COL_BORDER, 1);
    c.text(BOT_W - 24, 10, 0.5f, COL_TEXT, "X", ALIGN_CENTER);

    // Buy / Sell (/ Upgrade at the blacksmith) toggle
    bool smith = ui.shop == "Blacksmith";
    bool extra = ui.shop == "Carpenter" || ui.shop == "AnimalShop";
    bool third = smith || extra;
    const char* thirdName = smith ? "Upgrade" : ui.shop == "Carpenter" ? "Build" : "Animals";
    for (int i = 0; i < (third ? 3 : 2); i++) {
        bool on = i == 2 ? ui.shopUpgrade : (!ui.shopUpgrade && (i == 1) == ui.shopSell);
        box(c, 4 + i * 60, 38, 58, 18, on ? COL_PANEL : COL_TAB_OFF, COL_BORDER, 1);
        c.text(33 + i * 60, 39, 0.45f, COL_TEXT, i == 2 ? thirdName : (i ? "Sell" : "Buy"), ALIGN_CENTER);
    }
    float scrollX = third ? 190 : 128;

    if (ui.shopUpgrade && extra) {
        drawShopExtra(c, g, ui);
        box(c, scrollX, 38, 40, 18, COL_PANEL_DARK, COL_BORDER, 1);
        c.text(scrollX + 20, 39, 0.42f, COL_TEXT, "Up", ALIGN_CENTER);
        box(c, scrollX + 44, 38, 40, 18, COL_PANEL_DARK, COL_BORDER, 1);
        c.text(scrollX + 64, 39, 0.42f, COL_TEXT, "Down", ALIGN_CENTER);
    } else if (ui.shopUpgrade) {
        auto offers = g.upgradeOffers();
        if (g.upgradeTool >= 0) {
            const ToolDef* t = g.data.tool(g.upgradeTool);
            int left = g.upgradeReadyDay - g.absoluteDay();
            std::string msg = (t ? t->name : std::string("Your tool")) +
                              (left > 0 ? " - ready in " + std::to_string(left) + (left == 1 ? " day" : " days")
                                        : " is ready!");
            box(c, 4, LIST_Y, BOT_W - 8, ROW_H - 2, COL_SLOT_ON, COL_SEL, 1);
            Slot ts; ts.type = SLOT_TOOL; ts.id = g.upgradeTool; ts.count = 1;
            slotIcon(c, g, ts, 8, LIST_Y + 3);
            c.text(28, LIST_Y + 4, 0.42f, COL_TEXT, msg);
        }
        float y0 = LIST_Y + (g.upgradeTool >= 0 ? ROW_H + 4 : 0);
        for (size_t i = 0; i < offers.size() && i < 5; i++) {
            const UpgradeOffer& o = offers[i];
            float y = y0 + i * (ROW_H + 6);
            box(c, 4, y, BOT_W - 8, ROW_H + 4, COL_SLOT, COL_SLOT_EDGE, 1);
            Slot ts; ts.type = SLOT_TOOL; ts.id = o.next; ts.count = 1;
            slotIcon(c, g, ts, 8, y + 5);
            const ToolDef* t = g.data.tool(o.next);
            c.text(28, y + 1, 0.45f, COL_TEXT, t ? t->name : "?");
            int have = o.bar >= 0 ? g.countOf(Ingredient{o.bar, o.bars, false}) : 0;
            const ObjectDef* bar = g.data.object(o.bar);
            snprintf(buf, sizeof buf, "%dg + %d/%d %s", o.price, std::min(have, 99), o.bars,
                     bar ? bar->name.c_str() : "bars");
            bool ok = g.money >= o.price && have >= o.bars && g.upgradeTool < 0;
            c.text(28, y + 14, 0.36f, ok ? COL_GREEN : COL_TEXT_SOFT, buf);
            box(c, 232, y + 4, 78, 20, ok ? COL_GREEN : RGBA(0x80, 0x70, 0x60), COL_GREEN_DARK, 1);
            c.text(271, y + 6, 0.42f, COL_CREAM, "Upgrade", ALIGN_CENTER);
        }
        if (offers.empty() && g.upgradeTool < 0)
            c.text(160, 110, 0.45f, COL_PANEL, "Nothing to upgrade.", ALIGN_CENTER);
        c.text(8, 214, 0.36f, COL_CREAM, "Takes 2 days. Come back to pick it up.");
    } else if (!ui.shopSell) {
        auto stock = g.shopStock(ui.shop);
        ui.shopScroll = std::max(0, std::min(ui.shopScroll, (int)stock.size() - SHOP_ROWS));
        for (int r = 0; r < SHOP_ROWS; r++) {
            int i = ui.shopScroll + r;
            if (i >= (int)stock.size()) break;
            float y = LIST_Y + r * ROW_H;
            box(c, 4, y, BOT_W - 8, ROW_H - 2, COL_SLOT, COL_SLOT_EDGE, 1);
            const ObjectDef* o = g.data.object(stock[i].itemId);
            if (o) {
                itemIcon(c, o, 8, y + 3);
                c.text(28, y + 3, 0.45f, COL_TEXT, o->name);
            }
            snprintf(buf, sizeof buf, "%dg", stock[i].price);
            c.text(206, y + 3, 0.45f, COL_TEXT, buf, ALIGN_RIGHT);
            int q = ui.cart.count(stock[i].itemId) ? ui.cart[stock[i].itemId] : 0;
            box(c, 222, y + 2, 26, 18, COL_PANEL_DARK, COL_BORDER, 1);
            c.text(235, y + 2, 0.5f, COL_TEXT, "-", ALIGN_CENTER);
            c.text(266, y + 3, 0.45f, COL_TEXT, std::to_string(q), ALIGN_CENTER);
            box(c, 284, y + 2, 26, 18, COL_PANEL_DARK, COL_BORDER, 1);
            c.text(297, y + 2, 0.5f, COL_TEXT, "+", ALIGN_CENTER);
        }
        if (stock.empty()) c.text(160, 110, 0.5f, COL_PANEL, "Nothing for sale right now.", ALIGN_CENTER);
        if ((int)stock.size() > SHOP_ROWS) {
            box(c, scrollX, 38, 40, 18, COL_PANEL_DARK, COL_BORDER, 1);
            c.text(scrollX + 20, 39, 0.42f, COL_TEXT, "Up", ALIGN_CENTER);
            box(c, scrollX + 44, 38, 40, 18, COL_PANEL_DARK, COL_BORDER, 1);
            c.text(scrollX + 64, 39, 0.42f, COL_TEXT, "Down", ALIGN_CENTER);
        }
        int total = cartTotal(g, ui);
        bool afford = total <= g.money;
        box(c, 170, 206, 146, 30, total > 0 && afford ? COL_GREEN : RGBA(0x80, 0x70, 0x60), COL_GREEN_DARK);
        snprintf(buf, sizeof buf, total > 0 ? "Buy  %dg" : "Pick items", total);
        c.text(243, 212, 0.5f, COL_CREAM, buf, ALIGN_CENTER);
        c.text(8, 214, 0.38f, afford ? COL_CREAM : RGBA(0xFF, 0x90, 0x80),
               afford ? "+ / - to set amounts" : "Not enough money");
    } else {
        auto slots = sellableSlots(g);
        ui.shopScroll = std::max(0, std::min(ui.shopScroll, (int)slots.size() - SHOP_ROWS));
        for (int r = 0; r < SHOP_ROWS; r++) {
            int k = ui.shopScroll + r;
            if (k >= (int)slots.size()) break;
            const Slot& s = g.inv[slots[k]];
            float y = LIST_Y + r * ROW_H;
            box(c, 4, y, BOT_W - 8, ROW_H - 2, COL_SLOT, COL_SLOT_EDGE, 1);
            slotIcon(c, g, s, 8, y + 3);
            c.text(28, y + 3, 0.45f, COL_TEXT, slotName(g, s) + "  x" + std::to_string(s.count));
            box(c, 222, y + 2, 88, 18, COL_GREEN, COL_GREEN_DARK, 1);
            snprintf(buf, sizeof buf, "Sell %dg", g.sellPrice(s) * s.count);
            c.text(266, y + 3, 0.42f, COL_CREAM, buf, ALIGN_CENTER);
        }
        if (slots.empty()) c.text(160, 110, 0.5f, COL_PANEL, "Nothing to sell.", ALIGN_CENTER);
    }
}

static void touchShop(Game& g, Ui& ui, int tx, int ty) {
    if (hit(tx, ty, BOT_W - 38, 7, 28, 26)) {
        ui.shop.clear();
        ui.cart.clear();
        return;
    }
    bool smith = ui.shop == "Blacksmith";
    bool extra = ui.shop == "Carpenter" || ui.shop == "AnimalShop";
    float scrollX = smith || extra ? 190 : 128;
    if (hit(tx, ty, 4, 38, 58, 18)) { ui.shopSell = false; ui.shopUpgrade = false; ui.shopScroll = 0; return; }
    if (hit(tx, ty, 64, 38, 58, 18)) { ui.shopSell = true; ui.shopUpgrade = false; ui.shopScroll = 0; return; }
    if ((smith || extra) && hit(tx, ty, 124, 38, 58, 18)) { ui.shopUpgrade = true; ui.shopScroll = 0; return; }
    if (ui.shopUpgrade && extra) {
        if (hit(tx, ty, scrollX, 38, 40, 18)) { ui.shopScroll--; return; }
        if (hit(tx, ty, scrollX + 44, 38, 40, 18)) { ui.shopScroll++; return; }
        touchShopExtra(g, ui, tx, ty);
        return;
    }
    if (ui.shopUpgrade) {
        auto offers = g.upgradeOffers();
        float y0 = LIST_Y + (g.upgradeTool >= 0 ? ROW_H + 4 : 0);
        for (size_t i = 0; i < offers.size() && i < 5; i++)
            if (hit(tx, ty, 232, y0 + i * (ROW_H + 6) + 4, 78, 20)) { g.startUpgrade(offers[i]); return; }
        return;
    }
    if (hit(tx, ty, scrollX, 38, 40, 18)) { ui.shopScroll--; return; }
    if (hit(tx, ty, scrollX + 44, 38, 40, 18)) { ui.shopScroll++; return; }
    if (!ui.shopSell) {
        auto stock = g.shopStock(ui.shop);
        for (int r = 0; r < SHOP_ROWS; r++) {
            int i = ui.shopScroll + r;
            if (i >= (int)stock.size()) break;
            float y = LIST_Y + r * ROW_H;
            int& q = ui.cart[stock[i].itemId];
            if (hit(tx, ty, 222, y + 2, 26, 18)) q = std::max(0, q - 1);
            if (hit(tx, ty, 284, y + 2, 26, 18)) q = std::min(999, q + 1);
        }
        if (hit(tx, ty, 170, 206, 146, 30)) {
            int total = cartTotal(g, ui);
            if (total > 0 && total <= g.money) {
                for (auto& kv : ui.cart)
                    if (kv.second > 0) g.buy(ui.shop, kv.first, kv.second);
                ui.cart.clear();
            }
        }
    } else {
        auto slots = sellableSlots(g);
        for (int r = 0; r < SHOP_ROWS; r++) {
            int k = ui.shopScroll + r;
            if (k >= (int)slots.size()) break;
            if (hit(tx, ty, 222, LIST_Y + r * ROW_H + 2, 88, 18)) g.sellSlot(slots[k]);
        }
    }
}

// --------------------------------------------------------------- summary
// ---------------------------------------------------------------- fishing takeover
// Bottom screen while fishing: the reeling meter on the left, what to do on
// the right, a big button. The top screen stays on you at the water's edge.
static const float FM_X = 10, FM_Y = 8, FM_W = 76, FM_H = 224;
static const float FB_X = 94, FB_Y = 166, FB_W = 216, FB_H = 64;   // big button

static void drawFishing(Canvas& c, Game& g) {
    const Fishing& f = g.fishing;
    c.rect(0, 0, BOT_W, BOT_H, COL_BG);
    // meter panel
    box(c, FM_X, FM_Y, FM_W, FM_H, COL_PANEL, COL_BORDER, 2);
    float wx = FM_X + 14, wy = FM_Y + 8, ww = 30, wh = FM_H - 16;
    box(c, wx, wy, ww, wh, RGBA(0x2F, 0x6F, 0xA0), RGBA(0x1C, 0x3C, 0x5C), 2);
    float px2 = wx + ww + 6, pw2 = 10;
    box(c, px2, wy, pw2, wh, RGBA(0x5C, 0x3A, 0x22), RGBA(0x30, 0x20, 0x10), 2);
    if (f.state == FS_REEL || (f.state == FS_DONE && f.fish >= 0)) {
        float inner = wh - 4;
        float barH = inner * f.barSize, barY = wy + 2 + inner * (1 - f.barPos - f.barSize);
        c.rect(wx + 2, barY, ww - 4, barH, RGBA(0x7C, 0xC8, 0x6A));
        c.rect(wx + 2, barY, ww - 4, 2, RGBA(0x4E, 0x96, 0x3E));
        c.rect(wx + 2, barY + barH - 2, ww - 4, 2, RGBA(0x4E, 0x96, 0x3E));
        float fy = wy + 2 + inner * (1 - f.fishPos) - 8;
        fy = std::max(wy + 2, std::min(wy + wh - 18, fy));
        itemIcon(c, g.data.object(f.fish), wx + ww / 2 - 8, fy);
        float ph = (wh - 4) * std::max(0.0f, std::min(1.0f, f.progress));
        uint32_t pc = f.progress > 0.66f ? RGBA(0x7C, 0xD0, 0x42)
                    : f.progress > 0.33f ? RGBA(0xF0, 0xC0, 0x30) : RGBA(0xE0, 0x50, 0x30);
        c.rect(px2 + 2, wy + wh - 2 - ph, pw2 - 4, ph, pc);
    }

    // what's happening
    std::string title, line;
    switch (f.state) {
        case FS_CHARGE: title = "Casting..."; line = "Let go of A to cast. Further when the meter is full."; break;
        case FS_CAST:
        case FS_WAIT: title = "Waiting for a bite"; line = "Watch the bobber. Something will nibble soon."; break;
        case FS_BITE: title = "Something's biting!"; line = "Press A or tap the button now!"; break;
        case FS_REEL: title = "Fish on!"; line = "Keep the green bar over the fish until the meter fills."; break;
        case FS_DONE: {
            const ObjectDef* o = g.data.object(f.fish);
            title = f.caught ? "Caught it!" : "It got away...";
            line = f.caught ? (o ? o->name : std::string("A fish")) +
                                  (f.size > 0 ? "  " + std::to_string(f.size) + " in" : std::string()) +
                                  (f.perfect && f.size > 0 ? "  Perfect!" : "")
                            : "Keep the bar on the fish next time.";
            break;
        }
        default: break;
    }
    box(c, FB_X, FM_Y, FB_W, 64, COL_PANEL, COL_BORDER, 2);
    c.text(FB_X + 8, FM_Y + 4, 0.58f, COL_TEXT, title);
    // wrap the line
    {
        std::string ln, word, rest = line + " ";
        float y = FM_Y + 26;
        for (char ch : rest) {
            if (ch == ' ') {
                if (c.textWidth(ln + word, 0.4f) > FB_W - 16 && !ln.empty()) {
                    c.text(FB_X + 8, y, 0.4f, COL_TEXT_SOFT, ln);
                    y += 14;
                    ln.clear();
                }
                ln += word + " ";
                word.clear();
            } else {
                word += ch;
            }
        }
        c.text(FB_X + 8, y, 0.4f, COL_TEXT_SOFT, ln);
    }
    if (f.state == FS_REEL) {
        box(c, FB_X, FM_Y + 72, FB_W, 80, COL_SLOT, COL_SLOT_EDGE, 1);
        c.text(FB_X + 8, FM_Y + 78, 0.42f, COL_TEXT, "Hold A or touch and hold anywhere");
        c.text(FB_X + 8, FM_Y + 92, 0.42f, COL_TEXT, "to lift the bar.");
        c.text(FB_X + 8, FM_Y + 110, 0.42f, COL_TEXT_SOFT, "Let go to let it sink.");
        char buf[48];
        snprintf(buf, sizeof buf, "Fishing level %d", g.skillLevel(SK_FISHING));
        c.text(FB_X + 8, FM_Y + 128, 0.38f, COL_TEXT_SOFT, buf);
    }
    // the big button
    const char* label = f.state == FS_REEL ? "Hold to reel"
                      : f.state == FS_BITE ? "Hook it!"
                      : f.state == FS_DONE ? "OK"
                      : f.state == FS_CHARGE ? "" : "Reel in";
    if (*label) {
        bool hot = f.state == FS_BITE || f.state == FS_REEL;
        box(c, FB_X, FB_Y, FB_W, FB_H, hot ? COL_GREEN : COL_PANEL_DARK, hot ? COL_GREEN_DARK : COL_BORDER, 2);
        c.text(FB_X + FB_W / 2, FB_Y + 20, 0.62f, hot ? COL_CREAM : COL_TEXT, label, ALIGN_CENTER);
    }
}

void fishingTouch(const sv::Game& g, bool down, bool held, int x, int y, sv::Input& in) {
    const Fishing& f = g.fishing;
    bool onButton = hit(x, y, FB_X, FB_Y, FB_W, FB_H);
    switch (f.state) {
        case FS_REEL: if (held) in.hold = true; break;       // anywhere
        case FS_BITE: if (down) in.action = true; break;     // anywhere: speed matters
        case FS_WAIT:
        case FS_DONE: if (down && onButton) in.action = true; break;
        default: break;
    }
}

// ---------------------------------------------------------------- letters
static const float LT_BTN_Y = 206;

static void drawLetter(Canvas& c, Game& g) {
    const Game::Letter& L = g.letter;
    c.rect(0, 0, BOT_W, BOT_H, COL_BG);
    if (c.texWidth("LooseSprites_letterBG") > 0)
        c.image("LooseSprites_letterBG", 0, 0, 312, 196, 4, 4);   // the paper from your files
    else
        box(c, 4, 4, 312, 196, RGBA(0xFA, 0xEE, 0xD2), COL_BORDER, 2);
    // body: '^' is a line break in letters
    float textH = L.items.empty() && L.money <= 0 ? 186 : 150;
    std::string body = L.text;
    {
        std::string ln, word, rest = body + " ";
        float y = 14;
        auto flush = [&](bool force) {
            if (y < 4 + textH) c.text(16, y, 0.42f, COL_TEXT, ln);
            y += 14;
            ln.clear();
            (void)force;
        };
        for (char ch : rest) {
            if (ch == ' ' || ch == '^') {
                if (c.textWidth(ln + word, 0.42f) > 288 && !ln.empty()) flush(false);
                ln += word + " ";
                word.clear();
                if (ch == '^') flush(true);
            } else {
                word += ch;
            }
        }
        if (!ln.empty()) flush(true);
    }
    // attachments
    if (!L.items.empty() || L.money > 0) {
        float x = 16, y = 164;
        for (auto& it : L.items) {
            box(c, x, y, 26, 26, COL_SLOT, COL_SLOT_EDGE, 1);
            itemIcon(c, g.data.object(it.first), x + 5, y + 5);
            if (it.second > 1) countText(c, x + 24, y + 16, it.second);
            x += 30;
        }
        if (L.money > 0) {
            char buf[32];
            snprintf(buf, sizeof buf, "%dg", L.money);
            c.text(x + 4, y + 6, 0.5f, COL_TEXT, buf);
        }
    }
    bool hasStuff = !L.items.empty() || L.money > 0;
    if (hasStuff) {
        box(c, 8, LT_BTN_Y, 150, 28, COL_GREEN, COL_GREEN_DARK, 2);
        c.text(83, LT_BTN_Y + 6, 0.48f, COL_CREAM, "Take", ALIGN_CENTER);
    }
    box(c, hasStuff ? 162 : 8, LT_BTN_Y, hasStuff ? 150 : 304, 28, COL_PANEL_DARK, COL_BORDER, 2);
    c.text(hasStuff ? 237 : 160, LT_BTN_Y + 6, 0.48f, COL_TEXT, g.mailbox.size() > 1 ? "Next letter" : "Close", ALIGN_CENTER);
}

static void touchLetter(Game& g, int tx, int ty) {
    bool hasStuff = !g.letter.items.empty() || g.letter.money > 0;
    if (hasStuff && hit(tx, ty, 8, LT_BTN_Y, 150, 28)) { g.takeLetterItems(); return; }
    if (hit(tx, ty, hasStuff ? 162 : 8, LT_BTN_Y, hasStuff ? 150 : 304, 28)) {
        g.closeLetter();
        g.openMail();   // straight on to the next one, if any
    }
}

// ---------------------------------------------------------------- journal
static const int QJ_ROWS = 4, QJ_ROW_H = 18, QJ_Y = PANEL_Y + 18;

static void wrapText(Canvas& c, float x, float y, float w, float scale, uint32_t col, const std::string& text,
                     int maxLines) {
    std::string ln, word, rest = text + " ";
    int lines = 0;
    const int LH = scale < 0.41f ? 10 : 14;
    for (char ch : rest) {
        if (ch == ' ' || ch == '^') {
            if (c.textWidth(ln + word, scale) > w && !ln.empty()) {
                if (lines < maxLines) c.text(x, y + lines * LH, scale, col, ln);
                lines++;
                ln.clear();
            }
            ln += word + " ";
            word.clear();
            if (ch == '^') {
                if (lines < maxLines) c.text(x, y + lines * LH, scale, col, ln);
                lines++;
                ln.clear();
            }
        } else {
            word += ch;
        }
    }
    if (lines < maxLines) c.text(x, y + lines * LH, scale, col, ln);
}

static void drawQuestsTab(Canvas& c, Game& g, Ui& ui) {
    g.hasUnseenQuest = false;
    c.text(HOT_X, PANEL_Y + 3, 0.45f, COL_TEXT, "Journal");
    int n = (int)g.quests.size();
    char buf[96];
    snprintf(buf, sizeof buf, "%d quest%s", n, n == 1 ? "" : "s");
    c.text(BOT_W - HOT_X, PANEL_Y + 4, 0.38f, COL_TEXT_SOFT, buf, ALIGN_RIGHT);
    if (!n) {
        c.text(BOT_W / 2, PANEL_Y + 80, 0.45f, COL_TEXT_SOFT, "No quests right now.", ALIGN_CENTER);
        return;
    }
    ui.questSel = std::max(0, std::min(ui.questSel, n - 1));
    ui.questScroll = std::max(0, std::min(ui.questScroll, std::max(0, n - QJ_ROWS)));
    for (int r = 0; r < QJ_ROWS && ui.questScroll + r < n; r++) {
        int i = ui.questScroll + r;
        const QuestState& qs = g.quests[i];
        const QuestDef* q = g.data.quest(qs.id);
        float y = QJ_Y + r * QJ_ROW_H;
        box(c, 10, y, 284, QJ_ROW_H - 2, i == ui.questSel ? COL_SLOT_ON : COL_SLOT,
            i == ui.questSel ? COL_SEL : COL_SLOT_EDGE, i == ui.questSel ? 2 : 1);
        c.text(18, y + 1, 0.42f, COL_TEXT, q ? q->title : qs.id);
        if (qs.done) c.text(286, y + 1, 0.42f, COL_GREEN, "Done!", ALIGN_RIGHT);
    }
    if (n > QJ_ROWS) {
        box(c, 298, QJ_Y, 14, 38, COL_PANEL_DARK, COL_BORDER, 1);
        box(c, 298, QJ_Y + 40, 14, 38, COL_PANEL_DARK, COL_BORDER, 1);
        c.text(305, QJ_Y + 12, 0.4f, COL_TEXT, "^", ALIGN_CENTER);
        c.text(305, QJ_Y + 52, 0.4f, COL_TEXT, "v", ALIGN_CENTER);
    }
    const QuestState& qs = g.quests[ui.questSel];
    const QuestDef* q = g.data.quest(qs.id);
    if (!q) return;
    float dy = QJ_Y + QJ_ROWS * QJ_ROW_H + 2;
    box(c, 10, dy, 300, 232 - dy, COL_SLOT, COL_SLOT_EDGE, 1);
    wrapText(c, 16, dy + 4, 288, 0.38f, COL_TEXT_SOFT, q->description, 5);
    std::string obj = q->objective;
    int goal = g.questGoal(*q);
    if (goal > 0 && !qs.done) obj += "  (" + std::to_string(std::min(qs.progress, goal)) + "/" + std::to_string(goal) + ")";
    wrapText(c, 16, dy + 56, qs.done ? 196 : 288, 0.42f, COL_TEXT, obj, 2);
    if (qs.done) {
        bool money = q->reward > 0;
        box(c, 214, dy + 56, 90, 24, COL_GREEN, COL_GREEN_DARK, 2);
        snprintf(buf, sizeof buf, money ? "Collect %dg" : "Done", q->reward);
        c.text(259, dy + 59, 0.42f, COL_CREAM, buf, ALIGN_CENTER);
    } else if (q->reward > 0) {
        snprintf(buf, sizeof buf, "Reward: %dg", q->reward);
        c.text(304, 216, 0.38f, COL_TEXT_SOFT, buf, ALIGN_RIGHT);
    }
}

static void touchQuestsTab(Game& g, Ui& ui, int tx, int ty) {
    int n = (int)g.quests.size();
    if (!n) return;
    if (n > QJ_ROWS && hit(tx, ty, 296, QJ_Y, 18, 39)) { ui.questScroll--; return; }
    if (n > QJ_ROWS && hit(tx, ty, 296, QJ_Y + 39, 18, 40)) { ui.questScroll++; return; }
    for (int r = 0; r < QJ_ROWS && ui.questScroll + r < n; r++)
        if (hit(tx, ty, 10, QJ_Y + r * QJ_ROW_H, 284, QJ_ROW_H - 2)) { ui.questSel = ui.questScroll + r; return; }
    float dy = QJ_Y + QJ_ROWS * QJ_ROW_H + 2;
    if (hit(tx, ty, 214, dy + 56, 90, 24) && ui.questSel < n && g.quests[ui.questSel].done)
        g.collectQuest((size_t)ui.questSel);
}

// ---------------------------------------------------------------- level up
static const char* SKILL_TITLE[SK_COUNT] = {"Farming", "Fishing", "Foraging", "Mining", "Combat"};

static const float PC_Y = 64, PC_W = 140, PC_H = 128;

static void drawLevelUp(Canvas& c, Game& g, Ui& ui) {
    auto lu = g.levelUps.front();
    c.rect(0, 0, BOT_W, BOT_H, COL_BG);
    box(c, 12, 10, 296, 220, COL_PANEL, COL_BORDER, 3);
    char buf[64];
    snprintf(buf, sizeof buf, "%s Level %d", SKILL_TITLE[lu.first], lu.second);
    c.text(160, 18, 0.7f, COL_TEXT, buf, ALIGN_CENTER);
    std::vector<int> choice = g.professionChoices(lu.first, lu.second);
    if (!choice.empty()) {
        c.text(160, 44, 0.45f, COL_TEXT, "Choose a profession", ALIGN_CENTER);
        for (int k = 0; k < 2; k++) {
            const ProfessionDef* p = g.data.profession(choice[k]);
            float x = 18 + k * (PC_W + 4);
            bool on = ui.profSel == k;
            box(c, x, PC_Y, PC_W, PC_H, on ? COL_SLOT_ON : COL_SLOT, on ? COL_SEL : COL_SLOT_EDGE, on ? 2 : 1);
            if (!p) continue;
            c.text(x + PC_W / 2, PC_Y + 6, 0.5f, COL_TEXT, p->name, ALIGN_CENTER);
            wrapText(c, x + 8, PC_Y + 28, PC_W - 16, 0.38f, COL_TEXT_SOFT, p->description, 9);
        }
        c.text(160, 210, 0.4f, COL_BORDER, "Tap one, or pick with left/right and A", ALIGN_CENTER);
        return;
    }
    // recipes that this level unlocked
    snprintf(buf, sizeof buf, "s %s %d", SKILL_TITLE[lu.first], lu.second);
    std::vector<const Recipe*> fresh;
    for (auto& r : g.data.recipes)
        if (r.unlock == buf) fresh.push_back(&r);
    float y = 50;
    if (fresh.empty()) {
        c.text(160, y + 10, 0.45f, COL_TEXT_SOFT, "Your skill grows.", ALIGN_CENTER);
    } else {
        c.text(24, y, 0.45f, COL_TEXT, fresh.size() == 1 ? "New recipe:" : "New recipes:");
        y += 18;
        for (size_t i = 0; i < fresh.size() && i < 6; i++) {
            box(c, 24, y, 24, 24, COL_SLOT, COL_SLOT_EDGE, 1);
            Slot s;
            s.type = fresh[i]->outBig ? SLOT_BIG : SLOT_OBJECT;
            s.id = fresh[i]->outId;
            s.count = 1;
            slotIcon(c, g, s, 28, y + 4);
            c.text(56, y + 4, 0.45f, COL_TEXT, fresh[i]->name);
            y += 26;
        }
    }
    if (g.levelUps.size() > 1) {
        snprintf(buf, sizeof buf, "+%d more", (int)g.levelUps.size() - 1);
        c.text(296, 210, 0.38f, COL_TEXT_SOFT, buf, ALIGN_RIGHT);
    }
    c.text(160, 210, 0.42f, COL_BORDER, "Tap or press A", ALIGN_CENTER);
}

void summaryMove(Game& g, Ui& ui, int dir) {
    (void)g;
    ui.profSel = std::max(0, std::min(1, ui.profSel + dir));
}

static void touchLevelUp(Game& g, Ui& ui, int tx, int ty) {
    auto lu = g.levelUps.front();
    std::vector<int> choice = g.professionChoices(lu.first, lu.second);
    if (choice.empty()) { summaryAdvance(g, ui); return; }
    for (int k = 0; k < 2; k++)
        if (hit(tx, ty, 18 + k * (PC_W + 4), PC_Y, PC_W, PC_H)) {
            ui.profSel = k;
            summaryAdvance(g, ui);
        }
}

void summaryAdvance(Game& g, Ui& ui) {
    if (!g.levelUps.empty()) {
        auto lu = g.levelUps.front();
        std::vector<int> choice = g.professionChoices(lu.first, lu.second);
        if (!choice.empty()) g.chooseProfession(choice[std::max(0, std::min(1, ui.profSel))]);
        ui.profSel = 0;
        g.levelUps.erase(g.levelUps.begin());
        return;
    }
    ui.showSummary = false;
}

static void drawSummary(Canvas& c, Game& g) {
    c.rect(0, 0, BOT_W, BOT_H, COL_BG);
    box(c, 20, 40, 280, 150, COL_PANEL, COL_BORDER, 3);
    c.text(160, 52, 0.7f, COL_TEXT, g.passedOut ? "You passed out..." : "Good morning!", ALIGN_CENTER);
    char buf[64];
    snprintf(buf, sizeof buf, "Shipped yesterday: %dg", g.lastEarnings);
    c.text(160, 88, 0.55f, COL_TEXT, buf, ALIGN_CENTER);
    std::string sn = seasonName(g.season);
    sn[0] = (char)toupper((unsigned char)sn[0]);
    snprintf(buf, sizeof buf, "%s %d, Year %d", sn.c_str(), g.day, g.year);
    c.text(160, 110, 0.55f, COL_TEXT, buf, ALIGN_CENTER);
    c.text(160, 130, 0.5f, COL_TEXT, g.raining ? "It's raining today." : "Clear skies.", ALIGN_CENTER);
    c.text(160, 164, 0.45f, COL_BORDER, "Tap or press A", ALIGN_CENTER);
}

// --------------------------------------------------------------- entry points
// ---------------------------------------------------------------- bundles
static std::string bundleRewardText(const Game& g, const Bundle& b) {
    std::istringstream rs(b.reward);
    std::string kind, id;
    int n = 1;
    if (!(rs >> kind >> id)) return "";
    rs >> n;
    std::string name;
    if (kind == "O") { const ObjectDef* o = g.data.object(g.data.objectIdFor(id)); name = o ? o->name : ""; }
    else if (kind == "BO") { const BigDef* bd = g.data.big(atoi(id.c_str())); name = bd ? bd->name : ""; }
    if (name.empty()) return "";
    return (n > 1 ? std::to_string(n) + " " : "") + name;
}
// The Community Center's bundles: rooms -> a room's bundles -> one bundle's items.
static const int BN_Y = 40, BN_H = 28;

static void drawBundles(Canvas& c, Game& g) {
    c.rect(0, 0, BOT_W, BOT_H, RGBA(0x2A, 0x3A, 0x22));
    box(c, 4, 4, BOT_W - 8, 32, COL_PANEL, COL_BORDER);
    box(c, BOT_W - 38, 7, 28, 26, COL_PANEL_DARK, COL_BORDER, 1);
    if (g.bundleView == 0) c.text(BOT_W - 24, 10, 0.5f, COL_TEXT, "X", ALIGN_CENTER);
    else c.text(BOT_W - 24, 13, 0.32f, COL_TEXT, "Back", ALIGN_CENTER);   // (the game font draws < as a heart)
    char buf[96];
    auto all = g.bundles();
    if (g.bundleView == 0) {
        c.text(10, 9, 0.52f, COL_TEXT, "Community Center");
        auto rooms = Game::bundleRooms();
        for (size_t i = 0; i < rooms.size(); i++) {
            int n = 0, done = 0;
            for (auto& b : all) if (b.room == rooms[i]) { n++; done += g.bundleDone(b); }
            float y = BN_Y + i * BN_H;
            bool fin = n > 0 && done == n;
            box(c, 8, y, BOT_W - 16, BN_H - 3, fin ? RGBA(0x9C, 0xC8, 0x6A) : COL_SLOT, COL_SLOT_EDGE, 1);
            c.text(16, y + 4, 0.48f, COL_TEXT, rooms[i]);
            snprintf(buf, sizeof buf, fin ? "Restored!" : "%d / %d bundles", done, n);
            c.text(BOT_W - 18, y + 5, 0.42f, COL_TEXT, buf, ALIGN_RIGHT);
        }
        if (all.empty()) c.text(160, 120, 0.45f, COL_CREAM, "No bundle data (re-run the converter).", ALIGN_CENTER);
        return;
    }
    if (g.bundleView == 1) {
        c.text(10, 9, 0.52f, COL_TEXT, g.bundleRoom);
        int r = 0;
        for (auto& b : all) {
            if (b.room != g.bundleRoom) continue;
            float y = BN_Y + r * BN_H;
            bool fin = g.bundleDone(b);
            box(c, 8, y, BOT_W - 16, BN_H - 3, fin ? RGBA(0x9C, 0xC8, 0x6A) : COL_SLOT, COL_SLOT_EDGE, 1);
            c.text(16, y + 4, 0.46f, COL_TEXT, b.name + " Bundle");
            snprintf(buf, sizeof buf, fin ? "Done" : "%d / %d", g.bundleGivenCount(b), b.needed);
            c.text(BOT_W - 18, y + 5, 0.42f, COL_TEXT, buf, ALIGN_RIGHT);
            r++;
        }
        return;
    }
    for (auto& b : all) {
        if (b.index != g.bundleSel) continue;
        c.text(10, 6, 0.48f, COL_TEXT, b.name + " Bundle");
        snprintf(buf, sizeof buf, "%d of %d needed", g.bundleGivenCount(b), b.needed);
        c.text(10, 21, 0.36f, COL_TEXT_SOFT, buf);
        auto it = g.bundleGiven.find(b.index);
        for (size_t i = 0; i < b.items.size() && i < 12; i++) {
            const Bundle::Item& bi = b.items[i];
            float x = 8 + (i % 2) * 154, y = BN_Y + (i / 2) * 32;
            bool given = it != g.bundleGiven.end() && i < it->second.size() && it->second[i];
            box(c, x, y, 150, 29, given ? RGBA(0x9C, 0xC8, 0x6A) : COL_SLOT, COL_SLOT_EDGE, 1);
            const ObjectDef* o = bi.id >= 0 ? g.data.object(bi.id) : nullptr;
            if (o) itemIcon(c, o, x + 4, y + 6);
            std::string nm = bi.id == -1 ? std::to_string(bi.count) + "g" : o ? o->name : "Any (category)";
            if (bi.count > 1 && bi.id != -1) nm += " x" + std::to_string(bi.count);
            if (bi.quality > 0) nm += bi.quality >= 2 ? " (gold)" : " (silver)";
            c.text(x + 24, y + 2, 0.36f, COL_TEXT, nm);
            if (given) {
                c.text(x + 24, y + 15, 0.36f, COL_GREEN_DARK, "Given");
            } else {
                int have = g.bundleHave(bi);
                bool ok = have >= bi.count && !g.bundleDone(b);
                snprintf(buf, sizeof buf, bi.id == -1 ? "you have %dg" : "have %d", have);
                c.text(x + 24, y + 15, 0.34f, ok ? COL_GREEN : COL_TEXT_SOFT, buf);
                box(c, x + 104, y + 6, 42, 18, ok ? COL_GREEN : RGBA(0x80, 0x70, 0x60), COL_GREEN_DARK, 1);
                c.text(x + 125, y + 7, 0.38f, COL_CREAM, "Give", ALIGN_CENTER);
            }
        }
        if (bundleRewardText(g, b).size()) c.text(8, 220, 0.36f, COL_CREAM, "Reward: " + bundleRewardText(g, b));
    }
}

static void touchBundles(Game& g, int tx, int ty) {
    if (hit(tx, ty, BOT_W - 38, 7, 28, 26)) {   // close / back
        g.bundleView = g.bundleView == 0 ? -1 : g.bundleView - 1;
        return;
    }
    auto all = g.bundles();
    if (g.bundleView == 0) {
        auto rooms = Game::bundleRooms();
        for (size_t i = 0; i < rooms.size(); i++)
            if (hit(tx, ty, 8, BN_Y + i * BN_H, BOT_W - 16, BN_H - 3)) { g.bundleRoom = rooms[i]; g.bundleView = 1; return; }
        return;
    }
    if (g.bundleView == 1) {
        int r = 0;
        for (auto& b : all) {
            if (b.room != g.bundleRoom) continue;
            if (hit(tx, ty, 8, BN_Y + r * BN_H, BOT_W - 16, BN_H - 3)) { g.bundleSel = b.index; g.bundleView = 2; return; }
            r++;
        }
        return;
    }
    for (auto& b : all) {
        if (b.index != g.bundleSel) continue;
        for (size_t i = 0; i < b.items.size() && i < 12; i++) {
            float x = 8 + (i % 2) * 154, y = BN_Y + (i / 2) * 32;
            if (hit(tx, ty, x + 104, y + 6, 42, 18)) { g.bundleGive(b.index, (int)i); return; }
        }
    }
}

// ---------------------------------------------------------------- the museum
static std::vector<int> museumSlots(const Game& g) {
    std::vector<int> out;
    for (int i = 0; i < INV_SIZE; i++) if (g.museumWants(g.inv[i])) out.push_back(i);
    return out;
}

static void drawMuseum(Canvas& c, Game& g) {
    c.rect(0, 0, BOT_W, BOT_H, RGBA(0x30, 0x24, 0x1A));
    box(c, 4, 4, BOT_W - 8, 32, COL_PANEL, COL_BORDER);
    c.text(10, 6, 0.5f, COL_TEXT, "Museum donations");
    char buf[64];
    snprintf(buf, sizeof buf, "%d of %d donated", (int)g.museumDonated.size(), (int)g.data.museumItems.size());
    c.text(10, 21, 0.36f, COL_TEXT_SOFT, buf);
    box(c, BOT_W - 38, 7, 28, 26, COL_PANEL_DARK, COL_BORDER, 1);
    c.text(BOT_W - 24, 10, 0.5f, COL_TEXT, "X", ALIGN_CENTER);
    auto slots = museumSlots(g);
    for (size_t r = 0; r < slots.size() && r < 7; r++) {
        const Slot& s = g.inv[slots[r]];
        float y = 40 + r * 27;
        box(c, 8, y, BOT_W - 16, 25, COL_SLOT, COL_SLOT_EDGE, 1);
        slotIcon(c, g, s, 12, y + 4);
        const ObjectDef* o = g.data.object(s.id);
        c.text(34, y + 5, 0.44f, COL_TEXT, o ? o->name : "?");
        box(c, 236, y + 3, 72, 19, COL_GREEN, COL_GREEN_DARK, 1);
        c.text(272, y + 4, 0.42f, COL_CREAM, "Donate", ALIGN_CENTER);
    }
    if (slots.empty())
        c.text(160, 120, 0.42f, COL_CREAM, "Nothing new to donate right now.", ALIGN_CENTER);
}

static void touchMuseum(Game& g, int tx, int ty) {
    if (hit(tx, ty, BOT_W - 38, 7, 28, 26)) { g.museumOpen = false; return; }
    auto slots = museumSlots(g);
    for (size_t r = 0; r < slots.size() && r < 7; r++)
        if (hit(tx, ty, 236, 40 + r * 27 + 3, 72, 19)) { g.museumDonate(slots[r]); return; }
}

void drawBottom(Canvas& c, Game& g, Ui& ui) {
    if (ui.showSummary) {
        if (!g.levelUps.empty()) drawLevelUp(c, g, ui);
        else drawSummary(c, g);
        return;
    }
    if (g.dialogue.active) { drawDialogue(c, g, ui); return; }
    if (g.cutscene.active) {
        c.rect(0, 0, BOT_W, BOT_H, COL_BG);
        box(c, 40, 80, 240, 70, COL_PANEL, COL_BORDER, 3);
        c.text(160, 92, 0.6f, COL_TEXT, placeName(g, g.location().name), ALIGN_CENTER);
        c.text(160, 120, 0.42f, COL_TEXT_SOFT, g.cutscene.skippable ? "START to skip" : "...", ALIGN_CENTER);
        return;
    }
    if (!ui.shop.empty()) { drawShop(c, g, ui); return; }
    if (g.bundleView >= 0) { drawBundles(c, g); return; }
    if (g.museumOpen) { drawMuseum(c, g); return; }
    if (g.fishingActive()) { drawFishing(c, g); return; }
    if (g.letterOpen) { drawLetter(c, g); return; }
    if (g.chestOpen) { drawChest(c, g); return; }
    c.rect(0, 0, BOT_W, BOT_H, COL_BG);
    drawHotbarAndTabs(c, g, ui);
    if (ui.tab == TAB_ITEMS) drawItemsTab(c, g, ui);
    else if (ui.tab == TAB_CRAFT) drawCraftTab(c, g, ui);
    else if (ui.tab == TAB_QUESTS) drawQuestsTab(c, g, ui);
    else if (ui.tab == TAB_SOCIAL) drawSocialTab(c, g, ui);
    else if (ui.tab == TAB_MAP) drawMapTab(c, g);
    else drawOptionsTab(c, g, ui);
}

void touchBottom(Game& g, Ui& ui, int tx, int ty, const std::string& savePath) {
    if (ui.showSummary) {
        if (!g.levelUps.empty()) touchLevelUp(g, ui, tx, ty);
        else summaryAdvance(g, ui);
        return;
    }
    if (g.dialogue.active) { touchDialogue(g, ui, tx, ty); return; }
    if (g.cutscene.active) return;
    if (!ui.shop.empty()) { touchShop(g, ui, tx, ty); return; }
    if (g.bundleView >= 0) { touchBundles(g, tx, ty); return; }
    if (g.museumOpen) { touchMuseum(g, tx, ty); return; }
    if (g.fishingActive()) return;   // handled by fishingTouch() as input
    if (g.letterOpen) { touchLetter(g, tx, ty); return; }
    if (g.chestOpen) { touchChest(g, tx, ty); return; }
    int s = slotAt(tx, ty, ui.tab == TAB_ITEMS);
    if (s >= 0) {
        if (ui.picked < 0) {
            if (s < HOTBAR) g.selectSlot(s);
            if (!g.inv[s].empty() && ui.tab == TAB_ITEMS) ui.picked = s;
        } else if (ui.picked == s) {
            ui.picked = -1;
        } else {
            g.swapSlots(ui.picked, s);
            if (s < HOTBAR) g.selectSlot(s);
            ui.picked = -1;
        }
        return;
    }
    if (ty >= TAB_Y && ty < TAB_Y + TAB_H) {
        ui.tab = std::min(TAB_COUNT - 1, (tx - 4) * TAB_COUNT / (BOT_W - 8));
        ui.picked = -1;
        return;
    }
    if (ui.tab == TAB_ITEMS) touchItemsTab(g, ui, tx, ty);
    else if (ui.tab == TAB_CRAFT) touchCraftTab(g, ui, tx, ty);
    else if (ui.tab == TAB_QUESTS) touchQuestsTab(g, ui, tx, ty);
    else if (ui.tab == TAB_SOCIAL) touchSocialTab(ui, tx, ty);
    else if (ui.tab == TAB_OPTIONS) touchOptionsTab(g, ui, tx, ty, savePath);
}

bool sceneAfterUpdate(Game& g, Ui& ui, const std::string& savePath) {
    if (ui.statusTime > 0) ui.statusTime -= 1.0f / 60;
    if (!g.openShop.empty()) {
        ui.shop = g.openShop;
        ui.shopSell = false;
        ui.shopUpgrade = false;
        ui.shopScroll = 0;
        ui.cart.clear();
        g.openShop.clear();
    }
    if (g.dayEnded) {
        g.dayEnded = false;
        g.save(savePath);
        ui.showSummary = true;
        ui.shop.clear();
        ui.tab = TAB_ITEMS;
    }
    if (!ui.showSummary) g.passedOut = false;
    bool drop = false;
    if (g.season != ui.loadedSeason || g.location().name != ui.loadedMap) {
        drop = ui.loadedSeason != -1;
        ui.loadedSeason = g.season;
        ui.loadedMap = g.location().name;
    }
    return drop;
}
