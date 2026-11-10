// Host tests for the game rules, run against the placeholder world:
//   python3 tests/make_fake_content.py fake
//   python3 tools/convert.py "fake/Content (unpacked)" out
//   g++ -std=c++17 -Isource -Isource/core tests/core_test.cpp source/core/*.cpp -o core_test
//   ./core_test out/3ds/stardew3ds/
#include <cassert>
#include <cstdio>

#include "game.h"
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

    // save / load round trip
    assert(g.save("/tmp/sv_test.sav"));
    Game g2(d);
    assert(g2.load("/tmp/sv_test.sav"));
    assert(g2.day == g.day && g2.money == g.money);
    assert(g2.farmLocation().soils[ty * g2.farmLocation().map.width() + tx].seed == 472);
    printf("all core tests passed\n");
    return 0;
}
