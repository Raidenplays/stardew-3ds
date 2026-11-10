// stardew3ds -- an original 3DS farming engine that plays with assets
// converted from your own copy of Stardew Valley (see tools/convert.py).
#include <3ds.h>
#include <citro2d.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "core/game.h"
#include "platform/texture.h"

using namespace sv;

static const char* ROOT = "sdmc:/3ds/stardew3ds/";
static const int TOP_W = 400, TOP_H = 240, BOT_W = 320, BOT_H = 240;
static const int TILE = 16;

// ------------------------------------------------------------------ colours
static u32 COL_PANEL, COL_PANEL_DARK, COL_BORDER, COL_TEXT, COL_SLOT, COL_SLOT_SEL,
    COL_SLOT_PICK, COL_ENERGY, COL_ENERGY_BG, COL_WATER, COL_BG;

static void initColors() {
    COL_PANEL = C2D_Color32(0xF9, 0xD8, 0x96, 0xFF);       // parchment
    COL_PANEL_DARK = C2D_Color32(0xDC, 0xA4, 0x56, 0xFF);
    COL_BORDER = C2D_Color32(0x85, 0x3D, 0x1A, 0xFF);      // wood brown
    COL_TEXT = C2D_Color32(0x3B, 0x1F, 0x0E, 0xFF);
    COL_SLOT = C2D_Color32(0xFC, 0xE6, 0xB4, 0xFF);
    COL_SLOT_SEL = C2D_Color32(0xE0, 0x3C, 0x28, 0xFF);
    COL_SLOT_PICK = C2D_Color32(0x3C, 0x8C, 0xE0, 0xFF);
    COL_ENERGY = C2D_Color32(0x4C, 0xC8, 0x3C, 0xFF);
    COL_ENERGY_BG = C2D_Color32(0x40, 0x20, 0x10, 0xFF);
    COL_WATER = C2D_Color32(0x40, 0x90, 0xF0, 0xFF);
    COL_BG = C2D_Color32(0x2A, 0x1A, 0x10, 0xFF);
}

// ------------------------------------------------------------- stereo 3D
// Pixels of horizontal shift per depth unit for the eye being drawn.
// Positive depth pops out of the screen, negative sinks in.
static float g_eye = 0.0f;
static inline float D(float depth) { return depth * g_eye; }

// --------------------------------------------------------------------- text
static C2D_TextBuf g_textBuf;

static float g_uiDepth = 0.0f;   // depth applied to text()/box() calls

static void text(float x, float y, float scale, u32 color, const std::string& s, int align = 0) {
    x += D(g_uiDepth);
    C2D_Text t;
    C2D_TextParse(&t, g_textBuf, s.c_str());
    C2D_TextOptimize(&t);
    u32 flags = C2D_WithColor | (align == 1 ? C2D_AlignCenter : align == 2 ? C2D_AlignRight : 0);
    C2D_DrawText(&t, flags, x, y, 0.5f, scale, scale, color);
}

static void box(float x, float y, float w, float h, u32 fill, u32 border, float bw = 2) {
    x += D(g_uiDepth);
    C2D_DrawRectSolid(x, y, 0.5f, w, h, border);
    C2D_DrawRectSolid(x + bw, y + bw, 0.5f, w - 2 * bw, h - 2 * bw, fill);
}

// ------------------------------------------------------------- tex helpers
struct Renderer {
    TexCache tex{ROOT};
    const GameData* data = nullptr;
    int loadedSeason = -1;

    const Texture* objects() { return tex.get("Maps_springobjects"); }

    void objectIcon(int spriteIndex, float x, float y, float scale = 1.0f,
                    const C2D_ImageTint* tint = nullptr) {
        const Texture* t = objects();
        if (!t) return;
        t->draw((spriteIndex % 24) * 16, (spriteIndex / 24) * 16, 16, 16, x, y, false, tint, scale);
    }

    void slotIcon(const Slot& s, float x, float y, float scale = 1.0f) {
        if (s.type == SLOT_OBJECT) {
            const ObjectDef* o = data->object(s.id);
            if (o) objectIcon(o->spriteIndex, x, y, scale);
        } else if (s.type == SLOT_TOOL) {
            const ToolDef* td = data->tool(s.id);
            if (!td) return;
            const Texture* t = tex.get(td->texture);
            if (!t) return;
            int cols = std::max(1, t->width / 16);
            t->draw((td->spriteIndex % cols) * 16, (td->spriteIndex / cols) * 16, 16, 16, x, y,
                    false, nullptr, scale);
        }
    }

    std::string slotName(const Slot& s) {
        if (s.type == SLOT_OBJECT) {
            const ObjectDef* o = data->object(s.id);
            return o ? o->name : "?";
        }
        if (s.type == SLOT_TOOL) {
            const ToolDef* t = data->tool(s.id);
            return t ? t->name : "?";
        }
        return "";
    }
};

// ------------------------------------------------------- hoe dirt borders
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

// --------------------------------------------------------------- top screen
struct DrawItem {
    float sortY;
    int kind;   // 0 debris, 1 crop, 2 building, 3 player
    int a, b;
};

static void drawWorld(Renderer& R, Game& g, uint32_t timeMs, float& camXOut, float& camYOut) {
    const TileMap& m = g.map;
    int mapW = m.width() * TILE, mapH = m.height() * TILE;
    float camX = g.px - TOP_W / 2.0f, camY = g.py - 16 - TOP_H / 2.0f;
    camX = std::max(0.0f, std::min(camX, (float)(mapW - TOP_W)));
    camY = std::max(0.0f, std::min(camY, (float)(mapH - TOP_H)));
    if (mapW < TOP_W) camX = (mapW - TOP_W) / 2.0f;
    if (mapH < TOP_H) camY = (mapH - TOP_H) / 2.0f;
    camX = std::floor(camX);
    camY = std::floor(camY);
    camXOut = camX;
    camYOut = camY;

    int x0 = std::max(0, (int)(camX / TILE) - 1), y0 = std::max(0, (int)(camY / TILE) - 1);
    int x1 = std::min(m.width() - 1, (int)((camX + TOP_W) / TILE) + 1);
    int y1 = std::min(m.height() - 1, (int)((camY + TOP_H) / TILE) + 3);   // +tall sprites

    // Resolve tileset textures for this season once.
    std::vector<const Texture*> tsTex(m.tilesets.size());
    for (size_t i = 0; i < m.tilesets.size(); i++)
        tsTex[i] = R.tex.get(m.tilesets[i].textureFor(g.season));

    auto drawLayer = [&](int layer, float depth) {
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++) {
                uint16_t t = m.tile(layer, x, y);
                if (t == NO_TILE) continue;
                int ts = m.tileset(layer, x, y);
                const Texture* tx = tsTex[ts];
                if (!tx) continue;
                t = m.animatedTile(ts, t, timeMs);
                int cols = m.tilesets[ts].columns > 0 ? m.tilesets[ts].columns : tx->width / 16;
                tx->draw((t % cols) * 16, (t / cols) * 16, 16, 16, x * TILE - camX + D(depth), y * TILE - camY);
            }
    };

    drawLayer(L_BACK, -1.0f);

    // Tilled soil
    const Texture* dirt = R.tex.get("TerrainFeatures_hoeDirt");
    if (dirt) {
        C2D_ImageTint wet;
        C2D_PlainImageTint(&wet, C2D_Color32(0x90, 0x80, 0x80, 0xFF), 1.0f);
        bool wetHalf = dirt->width >= 128;
        auto tilled = [&](int x, int y) { return m.inBounds(x, y) && g.soil(x, y).tilled; };
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++) {
                const Soil& s = g.soil(x, y);
                if (!s.tilled) continue;
                int sum = (tilled(x, y - 1) ? 100 : 0) + (tilled(x + 1, y) ? 10 : 0) +
                          (tilled(x, y + 1) ? 500 : 0) + (tilled(x - 1, y) ? 1000 : 0);
                int idx = hoeDirtIndex(sum);
                int sx = (idx % 4) * 16, sy = (idx / 4) * 16;
                float dx = x * TILE - camX + D(-1.0f), dy = y * TILE - camY;
                dirt->draw(sx, sy, 16, 16, dx, dy);
                if (s.watered) {
                    if (wetHalf) dirt->draw(sx + 64, sy, 16, 16, dx, dy);
                    else dirt->draw(sx, sy, 16, 16, dx, dy, false, &wet);
                }
            }
    }

    drawLayer(L_BUILDINGS, -0.5f);

    // Y-sorted sprites
    std::vector<DrawItem> items;
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            if (g.debris(x, y) != DEB_NONE) items.push_back({(float)(y * TILE + 15), 0, x, y});
            if (g.soil(x, y).seed >= 0) items.push_back({(float)(y * TILE + 14), 1, x, y});
        }
    for (size_t i = 0; i < g.data.layout.buildings.size(); i++) {
        auto& b = g.data.layout.buildings[i];
        items.push_back({(float)((b.tileY + b.footH) * TILE), 2, (int)i, 0});
    }
    items.push_back({g.py, 3, 0, 0});
    std::stable_sort(items.begin(), items.end(),
                     [](const DrawItem& a, const DrawItem& b) { return a.sortY < b.sortY; });

    const Texture* crops = R.tex.get("TileSheets_crops");
    C2D_ImageTint deadTint;
    C2D_PlainImageTint(&deadTint, C2D_Color32(0x90, 0x70, 0x40, 0xFF), 1.0f);

    for (auto& it : items) {
        switch (it.kind) {
            case 0: {
                static const int weeds[2] = {674, 675}, stones[2] = {343, 450}, twigs[2] = {294, 295};
                int v = g.debrisVariant(it.a, it.b) & 1;
                DebrisType d = g.debris(it.a, it.b);
                int idx = d == DEB_WEED ? weeds[v] : d == DEB_STONE ? stones[v] : twigs[v];
                R.objectIcon(idx, it.a * TILE - camX, it.b * TILE - camY);
                break;
            }
            case 1: {
                if (!crops) break;
                const Soil& s = g.soil(it.a, it.b);
                const CropDef* c = g.data.cropForSeed(s.seed);
                if (!c) break;
                int frame = g.cropFrame(it.a, it.b);
                int sx = (c->spriteIndex % 2) * 128 + frame * 16, sy = (c->spriteIndex / 2) * 32;
                crops->draw(sx, sy, 16, 32, it.a * TILE - camX, it.b * TILE - 16 - camY,
                            (it.a + it.b) & 1 ? true : false, s.dead ? &deadTint : nullptr);
                break;
            }
            case 2: {
                auto& b = g.data.layout.buildings[it.a];
                const Texture* t = R.tex.get(b.texture);
                if (!t) break;
                float footPxW = b.footW * TILE;
                float dx = b.tileX * TILE + (footPxW - b.srcW) / 2.0f - camX;
                float dy = (b.tileY + b.footH) * TILE - b.srcH - camY;
                t->draw(b.srcX, b.srcY, b.srcW, b.srcH, dx, dy);
                break;
            }
            case 3: {
                const Texture* body = R.tex.get("Characters_Farmer_farmer_base");
                float dx = std::floor(g.px - 8 - camX), dy = std::floor(g.py - 30 - camY);
                static const int dirRow[4] = {0, 1, 2, 1};   // down, right, up, left(=right flipped)
                bool flip = g.facing == FACE_LEFT;
                int step = 0;
                if (g.moving) {
                    static const int cycle[4] = {0, 1, 0, 2};
                    step = cycle[(int)(g.walkTime * 8) & 3];
                }
                int frame = dirRow[g.facing] * 6 + step;
                if (body) body->draw((frame % 6) * 16, (frame / 6) * 32, 16, 32, dx, dy, flip);
                const Texture* hair = R.tex.get("Characters_Farmer_hairstyles");
                if (hair) {
                    C2D_ImageTint ht;
                    C2D_PlainImageTint(&ht, C2D_Color32(0x8A, 0x4E, 0x26, 0xFF), 1.0f);
                    int style = 0;
                    int bob = (step != 0) ? 1 : 0;
                    hair->draw((style % 8) * 16, (style / 8) * 96 + dirRow[g.facing] * 32, 16, 32,
                               dx, dy + bob, flip, &ht);
                }
                // tool swing: flash the tool icon over the target tile
                if (g.toolTimer > 0 && g.inv[g.selected].type == SLOT_TOOL) {
                    int tx, ty;
                    g.targetTile(tx, ty);
                    R.slotIcon(g.inv[g.selected], tx * TILE - camX, ty * TILE - camY - 6);
                }
                break;
            }
        }
    }

    drawLayer(L_FRONT, 1.0f);
    drawLayer(L_ALWAYSFRONT, 1.5f);

    // target cursor
    const Slot& sel = g.inv[g.selected];
    if (!sel.empty() && g.toolTimer <= 0) {
        int tx, ty;
        g.targetTile(tx, ty);
        float cx = tx * TILE - camX + D(-1.0f), cy = ty * TILE - camY;
        u32 c = C2D_Color32(0xFF, 0xFF, 0xFF, 0x50);
        C2D_DrawRectSolid(cx, cy, 0.5f, 16, 1, c);
        C2D_DrawRectSolid(cx, cy + 15, 0.5f, 16, 1, c);
        C2D_DrawRectSolid(cx, cy, 0.5f, 1, 16, c);
        C2D_DrawRectSolid(cx + 15, cy, 0.5f, 1, 16, c);
    }
}

static void drawWeatherAndLight(Game& g, uint32_t timeMs) {
    if (g.raining) {
        u32 c = C2D_Color32(0xA0, 0xB8, 0xFF, 0x90);
        for (int i = 0; i < 60; i++) {
            uint32_t h = (uint32_t)i * 2654435761u;
            float x = (float)((h >> 7) % 440) - ((timeMs / 6 + i * 37) % 240) * 0.35f;
            float y = (float)(((h >> 3) + timeMs / 3) % 260) - 10;
            C2D_DrawLine(x + D(2.0f), y, c, x - 4 + D(2.0f), y + 10, c, 1.0f, 0.5f);
        }
        C2D_DrawRectSolid(0, 0, 0.5f, TOP_W, TOP_H, C2D_Color32(0x20, 0x30, 0x50, 0x30));
    }
    // dusk from 6pm, dark by 8pm
    int t = g.timeOfDay;
    if (t > 18 * 60) {
        float k = std::min(1.0f, (t - 18 * 60) / 120.0f);
        C2D_DrawRectSolid(0, 0, 0.5f, TOP_W, TOP_H, C2D_Color32(0x10, 0x10, 0x40, (u8)(k * 120)));
    }
}

static void drawHud(Renderer& R, Game& g) {
    g_uiDepth = 3.0f;
    // Clock panel
    float x = TOP_W - 104, y = 4;
    box(x, y, 100, 52, COL_PANEL, COL_BORDER);
    static const char* days[7] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
    char buf[48];
    snprintf(buf, sizeof buf, "%s %d", days[g.weekday()], g.day);
    text(x + 50, y + 3, 0.5f, COL_TEXT, buf, 1);
    text(x + 50, y + 18, 0.5f, COL_TEXT, g.timeString(), 1);
    snprintf(buf, sizeof buf, "%dg", g.money);
    text(x + 94, y + 34, 0.45f, COL_TEXT, buf, 2);
    text(x + 6, y + 34, 0.45f, COL_TEXT, seasonShort(g.season));

    // Energy bar
    float ex = TOP_W - 16, ey = TOP_H - 74;
    box(ex - 2, ey - 2, 14, 72, COL_PANEL, COL_BORDER);
    float hz = D(3.0f);
    C2D_DrawRectSolid(ex + 1 + hz, ey + 1, 0.5f, 8, 66, COL_ENERGY_BG);
    float k = (float)g.energy / MAX_ENERGY;
    u32 ec = k > 0.5f ? COL_ENERGY
                      : (k > 0.2f ? C2D_Color32(0xF0, 0xC0, 0x20, 0xFF) : C2D_Color32(0xE0, 0x30, 0x20, 0xFF));
    C2D_DrawRectSolid(ex + 1 + hz, ey + 1 + 66 * (1 - k), 0.5f, 8, 66 * k, ec);
    text(ex + 5, ey + 66, 0.4f, COL_TEXT, "E", 1);

    // Watering can level when held
    const Slot& sel = g.inv[g.selected];
    if (sel.type == SLOT_TOOL) {
        const ToolDef* td = g.data.tool(sel.id);
        if (td && td->kind == TOOL_CAN) {
            float wx = TOP_W - 34, wy = TOP_H - 74;
            box(wx - 2, wy - 2, 14, 72, COL_PANEL, COL_BORDER);
            float wk = (float)g.water / CAN_CAPACITY;
            C2D_DrawRectSolid(wx + 1 + hz, wy + 1 + 66 * (1 - wk), 0.5f, 8, 66 * wk, COL_WATER);
        }
    }

    // Held item name + toast
    if (!sel.empty()) {
        std::string n = R.slotName(sel);
        if (sel.type == SLOT_OBJECT) n += " x" + std::to_string(sel.count);
        text(6, TOP_H - 18, 0.45f, C2D_Color32(0xFF, 0xFF, 0xFF, 0xFF), n);
    }
    if (g.toastTime > 0 && !g.toast.empty()) {
        box(TOP_W / 2 - 110, 6, 220, 22, COL_PANEL, COL_BORDER);
        text(TOP_W / 2, 9, 0.5f, COL_TEXT, g.toast, 1);
    }
}

// ------------------------------------------------------------ bottom screen
enum Tab { TAB_ITEMS = 0, TAB_SHOP, TAB_MENU, TAB_COUNT };

struct Ui {
    int tab = TAB_ITEMS;
    int picked = -1;          // inventory slot picked up for moving
    int shopScroll = 0;
    std::string status;
    float statusTime = 0;
    bool quit = false;
    bool showSummary = false;
};

static const int SLOT_PX = 24, GRID_X = 16, GRID_Y = 34;

static bool hit(int tx, int ty, float x, float y, float w, float h) {
    return tx >= x && ty >= y && tx < x + w && ty < y + h;
}

static void drawTabs(Ui& ui) {
    static const char* names[TAB_COUNT] = {"Items", "Seeds", "Menu"};
    for (int i = 0; i < TAB_COUNT; i++) {
        float x = 4 + i * 80;
        box(x, 2, 76, 24, i == ui.tab ? COL_PANEL : COL_PANEL_DARK, COL_BORDER);
        text(x + 38, 5, 0.55f, COL_TEXT, names[i], 1);
    }
}

static void drawItemsTab(Renderer& R, Game& g, Ui& ui) {
    for (int i = 0; i < INV_SIZE; i++) {
        int row = i / HOTBAR, col = i % HOTBAR;
        float x = GRID_X + col * SLOT_PX, y = GRID_Y + row * (SLOT_PX + (row == 0 ? 6 : 0)) + (row > 0 ? 6 : 0);
        u32 border = i == ui.picked ? COL_SLOT_PICK : (i == g.selected ? COL_SLOT_SEL : COL_BORDER);
        box(x, y, SLOT_PX, SLOT_PX, COL_SLOT, border, i == g.selected || i == ui.picked ? 2 : 1);
        const Slot& s = g.inv[i];
        if (!s.empty()) {
            R.slotIcon(s, x + 4, y + 4);
            if (s.type == SLOT_OBJECT && s.count > 1)
                text(x + SLOT_PX - 1, y + SLOT_PX - 11, 0.35f, COL_TEXT, std::to_string(s.count), 2);
        }
    }
    // Info line
    const Slot& sel = g.inv[ui.picked >= 0 ? ui.picked : g.selected];
    std::string info = R.slotName(sel);
    if (sel.type == SLOT_OBJECT) {
        const ObjectDef* o = g.data.object(sel.id);
        if (o && o->price > 0) info += "   sells " + std::to_string(o->price) + "g";
        const CropDef* c = g.data.cropForSeed(sel.id);
        if (c) info += "   " + std::to_string(c->totalDays()) + " days";
    }
    box(8, 146, 304, 22, COL_PANEL, COL_BORDER, 1);
    text(14, 149, 0.48f, COL_TEXT, info);

    // Buttons
    box(8, 174, 96, 28, COL_PANEL_DARK, COL_BORDER);
    text(56, 180, 0.5f, COL_TEXT, "Trash", 1);
    box(112, 174, 96, 28, COL_PANEL_DARK, COL_BORDER);
    text(160, 180, 0.5f, COL_TEXT, "Eat", 1);
    box(216, 174, 96, 28, COL_PANEL_DARK, COL_BORDER);
    text(264, 180, 0.5f, COL_TEXT, "Bed", 1);

    char buf[64];
    snprintf(buf, sizeof buf, "%s %d, Y%d   %dg   Energy %d", seasonShort(g.season), g.day, g.year,
             g.money, g.energy);
    text(160, 212, 0.45f, C2D_Color32(0xFF, 0xE8, 0xB0, 0xFF), buf, 1);
    text(160, 226, 0.38f, C2D_Color32(0xC8, 0xA8, 0x80, 0xFF),
         "Tap = select / move   A use   B walk   L/R hotbar", 1);
}

static int slotAt(int tx, int ty) {
    for (int i = 0; i < INV_SIZE; i++) {
        int row = i / HOTBAR, col = i % HOTBAR;
        float x = GRID_X + col * SLOT_PX, y = GRID_Y + row * (SLOT_PX + (row == 0 ? 6 : 0)) + (row > 0 ? 6 : 0);
        if (hit(tx, ty, x, y, SLOT_PX, SLOT_PX)) return i;
    }
    return -1;
}

static void touchItemsTab(Game& g, Ui& ui, int tx, int ty) {
    int s = slotAt(tx, ty);
    if (s >= 0) {
        if (ui.picked < 0) {
            if (s < HOTBAR) g.selectSlot(s);
            if (!g.inv[s].empty()) ui.picked = s;
        } else if (ui.picked == s) {
            ui.picked = -1;
        } else {
            g.swapSlots(ui.picked, s);
            if (s < HOTBAR) g.selectSlot(s);
            ui.picked = -1;
        }
        return;
    }
    int target = ui.picked >= 0 ? ui.picked : g.selected;
    if (hit(tx, ty, 8, 174, 96, 28)) {
        if (g.trashSlot(target)) ui.picked = -1;
    } else if (hit(tx, ty, 112, 174, 96, 28)) {
        Slot& sl = g.inv[target];
        const ObjectDef* o = sl.type == SLOT_OBJECT ? g.data.object(sl.id) : nullptr;
        if (o && o->edibility > 0 && o->edibility != -300) {
            int gain = (int)std::ceil(o->edibility * 2.5f);
            g.energy = std::min(MAX_ENERGY, g.energy + gain);
            g.toast = "Ate " + o->name;
            g.toastTime = 2;
            if (--sl.count <= 0) { sl = Slot(); ui.picked = -1; }
        }
    } else if (hit(tx, ty, 216, 174, 96, 28)) {
        g.goToBed();
    }
}

static void drawShopTab(Renderer& R, Game& g, Ui& ui) {
    auto stock = g.shopStock();
    box(4, 30, 312, 22, COL_PANEL, COL_BORDER, 1);
    char buf[64];
    snprintf(buf, sizeof buf, "Seed stall (%s)   You have %dg", seasonName(g.season), g.money);
    text(160, 33, 0.48f, COL_TEXT, buf, 1);
    const int rows = 7;
    ui.shopScroll = std::max(0, std::min(ui.shopScroll, (int)stock.size() - rows));
    for (int r = 0; r < rows; r++) {
        int i = ui.shopScroll + r;
        if (i >= (int)stock.size()) break;
        float y = 56 + r * 24;
        box(4, y, 312, 22, COL_SLOT, COL_BORDER, 1);
        const ObjectDef* o = g.data.object(stock[i].seedId);
        if (o) {
            R.objectIcon(o->spriteIndex, 8, y + 3);
            text(28, y + 3, 0.47f, COL_TEXT, o->name);
        }
        snprintf(buf, sizeof buf, "%dg", stock[i].price);
        text(206, y + 3, 0.47f, COL_TEXT, buf, 2);
        box(212, y + 2, 46, 18, COL_PANEL_DARK, COL_BORDER, 1);
        text(235, y + 3, 0.45f, COL_TEXT, "Buy 1", 1);
        box(262, y + 2, 50, 18, COL_PANEL_DARK, COL_BORDER, 1);
        text(287, y + 3, 0.45f, COL_TEXT, "Buy 5", 1);
    }
    if (stock.size() > (size_t)rows) {
        box(4, 226, 60, 14, COL_PANEL_DARK, COL_BORDER, 1);
        text(34, 226, 0.4f, COL_TEXT, "Up", 1);
        box(256, 226, 60, 14, COL_PANEL_DARK, COL_BORDER, 1);
        text(286, 226, 0.4f, COL_TEXT, "Down", 1);
    }
    if (stock.empty()) text(160, 100, 0.5f, COL_PANEL, "Nothing grows this season.", 1);
}

static void touchShopTab(Game& g, Ui& ui, int tx, int ty) {
    auto stock = g.shopStock();
    if (hit(tx, ty, 4, 226, 60, 14)) { ui.shopScroll--; return; }
    if (hit(tx, ty, 256, 226, 60, 14)) { ui.shopScroll++; return; }
    for (int r = 0; r < 7; r++) {
        int i = ui.shopScroll + r;
        if (i >= (int)stock.size()) break;
        float y = 56 + r * 24;
        if (hit(tx, ty, 212, y + 2, 46, 18)) g.buy(stock[i].seedId, 1);
        else if (hit(tx, ty, 262, y + 2, 50, 18)) g.buy(stock[i].seedId, 5);
    }
}

static void drawMenuTab(Game& g, Ui& ui) {
    const char* labels[3] = {"Save", "Go to bed (ends day)", "Save & quit"};
    for (int i = 0; i < 3; i++) {
        box(40, 40 + i * 44, 240, 36, COL_PANEL_DARK, COL_BORDER);
        text(160, 49 + i * 44, 0.6f, COL_TEXT, labels[i], 1);
    }
    if (ui.statusTime > 0) text(160, 176, 0.5f, COL_PANEL, ui.status, 1);
    text(160, 210, 0.4f, C2D_Color32(0xC8, 0xA8, 0x80, 0xFF),
         "The game also saves every time you sleep.", 1);
    (void)g;
}

static void touchMenuTab(Game& g, Ui& ui, int tx, int ty) {
    std::string path = std::string(ROOT) + "save.sav";
    for (int i = 0; i < 3; i++)
        if (hit(tx, ty, 40, 40 + i * 44, 240, 36)) {
            if (i == 0 || i == 2) {
                ui.status = g.save(path) ? "Saved." : "Save FAILED (SD card?)";
                ui.statusTime = 3;
                if (i == 2) ui.quit = true;
            } else {
                g.goToBed();
            }
        }
}

static void drawSummary(Game& g) {
    box(20, 40, 280, 150, COL_PANEL, COL_BORDER, 3);
    text(160, 52, 0.7f, COL_TEXT, g.passedOut ? "You passed out..." : "Good morning!", 1);
    char buf[64];
    snprintf(buf, sizeof buf, "Shipped yesterday: %dg", g.lastEarnings);
    text(160, 88, 0.55f, COL_TEXT, buf, 1);
    snprintf(buf, sizeof buf, "%s %d, Year %d", seasonName(g.season), g.day, g.year);
    text(160, 110, 0.55f, COL_TEXT, buf, 1);
    text(160, 130, 0.5f, COL_TEXT, g.raining ? "It's raining today." : "Clear skies.", 1);
    text(160, 164, 0.45f, COL_BORDER, "Tap or press A", 1);
}

// --------------------------------------------------------------------- main
static void fatalScreen(C3D_RenderTarget* top, const std::string& msg) {
    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & KEY_START) break;
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        C2D_TextBufClear(g_textBuf);
        C2D_TargetClear(top, COL_BG);
        C2D_SceneBegin(top);
        text(200, 40, 0.7f, C2D_Color32(0xFF, 0xD0, 0x80, 0xFF), "stardew3ds", 1);
        C2D_Text t;
        C2D_TextParse(&t, g_textBuf, msg.c_str());
        C2D_DrawText(&t, C2D_WithColor | C2D_WordWrap, 16, 80, 0.5f, 0.5f, 0.5f,
                     C2D_Color32(0xFF, 0xFF, 0xFF, 0xFF), 368.0f);
        text(200, 210, 0.45f, C2D_Color32(0xC0, 0xC0, 0xC0, 0xFF), "Press START to exit", 1);
        C3D_FrameEnd(0);
    }
}

int main() {
    gfxInitDefault();
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(12288);   // both eyes + bottom screen per frame
    C2D_Prepare();
    C2D_SetTintMode(C2D_TintMult);
    initColors();
    g_textBuf = C2D_TextBufNew(4096);

    C3D_RenderTarget* top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    C3D_RenderTarget* topR = C2D_CreateScreenTarget(GFX_TOP, GFX_RIGHT);
    C3D_RenderTarget* bot = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    gfxSet3D(true);

    GameData data;
    TileMap map;
    std::string err;
    std::string root = ROOT;
    if (!data.load(root, &err) || !map.load(root + "maps/Farm.smap", &err)) {
        fatalScreen(top, "Couldn't load game files: " + err +
                             "\n\nRun tools/convert.py on your own Stardew Valley "
                             "'Content (unpacked)' folder and copy the result to "
                             "sdmc:/3ds/stardew3ds/");
        C2D_TextBufDelete(g_textBuf);
        C2D_Fini();
        C3D_Fini();
        gfxExit();
        return 0;
    }

    Game game(data, map);
    std::string savePath = root + "save.sav";
    if (!game.load(savePath)) game.newGame((uint32_t)osGetTime());

    Renderer R;
    R.data = &data;
    R.loadedSeason = game.season;
    Ui ui;

    u64 last = osGetTime();
    float holdA = 0;
    while (aptMainLoop() && !ui.quit) {
        hidScanInput();
        u32 kDown = hidKeysDown(), kHeld = hidKeysHeld();
        u64 now = osGetTime();
        float dt = std::min(0.05f, (now - last) / 1000.0f);
        last = now;

        // ---- input
        Input in;
        circlePosition cp;
        hidCircleRead(&cp);
        in.mx = cp.dx / 150.0f;
        in.my = -cp.dy / 150.0f;
        if (kHeld & KEY_DLEFT) in.mx = -1;
        if (kHeld & KEY_DRIGHT) in.mx = 1;
        if (kHeld & KEY_DUP) in.my = -1;
        if (kHeld & KEY_DDOWN) in.my = 1;
        in.walk = kHeld & KEY_B;
        in.prev = kDown & KEY_L;
        in.next = kDown & KEY_R;
        bool aDown = kDown & (KEY_A | KEY_Y);
        if (kHeld & (KEY_A | KEY_Y)) holdA += dt; else holdA = 0;
        in.action = aDown || (holdA > 0.45f && game.toolTimer <= 0);   // hold to keep swinging

        if (kDown & KEY_START) ui.tab = TAB_MENU;
        if (kDown & KEY_SELECT) ui.tab = (ui.tab + 1) % TAB_COUNT;

        if (ui.showSummary) {
            if (kDown & (KEY_A | KEY_TOUCH)) ui.showSummary = false;
            in = Input();
            dt = 0;
        }

        if (kDown & KEY_TOUCH) {
            touchPosition tp;
            hidTouchRead(&tp);
            if (!ui.showSummary) {
                if (tp.py < 28) {
                    ui.tab = std::min(TAB_COUNT - 1, tp.px / 80);
                    ui.picked = -1;
                } else if (ui.tab == TAB_ITEMS) {
                    touchItemsTab(game, ui, tp.px, tp.py);
                } else if (ui.tab == TAB_SHOP) {
                    touchShopTab(game, ui, tp.px, tp.py);
                } else {
                    touchMenuTab(game, ui, tp.px, tp.py);
                }
            }
        }

        game.update(dt, in);
        if (ui.statusTime > 0) ui.statusTime -= dt;

        if (game.dayEnded) {
            game.dayEnded = false;
            game.save(savePath);
            ui.showSummary = true;
            ui.tab = TAB_ITEMS;
            if (game.season != R.loadedSeason) {   // free last season's tilesheets
                R.tex.clear();
                R.loadedSeason = game.season;
            }
        }
        if (!ui.showSummary) game.passedOut = false;

        // ---- draw
        uint32_t timeMs = (uint32_t)(now & 0xFFFFFFFF);
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        C2D_TextBufClear(g_textBuf);

        // Top screen: once per eye. With the slider down only the left eye is drawn.
        float slider = osGet3DSliderState();
        bool stereo = slider > 0.01f;
        const float MAX_SHIFT = 2.0f;   // px per depth unit at full slider
        for (int eye = 0; eye < (stereo ? 2 : 1); eye++) {
            C3D_RenderTarget* t = eye == 0 ? top : topR;
            g_eye = stereo ? (eye == 0 ? -slider : slider) * MAX_SHIFT : 0.0f;
            C2D_TargetClear(t, C2D_Color32(0, 0, 0, 0xFF));
            C2D_SceneBegin(t);
            float camX, camY;
            g_uiDepth = 0.0f;
            drawWorld(R, game, timeMs, camX, camY);
            drawWeatherAndLight(game, timeMs);
            drawHud(R, game);
        }
        g_eye = 0.0f;
        g_uiDepth = 0.0f;

        C2D_TargetClear(bot, COL_BG);
        C2D_SceneBegin(bot);
        if (ui.showSummary) {
            drawSummary(game);
        } else {
            drawTabs(ui);
            if (ui.tab == TAB_ITEMS) drawItemsTab(R, game, ui);
            else if (ui.tab == TAB_SHOP) drawShopTab(R, game, ui);
            else drawMenuTab(game, ui);
        }
        C3D_FrameEnd(0);
    }

    game.save(savePath);
    R.tex.clear();
    C2D_TextBufDelete(g_textBuf);
    C2D_Fini();
    C3D_Fini();
    gfxExit();
    return 0;
}
