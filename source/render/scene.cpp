#include "scene.h"

#include <algorithm>
#include <cmath>
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

static void box(Canvas& c, float x, float y, float w, float h, uint32_t fill, uint32_t border,
                float bw = 2) {
    c.rect(x, y, w, h, border);
    c.rect(x + bw, y + bw, w - 2 * bw, h - 2 * bw, fill);
}

// ---------------------------------------------------------------- icons
static void objectIcon(Canvas& c, int spriteIndex, float x, float y) {
    c.image("Maps_springobjects", (spriteIndex % 24) * 16, (spriteIndex / 24) * 16, 16, 16, x, y);
}

static void slotIcon(Canvas& c, const Game& g, const Slot& s, float x, float y) {
    if (s.type == SLOT_OBJECT) {
        const ObjectDef* o = g.data.object(s.id);
        if (o) objectIcon(c, o->spriteIndex, x, y);
    } else if (s.type == SLOT_TOOL) {
        const ToolDef* td = g.data.tool(s.id);
        if (!td) return;
        int w = c.texWidth(td->texture);
        int cols = std::max(1, w / 16);
        c.image(td->texture, (td->spriteIndex % cols) * 16, (td->spriteIndex / cols) * 16, 16, 16, x, y);
    }
}

static std::string slotName(const Game& g, const Slot& s) {
    if (s.type == SLOT_OBJECT) {
        const ObjectDef* o = g.data.object(s.id);
        return o ? o->name : "?";
    }
    if (s.type == SLOT_TOOL) {
        const ToolDef* t = g.data.tool(s.id);
        return t ? t->name : "?";
    }
    return "";
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
static void drawFarmer(Canvas& c, const Game& g, float sx, float sy, int facing, bool moving, float walkTime,
                       int frameOverride) {
    float dx = std::floor(sx - 8), dy = std::floor(sy - 30);
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
    const Outfit& o = g.data.layout.outfit;
    const std::string base = "Characters_Farmer_farmer_base";
    c.image(base, fx, fy, 16, 32, dx, dy, flip);
    if (c.texWidth("Characters_Farmer_pants") > 0) {
        int px0 = (o.pants % 10) * 192, py0 = (o.pants / 10) * 688;
        c.image("Characters_Farmer_pants", px0 + fx, py0 + fy, 16, 32, dx, dy, flip,
                RGBA(o.pantsR, o.pantsG, o.pantsB));
    }
    if (c.texWidth("Characters_Farmer_shirts") > 0) {
        static const int shirtRow[4] = {0, 1, 3, 2};
        int shx = (o.shirt % 16) * 8, shy = (o.shirt / 16) * 32 + shirtRow[facing & 3] * 8;
        c.image("Characters_Farmer_shirts", shx, shy, 8, 8, dx + 4, dy + 15 + bob);
    }
    if (c.texWidth(base) >= 192) c.image(base, fx + 96, fy, 16, 32, dx, dy, flip);
    int hs = o.hair;
    c.image("Characters_Farmer_hairstyles", (hs % 8) * 16, (hs / 8) * 96 + dirRow[facing & 3] * 32, 16, 32,
            dx, dy + bob, flip, RGBA(o.hairR, o.hairG, o.hairB));
}

// ================================================================ top screen
struct DrawItem {
    float sortY;
    int kind;   // 0 debris, 1 crop, 2 building, 3 player, 4 villager, 5 furniture, 6 cutscene actor
    int a, b;
};

static void drawWorld(Canvas& c, Game& g, uint32_t timeMs) {
    const TileMap& m = g.map();
    int mapW = m.width() * TILE, mapH = m.height() * TILE;
    const Cutscene& cs = g.cutscene;
    float camX = g.px - TOP_W / 2.0f, camY = g.py - 16 - TOP_H / 2.0f;
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

    int x0 = std::max(0, (int)std::floor(camX / TILE) - 1), y0 = std::max(0, (int)std::floor(camY / TILE) - 1);
    int x1 = std::min(m.width() - 1, (int)((camX + TOP_W) / TILE) + 1);
    int y1 = std::min(m.height() - 1, (int)((camY + TOP_H) / TILE) + 3);

    std::vector<std::string> tsTex(m.tilesets.size());
    std::vector<int> tsCols(m.tilesets.size());
    for (size_t i = 0; i < m.tilesets.size(); i++) {
        tsTex[i] = m.tilesets[i].textureFor(g.season);
        tsCols[i] = m.tilesets[i].columns > 0 ? m.tilesets[i].columns : c.texWidth(tsTex[i]) / 16;
    }

    auto drawLayer = [&](int layer, float depth) {
        float sh = c.shift(depth);
        for (int y = y0; y <= y1; y++)
            for (int x = x0; x <= x1; x++) {
                uint16_t t = m.tile(layer, x, y);
                if (t == NO_TILE) continue;
                int ts = m.tileset(layer, x, y);
                int cols = tsCols[ts];
                if (cols <= 0) continue;
                t = m.animatedTile(ts, t, timeMs);
                c.image(tsTex[ts], (t % cols) * 16, (t / cols) * 16, 16, 16,
                        x * TILE - camX + sh, y * TILE - camY);
            }
    };

    drawLayer(L_BACK, -1.0f);

    // Tilled soil
    if (g.location().isFarm) {
        bool wetHalf = c.texWidth("TerrainFeatures_hoeDirt") >= 128;
        auto tilled = [&](int x, int y) { return m.inBounds(x, y) && g.soil(x, y).tilled; };
        float sh = c.shift(-1.0f);
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

    drawLayer(L_BUILDINGS, -0.5f);

    std::vector<DrawItem> items;
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            if (g.debris(x, y) != DEB_NONE) items.push_back({(float)(y * TILE + 15), 0, x, y});
            if (g.soil(x, y).seed >= 0) items.push_back({(float)(y * TILE + 14), 1, x, y});
        }
    if (g.location().isFarm)
        for (size_t i = 0; i < g.data.layout.buildings.size(); i++) {
            auto& b = g.data.layout.buildings[i];
            items.push_back({(float)((b.tileY + b.footH) * TILE), 2, (int)i, 0});
        }
    if (cs.active) {
        for (size_t i = 0; i < cs.actors.size(); i++)
            if (cs.actors[i].visible) items.push_back({cs.actors[i].y, 6, (int)i, 0});
    } else {
        items.push_back({g.py, 3, 0, 0});
        for (size_t i = 0; i < g.npcs.size(); i++)
            if (g.npcs[i].loc == g.location().name) items.push_back({g.npcs[i].y, 4, (int)i, 0});
    }
    for (size_t i = 0; i < g.data.layout.furniture.size(); i++) {
        const FurniturePlacement& fp = g.data.layout.furniture[i];
        if (fp.map != g.location().name) continue;
        auto fd = g.data.furniture.find(fp.id);
        int bh = fd != g.data.furniture.end() ? fd->second.boxH : 1;
        items.push_back({(float)((fp.y + bh) * TILE) - 1, 5, (int)i, 0});
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
                c.image("TileSheets_crops", sx, sy, 16, 32, it.a * TILE - camX,
                        it.b * TILE - 16 - camY, ((it.a + it.b) & 1) != 0,
                        s.dead ? RGBA(0x90, 0x70, 0x40) : 0);
                break;
            }
            case 2: {
                auto& b = g.data.layout.buildings[it.a];
                float dx = b.tileX * TILE + (b.footW * TILE - b.srcW) / 2.0f - camX;
                float dy = (b.tileY + b.footH) * TILE - b.srcH - camY;
                c.image(b.texture, b.srcX, b.srcY, b.srcW, b.srcH, dx, dy);
                break;
            }
            case 3: {
                drawFarmer(c, g, g.px - camX, g.py - camY, g.facing, g.moving, g.walkTime, -1);
                if (g.toolTimer > 0 && g.inv[g.selected].type == SLOT_TOOL) {
                    int tx, ty;
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
                    drawFarmer(c, g, ax, ay, a.facing, a.moving, a.walkTime, frame);
                } else {
                    int f = frame >= 0 ? frame : (a.facing & 3) * 4 + (a.moving ? ((int)(a.walkTime * 6)) & 3 : 0);
                    c.image("Characters_" + a.name, (f % 4) * 16, (f / 4) * 32, 16, 32, std::floor(ax - 8),
                            std::floor(ay - 30));
                }
                if (a.emoteT > 0 && a.emote >= 0) {
                    int ef = a.emote + ((int)((1.0f - a.emoteT) * 8) & 3);
                    c.image("LooseSprites_emotes", (ef % 4) * 16, (ef / 4) * 16, 16, 16, std::floor(ax - 8),
                            std::floor(ay - 50));
                }
                if (a.bubbleT > 0 && !a.bubble.empty()) {
                    float tw = c.textWidth(a.bubble, 0.42f) + 10;
                    box(c, ax - tw / 2, ay - 52, tw, 18, RGBA(0xFF, 0xFF, 0xFF), RGBA(0x60, 0x40, 0x30), 1);
                    c.text(ax, ay - 50, 0.42f, COL_TEXT, a.bubble, ALIGN_CENTER);
                }
                break;
            }
            case 5: {
                const FurniturePlacement& fp = g.data.layout.furniture[it.a];
                auto fd = g.data.furniture.find(fp.id);
                if (fd == g.data.furniture.end()) break;
                int sw = fd->second.spriteW * 16, sh = fd->second.spriteH * 16;
                int sx = (fp.id % 32) * 16, sy = (fp.id / 32) * 16;
                float bottom = (fp.y + fd->second.boxH) * TILE;
                c.image("TileSheets_furniture", sx, sy, sw, sh, fp.x * TILE - camX, bottom - sh - camY);
                break;
            }
            case 4: {
                const NpcState& n = g.npcs[it.a];
                int step = n.moving ? ((int)(n.walkTime * 6)) & 3 : 0;
                int frame = (n.facing & 3) * 4 + step;
                c.image("Characters_" + n.def->name, (frame % 4) * 16, (frame / 4) * 32, 16, 32,
                        std::floor(n.x - 8 - camX), std::floor(n.y - 30 - camY));
                break;
            }
        }
    }

    drawLayer(L_FRONT, 1.0f);
    drawLayer(L_ALWAYSFRONT, 1.5f);

    const Slot& sel = g.inv[g.selected];
    if (!sel.empty() && g.toolTimer <= 0 && !cs.active) {
        int tx, ty;
        g.targetTile(tx, ty);
        float cx = tx * TILE - camX + c.shift(-1.0f), cy = ty * TILE - camY;
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
        uint32_t col = RGBA(0xA0, 0xB8, 0xFF, 0x90);
        float sh = c.shift(2.0f);
        for (int i = 0; i < 60; i++) {
            uint32_t h = (uint32_t)i * 2654435761u;
            float x = (float)((h >> 7) % 440) - ((timeMs / 6 + i * 37) % 240) * 0.35f + sh;
            float y = (float)(((h >> 3) + timeMs / 3) % 260) - 10;
            c.line(x, y, x - 4, y + 10, col, 1.0f);
        }
        c.rect(0, 0, TOP_W, TOP_H, RGBA(0x20, 0x30, 0x50, 0x30));
    }
    int t = g.timeOfDay;
    if (t > 18 * 60) {
        float k = std::min(1.0f, (t - 18 * 60) / 120.0f);
        c.rect(0, 0, TOP_W, TOP_H, RGBA(0x10, 0x10, 0x40, (uint8_t)(k * 120)));
    }
}

static void drawHud(Canvas& c, Game& g) {
    float hs = c.shift(3.0f);
    float x = TOP_W - 104 + hs, y = 4;
    box(c, x, y, 100, 52, COL_PANEL, COL_BORDER);
    static const char* days[7] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
    char buf[48];
    snprintf(buf, sizeof buf, "%s %d", days[g.weekday()], g.day);
    c.text(x + 50, y + 3, 0.5f, COL_TEXT, buf, ALIGN_CENTER);
    c.text(x + 50, y + 18, 0.5f, COL_TEXT, g.timeString(), ALIGN_CENTER);
    snprintf(buf, sizeof buf, "%dg", g.money);
    c.text(x + 94, y + 34, 0.45f, COL_TEXT, buf, ALIGN_RIGHT);
    c.text(x + 6, y + 34, 0.45f, COL_TEXT, seasonShort(g.season));

    float ex = TOP_W - 16 + hs, ey = TOP_H - 74;
    box(c, ex - 2, ey - 2, 14, 72, COL_PANEL, COL_BORDER);
    c.rect(ex + 1, ey + 1, 8, 66, COL_ENERGY_BG);
    float k = (float)g.energy / MAX_ENERGY;
    uint32_t ec = k > 0.5f ? COL_ENERGY : (k > 0.2f ? RGBA(0xF0, 0xC0, 0x20) : RGBA(0xE0, 0x30, 0x20));
    c.rect(ex + 1, ey + 1 + 66 * (1 - k), 8, 66 * k, ec);
    c.text(ex + 5, ey + 66, 0.4f, COL_TEXT, "E", ALIGN_CENTER);

    const Slot& sel = g.inv[g.selected];
    if (sel.type == SLOT_TOOL) {
        const ToolDef* td = g.data.tool(sel.id);
        if (td && td->kind == TOOL_CAN) {
            float wx = TOP_W - 34 + hs, wy = TOP_H - 74;
            box(c, wx - 2, wy - 2, 14, 72, COL_PANEL, COL_BORDER);
            float wk = (float)g.water / CAN_CAPACITY;
            c.rect(wx + 1, wy + 1 + 66 * (1 - wk), 8, 66 * wk, COL_WATER);
        }
    }
    if (!sel.empty()) {
        std::string n = slotName(g, sel);
        if (sel.type == SLOT_OBJECT) n += " x" + std::to_string(sel.count);
        c.text(6 + hs, TOP_H - 18, 0.45f, COL_WHITE, n);
    }
    if (g.toastTime > 0 && !g.toast.empty()) {
        // centred in the space left of the clock
        float cx = (TOP_W - 108) / 2.0f + hs;
        box(c, cx - 140, 6, 280, 22, COL_PANEL, COL_BORDER);
        c.text(cx, 9, 0.5f, COL_TEXT, g.toast, ALIGN_CENTER);
    }
}

void drawTop(Canvas& c, Game& g, Ui& ui, uint32_t timeMs) {
    (void)ui;
    c.rect(0, 0, TOP_W, TOP_H, RGBA(0, 0, 0));
    drawWorld(c, g, timeMs);
    drawWeatherAndLight(c, g, timeMs);
    if (g.cutscene.active) {
        if (g.cutscene.fade > 0)
            c.rect(0, 0, TOP_W, TOP_H, RGBA(0, 0, 0, (uint8_t)(std::min(1.0f, g.cutscene.fade) * 255)));
        // letterbox bars, so it reads as a cutscene
        c.rect(0, 0, TOP_W, 12, RGBA(0, 0, 0));
        c.rect(0, TOP_H - 12, TOP_W, 12, RGBA(0, 0, 0));
        return;
    }
    drawHud(c, g);
    if (g.fade > 0) c.rect(0, 0, TOP_W, TOP_H, RGBA(0, 0, 0, (uint8_t)(g.fade * 255)));
}

// ============================================================ bottom screen
static const int SLOT = 24, HOT_X = 10, HOT_Y = 3;
static const int TAB_Y = 31, TAB_H = 20, PANEL_Y = 51;

static bool hit(int tx, int ty, float x, float y, float w, float h) {
    return tx >= x && ty >= y && tx < x + w && ty < y + h;
}

static void slotBox(Canvas& c, const Game& g, int i, float x, float y, bool picked) {
    bool sel = i == g.selected;
    uint32_t edge = picked ? COL_PICK : (sel ? COL_SEL : COL_SLOT_EDGE);
    box(c, x, y, SLOT, SLOT, sel ? COL_SLOT_ON : COL_SLOT, edge, (sel || picked) ? 2 : 1);
    const Slot& s = g.inv[i];
    if (s.empty()) return;
    slotIcon(c, g, s, x + 4, y + 4);
    if (s.type == SLOT_OBJECT && s.count > 1)
        c.text(x + SLOT - 1, y + SLOT - 11, 0.35f, COL_TEXT, std::to_string(s.count), ALIGN_RIGHT);
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
    static const char* names[TAB_COUNT] = {"Items", "Social", "Map", "Options"};
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
    if (sel.type == SLOT_OBJECT && sel.count > 1) name += "  x" + std::to_string(sel.count);
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
        info = "Press A to use. Hold A to keep going.";
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
    if (ui.statusTime > 0) c.text(160, PANEL_Y + 134, 0.5f, COL_TEXT, ui.status, ALIGN_CENTER);
    c.text(160, PANEL_Y + 160, 0.38f, COL_TEXT_SOFT, "The game also saves every time you sleep.", ALIGN_CENTER);
    (void)g;
}

static void touchOptionsTab(Game& g, Ui& ui, int tx, int ty, const std::string& savePath) {
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

static void drawShop(Canvas& c, Game& g, Ui& ui) {
    c.rect(0, 0, BOT_W, BOT_H, COL_BG);
    box(c, 4, 4, BOT_W - 8, 32, COL_PANEL, COL_BORDER);
    c.text(10, 8, 0.52f, COL_TEXT, shopTitle(ui.shop));
    char buf[64];
    snprintf(buf, sizeof buf, "%dg", g.money);
    c.text(BOT_W - 44, 10, 0.5f, COL_TEXT, buf, ALIGN_RIGHT);
    box(c, BOT_W - 38, 7, 28, 26, COL_PANEL_DARK, COL_BORDER, 1);
    c.text(BOT_W - 24, 10, 0.5f, COL_TEXT, "X", ALIGN_CENTER);

    // Buy / Sell toggle
    for (int i = 0; i < 2; i++) {
        bool on = (i == 1) == ui.shopSell;
        box(c, 4 + i * 60, 38, 58, 18, on ? COL_PANEL : COL_TAB_OFF, COL_BORDER, 1);
        c.text(33 + i * 60, 39, 0.45f, COL_TEXT, i ? "Sell" : "Buy", ALIGN_CENTER);
    }

    if (!ui.shopSell) {
        auto stock = g.shopStock(ui.shop);
        ui.shopScroll = std::max(0, std::min(ui.shopScroll, (int)stock.size() - SHOP_ROWS));
        for (int r = 0; r < SHOP_ROWS; r++) {
            int i = ui.shopScroll + r;
            if (i >= (int)stock.size()) break;
            float y = LIST_Y + r * ROW_H;
            box(c, 4, y, BOT_W - 8, ROW_H - 2, COL_SLOT, COL_SLOT_EDGE, 1);
            const ObjectDef* o = g.data.object(stock[i].itemId);
            if (o) {
                objectIcon(c, o->spriteIndex, 8, y + 3);
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
            box(c, 128, 38, 40, 18, COL_PANEL_DARK, COL_BORDER, 1);
            c.text(148, 39, 0.42f, COL_TEXT, "Up", ALIGN_CENTER);
            box(c, 172, 38, 40, 18, COL_PANEL_DARK, COL_BORDER, 1);
            c.text(192, 39, 0.42f, COL_TEXT, "Down", ALIGN_CENTER);
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
    if (hit(tx, ty, 4, 38, 58, 18)) { ui.shopSell = false; ui.shopScroll = 0; return; }
    if (hit(tx, ty, 64, 38, 58, 18)) { ui.shopSell = true; ui.shopScroll = 0; return; }
    if (hit(tx, ty, 128, 38, 40, 18)) { ui.shopScroll--; return; }
    if (hit(tx, ty, 172, 38, 40, 18)) { ui.shopScroll++; return; }
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
void drawBottom(Canvas& c, Game& g, Ui& ui) {
    if (ui.showSummary) { drawSummary(c, g); return; }
    if (g.dialogue.active) { drawDialogue(c, g, ui); return; }
    if (g.cutscene.active) {
        c.rect(0, 0, BOT_W, BOT_H, COL_BG);
        box(c, 40, 80, 240, 70, COL_PANEL, COL_BORDER, 3);
        c.text(160, 92, 0.6f, COL_TEXT, placeName(g, g.location().name), ALIGN_CENTER);
        c.text(160, 120, 0.42f, COL_TEXT_SOFT, g.cutscene.skippable ? "START to skip" : "...", ALIGN_CENTER);
        return;
    }
    if (!ui.shop.empty()) { drawShop(c, g, ui); return; }
    c.rect(0, 0, BOT_W, BOT_H, COL_BG);
    drawHotbarAndTabs(c, g, ui);
    if (ui.tab == TAB_ITEMS) drawItemsTab(c, g, ui);
    else if (ui.tab == TAB_SOCIAL) drawSocialTab(c, g, ui);
    else if (ui.tab == TAB_MAP) drawMapTab(c, g);
    else drawOptionsTab(c, g, ui);
}

void touchBottom(Game& g, Ui& ui, int tx, int ty, const std::string& savePath) {
    if (ui.showSummary) { ui.showSummary = false; return; }
    if (g.dialogue.active) { touchDialogue(g, ui, tx, ty); return; }
    if (g.cutscene.active) return;
    if (!ui.shop.empty()) { touchShop(g, ui, tx, ty); return; }
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
    else if (ui.tab == TAB_SOCIAL) touchSocialTab(ui, tx, ty);
    else if (ui.tab == TAB_OPTIONS) touchOptionsTab(g, ui, tx, ty, savePath);
}

bool sceneAfterUpdate(Game& g, Ui& ui, const std::string& savePath) {
    if (ui.statusTime > 0) ui.statusTime -= 1.0f / 60;
    if (!g.openShop.empty()) {
        ui.shop = g.openShop;
        ui.shopSell = false;
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
