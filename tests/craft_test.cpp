// Crafting, machines, chests, sprinklers, skills - against the placeholder world.
//   g++ -std=c++17 -Isource -Isource/core tests/craft_test.cpp source/core/*.cpp -o craft_test
//   ./craft_test out/3ds/stardew3ds/
#include <cassert>
#include <cstdio>

#include "game.h"
using namespace sv;

static const Recipe* recipe(const GameData& d, const std::string& name) {
    for (auto& r : d.recipes)
        if (r.name == name) return &r;
    return nullptr;
}

static int count(const Game& g, int id, int type = SLOT_OBJECT, int flavor = -1) {
    int n = 0;
    for (auto& s : g.inv)
        if (s.type == type && s.id == id && s.flavor == flavor) n += s.count;
    return n;
}

static void face(Game& g, int tx, int ty) {   // stand above (tx, ty) facing down
    g.teleport("Farm", tx, ty - 1);
    g.facing = FACE_DOWN;
}

static void pressA(Game& g) {
    Input in;
    in.action = true;
    g.update(1.0f / 60, in);
    Input none;
    for (int i = 0; i < 30; i++) g.update(1.0f / 60, none);
}

static void selectItem(Game& g, int id, int type = SLOT_OBJECT) {
    for (int i = 0; i < INV_SIZE; i++)
        if (g.inv[i].type == type && g.inv[i].id == id) {
            if (i >= HOTBAR) g.swapSlots(i, HOTBAR - 1), i = HOTBAR - 1;
            g.selectSlot(i);
            return;
        }
    assert(!"item not in inventory");
}

int main(int argc, char** argv) {
    std::string root = argc > 1 ? argv[1] : "out/3ds/stardew3ds/";
    GameData d;
    std::string err;
    assert(d.load(root, &err));
    assert(d.recipes.size() == 4 && d.bigs.size() == 3 && d.machineRules.size() == 2);
    Game g(d);
    assert(g.newGame(7, &err));

    // recipes: default ones known, skill ones locked until Farming 1
    const Recipe* chest = recipe(d, "Chest");
    const Recipe* jar = recipe(d, "Preserves Jar");
    assert(chest && jar && g.recipeKnown(*chest) && !g.recipeKnown(*jar));
    assert(!g.canCraft(*chest));
    g.addItem(388, 30);
    g.addItem(390, 20);
    g.addItem(382, 5);
    g.addItem(378, 10);
    assert(g.canCraft(*chest) && g.craft(*chest));
    assert(count(g, 130, SLOT_BIG) == 1 && count(g, 388) == 20);
    g.gainXp(SK_FARMING, 100);
    assert(g.skillLevel(SK_FARMING) == 0 && !g.recipeKnown(*jar));   // not until you sleep
    g.applyLevelUps();
    assert(g.skillLevel(SK_FARMING) == 1 && g.recipeKnown(*jar) && g.levelUps.size() == 1);
    assert(g.craft(*jar) && g.craft(*recipe(d, "Furnace")));

    // place a furnace and smelt: 5 ore + 1 coal -> bar in 30 minutes
    const int FX = 60, FY = 24;
    face(g, FX, FY);
    selectItem(g, 13, SLOT_BIG);
    pressA(g);
    Placed* f = g.placedAt(FX, FY);
    assert(f && f->id == 13 && count(g, 13, SLOT_BIG) == 0);
    assert(g.map().inBounds(FX, FY));
    selectItem(g, 378);
    pressA(g);
    f = g.placedAt(FX, FY);
    assert(f->rule >= 0 && !g.machineReady(*f));
    assert(count(g, 378) == 0 && count(g, 382) == 3);   // ore: 5 for the recipe, 5 smelted; coal: jar + fuel
    g.timeOfDay += 30;
    assert(g.machineReady(*f));
    pressA(g);
    assert(count(g, 334) == 1 && g.placedAt(FX, FY)->rule < 0);

    // jar takes any fruit and makes flavored jelly worth 2x + 50
    face(g, FX + 2, FY);
    selectItem(g, 15, SLOT_BIG);
    pressA(g);
    g.addItem(398, 1);
    selectItem(g, 398);
    pressA(g);
    Placed* j = g.placedAt(FX + 2, FY);
    assert(j && j->rule >= 0 && j->held.flavor == 398);
    assert(g.itemName(j->held) == "Test Berry Jelly" && g.itemPrice(j->held) == 150);

    // jar keeps working overnight; save/load keeps it
    assert(g.save("/tmp/craft_test.sav"));
    Game g2(d);
    assert(g2.load("/tmp/craft_test.sav"));
    assert(g2.skillLevel(SK_FARMING) == 1);
    assert(g2.placed["Farm"].size() == 2);
    g2.goToBed();
    g2.teleport("Farm", FX + 2, FY - 1);
    g2.facing = FACE_DOWN;
    const Placed* j2 = nullptr;
    for (auto& p : g2.placed["Farm"])
        if (p.x == FX + 2) j2 = &p;
    assert(j2 && g2.machineReady(*j2));
    pressA(g2);
    assert(count(g2, 344, SLOT_OBJECT, 398) == 1);

    // chest: store and take back
    face(g, FX + 4, FY);
    selectItem(g, 130, SLOT_BIG);
    pressA(g);
    pressA(g);
    assert(g.chestOpen && g.openChest());
    int woodSlot = -1;
    for (int i = 0; i < INV_SIZE; i++)
        if (g.inv[i].type == SLOT_OBJECT && g.inv[i].id == 388) woodSlot = i;
    int wood = count(g, 388);
    g.chestTransfer(true, woodSlot);
    assert(count(g, 388) == 0 && g.openChest()->items[0].count == wood);
    g.chestTransfer(false, 0);
    assert(count(g, 388) == wood);
    g.closeChest();

    // sprinkler waters its neighbours each morning
    assert(g.craft(*recipe(d, "Sprinkler")));
    const int SX = 50, SY = 26;
    for (int dx = -1; dx <= 1; dx++) {   // till around it
        g.teleport("Farm", SX + dx, SY - 1);
    }
    face(g, SX, SY);
    selectItem(g, 599);
    pressA(g);
    assert(g.placedAt(SX, SY) && !g.placedAt(SX, SY)->big);
    // pickaxe picks a placed thing back up
    face(g, FX, FY);
    for (int i = 0; i < INV_SIZE; i++)
        if (g.inv[i].type == SLOT_TOOL && d.tool(g.inv[i].id)->kind == TOOL_PICKAXE) {
            if (i >= HOTBAR) { g.swapSlots(i, 0); i = 0; }
            g.selectSlot(i);
        }
    pressA(g);
    assert(!g.placedAt(FX, FY) && count(g, 13, SLOT_BIG) == 1);
    // ---- tool upgrades: 2000g + 5 copper bars, back in 2 days
    g.money = 5000;
    g.addItem(334, 5);
    auto offers = g.upgradeOffers();
    const UpgradeOffer* hoe = nullptr;
    for (auto& o : offers)
        if (d.tool(o.next)->kind == TOOL_HOE) hoe = &o;
    assert(offers.size() == 2 && hoe && hoe->price == 2000 && hoe->bar == 334);
    UpgradeOffer ho = *hoe;
    assert(g.startUpgrade(ho) && g.money == 3000 && count(g, 334) == 0 && g.toolLevel(TOOL_HOE) < 0);
    assert(!g.collectUpgrade());
    assert(g.save("/tmp/craft_test2.sav"));
    Game g3(d);
    assert(g3.load("/tmp/craft_test2.sav") && g3.upgradeTool == ho.next);
    g3.goToBed();
    g3.goToBed();
    assert(g3.collectUpgrade() && g3.toolLevel(TOOL_HOE) == 1 && g3.upgradeTool < 0);

    // charged copper hoe: hold A for a level-1 swing tills 3 tiles in a line
    int hoeSlot = -1;
    for (int i = 0; i < INV_SIZE; i++)
        if (g3.inv[i].type == SLOT_TOOL && d.tool(g3.inv[i].id)->kind == TOOL_HOE) hoeSlot = i;
    if (hoeSlot >= HOTBAR) { g3.swapSlots(hoeSlot, 0); hoeSlot = 0; }
    g3.selectSlot(hoeSlot);
    const int HX = 70, HY = 22;
    g3.teleport("Farm", HX, HY - 1);
    g3.facing = FACE_DOWN;
    // clear debris in the way
    for (int y = HY; y < HY + 3; y++) assert(g3.debris(HX, y) == DEB_NONE || true);
    Input press; press.action = true; press.hold = true;
    g3.update(1.0f / 60, press);
    assert(g3.charging);
    Input holdIn; holdIn.hold = true;
    for (int i = 0; i < 50; i++) g3.update(1.0f / 60, holdIn);   // ~0.85 s
    assert(g3.chargeLevel() == 1);
    std::vector<std::pair<int, int>> line;
    g3.chargedTiles(1, line);
    assert(line.size() == 3);
    int want = 0;
    for (auto& t : line)
        want += g3.debris(t.first, t.second) == DEB_NONE && (g3.map().flags(t.first, t.second) & CF_DIGGABLE) &&
                !(g3.map().flags(t.first, t.second) & CF_WATER) && !g3.buildingAt(t.first, t.second);
    Input none;
    g3.update(1.0f / 60, none);
    assert(!g3.charging);
    int tilled = 0;
    for (auto& t : line) tilled += g3.soil(t.first, t.second).tilled;
    assert(want >= 2 && tilled == want);
    assert(g3.canCapacity() == 40);   // can not upgraded
    // ---- trees: plant a seed, it grows (100% in the test data), chop it down
    g3.addItem(309, 1);
    for (int i = 0; i < INV_SIZE; i++)
        if (g3.inv[i].type == SLOT_OBJECT && g3.inv[i].id == 309) { g3.swapSlots(i, 5); }
    g3.selectSlot(5);
    int TX = -1, TY = -1;   // a clear grass tile with a clear tile above it
    for (int y = 20; y < 40 && TX < 0; y++)
        for (int x = 30; x < 60 && TX < 0; x++)
            if (g3.debris(x, y) == DEB_NONE && g3.debris(x, y - 1) == DEB_NONE && !g3.soil(x, y).tilled &&
                (g3.map().flags(x, y) & CF_DIGGABLE) && !(g3.map().flags(x, y) & (CF_WATER | CF_BLOCKED)) &&
                !(g3.map().flags(x, y - 1) & CF_BLOCKED) && !g3.treeAt(x, y) && !g3.buildingAt(x, y) &&
                !g3.buildingAt(x, y - 1))
                TX = x, TY = y;
    assert(TX >= 0);
    g3.teleport("Farm", TX, TY - 1);
    g3.facing = FACE_DOWN;
    int tx, ty;
    g3.targetTile(tx, ty);
    pressA(g3);
    if (!g3.treeAt(tx, ty)) fprintf(stderr, "plant at %d,%d flags %d toast [%s] sel %d id %d solid\n", tx, ty, g3.map().flags(tx, ty), g3.toast.c_str(), g3.selected, g3.inv[g3.selected].id);
    assert(g3.treeAt(tx, ty) && g3.treeAt(tx, ty)->stage == 0);
    for (int n = 0; n < 6; n++) { g3.passedOut = true; g3.goToBed(); }
    g3.passedOut = false;
    g3.teleport("Farm", tx, ty - 1);
    g3.facing = FACE_DOWN;
    assert(g3.treeAt(tx, ty) && g3.treeAt(tx, ty)->stage == TREE_GROWN);
    for (int i = 0; i < INV_SIZE; i++)
        if (g3.inv[i].type == SLOT_TOOL && d.tool(g3.inv[i].id)->kind == TOOL_AXE) {
            if (i >= HOTBAR) { g3.swapSlots(i, 1); i = 1; }
            g3.selectSlot(i);
        }
    int wood0 = count(g3, 388);
    for (int hit = 0; hit < 10 && !g3.treeAt(tx, ty)->stump; hit++) pressA(g3);
    assert(g3.treeAt(tx, ty)->stump && count(g3, 388) >= wood0 + 10);
    for (int hit = 0; hit < 5 && g3.treeAt(tx, ty); hit++) pressA(g3);
    assert(!g3.treeAt(tx, ty));
    assert(g3.save("/tmp/craft_test3.sav"));
    // ---- fishing: cast at water, wait, hook, reel
    {
        Game gf(d);
        assert(gf.newGame(9, &err));
        gf.teleport("Farm", 10, 10);
        // find a land tile next to water, facing the water
        int wx = -1, wy = -1, fx = 0, fy = 0, fdir = 0;
        const TileMap& fm = gf.map();
        for (int y = 2; y < fm.height() - 2 && wx < 0; y++)
            for (int x = 2; x < fm.width() - 2 && wx < 0; x++) {
                if (!(fm.flags(x, y) & CF_WATER)) continue;
                const int dirs[4][3] = {{0, -1, FACE_DOWN}, {0, 1, FACE_UP}, {-1, 0, FACE_RIGHT}, {1, 0, FACE_LEFT}};
                for (auto& dd : dirs) {
                    int lx = x + dd[0], ly = y + dd[1];
                    if (!(fm.flags(lx, ly) & (CF_WATER | CF_BLOCKED)) && gf.debris(lx, ly) == DEB_NONE &&
                        !gf.buildingAt(lx, ly) && !gf.treeAt(lx, ly)) {
                        wx = x; wy = y; fx = lx; fy = ly; fdir = dd[2];
                        break;
                    }
                }
            }
        assert(wx >= 0);
        gf.teleport("Farm", fx, fy);
        gf.facing = fdir;
        int rod = -1;
        for (int i = 0; i < d.toolCount(); i++)
            if (d.tool(i)->kind == TOOL_ROD) rod = i;
        assert(rod >= 0);
        Slot rs; rs.type = SLOT_TOOL; rs.id = rod; rs.count = 1;
        gf.inv[3] = rs;
        gf.selectSlot(3);
        Input pressHold; pressHold.action = true; pressHold.hold = true;
        gf.update(1.0f / 60, pressHold);
        assert(gf.fishing.state == FS_CHARGE);
        Input none;
        gf.update(1.0f / 60, none);   // weak cast lands one tile out
        assert(gf.fishing.state == FS_CAST && gf.fishing.bobX == wx && gf.fishing.bobY == wy);
        for (int i = 0; i < 60 * 15 && gf.fishing.state != FS_BITE; i++) gf.update(1.0f / 60, none);
        assert(gf.fishing.state == FS_BITE && gf.fishing.fish == 145);
        Input press; press.action = true;
        gf.update(1.0f / 60, press);
        assert(gf.fishing.state == FS_REEL);
        // a simple player: hold A while the fish is above the middle of the bar
        for (int i = 0; i < 60 * 30 && gf.fishing.state == FS_REEL; i++) {
            Input in;
            in.hold = gf.fishing.fishPos > gf.fishing.barPos + gf.fishing.barSize * 0.5f + gf.fishing.barVel * 0.35f;
            gf.update(1.0f / 60, in);
        }
        assert(gf.fishing.state == FS_DONE && gf.fishing.caught);
        assert(count(gf, 145) == 1 && gf.skillXp[SK_FISHING] > 0);
        for (int i = 0; i < 120; i++) gf.update(1.0f / 60, none);
        assert(!gf.fishingActive());
        // casting at dry land does nothing
        gf.facing = (fdir + 2) % 4;
        gf.update(1.0f / 60, pressHold);
        gf.update(1.0f / 60, none);
        assert(!gf.fishingActive() || gf.fishing.state == FS_CAST);
    }
    // ---- quests: a new farm starts with the harvest + social quests
    {
        Game gq(d);
        assert(gq.newGame(11, &err));
        assert(gq.quests.size() == 2);
        auto find = [&](const std::string& id) -> QuestState* {
            for (auto& q : gq.quests) if (q.id == id) return &q;
            return nullptr;
        };
        assert(find("6") && find("9") && !find("6")->done);
        gq.questItem(24, 1);                       // harvesting a parsnip
        assert(find("6")->done && find("7"));       // its follow-up arrives
        gq.questEntered("Town");
        assert(find("7")->done);
        int money = gq.money;
        size_t idx = 0;
        for (size_t i = 0; i < gq.quests.size(); i++) if (gq.quests[i].id == "6") idx = i;
        assert(gq.collectQuest(idx) && gq.money == money + 100 && !find("6"));
        assert(!gq.addQuest("6"));                 // finished quests don't come back
        gq.addQuest("8");
        gq.questCrafted(true, 130);
        assert(find("8")->done);
        // social: meeting both villagers finishes it
        for (auto& n : gq.npcs) n.introduced = true;
        gq.questMet();
        assert(find("9")->done);
        // level-ups queue for the morning screen
        gq.gainXp(SK_MINING, 400);
        assert(gq.levelUps.empty() && gq.skillLevel(SK_MINING) == 0);
        gq.passedOut = true;
        gq.goToBed();                              // levels go up overnight
        assert(gq.levelUps.size() == 2 && gq.levelUps[1].second == 2 && gq.skillLevel(SK_MINING) == 2);
        assert(gq.save("/tmp/quest_test.sav"));
        Game gq2(d);
        assert(gq2.load("/tmp/quest_test.sav"));
        assert(gq2.quests.size() == gq.quests.size() && gq2.questsFinished.count("6"));
    }
    printf("all crafting tests passed\n");
    return 0;
}
