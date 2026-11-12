#include "title.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "scene.h"

using namespace sv;

static const uint32_t TXT = RGBA(0x3B, 0x1F, 0x0E), SOFT = RGBA(0x6B, 0x44, 0x23), BG = RGBA(0x2A, 0x1A, 0x10);
static const uint32_t SEL = RGBA(0xC8, 0x32, 0x1E), CREAM = RGBA(0xFF, 0xE8, 0xB0);

// The game's title sheet from your files (Minigames/TitleButtons): logo on
// top, then the New / Load / Co-op / Exit buttons, then their lit-up versions.
static const char* TITLE_TEX = "Minigames_TitleButtons";
static const int LOGO_W = 400, LOGO_H = 187, BTN_W = 74, BTN_H = 58, BTN_Y = 187;

static bool officialTitle(Canvas& c) { return c.texWidth(TITLE_TEX) >= 400; }

// Background pieces from your files: a scenic panorama, two big cloud shapes
// (sized for the PC, drawn small here) and a strip of treetops.
static const char* PANORAMA = "LooseSprites_stardewPanorama";
static const char* CLOUDS = "Minigames_Clouds";
static const char* TREES = "Minigames_treestrip";

void titleSky(Canvas& c, float w, float h, float t, bool bottom) {
    c.rect(0, 0, w, h, RGBA(0x6C, 0xB4, 0xEC));
    c.rect(0, h * 0.45f, w, h * 0.55f, RGBA(0x8C, 0xC8, 0xF2));
    if (!bottom && c.texWidth(PANORAMA) > 0)                   // 640x400 -> fills the top screen
        c.imageScaled(PANORAMA, 0, 0, 640, 400, 0, -10, w / 640.0f);
    if (c.texWidth(CLOUDS) > 0) {
        // the smaller clouds on the sheet, drawn at half size, drifting right
        struct Cl { int x, y, w, h; float sy, speed, phase; };
        static const Cl cl[5] = {{260, 336, 168, 116, 6, 5, 0},   {115, 349, 123, 82, 50, 3.5f, 180},
                                 {152, 447, 123, 55, 24, 6.5f, 300}, {20, 414, 82, 46, 74, 4.5f, 90},
                                 {410, 467, 64, 36, 92, 3, 240}};
        const float k = 0.5f;
        int n = bottom ? 3 : 5;
        for (int i = 0; i < n; i++) {
            float cw = cl[i].w * k;
            float x = std::fmod(cl[i].phase + t * cl[i].speed, w + cw) - cw;
            c.imageScaled(CLOUDS, cl[i].x, cl[i].y, cl[i].w, cl[i].h, x, cl[i].sy, k);
        }
    }
    if (bottom && c.texWidth(TREES) > 0)
        for (float x = 0; x < w; x += 256) c.image(TREES, 0, 0, 256, 64, x, h - 64);
}

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
    t.screen = TS_SPLASH;
    t.splashT = 0;
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

// ---------------------------------------------------------------- opening credits
// Like the original: on a white screen, the developer's logo, then (ours)
// "Ported by ItsRaiden" with the porter's logo, then white, then the title
// scene fades in.
//   0.0-5.3   developer logo (face + "Developed by" plate from TitleButtons)
//   5.7-10.0  Ported by ItsRaiden + logo (built into the app)
//   10.0-11.2 white, then the title scene
static const char* DEV_TEX = "Minigames_TitleButtons";
static const int FACE_X = 1, FACE_Y = 307, FACE_W = 83, FACE_H = 67;      // + a pressed version at 85,306
static const int PLATE_X = 173, PLATE_Y = 313, PLATE_W = 107, PLATE_H = 54;  // + a bold version at 283,312
// When the original's logo clicks (seconds into the credits, measured from the
// real game). Played through t.sound once the engine has audio.
static const float LOGO_CLICKS[] = {1.83f, 1.99f, 2.89f, 3.03f, 3.41f, 3.56f, 3.74f, 4.45f, 5.22f, 5.44f};
static const char* MY_LOGO = "UiLogo_user";
static const float DEV_END = 5.3f, PORT_START = 5.7f, PORT_END = 10.0f;
static const uint32_t CREDIT_BLUE = RGBA(0x4A, 0x6C, 0xD6);

void titleSkipIntro(TitleUi& t) {
    t.screen = TS_TITLE;
    t.introT = INTRO_SECONDS;
}

void titleTick(TitleUi& t, float dt) {
    if (t.screen == TS_SPLASH) {
        if (t.sound)
            for (float at : LOGO_CLICKS)
                if (t.splashT < at && t.splashT + dt >= at) t.sound("mouseClick");
        t.splashT += dt;
        if (t.splashT >= SPLASH_SECONDS) { t.screen = TS_TITLE; t.introT = 0; }
        return;
    }
    if (t.screen == TS_TITLE && t.introT < INTRO_SECONDS) t.introT += dt;
}

// 0..1: fades in at a, out before b
static float fadeWindow(float x, float a, float b, float in, float out) {
    if (x < a || x > b) return 0;
    if (x < a + in) return (x - a) / in;
    if (x > b - out) return (b - x) / out;
    return 1;
}

static void whiteOver(Canvas& c, float w, float alpha) {
    if (alpha > 0) c.rect(0, 0, w, 240, RGBA(255, 255, 255, (uint8_t)(255 * std::min(1.0f, alpha))));
}

static void drawSplash(Canvas& c, TitleUi& t) {
    c.rect(0, 0, 400, 240, RGBA(255, 255, 255));
    float s = t.splashT;
    float a = fadeWindow(s, 0, DEV_END, 0.3f, 0.7f);
    if (a > 0) {
        // The logo "hops" into its pressed look (bigger face, bold plate) for
        // a tenth of a second about five times a second: a short burst at
        // the start, a pause, then steadily until it fades (timed from the
        // original).
        auto hopping = [](float s) {
            float start = s >= 1.8f && s < 4.95f ? 1.8f : (s >= 0.17f && s < 0.7f ? 0.17f : -1);
            return start >= 0 && std::fmod(s - start, 0.2167f) < 0.1f;
        };
        bool hop = hopping(s);
        if (c.texWidth(DEV_TEX) >= 400 && !t.creditsText) {
            // the face's snow flickers between two frames and the plate between
            // its normal and bright versions, like the original
            float x = std::floor(200 - (FACE_W + 4 + PLATE_W) / 2.0f);
            float fy = 120 - FACE_H / 2, py = 120 - PLATE_H / 2, px = x + FACE_W + 4;
            if (hop) c.image(DEV_TEX, 85, 306, 85, 69, x - 1, fy - 1);
            else c.image(DEV_TEX, FACE_X, FACE_Y, FACE_W, FACE_H, x, fy);
            if (hop) c.image(DEV_TEX, 283, 312, 110, 57, px - 1, py - 1);
            else c.image(DEV_TEX, PLATE_X, PLATE_Y, PLATE_W, PLATE_H, px, py);
        } else {
            // text stand-in (previews): hops on the same beat, bolder when pressed
            float o = hop ? -1.0f : 0.0f;
            uint32_t col = hop ? RGBA(0x24, 0x44, 0xB0) : CREDIT_BLUE;
            for (int b = 0; b < (hop ? 2 : 1); b++) {
                c.text(200 + o + b, 98 + o, 0.5f, col, "Developed by", ALIGN_CENTER);
                c.text(200 + o + b, 118 + o, 0.5f, col, "ConcernedApe", ALIGN_CENTER);
            }
        }
        whiteOver(c, 400, 1 - a);
        return;
    }
    a = fadeWindow(s, PORT_START, PORT_END, 0.4f, 0.7f);
    if (a > 0) {
        int lw = c.texWidth(MY_LOGO), lh = c.texHeight(MY_LOGO);
        if (lw > 0 && lh > 0) {
            // the porter's logo fitted into 150x150 (whole-number steps when small so it stays sharp)
            float k = std::min(150.0f / lw, 150.0f / lh);
            if (k >= 1.0f) k = std::floor(k);
            c.imageScaled(MY_LOGO, 0, 0, lw, lh, std::floor(200 - lw * k / 2), std::floor(104 - lh * k / 2), k);
        }
        c.text(200, lw > 0 ? 186 : 98, 0.45f, CREDIT_BLUE, "Ported by", ALIGN_CENTER);
        c.text(200, lw > 0 ? 202 : 116, 0.6f, CREDIT_BLUE, "ItsRaiden", ALIGN_CENTER);
        whiteOver(c, 400, 1 - a);
    }
}

// ---------------------------------------------------------------- the title scene
// One tall scene behind both screens (the bottom screen is the part under the
// top one). It starts on the mountains and pans up into the sky, like the
// original. d = pixels above the horizon; the camera is the d at the top
// screen's top edge. Scene x runs 0..640; the top screen sees 120..520.
static const char* CUR_TEX = "LooseSprites_Cursors";
static const float CAM_START = 130, CAM_END = 520;
static const float FADE_IN = 1.6f, PAN_START = 1.6f, PAN_LEN = 10.2f;
static const float LOGO_START = 8.3f, LOGO_LEN = 3.1f, BTN_START = 11.8f, BTN_STEP = 0.2f;

struct SkyKey { float d; uint8_t r, g, b; };
// measured from the original's title sky (horizon glow -> deep blue)
static const SkyKey SKY[] = {{-9999, 160, 253, 218}, {0, 160, 253, 218}, {20, 115, 251, 223}, {40, 107, 242, 223},
                             {60, 100, 231, 225},   {80, 93, 219, 229},  {100, 86, 209, 233}, {120, 75, 196, 235},
                             {140, 68, 185, 241},   {160, 55, 169, 243}, {180, 36, 152, 249}, {200, 15, 135, 247},
                             {212, 4, 129, 245},    {242, 3, 118, 228},  {302, 0, 96, 188},   {332, 2, 87, 173},
                             {362, 1, 76, 152},     {392, 2, 61, 125},   {422, 0, 49, 102},   {9999, 0, 40, 85}};

static uint32_t skyAt(float d) {
    if (d > 200) d = 200 + (d - 200) * (222.0f / 320.0f);   // the 3DS column is taller: stretch the top
    int n = sizeof SKY / sizeof SKY[0];
    for (int i = 1; i < n; i++) {
        if (d <= SKY[i].d) {
            const SkyKey &a = SKY[i - 1], &b = SKY[i];
            float k = (d - a.d) / (b.d - a.d);
            return RGBA((uint8_t)(a.r + (b.r - a.r) * k), (uint8_t)(a.g + (b.g - a.g) * k),
                        (uint8_t)(a.b + (b.b - a.b) * k));
        }
    }
    return RGBA(0, 40, 85);
}

static float smooth01(float x) {
    x = std::max(0.0f, std::min(1.0f, x));
    return x * x * (3 - 2 * x);
}
static float easeOut(float x) {
    x = std::max(0.0f, std::min(1.0f, x));
    return 1 - (1 - x) * (1 - x) * (1 - x);
}

// Two of the sign's leaves flutter: the title sheet has three frames for each,
// drawn over the sign at the spot where one of them matches it exactly.
static void drawLogoLeaves(Canvas& c, float time, float logoY) {
    static const int seq[4] = {0, 1, 2, 1};
    int f = seq[(int)(time / 0.25f) % 4];
    int g = seq[((int)(time / 0.25f) + 2) % 4];   // out of step with the first
    c.image(TITLE_TEX, 296 + f * 27, 187, 27, 21, 327, logoY + 107);        // right, by the Y
    c.image(TITLE_TEX, 296 + g * 22, 208, 22, 18, 16, logoY + 16);          // top left
}

static float sceneCam(const TitleUi& t) {
    return CAM_START + (CAM_END - CAM_START) * smooth01((t.introT - PAN_START) / PAN_LEN);
}

struct SkyCloud { int sx, sy, w, h; float x, d, speed; };
// rects on Minigames/Clouds; placed through the whole sky, drifting
static const SkyCloud SKY_CLOUDS[] = {
    {14, 6, 480, 330, 330, 150, 1.2f},   // the big one, rising behind the mountains
    {0, 471, 149, 66, 40, 230, 3.0f},   {152, 447, 123, 55, 300, 260, 4.0f}, {410, 467, 64, 36, 470, 190, 2.5f},
    {260, 336, 168, 116, 60, 380, 2.0f}, {115, 349, 123, 82, 420, 420, 2.8f}, {20, 414, 82, 46, 250, 340, 3.5f},
    {0, 471, 149, 66, 380, 520, 3.2f},  {152, 447, 123, 55, 90, 600, 2.2f},  {410, 467, 64, 36, 210, 470, 4.5f},
    {260, 336, 168, 116, 330, 680, 1.8f}, {20, 414, 82, 46, 500, 610, 3.0f}, {115, 349, 123, 82, 140, 760, 2.4f},
};

static void drawScene(Canvas& c, const TitleUi& t, float cam, bool bottom) {
    const float W = bottom ? 320 : 400, ox = bottom ? 160 : 120, oy = bottom ? 240 : 0;
    auto Y = [&](float d) { return std::floor(cam - oy - d); };
    for (int y = 0; y < 240; y += 2) c.rect(0, y, W, 2, skyAt(cam - oy - y));
    // a few faint stars up high (original dots, not from the game)
    for (int i = 0; i < 48; i++) {
        uint32_t h = (uint32_t)i * 2654435761u;
        float sx = (float)(h % 640), d = 430 + (float)((h >> 10) % 300);
        float y = Y(d), x = sx - ox;
        if (y < 0 || y >= 240 || x < 0 || x >= W) continue;
        uint8_t al = (uint8_t)(90 + 70 * std::sin(t.t * 1.7f + i));
        c.rect(x, y, 1, 1, RGBA(255, 255, 255, al));
    }
    if (c.texWidth(CLOUDS) > 0) {
        for (const SkyCloud& cl : SKY_CLOUDS) {
            float y = Y(cl.d);
            if (y > 240 || y + cl.h < 0) continue;
            float span = 640 + cl.w;
            // drift left, like the original
            float x = std::fmod(std::fmod(cl.x - t.t * cl.speed, span) + span, span) - cl.w - ox;
            if (x > W || x + cl.w < 0) continue;
            c.image(CLOUDS, cl.sx, cl.sy, cl.w, cl.h, std::floor(x), y);
        }
    }
    if (c.texWidth(CUR_TEX) >= 640) {
        float gy = Y(-148);
        if (gy < 240) c.rect(0, std::max(0.0f, gy), W, 240, RGBA(10, 108, 44));
        c.image(CUR_TEX, 0, 886, 640, 148, -ox, Y(14));     // far, bluer range
        c.image(CUR_TEX, 0, 737, 640, 148, -ox, Y(0));      // near, green range
        if (c.texWidth(TREES) > 0)
            for (float x = -std::fmod(ox, 256.0f); x < W; x += 256) c.image(TREES, 0, 0, 256, 64, x, Y(-136));
    }
    // two purple birds flying across the valley at the start
    if (t.introT < 9 && c.texWidth(DEV_TEX) >= 400) {
        int fr = (int)(t.t * 8) % 4;
        for (int i = 0; i < 2; i++) {
            float x = 470 + i * 34 - t.introT * 26 - ox, y = Y(-62 - i * 14);
            if (x > -26 && x < W && y > -18 && y < 240) c.image(DEV_TEX, 296 + fr * 26, 230, 26, 18, std::floor(x), y);
        }
    }
    // dark bushes framing the bottom corners of the opening shot
    if (c.texWidth(CLOUDS) > 0) {
        c.image(CLOUDS, 0, 554, 164, 142, 120 - ox, Y(-110) - 142);
        c.image(CLOUDS, 390, 543, 122, 157, 520 - 122 - ox, Y(-110) - 157);
    }
}

static const float BTN_X0 = 6, BTN_GAP = 78, BTNS_Y4 = 92;
static bool btnEnabled(const TitleUi& t, int i) { return i == 0 || i == 3 || (i == 1 && t.hasSave); }

// ---------------------------------------------------------------- drawing

// Who made this and who didn't: on the title's touch screen, between the buttons and the Circle Pad Pro one.
static void unofficialNotice(Canvas& c, float y) {
    c.rect(8, y - 3, 304, 50, RGBA(0x10, 0x0C, 0x08, 0x90));
    c.text(160, y, 0.38f, RGBA(0xFF, 0xF4, 0xD8), "Unofficial fan port. Not affiliated with ConcernedApe.", ALIGN_CENTER);
    c.text(160, y + 11, 0.38f, RGBA(0xE0, 0xD0, 0xB0), "Stardew Valley is a trademark of ConcernedApe LLC.", ALIGN_CENTER);
    c.text(160, y + 22, 0.38f, RGBA(0xE0, 0xD0, 0xB0), "Plays with your own copy of the game.", ALIGN_CENTER);
    c.text(160, y + 33, 0.38f, RGBA(0xE0, 0xD0, 0xB0), "Will be removed if ConcernedApe requests it.", ALIGN_CENTER);
}

void drawTitleTop(Canvas& c, const Game& g, TitleUi& t) {
    if (t.screen == TS_SPLASH) { drawSplash(c, t); return; }
    if (t.screen == TS_DONE_CONTINUE || t.screen == TS_EXIT) { c.rect(0, 0, 400, 240, RGBA(0, 0, 0)); return; }   // leaving: not the creator
    c.rect(0, 0, 400, 240, RGBA(0x7C, 0xB8, 0xE8));   // sky
    c.rect(0, 170, 400, 70, RGBA(0x5C, 0x9C, 0x3C));  // hill
    c.rect(0, 168, 400, 4, RGBA(0x4A, 0x82, 0x30));
    if (t.screen == TS_PADPRO) {
        titleSky(c, 400, 240, t.t, false);
        uiFrame(c, 18, 24, 364, 190);
        c.text(200, 34, 0.6f, TXT, "Circle Pad Pro", ALIGN_CENTER);
        const char* lines[4] = {"", "", "", ""};
        if (t.padState != 2) {
            lines[0] = "Looking for it...";
            lines[1] = "Sit the 3DS in the Circle Pad Pro and check it";
            lines[2] = "has a battery. The lights on the back of the";
            lines[3] = "3DS must face the accessory's window.";
        } else if (t.padStep == 0) {
            lines[0] = "Found it!";
            lines[1] = "Step 1: let go of the right stick so it";
            lines[2] = "rests in the middle, then press A.";
        } else if (t.padStep == 1) {
            lines[0] = "Step 2: roll the right stick all the way";
            lines[1] = "round its edge a few times, until the dot";
            lines[2] = "reaches every side of the box below.";
            lines[3] = "Then press A.";
        } else {
            lines[0] = "Saved. The right stick aims in the arcade";
            lines[1] = "games, and ZL / ZR step through your toolbar.";
            lines[2] = "Press A or B to go back.";
        }
        for (int i = 0; i < 4; i++) c.text(200, 70 + i * 22, 0.45f, i == 0 ? SEL : TXT, lines[i], ALIGN_CENTER);
        c.text(200, 186, 0.4f, SOFT, "B: back", ALIGN_CENTER);
        return;
    }
    if (t.screen == TS_TITLE) {
        if (officialTitle(c)) {
            t.official = true;
            t.t += 1.0f / 60;
            drawScene(c, t, sceneCam(t), false);
            float k = easeOut((t.introT - LOGO_START) / LOGO_LEN);
            if (t.introT >= LOGO_START) {
                float y = std::floor(-LOGO_H - 70 + (LOGO_H + 72) * k);
                c.image(TITLE_TEX, 0, 0, LOGO_W, LOGO_H, 0, y);
                drawLogoLeaves(c, t.t, y);
                // our own "3DS" sign hanging under the logo
                if (c.texWidth("UiBadge_3ds") > 0) c.image("UiBadge_3ds", 0, 0, 132, 62, 134, y + LOGO_H - 14);
            }
            whiteOver(c, 400, 1 - t.introT / FADE_IN);
            return;
        }
        t.official = false;
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

static const float ROW_X = 8, ROW_W = 304, ROW_H = 20, ROW_Y0 = 4;

static float rowY(int f) { return ROW_Y0 + f * (ROW_H + 2); }

static void arrows(Canvas& c, float y, bool on) {
    uint32_t ac = on ? SEL : SOFT;   // drawn, since the font's "<" is a heart
    c.line(153, y + 5, 147, y + 10, ac, 1);
    c.line(147, y + 10, 153, y + 15, ac, 1);
    c.line(153, y + 15, 153, y + 5, ac, 1);
    c.text(298, y + 3, 0.5f, on ? SEL : SOFT, ">", ALIGN_CENTER);
}

// The way into the Circle Pad Pro's calibration, at the foot of the title's bottom screen.
static const float PADBTN_X = 84, PADBTN_Y = 214, PADBTN_W = 152, PADBTN_H = 24;
static void padProButton(Canvas& c) {
    uiSlot(c, PADBTN_X, PADBTN_Y, PADBTN_W, PADBTN_H, false);
    c.text(160, PADBTN_Y + 5, 0.42f, TXT, "Circle Pad Pro", ALIGN_CENTER);
}

void drawTitleBottom(Canvas& c, const Game& g, TitleUi& t) {
    c.rect(0, 0, 320, 240, BG);
    if (t.screen == TS_SPLASH) { c.rect(0, 0, 320, 240, RGBA(255, 255, 255)); return; }   // white, like the top
    if (t.screen == TS_DONE_CONTINUE || t.screen == TS_EXIT) { c.rect(0, 0, 320, 240, RGBA(0, 0, 0)); return; }
    if (t.screen == TS_PADPRO) {
        titleSky(c, 320, 240, t.t, true);
        uiFrame(c, 90, 20, 140, 140);                     // the stick's travel, and where it is now
        c.rect(159, 30, 1, 120, SOFT);
        c.rect(100, 89, 120, 1, SOFT);
        if (t.padState == 2) {
            float dx = 160 + std::max(-1.0f, std::min(1.0f, t.padX)) * 56, dy = 90 + std::max(-1.0f, std::min(1.0f, t.padY)) * 56;
            c.rect(dx - 4, dy - 4, 9, 9, RGBA(0x30, 0x20, 0x10));
            c.rect(dx - 3, dy - 3, 7, 7, RGBA(0xE0, 0x40, 0x40));
        }
        static const char* const NAMES[3] = {"ZL", "ZR", "R"};
        for (int k = 0; k < 3; k++) {
            bool down = t.padButtons & (1 << k);
            uiSlot(c, 62 + k * 70, 170, 56, 26, down);
            c.text(90 + k * 70, 175, 0.45f, down ? SEL : TXT, NAMES[k], ALIGN_CENTER);
        }
        uiSlot(c, 110, 206, 100, 26, false);
        c.text(160, 211, 0.45f, TXT, "Back", ALIGN_CENTER);
        return;
    }
    if (t.screen == TS_TITLE && officialTitle(c)) {
        drawScene(c, t, sceneCam(t), true);
        // New, Load, Co-op, Exit pop in one by one; the lit row when picked
        for (int i = 0; i < 4; i++) {
            float since = t.introT - (BTN_START + i * BTN_STEP);
            if (since < 0) continue;
            bool on = t.cursor == i && t.introT >= INTRO_SECONDS;
            bool off = !btnEnabled(t, i);
            uint32_t tint = off ? RGBA(0xB0, 0xB0, 0xB0) : 0;
            float x = BTN_X0 + i * BTN_GAP;
            if (since < 0.1f) {   // pop: a touch bigger for a moment
                float k = 1.15f;
                c.imageScaled(TITLE_TEX, i * BTN_W, BTN_Y, BTN_W, BTN_H, x - BTN_W * (k - 1) / 2, BTNS_Y4 - BTN_H * (k - 1) / 2, k);
            } else {
                c.image(TITLE_TEX, i * BTN_W, BTN_Y + (on && !off ? BTN_H : 0), BTN_W, BTN_H, x, BTNS_Y4, false, tint);
            }
        }
        if (t.introT >= INTRO_SECONDS && t.cursor == 2)
            c.text(160, BTNS_Y4 - 16, 0.4f, CREAM, "Co-op is coming later", ALIGN_CENTER);
        if (t.introT >= INTRO_SECONDS) { unofficialNotice(c, 162); padProButton(c); }
        whiteOver(c, 320, 1 - t.introT / FADE_IN);
        return;
    }
    if (t.screen == TS_TITLE) {
        const char* labels[2] = {"New farm", "Continue"};
        for (int i = 0; i < 2; i++) {
            bool on = t.cursor == i, off = i == 1 && !t.hasSave;
            float y = 48 + i * 58;
            uiSlot(c, 40, y, 240, 52, on && !off);
            c.text(160, y + 16, 0.6f, off ? SOFT : TXT, labels[i], ALIGN_CENTER);
        }
        if (!t.hasSave) c.text(160, 32, 0.4f, CREAM, "No saved farm yet", ALIGN_CENTER);
        unofficialNotice(c, 164);
        padProButton(c);
        return;
    }
    static const char* names[CF_COUNT] = {"Name", "Farm name", "Favourite thing", "Hair", "Hair colour",
                                          "Shirt", "Pants colour", "Farm type", "Pet", ""};
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
            case CF_PET: {   // the stray Marnie brings by: cat, dog or turtle, and its coat
                int k = t.draft.petKind;
                snprintf(buf, sizeof buf, "%s %d", k == 2 ? "Dog" : k == 3 ? "Turtle" : "Cat", t.draft.petBreed + 1);
                val = buf;
                std::string tex = std::string(k == 2 ? "Animals_dog" : k == 3 ? "Animals_turtle" : "Animals_cat") +
                                  (t.draft.petBreed ? std::to_string(t.draft.petBreed) : "");
                int fr = k == 1 ? 18 : 16;
                if (c.texWidth(tex) > 0) c.image(tex, (fr % 4) * 32 + 8, (fr / 4) * 32 + 12, 16, 18, 168, y + 1);
                arrows(c, y, on);
                break;
            }
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
        case CF_PET: {   // cat 1-5, dog 1-5, turtle 1-2
            int k = wrap((t.draft.petKind == 2 ? 5 : t.draft.petKind == 3 ? 10 : 0) + t.draft.petBreed + dir, 12);
            t.draft.petKind = k >= 10 ? 3 : k >= 5 ? 2 : 1;
            t.draft.petBreed = k >= 10 ? k - 10 : k % 5;
            break;
        }
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
    bool intro = t.screen == TS_SPLASH || (t.screen == TS_TITLE && t.introT < INTRO_SECONDS);
    if (intro) {   // A / B / START skip the credits and the pan
        if (k == TK_A || k == TK_START || k == TK_B) titleSkipIntro(t);
        return;
    }
    if (t.screen == TS_PADPRO) {
        if (k == TK_B || ((k == TK_A || k == TK_START) && t.padStep >= 2)) t.screen = TS_TITLE;
        else if (k == TK_A && t.padState == 2) t.padConfirm = true;
        return;
    }
    if (t.screen == TS_TITLE) {
        if (t.official) {   // New, Load, Co-op, Exit in a row
            if (k == TK_LEFT) t.cursor = (t.cursor + 3) % 4;
            if (k == TK_RIGHT) t.cursor = (t.cursor + 1) % 4;
        } else if (k == TK_UP || k == TK_DOWN || k == TK_LEFT || k == TK_RIGHT) {
            t.cursor = t.cursor ? 0 : (t.hasSave ? 1 : 0);
        }
        if ((k == TK_A || k == TK_START) && t.sound && (t.cursor == 0 || t.cursor == 3 || (t.cursor == 1 && t.hasSave)))
            t.sound("select");
        if (k == TK_A || k == TK_START) {
            if (t.cursor == 0) t.screen = TS_CREATE;
            else if (t.cursor == 1 && t.hasSave) t.screen = TS_DONE_CONTINUE;
            else if (t.cursor == 3 && t.official) t.screen = TS_EXIT;
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
    if (t.screen == TS_SPLASH || (t.screen == TS_TITLE && t.introT < INTRO_SECONDS)) { titleSkipIntro(t); return; }
    if (t.screen == TS_PADPRO) {
        if (x >= 110 && x < 210 && y >= 206 && y < 232) t.screen = TS_TITLE;
        return;
    }
    if (t.screen == TS_TITLE && x >= PADBTN_X && x < PADBTN_X + PADBTN_W && y >= PADBTN_Y && y < PADBTN_Y + PADBTN_H) {
        t.screen = TS_PADPRO;
        t.padStep = 0;
        t.padConfirm = false;
        if (t.sound) t.sound("select");
        return;
    }
    if (t.screen == TS_TITLE) {
        if (t.official) {
            for (int i = 0; i < 4; i++) {
                float bx = BTN_X0 + i * BTN_GAP;
                if (x >= bx && x < bx + BTN_W && y >= BTNS_Y4 && y < BTNS_Y4 + BTN_H) {
                    t.cursor = i;
                    titleKey(t, data, TK_A);
                    return;
                }
            }
            return;
        }
        for (int i = 0; i < 2; i++)
            if (x >= 40 && x < 280 && y >= 48 + i * 58 && y < 100 + i * 58) {
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
