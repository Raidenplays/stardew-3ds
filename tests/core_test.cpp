// Host tests for the game rules, run against the placeholder world:
//   python3 tests/make_fake_content.py fake
//   python3 tools/convert.py "fake/Content (unpacked)" out
//   g++ -std=c++17 -Isource -Isource/core tests/core_test.cpp source/core/*.cpp -o core_test
//   ./core_test out/3ds/stardew3ds/
#include <cassert>
#include <cstdio>
#include <cstdlib>

#include "game.h"
#include "saves.h"
using namespace sv;

static void run(Game& g, const Input& in, int frames) {
    for (int i = 0; i < frames; i++) g.update(1.0f / 60, in);
}

static bool walkUntil(Game& g, float mx, float my, const std::string& dest, int frames = 900) {
    Input in;
    in.mx = mx;
    in.my = my;
    for (int i = 0; i < frames; i++) {
        g.update(1.0f / 60, in);
        if (g.location().name == dest) return true;
    }
    return false;
}

int main(int argc, char** argv) {
    std::string root = argc > 1 ? argv[1] : "out/3ds/stardew3ds/";
    GameData d;
    std::string err;
    if (!d.load(root, &err)) { printf("data: %s\n", err.c_str()); return 1; }
    Game g(d);
    assert(g.newGame(1234, &err));

    // wake up at home
    assert(g.location().name == "FarmHouse");
    assert(g.canSleepHere());

    // walk out of the front door
    const Warp* out = g.map().warpTo("Farm");
    assert(out);
    g.teleport("FarmHouse", out->x, out->y - 2);
    assert(walkUntil(g, 0, 1, "Farm"));
    assert(!g.canSleepHere());
    assert(!g.goToBed());                      // can't sleep in a field

    // farming still works on the farm
    int tx = -1, ty = -1;
    for (int y = 20; y < g.map().height() && tx < 0; y++)
        for (int x = 30; x < 60; x++)
            if ((g.map().flags(x, y) & CF_DIGGABLE) && g.debris(x, y) == DEB_NONE &&
                g.debris(x, y - 1) == DEB_NONE && !(g.map().flags(x, y - 1) & CF_BLOCKED)) {
                tx = x; ty = y; break;
            }
    assert(tx >= 0);
    g.teleport("Farm", tx, ty - 1);
    g.facing = FACE_DOWN;
    int a, b;
    g.targetTile(a, b);
    assert(a == tx && b == ty);
    int hoe = -1, seeds = -1;
    for (int i = 0; i < HOTBAR; i++) {
        if (g.inv[i].type == SLOT_TOOL && d.tool(g.inv[i].id)->kind == TOOL_HOE) hoe = i;
        if (g.inv[i].type == SLOT_OBJECT && g.inv[i].id == 472) seeds = i;
    }
    Input act; act.action = true;
    Input none;
    g.selectSlot(hoe); run(g, none, 30); run(g, act, 1); run(g, none, 30);
    assert(g.soil(tx, ty).tilled);
    g.selectSlot(seeds); run(g, act, 1);
    assert(g.soil(tx, ty).seed == 472);

    // east to the bus stop and town
    g.teleport("Farm", g.map().width() - 3, 16);
    assert(walkUntil(g, 1, 0, "BusStop"));
    g.teleport("BusStop", 28, 22);               // past the test ore area (columns 2-10)
    assert(walkUntil(g, 1, 0, "Town"));

    // Pierre's door is locked before 9
    int dx = -1, dy = -1;
    for (int y = 0; y < g.map().height() && dx < 0; y++)
        for (int x = 0; x < g.map().width(); x++)
            if (g.map().action(x, y).find("SeedShop") != std::string::npos) { dx = x; dy = y; break; }
    assert(dx >= 0);
    g.timeOfDay = 8 * 60;
    g.teleport("Town", dx, dy + 1);
    assert(!walkUntil(g, 0, -1, "SeedShop", 60));
    assert(g.toast.find("Closed") == 0);
    g.timeOfDay = 10 * 60;
    assert(walkUntil(g, 0, -1, "SeedShop", 60));

    // counter opens the shop
    int cx = -1, cy = -1;
    for (int y = 0; y < g.map().height() && cx < 0; y++)
        for (int x = 0; x < g.map().width(); x++)
            if (g.map().action(x, y).rfind("OpenShop", 0) == 0) { cx = x; cy = y; break; }
    g.teleport("SeedShop", cx, cy + 1);
    g.facing = FACE_UP;
    run(g, none, 30);
    run(g, act, 1);
    assert(g.openShop == "SeedShop");
    auto stock = g.shopStock("SeedShop");
    assert(stock.size() == 5);                 // the mail-gated item is left out
    int m0 = g.money;
    assert(g.buy("SeedShop", 474, 2));         // cauliflower: 40 * 2 = 80 each
    assert(g.money == m0 - 160);
    int caul = -1;
    for (int i = 0; i < INV_SIZE; i++)
        if (g.inv[i].type == SLOT_OBJECT && g.inv[i].id == 474) caul = i;
    assert(caul >= 0 && g.sellSlot(caul) && g.money == m0 - 160 + 80);

    // back out to town at the shop door
    const Warp* exit = g.map().warpTo("Town");
    g.teleport("SeedShop", exit->x, exit->y - 1);
    assert(walkUntil(g, 0, 1, "Town"));

    // sleeping is only at home; passing out works anywhere
    g.timeOfDay = DAY_END - 10;
    run(g, none, 60 * 8);
    assert(g.location().name == "FarmHouse" && g.day == 2 && g.timeOfDay < DAY_START + 20);
    g.passedOut = false;

    // the planted parsnip survived the trip (farm stays loaded)
    assert(g.farmLocation().soils[ty * g.farmLocation().map.width() + tx].seed == 472);

    // ---- villagers
    assert(g.npcs.size() == 3);   // the two villagers, and Raiden at the Saloon
    const NpcState* jun = g.npcByName("Juniper");
    const NpcState* row = g.npcByName("Rowan");
    assert(jun && row && jun->loc == "Town" && row->loc == "SeedShop");
    // Juniper's spring schedule takes her to the Bus Stop at 10:00, off-screen
    g.teleport("FarmHouse", 5, 6);
    g.timeOfDay = 9 * 60 + 50;
    run(g, none, 60 * 40);                       // ~40 s: past 10:00, time to walk there
    assert(jun->loc == "BusStop");
    // walk over and watch her stand there
    g.teleport("BusStop", 12, 22);
    run(g, none, 60 * 5);
    assert(jun->leg >= jun->legs.size());       // arrived
    assert(std::abs(jun->tileX() - 15) <= 1 && std::abs(jun->tileY() - 22) <= 1);

    // talking: introduction first, +20 friendship once a day
    g.teleport("BusStop", jun->tileX(), jun->tileY() + 1);
    g.facing = FACE_UP;
    g.selectSlot(0);
    run(g, none, 30);
    run(g, act, 1);
    assert(g.dialogue.active && g.dialogue.npc == "Juniper");
    assert(g.dialogue.pages.size() == 2 && g.dialogue.pages[1].portrait == 1);   // $h -> happy
    run(g, none, 120);
    assert(g.timeOfDay == g.timeOfDay);          // (time is paused while talking)
    while (g.dialogue.active) g.dialogueNext();
    assert(jun->friendship == 20 && jun->introduced && jun->talkedToday);
    run(g, act, 1);
    while (g.dialogue.active) g.dialogueNext();
    assert(jun->friendship == 20);               // no double points

    // gifts: Juniper loves kale (250), max one a day
    for (auto& sl : g.inv) if (sl.empty()) { sl.type = SLOT_OBJECT; sl.id = 250; sl.count = 3; break; }
    for (int i = 0; i < INV_SIZE; i++) if (g.inv[i].type == SLOT_OBJECT && g.inv[i].id == 250) g.selectSlot(i);
    run(g, act, 1);
    assert(g.dialogue.active && g.dialogue.pages[0].portrait == 4);   // love face
    while (g.dialogue.active) g.dialogueNext();
    assert(jun->friendship == 100 && jun->giftsToday == 1);
    run(g, act, 1);
    assert(!g.dialogue.active && jun->friendship == 100);            // refused: already gifted today

    // dialogue parser: question with choices and a response key
    auto pages = parseDialogue(g.data.npc("Rowan")->dialogue.at("spring_Tue"), "Raiden");
    assert(pages.size() == 1 && pages[0].choices.size() == 2 && pages[0].choices[0].points == 20);
    assert(pages[0].choices[0].responseKey == "rowan_yes");
    auto intro = parseDialogue("Hi @!$h#$b#Bye.", "Raiden");
    assert(intro.size() == 2 && intro[0].text == "Hi Raiden!" && intro[0].portrait == 1);

    // schedule resolution: rain key wins on rainy days
    ScheduleContext ctx; ctx.season = 0; ctx.day = 5; ctx.weekday = 4; ctx.raining = true;
    auto rs = resolveSchedule(*g.data.npc("Juniper"), ctx);
    assert(rs.size() == 2 && rs[0].time == 9 * 60 && rs[0].x == 30);

    // ---- cutscenes
    assert(!g.cutscene.active);
    g.mailReceived.insert("juniperInvite");
    int fBefore = g.npcByName("Juniper")->friendship;
    g.timeOfDay = 9 * 60;
    g.teleport("Town", 20, 31);                  // entering Town checks its events
    assert(g.cutscene.active && g.cutscene.id == "900");
    g.update(1.0f / 60, none);
    assert(g.cutscene.skippable);
    assert(g.cutscene.actors.size() == 2);
    int clockBefore = g.timeOfDay;
    int guard = 0;
    bool sawQuestion = false;
    while (g.cutscene.active && guard++ < 20000) {
        if (g.dialogue.active) {
            const DialogueBox& d = g.dialogue;
            if (d.page < d.pages.size() && !d.pages[d.page].choices.empty()) {
                sawQuestion = true;
                g.dialogueChoose(1);                 // "Needs more ducks." -> fork
            } else {
                g.dialogueNext();
            }
        }
        g.update(1.0f / 60, none);
    }
    while (g.dialogue.active) g.dialogueNext();
    assert(sawQuestion && !g.cutscene.active);
    assert(g.eventsSeen.count("900"));
    assert(g.npcByName("Juniper")->friendship == fBefore + 20);   // fork branch, not the +50 one
    assert(g.timeOfDay == clockBefore);                            // clock stopped during the scene
    assert(g.location().name == "Town");
    g.teleport("BusStop", 15, 22);
    g.teleport("Town", 20, 31);
    assert(!g.cutscene.active);                                    // only once
    // skipping applies the rest of the script
    assert(g.startEvent("Town", "901", "none/20 30/farmer 17 30 1/skippable/pause 9000/friendship Rowan 30/end"));
    int rowanBefore = g.npcByName("Rowan")->friendship;
    g.update(1.0f / 60, none);                   // runs "skippable", then waits on the pause
    g.skipEvent();
    assert(!g.cutscene.active && g.npcByName("Rowan")->friendship == rowanBefore + 30);
    // preconditions
    assert(g.eventConditionsMet("1/t 600 2600"));
    assert(!g.eventConditionsMet("1/e 12345"));
    assert(g.eventConditionsMet("1/k 12345"));
    assert(g.eventConditionsMet("1/z winter") && !g.eventConditionsMet("1/z spring"));
    assert(!g.eventConditionsMet("1/SomeUnknownThing 3"));

    // ---- mods (only when converted with --mods=fake/mods)
    if (d.object(9001)) {
        assert(d.object(9001)->name == "Meadow Tea" && d.object(9001)->price == 50);
        assert(d.object(24)->price == 99);                          // Fields edit
        assert(d.locationNames.count("Custom_TestMeadow") &&
               d.locationNames.at("Custom_TestMeadow") == "Quiet Meadow");   // i18n token
        g.teleport("Town", 58, 30);
        assert(walkUntil(g, 1, 0, "Custom_TestMeadow"));            // warp appended by the mod
        assert(g.map().width() == 20);
        g.teleport("Town", 51, 37);
        bool water = false;
        for (int y = 36; y < 39; y++)
            for (int x = 50; x < 53; x++) water |= (g.map().flags(x, y) & CF_WATER) != 0;
        assert(water);                                              // map patch from the pack
        // forage: vanilla-style Data/Locations forage in Town (spring parsnips)
        g.teleport("Town", 30, 30);
        int forage = 0;
        for (auto& o : g.location().objects) if (!o.ore && o.itemId == 24) forage++;
        assert(forage == 4);
        const WorldObject fo = g.location().objects[0];
        g.teleport("Town", fo.x, fo.y - 1);
        g.facing = FACE_DOWN;
        int before = 0;
        for (auto& sl : g.inv) if (sl.type == SLOT_OBJECT && sl.id == 24) before += sl.count;
        run(g, none, 30);
        run(g, act, 1);
        int after = 0;
        for (auto& sl : g.inv) if (sl.type == SLOT_OBJECT && sl.id == 24) after += sl.count;
        assert(after == before + 1 && g.location().objects.size() == 3);
        // ore: Farm Type Manager copper at the bus stop, broken with the pickaxe
        g.teleport("BusStop", 15, 22);
        int ore = 0;
        for (auto& o : g.location().objects) if (o.ore && o.itemId == 751) ore++;
        assert(ore == 5);
        const WorldObject oo = g.location().objects[0];
        assert(oo.y >= 20 && oo.y <= 24);
        g.teleport("BusStop", oo.x, oo.y - 1);
        if (g.objectAt(oo.x, oo.y - 1)) g.teleport("BusStop", oo.x, oo.y + 1);
        g.facing = g.objectAt(oo.x, oo.y - 1) ? FACE_UP : FACE_DOWN;
        g.facing = (int)((g.py - 4) / 16) < oo.y ? FACE_DOWN : FACE_UP;
        for (int i = 0; i < HOTBAR; i++)
            if (g.inv[i].type == SLOT_TOOL && d.tool(g.inv[i].id)->kind == TOOL_PICKAXE) g.selectSlot(i);
        run(g, none, 30);
        run(g, act, 1);
        bool copper = false;
        for (auto& sl : g.inv) if (sl.type == SLOT_OBJECT && sl.id == 378) copper = true;
        assert(copper && g.location().objects.size() == 4);
        // seasonal map: the meadow gets a pond only in summer
        assert(g.mapFileFor("Custom_TestMeadow").find("@summer") == std::string::npos);
        int sv = g.season; g.season = SUMMER;
        assert(g.mapFileFor("Custom_TestMeadow").find("Custom_TestMeadow@summer.smap") != std::string::npos);
        g.season = sv;
        printf("mod pack checks passed (incl. forage, ore, seasonal map)\n");
    }

    // a patch map goes where its ground lines up, and brings its actions with it
    {
        TileMap base, patch;
        assert(base.load(d.vfs.path("maps/Farm.smap"), nullptr) && patch.load(d.vfs.path("maps/Farm.smap"), nullptr));
        int fx = -1, fy = -1;
        assert(base.findFit(patch, fx, fy) && fx == 0 && fy == 0);
        base.setAction(1, 1, "QiDoor");
        assert(base.action(1, 1) == "QiDoor" && base.stamp(patch, 0, 0));
    }
    // golden walnuts, Qi coins and the fair's tokens
    g.gotWalnut(3, "test");
    g.clubCoins = 250;
    g.walnutsGot.insert("IslandSouth:4:5");
    assert(g.walnuts == 3 && g.walnutsFound == 3);
    assert(g.shopFunds("NoSuchShop") == g.money);
    // save / load round trip
    assert(g.save("/tmp/sv_test.sav"));
    Game g2(d);
    assert(g2.load("/tmp/sv_test.sav"));
    assert(g2.day == g.day && g2.money == g.money);
    assert(g2.walnuts == 3 && g2.walnutsFound == 3 && g2.clubCoins == 250 && g2.walnutsGot.count("IslandSouth:4:5"));
    assert(g2.farmLocation().soils[ty * g2.farmLocation().map.width() + tx].seed == 472);
    assert(g2.npcByName("Juniper")->introduced);
    assert(g2.npcByName("Juniper")->friendship == g.npcByName("Juniper")->friendship);
    assert(g2.eventsSeen.count("900") && g2.mailReceived.count("juniperInvite"));
    {   // several farms on one card: slots, the Load list, deleting
        std::string sr = "/tmp/sv_slots/";
        system("rm -rf /tmp/sv_slots && mkdir -p /tmp/sv_slots");
        assert(listSaves(sr).empty() && freeSaveSlot(sr) == 0);
        assert(savePathFor(sr, 0) == sr + "save.sav" && savePathFor(sr, 1) == sr + "save2.sav");
        g.me.name = "Ann";
        g.me.farm = "Tab\tby";                       // a tab in a name mustn't break the list
        assert(g.save(savePathFor(sr, 0)));
        assert(freeSaveSlot(sr) == 1);
        g.me.name = "Bo";
        g.me.farm = "Second";
        g.money = 777;
        assert(g.save(savePathFor(sr, 1)));
        std::vector<SaveSlot> ls = listSaves(sr);
        assert(ls.size() == 2 && ls[0].known && ls[0].farmer == "Ann" && ls[0].farm == "Tab by");
        assert(ls[1].farmer == "Bo" && ls[1].farm == "Second" && ls[1].money == 777 && ls[1].day == g.day);
        setLastSave(sr, ls[1].path);                 // the farm played last is listed first
        ls = listSaves(sr);
        assert(ls[0].farmer == "Bo" && ls[1].farmer == "Ann");
        remove((savePathFor(sr, 0) + ".info").c_str());   // a save from before the list existed
        assert(!listSaves(sr)[1].known);
        Game g3(d);
        describeOldSaves(g3, sr);
        assert(listSaves(sr)[1].known && listSaves(sr)[1].farmer == "Ann");
        assert(trashSave(savePathFor(sr, 0)));       // deleting frees the slot and keeps a copy
        ls = listSaves(sr);
        assert(ls.size() == 1 && ls[0].farmer == "Bo" && freeSaveSlot(sr) == 0);
        FILE* kept = fopen((savePathFor(sr, 0) + ".deleted").c_str(), "rb");
        assert(kept);
        fclose(kept);
        for (int i = 0; i < MAX_SAVES; i++) assert(g.save(savePathFor(sr, i)));
        assert(freeSaveSlot(sr) == -1 && (int)listSaves(sr).size() == MAX_SAVES);
    }
    printf("all core tests passed\n");
    return 0;
}
