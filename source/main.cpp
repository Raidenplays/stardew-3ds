// stardew3ds -- an original 3DS farming engine that plays with assets
// converted from your own copy of Stardew Valley (see tools/convert.py).
#include <3ds.h>
#include "platform/cpp_pad.h"
#include <citro2d.h>

#include <algorithm>
#include <cmath>
#include <string>

#include "core/game.h"
#include "platform/c2d_canvas.h"
#include "render/fonts.h"
#include "render/modscreen.h"
#include "render/scene.h"
#include "render/setupscreen.h"
#include "render/story.h"
#include "setup/setup.h"
#include "audio/audio.h"
#include "core/music.h"
#include "platform/audio_ndsp.h"
#include "render/title.h"

using namespace sv;

static const char* ROOT = "sdmc:/3ds/stardew3ds/";

static void fatalScreen(C2DCanvas& c, C3D_RenderTarget* top, const std::string& msg) {
    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & KEY_START) break;
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        texNewFrame();
        c.beginFrame();
        C2D_TargetClear(top, C2D_Color32(0x2A, 0x1A, 0x10, 0xFF));
        C2D_SceneBegin(top);
        c.text(200, 30, 0.7f, RGBA(0xFF, 0xD0, 0x80), "stardew3ds", ALIGN_CENTER);
        // crude word wrap
        std::string line, word;
        float y = 70;
        for (size_t i = 0; i <= msg.size(); i++) {
            char ch = i < msg.size() ? msg[i] : ' ';
            if (ch == ' ' || ch == '\n') {
                if ((line + word).size() > 52) { c.text(16, y, 0.5f, RGBA(255, 255, 255), line, ALIGN_LEFT); y += 16; line.clear(); }
                line += word + " ";
                word.clear();
                if (ch == '\n') { c.text(16, y, 0.5f, RGBA(255, 255, 255), line, ALIGN_LEFT); y += 16; line.clear(); }
            } else {
                word += ch;
            }
        }
        c.text(16, y, 0.5f, RGBA(255, 255, 255), line, ALIGN_LEFT);
        c.text(200, 214, 0.45f, RGBA(0xC0, 0xC0, 0xC0), "Press START to exit", ALIGN_CENTER);
        C3D_FrameEnd(0);
    }
}

// First launch: ask for the player's own Content folder and convert it here.
// Returns false if the player chose to exit.
static bool runSetup(C2DCanvas& c, C3D_RenderTarget* top, C3D_RenderTarget* bot, const std::string& root) {
    setup::Setup su(root);
    if (su.installed()) return true;
    auto resolve = [&root](const std::string& rel) { return root + rel; };
    // once the player's files are there, convert the menu art + fonts first so
    // every screen after this looks like the game
    auto prepare = [&] {
        su.prepareUi();
        c.dropTextures();
        loadUiFonts(c, resolve);
    };
    SetupView view = SetupView::NeedFiles;
    if (su.haveContent()) { prepare(); view = SetupView::Confirm; }
    auto now = [] { return (unsigned)osGetTime(); };
    u64 t0 = osGetTime();
    while (aptMainLoop()) {
        hidScanInput();
        u32 down = hidKeysDown();
        if (down & KEY_TOUCH) {
            touchPosition tp;
            hidTouchRead(&tp);
            if (setupButtonHit(tp.px, tp.py)) down |= KEY_A;
        }
        if (view != SetupView::Running) {
            if (down & KEY_START) return false;
            if (down & KEY_A) {
                if (view == SetupView::NeedFiles) {
                    if (su.locateGame()) { prepare(); view = SetupView::Confirm; }
                } else if (view == SetupView::Confirm) {
                    su.begin();
                    aptSetSleepAllowed(false);
                    view = SetupView::Running;
                } else if (view == SetupView::Result) {
                    return su.state() == setup::State::Done;
                }
            }
        } else if (!su.step(50, now)) {
            aptSetSleepAllowed(true);
            c.dropTextures();
            view = SetupView::Result;
        }
        float t = (osGetTime() - t0) / 1000.0f;
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        texNewFrame();
        c.beginFrame();
        C2D_TargetClear(top, C2D_Color32(0, 0, 0, 0xFF));
        C2D_SceneBegin(top);
        drawSetupTop(c, su, view, t);
        C2D_TargetClear(bot, C2D_Color32(0, 0, 0, 0xFF));
        C2D_SceneBegin(bot);
        drawSetupBottom(c, su, view, t);
        C3D_FrameEnd(0);
    }
    return false;
}

extern unsigned g_sfxDecodes, g_sfxDecodeMs;
static char perfLine1[96], perfLine2[96], perfLine3[96];   // performance readout text
static double tickMs() { return svcGetSystemTick() / (double)CPU_TICKS_PER_MSEC; }

int main() {
    gfxInitDefault();
    irrstInit();   // the New 3DS C-stick and ZL/ZR (does nothing on an old 3DS)
    romfsInit();   // built-in art: the ItsRaiden logo and the 3DS sign
    ptmuInit();    // battery level and whether it's charging
    bool haveMcu = R_SUCCEEDED(mcuHwcInit());   // the battery in percent (not every way of launching allows it)
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(12288);   // both eyes + bottom screen per frame
    C2D_Prepare();
    C2D_SetTintMode(C2D_TintMult);

    C3D_RenderTarget* top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    C3D_RenderTarget* topR = C2D_CreateScreenTarget(GFX_TOP, GFX_RIGHT);
    C3D_RenderTarget* bot = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    gfxSet3D(true);

    std::string root = ROOT;
    GameData data;
    data.vfs.setRoot(root);
    C2DCanvas canvas([&data](const std::string& rel) {
        if (rel == "tex/UiLogo_user.s3t" || rel == "tex/UiBadge_3ds.s3t" ||
            rel == "tex/Characters_Raiden.s3t" || rel == "tex/Portraits_Raiden.s3t" || rel == "tex/UiFont_pixel.s3t") return "romfs:/" + rel;
        return data.vfs.path(rel);
    });

    // ---- first launch: convert the player's own game files on the console
    if (!runSetup(canvas, top, bot, root)) {
        C2D_Fini();
        C3D_Fini();
        gfxExit();
        return 0;
    }

    // ---- sound: your own XACT files in audio/ + mod music; silent if either is missing
    NdspBackend soundOut;
    audio::Engine sound;
    sound.init(root, &soundOut);
    MusicState musicState;

    // ---- mod loader: new/missing mods (or SELECT held at boot) open the mod screen
    ModScreen mods;
    hidScanInput();
    bool showMods = modScreenInit(mods, data.vfs) || (hidKeysHeld() & KEY_SELECT);
    while (showMods && aptMainLoop() && !mods.done) {
        hidScanInput();
        u32 k = hidKeysDown();
        if (k & KEY_DUP) modScreenKey(mods, MK_UP);
        if (k & KEY_DDOWN) modScreenKey(mods, MK_DOWN);
        if (k & KEY_A) modScreenKey(mods, MK_TOGGLE);
        if (k & KEY_L) modScreenKey(mods, MK_MOVE_UP);
        if (k & KEY_R) modScreenKey(mods, MK_MOVE_DOWN);
        if (k & KEY_X) modScreenKey(mods, MK_ALL_ON);
        if (k & KEY_Y) modScreenKey(mods, MK_ALL_OFF);
        if (k & KEY_START) modScreenKey(mods, MK_PLAY);
        if (k & KEY_TOUCH) {
            touchPosition tp;
            hidTouchRead(&tp);
            modScreenTouch(mods, tp.px, tp.py);
        }
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        texNewFrame();
        canvas.beginFrame();
        C2D_TargetClear(top, C2D_Color32(0, 0, 0, 0xFF));
        C2D_SceneBegin(top);
        drawModTop(canvas, mods);
        C2D_TargetClear(bot, C2D_Color32(0, 0, 0, 0xFF));
        C2D_SceneBegin(bot);
        drawModBottom(canvas, mods);
        C3D_FrameEnd(0);
    }
    if (showMods) modScreenSave(mods, root);
    data.vfs.setMods(modScreenEnabled(mods));
    canvas.dropTextures();

    std::string err;
    Game game(data);
    std::string savePath = root + "save.sav";   // which farm: picked on the title's Load screen
    bool ok = data.load(root, &err);
    loadUiFonts(canvas, [&data](const std::string& rel) { return data.vfs.path(rel); });
    bool newFarm = false;
    std::string importNote;
    if (ok) {
        // PC farms brought over by the converter (import.txt, import2.txt, ...): each becomes a farm
        // of its own in the next free slot, so nothing already on the card is replaced.
        for (int n = 1; n <= sv::MAX_SAVES; n++) {
            std::string tag = n == 1 ? std::string() : std::to_string(n);
            std::string imp = root + "import" + tag + ".txt";
            FILE* fi = fopen(imp.c_str(), "r");
            if (!fi) continue;
            fclose(fi);
            int slot = sv::freeSaveSlot(root);
            if (slot < 0) {
                importNote = "All 12 farm slots are full: delete a farm (Load) to bring your PC farm over.";
                break;
            }
            std::string to = sv::savePathFor(root, slot), note;
            if (game.newGame((uint32_t)osGetTime(), &err) && game.applyImport(imp, &note) && game.save(to)) {
                std::string done = root + "import" + tag + "_done.txt";
                remove(done.c_str());
                rename(imp.c_str(), done.c_str());
                if (importNote.empty()) {       // the first one (the farm played last on the PC)
                    sv::setLastSave(root, to);  // comes first on the Load screen
                    importNote = note;
                }
            } else {
                importNote = "Couldn't bring your PC save over (import" + tag + ".txt).";
            }
        }
        sv::describeOldSaves(game, root);
    }
    if (ok) {
        // ---- title screen: Continue your farm, or make a new one
        TitleUi title;
        titleInit(title, data, !sv::listSaves(root).empty());
        titleSetSaves(title, root);
        bool hasSave = title.hasSave;
        if (!importNote.empty())
            title.note = importNote.rfind("Welcome", 0) == 0 ? "Your PC farm is here: choose Load." : importNote;
        title.keyboard = [](const std::string& hint, const std::string& cur, int maxLen) {
            SwkbdState kb;
            char buf[64] = {0};
            swkbdInit(&kb, SWKBD_TYPE_NORMAL, 2, std::min(maxLen, 63));
            swkbdSetHintText(&kb, hint.c_str());
            swkbdSetInitialText(&kb, cur.c_str());
            swkbdSetValidation(&kb, SWKBD_NOTEMPTY_NOTBLANK, 0, 0);
            SwkbdButton b = swkbdInputText(&kb, buf, sizeof buf);
            return b == SWKBD_BUTTON_RIGHT ? std::string(buf) : cur;
        };
        if (hasSave) game.load(title.saves[0].path);   // the title shows the farmer you played last
        title.sound = [&sound](const char* cue) { sound.play(cue); };
        u64 tLast = osGetTime();
        Ui padUi;                                         // just for the pad's saved settings
        loadUiSettings(padUi, root + "settings.txt");
        if (padUi.padCal[2] > 0) cpp_pad::calibrate(padUi.padCal[0], padUi.padCal[1], padUi.padCal[2], padUi.padCal[3]);
        int calLoX = 0, calHiX = 0, calLoY = 0, calHiY = 0, calCx = 0x800, calCy = 0x800;
        while (aptMainLoop() && (title.screen == TS_SPLASH || title.screen == TS_TITLE || title.screen == TS_CREATE || title.screen == TS_PADPRO || title.screen == TS_LOAD)) {
            hidScanInput();
            if (title.screen == TS_PADPRO) {              // calibrating the Circle Pad Pro
                cpp_pad::start();
                int rx, ry, rb;
                cpp_pad::raw(rx, ry, rb);
                title.padState = cpp_pad::state();
                title.padButtons = rb;
                if (title.padState != 2 && title.padStep < 2) title.padStep = 0;
                if (title.padStep == 0) { title.padX = (rx - 0x800) / 1100.0f; title.padY = -(ry - 0x800) / 1100.0f; }
                if (title.padStep == 1) {
                    calLoX = std::min(calLoX, rx); calHiX = std::max(calHiX, rx);
                    calLoY = std::min(calLoY, ry); calHiY = std::max(calHiY, ry);
                    title.padX = (rx - calCx) / 1100.0f; title.padY = -(ry - calCy) / 1100.0f;
                }
                if (title.padStep == 2) {
                    float fx, fy; bool b1, b2, b3;
                    cpp_pad::read(fx, fy, b1, b2, b3);
                    title.padX = fx; title.padY = fy;
                }
                if (title.padConfirm) {
                    title.padConfirm = false;
                    if (title.padStep == 0) {
                        calCx = rx; calCy = ry;
                        calLoX = calHiX = rx; calLoY = calHiY = ry;
                        title.padStep = 1;
                    } else if (title.padStep == 1) {
                        // the shorter reach each way, a touch inside the edge so a full push reads as full
                        int rangeX = std::min(calCx - calLoX, calHiX - calCx) * 9 / 10, rangeY = std::min(calCy - calLoY, calHiY - calCy) * 9 / 10;
                        if (rangeX >= 200 && rangeY >= 200) {
                            cpp_pad::calibrate(calCx, calCy, rangeX, rangeY);
                            padUi.padPro = true;
                            padUi.padCal[0] = calCx; padUi.padCal[1] = calCy; padUi.padCal[2] = rangeX; padUi.padCal[3] = rangeY;
                            saveUiSettings(padUi, root + "settings.txt");
                            title.padStep = 2;
                            sound.play("reward");
                        } else sound.play("cancel");      // the stick hasn't been all the way round yet
                    }
                }
            } else if (!padUi.padPro && cpp_pad::state() != 0) cpp_pad::stop();
            u64 tNow = osGetTime();
            float tdt = std::min(0.1f, (tNow - tLast) / 1000.0f);
            titleTick(title, tdt);
            tLast = tNow;
            // the title theme starts as the scene fades in, like the original
            sound.music(title.screen == TS_SPLASH ? "" : "MainTheme");
            sound.update(tdt);
            u32 k = hidKeysDown();
            if (k & KEY_UP) titleKey(title, data, TK_UP);
            if (k & KEY_DOWN) titleKey(title, data, TK_DOWN);
            if (k & KEY_LEFT) titleKey(title, data, TK_LEFT);
            if (k & KEY_RIGHT) titleKey(title, data, TK_RIGHT);
            if (k & KEY_A) titleKey(title, data, TK_A);
            if (k & KEY_B) titleKey(title, data, TK_B);
            if (k & KEY_X) titleKey(title, data, TK_X);
            if (k & KEY_START) titleKey(title, data, TK_START);
            if (k & KEY_TOUCH) {
                touchPosition tp;
                hidTouchRead(&tp);
                titleTouch(title, data, tp.px, tp.py);
            }
            C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
            texNewFrame();
            canvas.beginFrame();
            C2D_TargetClear(top, C2D_Color32(0, 0, 0, 0xFF));
            C2D_SceneBegin(top);
            drawTitleTop(canvas, game, title);
            C2D_TargetClear(bot, C2D_Color32(0, 0, 0, 0xFF));
            C2D_SceneBegin(bot);
            drawTitleBottom(canvas, game, title);
            C3D_FrameEnd(0);
        }
        if (title.screen == TS_EXIT || title.screen == TS_SPLASH || title.screen == TS_TITLE ||
            title.screen == TS_CREATE || title.screen == TS_PADPRO || title.screen == TS_LOAD) {   // Exit button (or HOME > close)
            C2D_Fini();
            C3D_Fini();
            romfsExit();
            gfxExit();
            return 0;
        }
        bool brought = false;   // the farm being opened is one just brought over from the PC
        if (title.screen == TS_DONE_NEW) {
            // a new farm takes the next free slot: it never replaces one that's already there
            int slot = sv::freeSaveSlot(root);
            savePath = sv::savePathFor(root, slot < 0 ? 0 : slot);
            ok = game.newGame((uint32_t)osGetTime(), title.draft, &err);
            newFarm = true;
        } else {
            savePath = title.chosenPath.empty() ? sv::savePathFor(root, 0) : title.chosenPath;
            brought = !importNote.empty() && !title.saves.empty() && savePath == title.saves[0].path;
            // never load a world into a different mod set without a copy of it first
            backupSaveIfModsChanged(root, savePath, data.vfs.signature());
            if (!game.load(savePath)) ok = game.newGame((uint32_t)osGetTime(), &err);
        }
        if (ok) sv::setLastSave(root, savePath);
        if (!brought) importNote.clear();
        canvas.dropTextures();
    }
    if (ok && !importNote.empty()) game.notify(importNote);
    if (ok && newFarm) {
        // Grandpa's story, the Joja office and the bus ride (START skips)
        StoryUi story;
        storyInit(story, data, game);
        loadUiFonts(canvas, [&data](const std::string& rel) { return data.vfs.path(rel); });
        u64 sLast = osGetTime();
        sound.music("");
        while (aptMainLoop() && !story.done) {
            hidScanInput();
            u64 sNow = osGetTime();
            float sdt = std::min(0.1f, (sNow - sLast) / 1000.0f);
            sLast = sNow;
            u32 k = hidKeysDown();
            if (k & (KEY_A | KEY_TOUCH)) storyA(story);
            if (k & KEY_START) storySkip(story);
            storyTick(story, sdt);
            sound.music(storyMusic(story));
            sound.update(sdt);
            C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
            texNewFrame();
            canvas.beginFrame();
            C2D_TargetClear(top, C2D_Color32(0, 0, 0, 0xFF));
            C2D_SceneBegin(top);
            drawStoryTop(canvas, game, story);
            C2D_TargetClear(bot, C2D_Color32(0, 0, 0, 0xFF));
            C2D_SceneBegin(bot);
            drawStoryBottom(canvas, game, story);
            C3D_FrameEnd(0);
        }
        canvas.dropTextures();
        game.startOpening();   // then Robin meets you at the bus stop
    }
    if (!ok) {
        fatalScreen(canvas, top,
                    "Couldn't load game files: " + err +
                        "\n\nGet the stardew3ds converter (Android app or PC tool) from "
                        "github.com/Raidenplays/stardew-3ds, convert your own Stardew Valley Content folder "
                        "with it, and send or copy the result to sdmc:/3ds/stardew3ds/");
        C2D_Fini();
        C3D_Fini();
        gfxExit();
        return 0;
    }

    Ui ui;
    loadUiSettings(ui, root + "settings.txt");
    game.keyboard = [](const std::string& hint, const std::string& cur, int maxLen) {   // naming your pet
        SwkbdState kb;
        char buf[64] = {0};
        swkbdInit(&kb, SWKBD_TYPE_NORMAL, 2, std::min(maxLen, 63));
        swkbdSetHintText(&kb, hint.c_str());
        swkbdSetInitialText(&kb, cur.c_str());
        swkbdSetValidation(&kb, SWKBD_NOTEMPTY_NOTBLANK, 0, 0);
        SwkbdButton b = swkbdInputText(&kb, buf, sizeof buf);
        return b == SWKBD_BUTTON_RIGHT ? std::string(buf) : cur;
    };
    u64 last = osGetTime();
    float holdA = 0;
    while (aptMainLoop() && !ui.quit) {
        hidScanInput();
        u32 kDown = hidKeysDown(), kHeld = hidKeysHeld();
        u64 now = osGetTime();
        // frame time from the CPU tick counter (the millisecond clock alone makes steps of 16/17ms and
        // movement judders), smoothed a little so one slow frame doesn't lurch the camera
        static u64 lastTick = svcGetSystemTick();
        static float smoothDt = 1.0f / 60.0f;
        u64 tickNow = svcGetSystemTick();
        float rawDt = std::min(0.05f, (float)((double)(tickNow - lastTick) / (double)SYSCLOCK_ARM11));
        lastTick = tickNow;
        smoothDt += (rawDt - smoothDt) * 0.35f;
        float dt = smoothDt;
        last = now;

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
        in.sx = (kHeld & KEY_A) ? 1 : (kHeld & KEY_Y) ? -1 : 0;
        in.sy = (kHeld & KEY_B) ? 1 : (kHeld & KEY_X) ? -1 : 0;
        {   // the battery, read every couple of seconds
            static int batTick = 0;
            if (batTick++ % 120 == 0) {
                u8 lv = 0, chg = 0, pct = 0;
                ui.battery = R_SUCCEEDED(PTMU_GetBatteryLevel(&lv)) ? std::min(5, (int)lv) : -1;
                ui.charging = R_SUCCEEDED(PTMU_GetBatteryChargeState(&chg)) && chg;
                ui.batteryPct = haveMcu && R_SUCCEEDED(MCUHWC_GetBatteryLevel(&pct)) ? std::min(100, (int)pct) : -1;
            }
        }
        {   // a Circle Pad Pro, when it's switched on in Options: its stick aims, ZL / ZR step through the toolbar
            static bool padOn = false;
            static bool lastZl = false, lastZr = false;
            if (ui.padPro != padOn) { padOn = ui.padPro; if (padOn) cpp_pad::start(); else cpp_pad::stop(); }
            int st = padOn ? cpp_pad::state() : 0;
            ui.padProState = st;
            float cx, cy;
            bool zl, zr, rr;
            cpp_pad::read(cx, cy, zl, zr, rr);
            if (cx > 0.5f) in.sx = 1; else if (cx < -0.5f) in.sx = -1;
            if (cy > 0.5f) in.sy = 1; else if (cy < -0.5f) in.sy = -1;
            // out in the world the stick turns you to face that way (one of the four, whichever is stronger)
            if (std::fabs(cx) > 0.5f || std::fabs(cy) > 0.5f) {
                if (std::fabs(cx) >= std::fabs(cy)) in.ax = cx > 0 ? 1 : -1;
                else in.ay = cy > 0 ? 1 : -1;
            }
            // and in the lists on the touch screen it scrolls
            static float scrollWait = 0;
            scrollWait -= dt;
            if (std::fabs(cy) > 0.5f && scrollWait <= 0) {
                int step = cy > 0 ? 1 : -1;
                int* list = !ui.shop.empty() ? &ui.shopScroll : ui.tab == TAB_CRAFT ? &ui.craftScroll : ui.tab == TAB_QUESTS ? &ui.questScroll
                          : ui.tab == TAB_SOCIAL ? (ui.collections ? &ui.colScroll : &ui.socialScroll) : nullptr;
                if (list) { *list = std::max(0, *list + step); scrollWait = 0.12f; }
            }
            if (zl && !lastZl) in.prev = true;
            if ((zr && !lastZr)) in.next = true;
            lastZl = zl; lastZr = zr;
        }
        if (kHeld & KEY_CSTICK_LEFT) in.sx = in.ax = -1;  // the New 3DS C-stick does the same
        if (kHeld & KEY_CSTICK_RIGHT) in.sx = in.ax = 1;
        if (kHeld & KEY_CSTICK_UP) in.sy = in.ay = -1;
        if (kHeld & KEY_CSTICK_DOWN) in.sy = in.ay = 1;
        if (in.ax && in.ay) in.ay = 0;
        in.hold = kHeld & (KEY_A | KEY_Y);
        in.prev = in.prev || (kDown & KEY_L);
        in.next = in.next || (kDown & KEY_R);
        bool aDown = kDown & (KEY_A | KEY_Y);
        if (kHeld & (KEY_A | KEY_Y)) holdA += dt; else holdA = 0;
        in.action = aDown || (holdA > 0.45f && game.toolTimer <= 0);
        if (uiModal(game, ui)) {
            // a box, shop or letter is open: stand still; the pad moves a pointer and A taps with it
            static float held = 0;
            float px = in.mx, py = in.my;
            bool pad = (kHeld & (KEY_DLEFT | KEY_DRIGHT | KEY_DUP | KEY_DDOWN)) != 0;
            if (kDown & KEY_DLEFT) ui.ptrX -= 13;
            if (kDown & KEY_DRIGHT) ui.ptrX += 13;
            if (kDown & KEY_DUP) ui.ptrY -= 13;
            if (kDown & KEY_DDOWN) ui.ptrY += 13;
            held = pad ? held + dt : 0;
            if (pad && held > 0.3f) { ui.ptrX += px * 170 * dt; ui.ptrY += py * 170 * dt; }
            else if (!pad && (std::fabs(px) > 0.25f || std::fabs(py) > 0.25f)) { ui.ptrX += px * 200 * dt; ui.ptrY += py * 200 * dt; }
            if (pad || std::fabs(px) > 0.25f || std::fabs(py) > 0.25f) ui.ptrOn = true;
            ui.ptrX = std::max(2.0f, std::min(316.0f, ui.ptrX));
            ui.ptrY = std::max(2.0f, std::min(236.0f, ui.ptrY));
            if (aDown && ui.ptrOn) touchBottom(game, ui, (int)ui.ptrX + 1, (int)ui.ptrY + 1, savePath);
            in.mx = in.my = 0;
            in.action = false;
            holdA = 0;
        }

        if (kDown & KEY_START) {
            if (game.cutscene.active) game.skipEvent();
            else ui.tab = TAB_OPTIONS;
        }
        if ((kDown & KEY_SELECT) && (kHeld & KEY_L)) ui.showPerf = !ui.showPerf;   // L + SELECT: performance readout
        else if (kDown & KEY_SELECT) ui.tab = (ui.tab + 1) % TAB_COUNT;
        if (!ui.shop.empty() && (kDown & KEY_B)) { ui.shop.clear(); ui.cart.clear(); }
        if (game.bundleView >= 0 && (kDown & KEY_B)) game.bundleView--;   // back a page (closes from the rooms)
        if (game.museumOpen && (kDown & KEY_B)) game.museumOpen = false;
        if (game.chestOpen && (kDown & KEY_B)) game.closeChest();
        if (game.letterOpen) {
            if (kDown & (KEY_A | KEY_B)) { game.closeLetter(); if (kDown & KEY_A) game.openMail(); }
            in = Input();
        }

        if (game.dialogue.active) {
            if (kDown & KEY_A) dialogueAdvance(game, ui);
            in = Input();
        }
        if (ui.night) {   // the end-of-day screens: nothing else moves
            if (kDown & KEY_A) nightAdvance(game, ui);
            in = Input();
            dt = 0;
        }
        if (ui.showSummary) {
            if (kDown & (KEY_DLEFT | KEY_CPAD_LEFT)) summaryMove(game, ui, -1);
            if (kDown & (KEY_DRIGHT | KEY_CPAD_RIGHT)) summaryMove(game, ui, 1);
            if (kDown & KEY_A) summaryAdvance(game, ui);
            in = Input();
            dt = 0;
        }
        if (game.fishingActive()) {
            if (kHeld & KEY_TOUCH) {
                touchPosition tp;
                hidTouchRead(&tp);
                fishingTouch(game, kDown & KEY_TOUCH, true, tp.px, tp.py, in);
            }
        } else if (kDown & KEY_TOUCH) {
            touchPosition tp;
            hidTouchRead(&tp);
            ui.ptrOn = false;   // the stylus is in hand: put the pointer away
            touchBottom(game, ui, tp.px, tp.py, savePath);
        }

        u64 tUpd = svcGetSystemTick();
        game.update(dt, in);
        {   // after a warp, stay black while the new area's textures are still loading,
            // then fade in (so the loading stutter happens behind the black screen)
            static unsigned lastLoads = 0;
            static float held = 0;
            unsigned loads = texStats().loads;
            if (game.fade > 0.5f && loads != lastLoads && held < 1.5f) {
                game.fade = 1.0f;
                held += dt;
            } else if (game.fade <= 0) {
                held = 0;
            }
            lastLoads = loads;
        }
        float updMs = (svcGetSystemTick() - tUpd) / (float)CPU_TICKS_PER_MSEC;
        // Textures are kept from one area to the next: dropping them all meant reloading the same
        // outdoor sheets, farmer and HUD on every screen change (15 to 20 seconds on the console).
        sceneAfterUpdate(game, ui, savePath);

        // music for where you are, then this frame's sound effects
        std::string want = chooseMusic(game, musicState, sound.currentMusic(), soundOut.musicPlaying());
        if (want != KEEP_MUSIC) sound.music(want);
        for (auto& cue : game.sounds) sound.play(cue);
        game.sounds.clear();
        sound.update(dt);

        uint32_t timeMs = (uint32_t)(now & 0xFFFFFFFF);
        {   // Options: a steady 30 frames a second, for places the old 3DS can't hold 60
            static int rate = 60;
            int want = ui.fps30 ? 30 : 60;
            if (want != rate) { C3D_FrameRate((float)want); rate = want; }
        }
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        texNewFrame();
        canvas.beginFrame();
        canvas.draws = 0;

        float slider = osGet3DSliderState();
        bool stereo = slider > 0.01f;
        const float MAX_SHIFT = 2.0f;   // px per depth unit, per eye, at full slider
        for (int eye = 0; eye < (stereo ? 2 : 1); eye++) {
            C3D_RenderTarget* t = eye == 0 ? top : topR;
            canvas.eye = stereo ? (eye == 0 ? slider : -slider) * MAX_SHIFT : 0.0f;   // + depth comes towards you: the left eye sees it further right
            C2D_TargetClear(t, C2D_Color32(0, 0, 0, 0xFF));
            C2D_SceneBegin(t);
            canvas.screen = t;
            drawTop(canvas, game, ui, timeMs);
        }
        canvas.screen = nullptr;
        canvas.eye = 0;
        unsigned topDraws = canvas.draws;
        // The bottom screen mostly shows menus that don't move: redraw it when
        // something was pressed or touched, and otherwise ~12 times a second.
        // A screen that isn't drawn keeps showing its last picture.
        // (things that move down there by themselves keep the faster rate)
        bool botLive = game.dialogue.active || game.cutscene.active || game.fishing.state != sv::FS_NONE || game.arcade.game ||
                       !ui.shop.empty() || ui.statusTime > 0 || game.toastTime > 0 || (kHeld & (KEY_L | KEY_R | KEY_X | KEY_Y | KEY_DUP | KEY_DDOWN | KEY_DLEFT | KEY_DRIGHT)) != 0;
        static unsigned botTick = 0;
        static u32 lastHeld = 0;
        bool botDirty = (kDown | (kHeld ^ lastHeld)) != 0 || (kHeld & KEY_TOUCH) || (++botTick % (botLive ? 3 : 30)) == 0;
        {   // ...or when something it shows has changed (money, energy, the bag, the tab, a new perf reading)
            static uint32_t lastSig = 0;
            uint32_t sig = (uint32_t)game.money * 31u + (uint32_t)game.energy * 131u + (uint32_t)game.health * 17u + (uint32_t)game.selected * 7u +
                           (uint32_t)ui.tab * 1009u + (uint32_t)(ui.shopScroll + ui.craftScroll * 3 + ui.questScroll * 5 + ui.socialScroll * 7 + ui.colScroll * 11) * 419u + (uint32_t)(ui.padProState + (ui.padPro ? 4 : 0)) * 811u + (uint32_t)(ui.battery * 3 + ui.batteryPct * 5 + ui.charging) * 613u + (uint32_t)game.day * 53u + (uint32_t)(game.timeOfDay / 10) * 3u + (uint32_t)ui.showPerf + (uint32_t)(ui.night * 2 + (ui.nightT >= 1.5f)) * 977u;
            for (int k = 0; k < game.invSlots; k++) sig = sig * 16777619u ^ (uint32_t)(game.inv[k].id * 64 + game.inv[k].count + game.inv[k].type * 7919);
            for (char ch : ui.status) sig = sig * 16777619u ^ (uint8_t)ch;
            if (ui.showPerf) for (const char* p = perfLine1; *p; p++) sig = sig * 16777619u ^ (uint8_t)*p;
            if (ui.ptrOn && uiModal(game, ui)) sig = sig * 16777619u ^ ((uint32_t)ui.ptrX * 400u + (uint32_t)ui.ptrY);
            if (sig != lastSig) botDirty = true;
            lastSig = sig;
        }
        lastHeld = kHeld;
        {   // a performance log on the SD card (perf.txt): one line every 20 seconds and whenever you change
            // place, so slow spots can be found without watching the readout
            static u64 logSince = osGetTime();
            static int logFrames = 0, logLines = 0;
            static float worstCpu = 0, worstGpu = 0, worstUpd = 0;
            static unsigned worstDraws = 0;
            static std::string logWhere;
            logFrames++;
            worstCpu = std::max(worstCpu, C3D_GetProcessingTime());
            worstGpu = std::max(worstGpu, C3D_GetDrawingTime());
            worstUpd = std::max(worstUpd, updMs);
            worstDraws = std::max(worstDraws, topDraws);
            u64 nowL = osGetTime();
            const std::string& where = game.location().name;
            if (logWhere.empty()) logWhere = where;
            if ((nowL - logSince >= 20000 || where != logWhere) && nowL - logSince >= 2000 && logLines < 600) {
                if (FILE* lf = fopen((std::string(ROOT) + "perf.txt").c_str(), logLines ? "a" : "w")) {
                    if (!logLines) fprintf(lf, "place\tfps\tworst update ms\tworst cpu ms\tworst gpu ms\tmost draws\t3D\n");
                    fprintf(lf, "%s\t%.1f\t%.1f\t%.1f\t%.1f\t%u\t%s\n", logWhere.c_str(), logFrames * 1000.0f / (float)(nowL - logSince),
                            worstUpd, worstCpu, worstGpu, worstDraws, stereo ? "on" : "off");
                    fclose(lf);
                    logLines++;
                }
            }
            if (nowL - logSince >= 20000 || where != logWhere) {
                logSince = nowL;
                logFrames = 0;
                worstCpu = worstGpu = worstUpd = 0;
                worstDraws = 0;
                logWhere = where;
            }
        }
        if (ui.showPerf) {
            // once a second: fps, CPU ms (game update / building the frame), GPU ms,
            // free GPU memory, texture pages loaded/evicted/failed, draw calls top+bottom
            static u64 since = 0;
            static int frames = 0;
            static float upd = 0, cpu = 0, gpu = 0;
            static float prof[PROF_COUNT];
            g_profClock = tickMs;
            for (int k = 0; k < PROF_COUNT; k++) prof[k] = std::max(prof[k], g_profMs[k]);
            static TexStats last;
            static u64 lastFrameT = 0;
            static unsigned worst = 0, lastSfx = 0, lastSfxMs = 0;
            {   // the longest single frame this second: what a stutter looks like in numbers
                u64 ft = osGetTime();
                if (lastFrameT) worst = std::max(worst, (unsigned)(ft - lastFrameT));
                lastFrameT = ft;
            }
            frames++;
            upd = std::max(upd, updMs);
            cpu = std::max(cpu, C3D_GetProcessingTime());
            gpu = std::max(gpu, C3D_GetDrawingTime());
            u64 nowT = osGetTime();
            if (nowT - since >= 1000) {
                TexStats st = texStats();
                float fps = frames * 1000.0f / (float)(nowT - since);
                snprintf(perfLine1, sizeof perfLine1, "%.0ffps upd %.0f cpu %.0f gpu %.0f worst %ums", fps, upd, cpu, gpu, worst);
                worst = 0;
                snprintf(perfLine2, sizeof perfLine2, "free %uK pg +%u %ums -%u !%u dr %u", (unsigned)(linearSpaceFree() / 1024),
                         st.loads - last.loads, st.loadMs - last.loadMs, st.evictions - last.evictions, st.failed - last.failed, topDraws);
                snprintf(perfLine3, sizeof perfLine3, "map %.0f obj %.0f frnt %.0f wth %.0f hud %.0f rd %d sfx +%u %ums", prof[PROF_MAP],
                         prof[PROF_OBJECTS], prof[PROF_FRONT], prof[PROF_WEATHER], prof[PROF_HUD], g_mapRedraws,
                         g_sfxDecodes - lastSfx, g_sfxDecodeMs - lastSfxMs);
                lastSfx = g_sfxDecodes; lastSfxMs = g_sfxDecodeMs;
                g_mapRedraws = 0;
                for (float& v : prof) v = 0;
                last = st;
                since = nowT;
                frames = 0;
                upd = cpu = gpu = 0;
            }
        }
        if (botDirty) {
            C2D_TargetClear(bot, C2D_Color32(0x2A, 0x1A, 0x10, 0xFF));
            C2D_SceneBegin(bot);
            unsigned before = canvas.draws;
            drawBottom(canvas, game, ui);
            if (ui.showPerf) {
                char l2[112];
                snprintf(l2, sizeof l2, "%s+%u", perfLine2, canvas.draws - before);
                canvas.rect(0, 0, 320, 42, RGBA(0, 0, 0));
                canvas.text(4, 28, 0.4f, RGBA(255, 255, 0), perfLine3, ALIGN_LEFT);
                canvas.text(4, 2, 0.4f, RGBA(255, 255, 0), perfLine1[0] ? perfLine1 : "measuring...", ALIGN_LEFT);
                canvas.text(4, 15, 0.4f, RGBA(255, 255, 0), l2, ALIGN_LEFT);
            }
        }
        C3D_FrameEnd(0);
    }

    // Leaving does not save: the day is kept when you go to bed, like the game on other machines.

    canvas.dropTextures();
    C2D_Fini();
    C3D_Fini();
    cpp_pad::stop();
    if (haveMcu) mcuHwcExit();
    ptmuExit();
    romfsExit();
    irrstExit();
    gfxExit();
    return 0;
}
