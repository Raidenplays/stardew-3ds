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
    bool titleDemo = false;   // title screen + character creation    // sleep to day 2 and read the first letter    // stand by water in the Forest and show the reeling meter   // place machines near the house, shoot the Craft tab + a chest    // "Loc[@x,y],Loc2,..." -> one screenshot per location
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
        else font = a;
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
    c.resolveFn = [](const std::string& rel, void* ctx) { return ((Vfs*)ctx)->path(rel); };
    c.resolveCtx = &data.vfs;
    if (!getenv("S3DS_SYSTEM_FONT")) loadUiFonts(c, [&data](const std::string& rel) { return data.vfs.path(rel); });
    Ui ui;
    sceneAfterUpdate(g, ui, g_save);
    Input none;

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
        t.splashT = 5.0f;
        shoot("credits_port");
        titleKey(t, data, TK_A);              // skip to the title
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
            if (g_shot >= 10) break;
        }
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
