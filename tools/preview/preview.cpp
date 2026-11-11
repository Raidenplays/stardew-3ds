#include <chrono>
#include <algorithm>
#include <algorithm>
// PC screenshot tool. Runs the real game code through a scripted walk and
// writes 3DS-shaped PNGs (top screen over bottom screen) using the same
// drawing code as the console.
//
//   make -C tools/preview
//   ./tools/preview/preview <converted root>/3ds/stardew3ds/ shots/
#include <cstdio>
#include <string>

#include "core/game.h"
#include "host_canvas.h"
#include "render/fonts.h"
#include "render/modscreen.h"
#include "render/title.h"
#include "render/setupscreen.h"
#include "render/story.h"
#include "setup/setup.h"
#include "render/scene.h"

using namespace sv;

static std::string g_root, g_out, g_save = "/tmp/stardew3ds_preview.sav";
static uint32_t g_timeMs = 0;
static int g_shot = 0;

static void step(Game& g, Ui& ui, const Input& in, int frames) {
    for (int i = 0; i < frames; i++) {
        g.update(1.0f / 60, in);
        sceneAfterUpdate(g, ui, g_save);
        g_timeMs += 16;
    }
}

// Walk in a direction until the location changes (or give up).
static bool walkUntilWarp(Game& g, Ui& ui, float mx, float my, int maxFrames = 1200) {
    std::string start = g.location().name;
    Input in;
    in.mx = mx;
    in.my = my;
    for (int i = 0; i < maxFrames; i++) {
        step(g, ui, in, 1);
        if (g.location().name != start) return true;
    }
    return false;
}

static void tap(Game& g, Ui& ui, int x, int y) { touchBottom(g, ui, x, y, g_save); }

static void shot(HostCanvas& c, Game& g, Ui& ui, const std::string& name) {
    g.fade = 0;
    // 400 x 480: top screen, then bottom screen centred under it
    c.resize(400, 480);
    c.rect(0, 0, 400, 480, RGBA(0x18, 0x18, 0x1C));
    c.setOrigin(0, 0);
    c.setClip(0, 0, 400, 240);
    drawTop(c, g, ui, g_timeMs);
    c.setOrigin(40, 240);
    c.setClip(40, 240, 320, 240);
    drawBottom(c, g, ui);
    c.setOrigin(0, 0);
    c.setClip(0, 0, 400, 480);
    char path[512];
    snprintf(path, sizeof path, "%s/%02d_%s.png", g_out.c_str(), ++g_shot, name.c_str());
    c.writePng(path, 2);
    printf("  %s  [%s %s]\n", path, g.location().name.c_str(), g.timeString().c_str());
}

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: preview <sd root ending in stardew3ds/> <out dir> [font.ttf]\n");
        return 1;
    }
    g_root = argv[1];
    if (g_root.back() != '/') g_root += '/';
    g_out = argv[2];
    std::string font = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf";
    std::string eventArg;   // "Location:eventId" -> record that cutscene instead of the walk
    std::string tourArg;
    std::string modsArg;    // "a,b" or "auto" (mods.txt + new mods)
    bool modScreenShot = false;
    bool craftDemo = false;
    bool fishDemo = false;
    bool mailDemo = false;
    bool titleDemo = false;   // title screen + character creation
    std::string whereMap, weatherArg;
    int atX = -1, atY = -1;
    bool swingDemo = false, setupDemo = false;
    int timeArg = -1;
    int dayArg = -1;
    int mapCacheArg = 0;
    bool panArg = false;
    bool animalsArg = false;
    int mineArg = 0;   // HHMM: run the villagers' day up to then
    bool storyDemo = false;   // the new-game story, frames at 10 fps   // first-launch setup screens (runs the real converter on <sd root>)
    bool introFrames = false;
    int introFps = 10; // top-screen frames of the opening credits -> title (for a GIF)    // sleep to day 2 and read the first letter    // stand by water in the Forest and show the reeling meter   // place machines near the house, shoot the Craft tab + a chest    // "Loc[@x,y],Loc2,..." -> one screenshot per location
    for (int i = 3; i < argc; i++) {
        std::string a = argv[i];
        if (a.rfind("--event=", 0) == 0) eventArg = a.substr(8);
        else if (a.rfind("--tour=", 0) == 0) tourArg = a.substr(7);
        else if (a.rfind("--mods=", 0) == 0) modsArg = a.substr(7);
        else if (a == "--modscreen") modScreenShot = true;
        else if (a == "--craft") craftDemo = true;
        else if (a == "--fish") fishDemo = true;
        else if (a == "--mail") mailDemo = true;
        else if (a == "--title") titleDemo = true;
        else if (a == "--intro-frames") introFrames = true;
        else if (a.rfind("--intro-frames=", 0) == 0) { introFrames = true; introFps = atoi(a.c_str() + 15); }
        else if (a == "--setup") setupDemo = true;
        else if (a == "--swing") swingDemo = true;
        else if (a.rfind("--where=", 0) == 0) whereMap = a.substr(8);
        else if (a.rfind("--weather=", 0) == 0) weatherArg = a.substr(10);
        else if (a.rfind("--at=", 0) == 0) sscanf(a.c_str() + 5, "%d,%d", &atX, &atY);
        else if (a == "--story") storyDemo = true;
        else if (a.rfind("--time=", 0) == 0) timeArg = atoi(a.c_str() + 7);
        else if (a == "--mapcache") mapCacheArg = 1;
        else if (a == "--pan") panArg = true;
        else if (a == "--animals") animalsArg = true;
        else if (a.rfind("--mine=", 0) == 0) mineArg = atoi(a.c_str() + 7);
        else if (a.rfind("--day=", 0) == 0) dayArg = atoi(a.c_str() + 6);   // season*28 + day, e.g. 16 or 64
        else font = a;
    }

    if (setupDemo) {
        setup::Setup su(g_root);
        auto shot = [&](SetupView v, const char* name) {
            HostCanvas mc(g_root, font);
            mc.resize(400, 480);
            mc.rect(0, 0, 400, 480, RGBA(0x18, 0x18, 0x1C));
            mc.setClip(0, 0, 400, 240);
            loadUiFonts(mc, [](const std::string& rel) { return g_root + rel; });
            drawSetupTop(mc, su, v, 30);
            mc.setOrigin(40, 240);
            mc.setClip(40, 240, 320, 240);
            drawSetupBottom(mc, su, v, 30);
            std::string path = g_out + "/setup_" + name + ".png";
            mc.writePng(path.c_str(), 2);
            printf("  %s\n", path.c_str());
        };
        shot(SetupView::NeedFiles, "1_needfiles");
        su.prepareUi();
        shot(SetupView::Confirm, "2_confirm");
        su.begin();
        unsigned fake = 0;
        auto now = [&] { return fake += 10; };
        size_t half = su.jobCount() / 2;
        for (size_t i = 0; i < half; i++) su.step(0, now);
        shot(SetupView::Running, "3_running");
        while (su.step(0, now)) {}
        shot(SetupView::Result, "4_result");
        for (auto& w : su.warnings()) printf("  warning: %s\n", w.c_str());
        printf("  state %d: %s\n", (int)su.state(), su.error().c_str());
        return 0;
    }

    GameData data;
    std::string err;
    data.vfs.setRoot(g_root);
    if (modsArg == "auto" || modScreenShot) {
        ModScreen ms;
        modScreenInit(ms, data.vfs);
        if (modScreenShot) {
            HostCanvas mc(g_root, font);
            mc.resize(400, 480);
            mc.rect(0, 0, 400, 480, RGBA(0x18, 0x18, 0x1C));
            mc.setClip(0, 0, 400, 240);
            drawModTop(mc, ms);
            mc.setOrigin(40, 240);
            mc.setClip(40, 240, 320, 240);
            drawModBottom(mc, ms);
            std::string path = g_out + "/mods.png";
            mc.writePng(path.c_str(), 2);
            printf("  %s\n", path.c_str());
            return 0;
        }
        data.vfs.setMods(modScreenEnabled(ms));
    } else if (!modsArg.empty()) {
        std::vector<std::string> ids;
        size_t pos = 0;
        while (pos <= modsArg.size()) {
            size_t comma = modsArg.find(',', pos);
            ids.push_back(modsArg.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos));
            if (comma == std::string::npos) break;
            pos = comma + 1;
        }
        data.vfs.setMods(ids);
    }
    if (!data.load(g_root, &err)) { fprintf(stderr, "data: %s\n", err.c_str()); return 1; }
    Game g(data);
    if (!g.newGame(42, &err)) { fprintf(stderr, "newGame: %s\n", err.c_str()); return 1; }
    HostCanvas c(g_root, font);
    c.resolveFn = [](const std::string& rel, void* ctx) {
        if (rel == "tex/UiLogo_user.s3t" || rel == "tex/UiBadge_3ds.s3t") return std::string(REPO_ROMFS "/") + rel;
        return ((Vfs*)ctx)->path(rel);
    };
    c.resolveCtx = &data.vfs;
    if (!getenv("S3DS_SYSTEM_FONT")) loadUiFonts(c, [&data](const std::string& rel) { return data.vfs.path(rel); });
    Ui ui;
    sceneAfterUpdate(g, ui, g_save);
    Input none;

    if (storyDemo) {
        StoryUi st;
        g.me.name = "Raiden";
        g.me.farm = "Truro";
        storyInit(st, data, g);
        int n = 0;
        float sinceA = 0;
        while (!st.done && n < 4000) {
            storyTick(st, 0.1f);
            if (st.part == 5) { sinceA += 0.1f; if (sinceA > 4) { storyA(st); sinceA = 0; } }
            c.resize(400, 480);
            c.rect(0, 0, 400, 480, RGBA(0x18, 0x18, 0x1C));
            c.setOrigin(0, 0);
            c.setClip(0, 0, 400, 240);
            drawStoryTop(c, g, st);
            c.setOrigin(40, 240);
            c.setClip(40, 240, 320, 240);
            drawStoryBottom(c, g, st);
            c.setOrigin(0, 0);
            c.setClip(0, 0, 400, 480);
            char path[512];
            snprintf(path, sizeof path, "%s/s%04d.png", g_out.c_str(), n++);
            c.writePng(path, 1);
        }
        printf("  %d frames\n", n);
        return 0;
    }

    if (introFrames) {
        TitleUi t;
        titleInit(t, data, false);
        t.creditsText = !getenv("S3DS_REAL_CREDITS");
        int n = 0;
        const float step = 1.0f / (introFps > 0 ? introFps : 10);
        for (float time = 0; time < SPLASH_SECONDS + INTRO_SECONDS + 2.0f; time += step) {
            titleTick(t, step);
            t.t += step - 1.0f / 60;   // drawTitleTop advances the clouds by 1/60 per frame
            c.resize(400, 480);
            c.rect(0, 0, 400, 480, RGBA(0x18, 0x18, 0x1C));
            c.setOrigin(0, 0);
            c.setClip(0, 0, 400, 240);
            drawTitleTop(c, g, t);
            c.setOrigin(40, 240);
            c.setClip(40, 240, 320, 240);
            drawTitleBottom(c, g, t);
            c.setOrigin(0, 0);
            c.setClip(0, 0, 400, 480);
            char path[512];
            snprintf(path, sizeof path, "%s/f%04d.png", g_out.c_str(), n++);
            c.writePng(path, 1);
        }
        printf("  %d frames\n", n);
        return 0;
    }

    if (titleDemo) {
        TitleUi t;
        titleInit(t, data, false);
        t.keyboard = [](const std::string& hint, const std::string&, int) {
            return std::string(hint == "Your name" ? "Raiden" : hint == "Farm name" ? "Truro" : "Pizza");
        };
        auto shoot = [&](const char* name) {
            c.resize(400, 480);
            c.rect(0, 0, 400, 480, RGBA(0x18, 0x18, 0x1C));
            c.setOrigin(0, 0);
            c.setClip(0, 0, 400, 240);
            drawTitleTop(c, g, t);
            c.setOrigin(40, 240);
            c.setClip(40, 240, 320, 240);
            drawTitleBottom(c, g, t);
            c.setOrigin(0, 0);
            c.setClip(0, 0, 400, 480);
            std::string path = g_out + "/" + name + ".png";
            c.writePng(path.c_str(), 2);
            printf("  %s\n", path.c_str());
        };
        t.splashT = 1.5f;
        shoot("credits_original");
        t.splashT = 7.5f;
        shoot("credits_port");
        t.screen = TS_TITLE;
        for (float at : {0.8f, 3.0f, 7.0f, 10.0f, 11.9f}) {
            t.introT = at;
            char nm[32];
            snprintf(nm, sizeof nm, "intro_%04.1f", at);
            shoot(nm);
        }
        titleSkipIntro(t);
        shoot("title");
        titleKey(t, data, TK_A);              // New farm
        titleKey(t, data, TK_A);              // type a name
        titleKey(t, data, TK_DOWN);
        titleKey(t, data, TK_A);              // farm name
        titleKey(t, data, TK_DOWN);
        titleKey(t, data, TK_A);              // favourite
        titleKey(t, data, TK_DOWN);
        for (int i = 0; i < 3; i++) titleKey(t, data, TK_RIGHT);   // hair
        titleKey(t, data, TK_DOWN);
        for (int i = 0; i < 2; i++) titleKey(t, data, TK_RIGHT);   // hair colour
        titleKey(t, data, TK_DOWN);
        t.t = 1.6f;
        shoot("create");
        return 0;
    }

    if (mailDemo) {
        g.passedOut = true;
        g.goToBed();
        step(g, ui, none, 10);
        ui.showSummary = false;
        g.levelUps.clear();
        step(g, ui, none, 10);
        shot(c, g, ui, "mail_waiting");
        if (g.openMail()) shot(c, g, ui, "letter");
        return 0;
    }

    if (fishDemo) {
        g.timeOfDay = 9 * 60;
        g.teleport("Forest", 40, 40);
        const TileMap& m = g.map();
        int best = 1 << 30, fx = 40, fy = 40, wx = 40, wy = 39;
        for (int y = 2; y < m.height() - 2; y++)
            for (int x = 2; x < m.width() - 2; x++)
                if ((m.flags(x, y) & CF_WATER) && !(m.flags(x, y + 2) & (CF_WATER | CF_BLOCKED))) {
                    int dd = (x - 40) * (x - 40) + (y - 40) * (y - 40);
                    if (dd < best) { best = dd; fx = x; fy = y + 2; wx = x; wy = y; }
                }
        g.teleport("Forest", fx, fy);
        g.facing = FACE_UP;
        for (int i = 0; i < g.data.toolCount(); i++)
            if (g.data.tool(i)->kind == TOOL_ROD) { Slot r; r.type = SLOT_TOOL; r.id = i; r.count = 1; g.inv[5] = r; g.selectSlot(5); break; }
        step(g, ui, none, 5);
        Fishing& f = g.fishing;
        f.state = FS_REEL;
        f.bobX = wx; f.bobY = wy;
        f.fish = 142;   // a carp, if your data has it
        if (!g.data.object(f.fish) && !g.data.fish.empty()) f.fish = g.data.fish.begin()->first;
        f.barSize = 0.2f; f.barPos = 0.35f; f.fishPos = 0.5f; f.progress = 0.62f;
        ui.tab = TAB_ITEMS;
        shot(c, g, ui, "fishing");
        return 0;
    }

    if (craftDemo) {
        g.timeOfDay = 10 * 60;
        g.teleport("Farm", g.data.layout.doorX, g.data.layout.doorY + 2);
        g.skillXp[SK_FARMING] = 800;
        g.skillXp[SK_MINING] = 400;
        g.skillXp[SK_FORAGING] = 150;
        // up to 4 machines that take an item, each with its first input
        int placedN = 0;
        int bx = g.data.layout.doorX - 3, by = g.data.layout.doorY + 4;
        std::vector<int> done;
        for (size_t i = 0; i < g.data.machineRules.size() && placedN < 4; i++) {
            const MachineRule& r = g.data.machineRules[i];
            if (r.autoRun || r.inputs.empty() || std::count(done.begin(), done.end(), r.machine)) continue;
            done.push_back(r.machine);
            Slot m; m.type = SLOT_BIG; m.id = r.machine; m.count = 1;
            g.addSlot(m);
            for (int k = 0; k < INV_SIZE; k++) if (g.inv[k].type == SLOT_BIG && g.inv[k].id == r.machine) { g.swapSlots(k, 11); }
            g.selectSlot(11);
            g.teleport("Farm", bx + placedN * 2, by - 1);
            g.facing = FACE_DOWN;
            Input a; a.action = true;
            g.update(1.0f / 60, a);
            step(g, ui, none, 20);
            g.addItem(r.inputs[r.inputs.size() / 2], r.inputCount);
            if (r.fuel >= 0) g.addItem(r.fuel, r.fuelCount);
            for (int k = 0; k < INV_SIZE; k++) if (g.inv[k].type == SLOT_OBJECT && g.inv[k].id == r.inputs[r.inputs.size() / 2]) g.swapSlots(k, 11);
            g.selectSlot(11);
            g.update(1.0f / 60, a);
            step(g, ui, none, 20);
            placedN++;
        }
        // finish the first one so its bubble shows
        if (Placed* p = g.placedAt(bx, by)) if (p->rule >= 0) p->readyAt = g.nowMinutes();
        Slot ch; ch.type = SLOT_BIG; ch.id = 130; ch.count = 2;
        if (g.data.big(130)) g.addSlot(ch);
        g.addItem(388, 120); g.addItem(390, 80); g.addItem(334, 3); g.addItem(335, 2);
        g.teleport("Farm", bx + 2, by + 2);
        g.facing = FACE_UP;
        g.selectSlot(0);
        step(g, ui, none, 10);
        ui.tab = TAB_CRAFT;
        for (size_t i = 0; i < g.data.recipes.size(); i++)
            if (g.recipeKnown(g.data.recipes[i]) && g.data.recipes[i].outBig) { ui.craftSel = (int)i; break; }
        shot(c, g, ui, "machines_and_crafting");
        // journal and a morning level-up screen
        ui.tab = TAB_QUESTS;
        if (!g.quests.empty() && g.quests.size() > 1) g.quests[1].progress = 3;
        shot(c, g, ui, "journal");
        g.levelUps.push_back({SK_FARMING, 2});
        ui.showSummary = true;
        shot(c, g, ui, "level_up");
        g.levelUps.clear();
        g.levelUps.push_back({SK_FARMING, 5});
        ui.profSel = 1;
        shot(c, g, ui, "profession");
        ui.showSummary = false;
        g.levelUps.clear();
        ui.tab = TAB_CRAFT;
        // chest
        if (g.data.big(130)) {
            for (int k = 0; k < INV_SIZE; k++) if (g.inv[k].type == SLOT_BIG && g.inv[k].id == 130) { g.swapSlots(k, 11); }
            g.selectSlot(11);
            g.teleport("Farm", bx + 8, by - 1);
            g.facing = FACE_DOWN;
            Input a; a.action = true;
            g.update(1.0f / 60, a); step(g, ui, none, 20);
            g.update(1.0f / 60, a); step(g, ui, none, 20);
            for (int k = 6; k < 10; k++) g.chestTransfer(true, k);
            shot(c, g, ui, "chest");
        }
        return 0;
    }

    if (!tourArg.empty()) {
        size_t pos = 0;
        while (pos <= tourArg.size()) {
            size_t comma = tourArg.find(',', pos);
            std::string item = tourArg.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
            pos = comma == std::string::npos ? tourArg.size() + 1 : comma + 1;
            if (item.empty()) continue;
            std::string loc = item;
            int tx = -1, ty = -1;
            size_t at = item.find('@');
            if (at != std::string::npos) {
                loc = item.substr(0, at);
                sscanf(item.c_str() + at + 1, "%d;%d", &tx, &ty);
            }
            g.timeOfDay = 11 * 60;
            if (!g.teleport(loc, 0, 0)) { printf("  ! %s not found\n", loc.c_str()); continue; }
            if (tx < 0) { tx = g.map().width() / 2; ty = g.map().height() / 2; }
            g.teleport(loc, tx, ty);
            if (g.cutscene.active) g.skipEvent();
            while (g.dialogue.active) g.dialogueNext();
            step(g, ui, none, 30);
            ui.tab = TAB_MAP;
            shot(c, g, ui, loc);
        }
        return 0;
    }

    if (!eventArg.empty()) {
        std::string loc = eventArg.substr(0, eventArg.find(':')), id = eventArg.substr(eventArg.find(':') + 1);
        FILE* f = fopen(data.vfs.path("data/events/" + loc + ".dat").c_str(), "rb");
        std::string key, script;
        if (f) {
            char buf[1 << 16];
            std::string line;
            int ch;
            while ((ch = fgetc(f)) != EOF) {
                if (ch != '\n') { line += (char)ch; continue; }
                size_t tab = line.find('\t');
                std::string k = line.substr(0, tab);
                if (tab != std::string::npos && k.substr(0, k.find('/')) == id) { key = k; script = line.substr(tab + 1); break; }
                line.clear();
            }
            (void)buf;
            fclose(f);
        }
        if (script.empty()) { fprintf(stderr, "event %s not found\n", eventArg.c_str()); return 1; }
        g.timeOfDay = 10 * 60;
        g.teleport(loc, 10, 10);
        if (!g.cutscene.active && !g.startEvent(loc, key, script)) { fprintf(stderr, "couldn't start\n"); return 1; }
        int dialogues = 0, frames = 0;
        bool heldShot = false;
        float sinceShot = 99;
        while (g.cutscene.active && frames < 60 * 600) {
            if (g.dialogue.active) {
                if (dialogues++ < 4 || (dialogues % 6) == 0) shot(c, g, ui, "event_" + std::to_string(g_shot + 1));
                const DialogueBox& d = g.dialogue;
                if (d.page < d.pages.size() && !d.pages[d.page].choices.empty()) g.dialogueChoose(0);
                else dialogueAdvance(g, ui);
                sinceShot = 0;
            }
            step(g, ui, none, 1);
            frames++;
            sinceShot += 1.0f / 60;
            if (sinceShot > 2.5f && g.cutscene.active && g.cutscene.fade < 0.5f) {
                shot(c, g, ui, "event_" + std::to_string(g_shot + 1));
                sinceShot = 0;
            }
            if (!g.cutscene.aboveHead.empty() && !heldShot) { heldShot = true; shot(c, g, ui, "event_item_above_head"); }
            if (g_shot >= 14 || frames > 60 * 400) break;
        }
        for (const Slot& s : g.inv)
            if (s.type == SLOT_TOOL && data.tool(s.id)) printf("  has tool: %s\n", data.tool(s.id)->name.c_str());
        printf("  cutscene %s: %d dialogue pages, %d frames, %s\n", eventArg.c_str(), dialogues, frames,
               g.cutscene.active ? "still running (stopped capturing)" : "finished");
        return 0;
    }

    // 1. Wake up at home
    step(g, ui, none, 30);
    shot(c, g, ui, "wake_up_farmhouse");

    // 2. Walk out the front door (find the warp back to the Farm)
    if (const Warp* w = g.map().warpTo("Farm")) g.teleport(g.location().name, w->x, w->y - 2);
    if (!walkUntilWarp(g, ui, 0, 1)) printf("  ! couldn't leave the farmhouse\n");
    step(g, ui, none, 20);
    shot(c, g, ui, "farm_morning");
    if (animalsArg) {   // Robin builds a coop and a barn, Marnie sells animals, a look inside
        g.money = 100000;
        g.addItem(388, 999); g.addItem(390, 999);
        g.teleport("Farm", 60, 20);
        for (auto& d : const_cast<Location&>(g.location()).debris) d = DEB_NONE;   // a cleared farm
        Input a; a.action = true;
        const char* want[3] = {"Coop", "Barn", "Silo"};
        for (int k = 0; k < 3; k++) {
            g.startBuilding(want[k]);
            int tries = 0;
            g.placeX = 50 + k * 9; g.placeY = 22;
            while (!g.placeOk(g.placeX, g.placeY) && tries++ < 600) { g.placeX = 44 + tries % 30; g.placeY = 20 + tries / 30; }
            if (k == 0) { step(g, ui, none, 5); shot(c, g, ui, "placing"); }
            g.updatePlacing(a, 0.016f);
            if (k == 0) { step(g, ui, none, 5); shot(c, g, ui, "construction"); }
            for (int n = 0; n < 4; n++) g.animalsNight();
        }
        printf("  %zu buildings built\n", g.buildings.size());
        for (auto& b : g.buildings) printf("    %s at %d,%d (%d days)\n", b.type.c_str(), b.x, b.y, b.daysLeft);
        const char* buy[5] = {"White Chicken", "White Chicken", "Duck", "White Cow", "White Cow"};
        for (auto b : buy) g.buyAnimal(b);
        g.hay = 100;
        for (int n = 0; n < 6; n++) g.animalsNight();
        FarmBuilding& cb = g.buildings[0];
        g.teleport("Farm", cb.x + 3, cb.y + 5);
        step(g, ui, none, 5);
        shot(c, g, ui, "farm_buildings");
        for (int k = 0; k < 2 && k < (int)g.buildings.size(); k++) {
            FarmBuilding& b = g.buildings[k];
            const BuildingType* bt = g.typeOf(b);
            g.teleport("Farm", b.x + bt->doorX, b.y + bt->doorY + 1);
            g.facing = FACE_UP;
            g.farmBuildingInteract(b.x + bt->doorX, b.y + bt->doorY);
            g.hopperInteract(bt->hopperX, bt->hopperY);
            step(g, ui, none, 90);
            printf("  inside %s: %zu things on the floor\n", g.location().name.c_str(), g.location().objects.size());
            shot(c, g, ui, std::string("inside_") + b.type);
        }
        ui.shop = "AnimalShop"; ui.shopUpgrade = true;
        shot(c, g, ui, "marnie_animals");
        ui.shop = "Carpenter";
        shot(c, g, ui, "robin_build");
        return 0;
    }
    if (mineArg > 0) {   // a mine floor: as you arrive, then after breaking rocks until the ladder shows
        g.enterMine(mineArg);
        step(g, ui, none, 30);
        shot(c, g, ui, "mine_" + std::to_string(mineArg));
        printf("  floor %d: %zu rocks, %zu monsters\n", mineArg, g.location().objects.size(), g.location().monsters.size());
        {   // fight one: stand next to it, swing until it's gone
            int sw = -1;
            for (int i = 0; i < HOTBAR; i++)
                if (g.inv[i].type == SLOT_TOOL && data.tool(g.inv[i].id)->kind == TOOL_WEAPON) sw = i;
            printf("  sword in slot %d\n", sw);
            if (sw >= 0 && !g.location().monsters.empty()) {
                g.selectSlot(sw);
                int before = (int)g.location().monsters.size();
                for (int round = 0; round < 40 && (int)g.location().monsters.size() == before; round++) {
                    const auto& mo = g.location().monsters.front();
                    int mx = (int)(mo.x / 16), my = (int)(mo.y / 16);
                    g.teleport(g.location().name, mx, my - 1);
                    g.fade = 0;
                    g.facing = FACE_DOWN;
                    Input a; a.action = true;
                    step(g, ui, a, 1);
                    if (round == 0) shot(c, g, ui, "mine_fight");
                    step(g, ui, none, 20);
                }
                printf("  monsters %d -> %zu, health %d\n", before, g.location().monsters.size(), g.health);
                for (int i = 0; i < 36; i++) if (g.inv[i].type == SLOT_OBJECT) printf("    has %s x%d\n", g.itemName(g.inv[i]).c_str(), g.inv[i].count);
            }
        }
        int broken = 0;
        size_t pick = 0;
        while (pick < g.location().objects.size() && broken < 400) {
            const WorldObject o = g.location().objects[pick];
            static const int sdx[4] = {0, -1, 0, 1}, sdy[4] = {1, 0, -1, 0}, face[4] = {FACE_UP, FACE_RIGHT, FACE_DOWN, FACE_LEFT};
            bool placed = false;
            for (int k = 0; k < 4 && !placed; k++) {
                g.teleport(g.location().name, o.x + sdx[k], o.y + sdy[k]);
                g.fade = 0;
                if ((int)(g.px / 16) == o.x + sdx[k] && (int)((g.py - 4) / 16) == o.y + sdy[k]) { g.facing = face[k]; placed = true; }
            }
            if (!placed) { pick++; continue; }
            step(g, ui, none, 2);
            for (int i = 0; i < HOTBAR; i++)
                if (g.inv[i].type == SLOT_TOOL && data.tool(g.inv[i].id)->kind == TOOL_PICKAXE) g.selectSlot(i);
            g.energy = 270;
            { Input a; a.action = true; step(g, ui, a, 1);
              if (broken < 3) printf("  rock %d,%d at me %.0f,%.0f sel %d tool %.2f energy %d dlg %d cs %d\n", o.x, o.y, g.px, g.py, g.selected, g.toolTimer, g.energy, g.dialogue.active, g.cutscene.active);
              step(g, ui, none, 25); }
            broken++;
            bool ladder = false;
            const TileMap& mm = g.map();
            for (int y = 0; y < mm.height() && !ladder; y++)
                for (int x = 0; x < mm.width(); x++)
                    if (mm.tile(L_BUILDINGS, x, y) == 173) { ladder = true; printf("  ladder at %d,%d after %d rocks (tile %d sheet %d '%s' cols %d)\n", x, y, broken, mm.tile(L_BUILDINGS, x, y), mm.tileset(L_BUILDINGS, x, y), mm.tilesets[mm.tileset(L_BUILDINGS, x, y)].texture.c_str(), mm.tilesets[mm.tileset(L_BUILDINGS, x, y)].columns); g.teleport(g.location().name, x + 2, y + 2); break; }
            if (ladder) break;
        }
        printf("  broke %d, %zu rocks left, toast '%s'\n", broken, g.location().objects.size(), g.toast.c_str());
        step(g, ui, none, 30);
        shot(c, g, ui, "mine_ladder");
        g.facing = FACE_UP;
        { Input a; a.action = true; step(g, ui, a, 1); }
        step(g, ui, none, 30);
        printf("  now in %s\n", g.location().name.c_str());
        shot(c, g, ui, "mine_next");
        return 0;
    }
    if (!whereMap.empty()) {
        // time the top screen at a location (centre of the map), per drawing stage
        if (!g.hasMap(whereMap)) { printf("  ! no map %s\n", whereMap.c_str()); return 1; }
        g.teleport(whereMap, 10, 10);
        ui.mapCache = mapCacheArg;
        if (dayArg > 0) { g.season = (dayArg - 1) / 28; g.day = (dayArg - 1) % 28 + 1; }
        g.teleport(whereMap, atX >= 0 ? atX : g.map().width() / 2, atY >= 0 ? atY : g.map().height() / 2);
        if (timeArg >= 0) {
            int until = (timeArg / 100) * 60 + timeArg % 100;
            while (g.timeOfDay < until) {
                g.timeOfDay += 1;
                for (int k = 0; k < 10; k++) g.npcsUpdate(0.1f);
            }
            for (auto& n : g.npcs)
                if (n.anim) printf("  %s in %s at %d,%d: animating (frame %d)\n", n.def->name.c_str(), n.loc.c_str(), n.tileX(), n.tileY(), n.animFrame());
        }
        const TileMap& mm = g.map();
        step(g, ui, none, 5);
        if (weatherArg == "rain" || weatherArg == "storm") { g.raining = true; g.stormy = weatherArg == "storm"; }
        if (weatherArg == "snow") { g.season = 3; g.snowing = true; }
        if (weatherArg == "storm") g.lightning = 0.6f;
        if (weatherArg == "night") { g.timeOfDay = 21 * 60; g.updateNightTiles(); }
        for (int k = 0; k < 3; k++) drawTop(c, g, ui, 0);   // warm up: load its textures
        g_profClock = [] { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count(); };
        float tot[PROF_COUNT] = {};
        c.draws = 0;
        const int N = 20;
        for (int k = 0; k < N; k++) {
            drawTop(c, g, ui, (uint32_t)(k * 16));
            for (int q = 0; q < PROF_COUNT; q++) tot[q] += g_profMs[q];
        }
        printf("  %s (%dx%d tiles, %zu tilesets, %zu NPCs here): draws/frame %u\n", whereMap.c_str(), mm.width(), mm.height(),
               mm.tilesets.size(), (size_t)std::count_if(g.npcs.begin(), g.npcs.end(), [&](const NpcState& n) { return n.loc == whereMap; }),
               c.draws / N);
        printf("  host ms/frame: map %.2f obj %.2f front %.2f weather %.2f hud %.2f\n", tot[0] / N, tot[1] / N, tot[2] / N,
               tot[3] / N, tot[4] / N);
        shot(c, g, ui, "where_" + whereMap);
        if (panArg) {
            ui.tab = TAB_OPTIONS;
            shot(c, g, ui, "options_tab");
            ui.tab = TAB_ITEMS;   // walk the camera around: the map cache has to follow
            int bx = (int)(g.px / 16), by = (int)(g.py / 16);
            static const int path[6][2] = {{5, 0}, {11, 2}, {17, 9}, {9, 14}, {-6, 6}, {-14, -3}};
            for (int k = 0; k < 6; k++) {
                g.teleport(whereMap, bx + path[k][0], by + path[k][1]);
                for (int f = 0; f < 3; f++) drawTop(c, g, ui, (uint32_t)(f * 300 + k * 977));
                shot(c, g, ui, "pan" + std::to_string(k));
            }
        }
        return 0;
    }
    if (swingDemo) {
        // every tool x facing x phase, the top screen only (crop around the farmer later)
        for (int i = 0; i < HOTBAR; i++) {
            const Slot& s = g.inv[i];
            if (s.type != SLOT_TOOL) continue;
            const ToolDef* td = data.tool(s.id);
            if (!td || !(td->kind == TOOL_HOE || td->kind == TOOL_AXE || td->kind == TOOL_PICKAXE || td->kind == TOOL_CAN)) continue;
            g.selectSlot(i);
            for (int f = 0; f < 4; f++)
                for (float left : {0.27f, 0.15f, 0.05f}) {
                    g.facing = f;
                    g.toolTimer = left;
                    g.moving = false;
                    shot(c, g, ui, "swing_" + td->key + "_" + std::to_string(f) + "_" + std::to_string((int)(left * 100)));
                }
        }
        g.toolTimer = 0;
        // effects: stone chips and a felled tree, a few moments in
        g.facing = FACE_RIGHT;
        g.burst("Maps_springobjects", (343 % 24) * 16, (343 / 24) * 16, 16, 16, g.px + 20, g.py, 7);
        Game::Effect e;
        e.kind = Game::Effect::FALLING_TREE;
        e.tex = "TerrainFeatures_tree1_spring";
        e.sw = 48; e.sh = 96;
        e.x = g.px + 60; e.y = g.py - 4; e.dir = 1; e.life = 1.4f;
        g.effects.push_back(e);
        for (int i = 0; i < HOTBAR; i++)
            if (g.inv[i].type == SLOT_OBJECT) { g.selectSlot(i); break; }
        for (int f = 0; f < 4; f++) {
            g.facing = f;
            g.moving = f == 1;
            g.walkTime = 0.15f;
            shot(c, g, ui, "carry_" + std::to_string(f));
        }
        g.moving = false;
        // fishing: every state, facing right / left / down
        g.giveTool("BambooPole");
        for (int i = 0; i < INV_SIZE; i++)
            if (g.inv[i].type == SLOT_TOOL && data.tool(g.inv[i].id) && data.tool(g.inv[i].id)->kind == TOOL_ROD) { g.selectSlot(i); break; }
        int ftx = (int)(g.px / 16), fty = (int)(g.py / 16);
        for (int f : {FACE_RIGHT, FACE_LEFT, FACE_DOWN}) {
            g.facing = f;
            g.fishing.bobX = ftx + (f == FACE_RIGHT ? 4 : f == FACE_LEFT ? -4 : 0);
            g.fishing.bobY = fty + (f == FACE_DOWN ? 4 : 0);
            struct St { FishState s; float t; } sts[] = {{FS_CHARGE, 0.5f}, {FS_CAST, 0.25f}, {FS_WAIT, 0.15f}, {FS_WAIT, 1.0f}};
            int k = 0;
            for (auto st : sts) {
                g.fishing.state = st.s;
                g.fishing.t = st.t;
                g.fishing.power = 0.7f;
                shot(c, g, ui, "fish_" + std::to_string(f) + "_" + std::to_string(k++));
            }
        }
        g.fishing.state = FS_NONE;
        g.addItem(390, 1);   // stone
        g.itemFlies(390, g.px + 20, g.py);
        g.addItem(388, 3);   // wood
        g.itemFlies(388, g.px + 60, g.py - 4, 3);
        g.addItem(388, 2);
        for (int k = 0; k < 4; k++) {
            shot(c, g, ui, "effects_" + std::to_string(k));
            g.updateEffects(0.3f);
        }
        printf("  player at %.0f,%.0f\n", g.px, g.py);
        return 0;
    }

    // 3. Farm a little plot right below the porch
    Input down; down.my = 1;
    step(g, ui, down, 40);
    int hoe = -1, seeds = -1, can = -1;
    for (int i = 0; i < HOTBAR; i++) {
        const Slot& s = g.inv[i];
        if (s.type == SLOT_TOOL && data.tool(s.id)->kind == TOOL_HOE) hoe = i;
        if (s.type == SLOT_TOOL && data.tool(s.id)->kind == TOOL_CAN) can = i;
        if (s.type == SLOT_OBJECT && data.cropForSeed(s.id)) seeds = i;
    }
    Input act; act.action = true;
    Input left; left.mx = -1;
    for (int col = 0; col < 4; col++) {
        for (int k : {hoe, seeds, can}) {
            if (k < 0) continue;
            g.selectSlot(k);
            g.facing = FACE_DOWN;
            step(g, ui, act, 1);
            step(g, ui, none, 25);
        }
        step(g, ui, left, 13);
        g.facing = FACE_DOWN;
    }
    g.selectSlot(seeds >= 0 ? seeds : 0);
    shot(c, g, ui, "first_crops");

    // 4. Head east to the bus stop
    g.teleport("Farm", g.farmLocation().map.width() - 3, 16);
    if (!walkUntilWarp(g, ui, 1, 0)) printf("  ! no warp east of the farm\n");
    step(g, ui, none, 10);
    shot(c, g, ui, "bus_stop");

    // 5. Into town, map tab open
    if (!walkUntilWarp(g, ui, 1, 0)) printf("  ! no warp east of the bus stop\n");
    step(g, ui, none, 10);
    ui.tab = TAB_MAP;
    shot(c, g, ui, "pelican_town_map_tab");
    ui.tab = TAB_ITEMS;

    // 6. Pierre's before opening time
    const TileMap& town = g.map();
    int doorX = -1, doorY = -1;
    for (int y = 0; y < town.height() && doorX < 0; y++)
        for (int x = 0; x < town.width(); x++)
            if (town.action(x, y).find("SeedShop") != std::string::npos) { doorX = x; doorY = y; break; }
    if (doorX >= 0) {
        g.timeOfDay = 8 * 60;
        g.teleport(g.location().name, doorX, doorY + 1);
        g.facing = FACE_UP;
        Input up; up.my = -1;
        step(g, ui, up, 40);
        shot(c, g, ui, "pierres_closed");

        // 7. ...and after 9am
        g.timeOfDay = 10 * 60;
        g.toastTime = 0;
        walkUntilWarp(g, ui, 0, -1, 60);
        step(g, ui, none, 10);
        shot(c, g, ui, "inside_pierres");

        // 8. Talk to the counter
        const TileMap& shop = g.map();
        for (int y = 0; y < shop.height(); y++)
            for (int x = 0; x < shop.width(); x++)
                if (shop.action(x, y).rfind("OpenShop", 0) == 0 || shop.action(x, y) == "Buy General") {
                    g.teleport(g.location().name, x, y + 1);
                    g.facing = FACE_UP;
                    y = shop.height();
                    break;
                }
        step(g, ui, act, 1);
        step(g, ui, none, 5);
        if (!ui.shop.empty()) {
            tap(g, ui, 297, 58 + 2 + 5);    // + on row 0
            tap(g, ui, 297, 58 + 2 + 5);
            tap(g, ui, 297, 58 + 2 + 5);
            tap(g, ui, 297, 82 + 2 + 5);    // + on row 1
            shot(c, g, ui, "pierres_shop_buy");
            tap(g, ui, 240, 220);           // Buy
            tap(g, ui, 90, 45);             // Sell tab
            shot(c, g, ui, "pierres_shop_sell");
            tap(g, ui, BOT_W - 24, 20);     // close
            // talk to whoever is in the shop
            for (auto& n : g.npcs) {
                if (n.loc != g.location().name) continue;
                g.teleport(g.location().name, n.tileX(), n.tileY() + 1);
                g.facing = FACE_UP;
                g.selectSlot(0);   // a tool, so A talks instead of giving a gift
                step(g, ui, none, 1);
                Input a1; a1.action = true;
                step(g, ui, a1, 1);
                break;
            }
            if (g.dialogue.active) shot(c, g, ui, "talking");
            while (g.dialogue.active) {
                const DialogueBox& d = g.dialogue;
                if (d.page < d.pages.size() && !d.pages[d.page].choices.empty()) g.dialogueChoose(0);
                else dialogueAdvance(g, ui);
            }
            ui.tab = TAB_SOCIAL;
            shot(c, g, ui, "social_tab");
            ui.tab = TAB_ITEMS;
        } else {
            printf("  ! shop didn't open\n");
        }
    } else {
        printf("  ! no SeedShop door found in %s\n", g.location().name.c_str());
    }

    // 9. Evening back on the farm
    g.teleport("Farm", 60, 22);
    g.timeOfDay = 19 * 60 + 30;
    step(g, ui, none, 5);
    shot(c, g, ui, "farm_evening");
    return 0;
}
