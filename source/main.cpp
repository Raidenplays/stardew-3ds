// stardew3ds -- an original 3DS farming engine that plays with assets
// converted from your own copy of Stardew Valley (see tools/convert.py).
#include <3ds.h>
#include <citro2d.h>

#include <algorithm>
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

static char perfLine1[96], perfLine2[96], perfLine3[96];   // performance readout text
static double tickMs() { return svcGetSystemTick() / (double)CPU_TICKS_PER_MSEC; }

int main() {
    gfxInitDefault();
    romfsInit();   // built-in art: the ItsRaiden logo and the 3DS sign
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
            rel == "tex/Characters_Raiden.s3t" || rel == "tex/Portraits_Raiden.s3t") return "romfs:/" + rel;
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
    std::string savePath = root + "save.sav";
    // never load a world into a different mod set without a copy of it first
    backupSaveIfModsChanged(root, savePath, data.vfs.signature());
    bool ok = data.load(root, &err);
    loadUiFonts(canvas, [&data](const std::string& rel) { return data.vfs.path(rel); });
    bool newFarm = false;
    std::string importNote;
    if (ok) {   // a PC save brought over with tools/saveimport.py: build the farm from it once
        std::string imp = root + "import.txt";
        if (FILE* fi = fopen(imp.c_str(), "r")) {
            fclose(fi);
            if (FILE* old = fopen(savePath.c_str(), "rb")) {   // keep the farm that was here
                std::string bak = savePath + ".before_import";
                if (FILE* nb = fopen(bak.c_str(), "wb")) {
                    char cb[4096];
                    size_t n;
                    while ((n = fread(cb, 1, sizeof cb, old)) > 0) fwrite(cb, 1, n, nb);
                    fclose(nb);
                }
                fclose(old);
            }
            if (game.newGame((uint32_t)osGetTime(), &err) && game.applyImport(imp, &importNote) && game.save(savePath)) {
                remove((root + "import_done.txt").c_str());
                rename(imp.c_str(), (root + "import_done.txt").c_str());
            } else {
                importNote = "Couldn't bring your PC save over (import.txt).";
            }
        }
    }
    if (ok) {
        // ---- title screen: Continue your farm, or make a new one
        FILE* sf = fopen(savePath.c_str(), "rb");
        bool hasSave = sf != nullptr;
        if (sf) fclose(sf);
        TitleUi title;
        titleInit(title, data, hasSave);
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
        if (hasSave) game.load(savePath);   // the title shows your farmer
        title.sound = [&sound](const char* cue) { sound.play(cue); };
        u64 tLast = osGetTime();
        while (aptMainLoop() && (title.screen == TS_SPLASH || title.screen == TS_TITLE || title.screen == TS_CREATE)) {
            hidScanInput();
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
            title.screen == TS_CREATE) {   // Exit button (or HOME > close)
            C2D_Fini();
            C3D_Fini();
            romfsExit();
            gfxExit();
            return 0;
        }
        if (title.screen == TS_DONE_NEW) {
            ok = game.newGame((uint32_t)osGetTime(), title.draft, &err);
            newFarm = true;
        } else if (!hasSave || !game.load(savePath)) {
            ok = game.newGame((uint32_t)osGetTime(), &err);
        }
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
                        "\n\nRun tools/convert.py on your own Stardew Valley 'Content (unpacked)' "
                        "folder and copy the result to sdmc:/3ds/stardew3ds/");
        C2D_Fini();
        C3D_Fini();
        gfxExit();
        return 0;
    }

    Ui ui;
    loadUiSettings(ui, root + "settings.txt");
    u64 last = osGetTime();
    float holdA = 0;
    while (aptMainLoop() && !ui.quit) {
        hidScanInput();
        u32 kDown = hidKeysDown(), kHeld = hidKeysHeld();
        u64 now = osGetTime();
        float dt = std::min(0.05f, (now - last) / 1000.0f);
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
        in.hold = kHeld & (KEY_A | KEY_Y);
        in.prev = kDown & KEY_L;
        in.next = kDown & KEY_R;
        bool aDown = kDown & (KEY_A | KEY_Y);
        if (kHeld & (KEY_A | KEY_Y)) holdA += dt; else holdA = 0;
        in.action = aDown || (holdA > 0.45f && game.toolTimer <= 0);

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
        if (sceneAfterUpdate(game, ui, savePath)) canvas.dropTextures();

        // music for where you are, then this frame's sound effects
        std::string want = chooseMusic(game, musicState, sound.currentMusic(), soundOut.musicPlaying());
        if (want != KEEP_MUSIC) sound.music(want);
        for (auto& cue : game.sounds) sound.play(cue);
        game.sounds.clear();
        sound.update(dt);

        uint32_t timeMs = (uint32_t)(now & 0xFFFFFFFF);
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        texNewFrame();
        canvas.beginFrame();
        canvas.draws = 0;

        float slider = osGet3DSliderState();
        bool stereo = slider > 0.01f;
        const float MAX_SHIFT = 2.0f;   // px per depth unit at full slider
        for (int eye = 0; eye < (stereo ? 2 : 1); eye++) {
            C3D_RenderTarget* t = eye == 0 ? top : topR;
            canvas.eye = stereo ? (eye == 0 ? -slider : slider) * MAX_SHIFT : 0.0f;
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
        static unsigned botTick = 0;
        static u32 lastHeld = 0;
        bool botDirty = (kDown | (kHeld ^ lastHeld)) != 0 || (kHeld & KEY_TOUCH) || (++botTick % 5) == 0;
        lastHeld = kHeld;
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
            frames++;
            upd = std::max(upd, updMs);
            cpu = std::max(cpu, C3D_GetProcessingTime());
            gpu = std::max(gpu, C3D_GetDrawingTime());
            u64 nowT = osGetTime();
            if (nowT - since >= 1000) {
                TexStats st = texStats();
                float fps = frames * 1000.0f / (float)(nowT - since);
                snprintf(perfLine1, sizeof perfLine1, "%.0ffps  upd %.0fms  cpu %.0fms  gpu %.0fms", fps, upd, cpu, gpu);
                snprintf(perfLine2, sizeof perfLine2, "vram %uK  pages +%u -%u !%u  draws %u", (unsigned)(linearSpaceFree() / 1024),
                         st.loads - last.loads, st.evictions - last.evictions, st.failed - last.failed, topDraws);
                snprintf(perfLine3, sizeof perfLine3, "map %.0f obj %.0f front %.0f wthr %.0f hud %.0f ms", prof[PROF_MAP],
                         prof[PROF_OBJECTS], prof[PROF_FRONT], prof[PROF_WEATHER], prof[PROF_HUD]);
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

    game.save(savePath);
    canvas.dropTextures();
    C2D_Fini();
    C3D_Fini();
    romfsExit();
    gfxExit();
    return 0;
}
