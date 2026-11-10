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
    for (int i = 3; i < argc; i++) {
        std::string a = argv[i];
        if (a.rfind("--event=", 0) == 0) eventArg = a.substr(8);
        else font = a;
    }

    GameData data;
    std::string err;
    if (!data.load(g_root, &err)) { fprintf(stderr, "data: %s\n", err.c_str()); return 1; }
    Game g(data);
    if (!g.newGame(42, &err)) { fprintf(stderr, "newGame: %s\n", err.c_str()); return 1; }
    HostCanvas c(g_root, font);
    Ui ui;
    sceneAfterUpdate(g, ui, g_save);
    Input none;

    if (!eventArg.empty()) {
        std::string loc = eventArg.substr(0, eventArg.find(':')), id = eventArg.substr(eventArg.find(':') + 1);
        FILE* f = fopen((g_root + "data/events/" + loc + ".dat").c_str(), "rb");
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
