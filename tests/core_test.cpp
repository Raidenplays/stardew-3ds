// Host test: g++ -std=c++17 -Isource/core tests/core_test.cpp source/core/*.cpp
#include <cassert>
#include <cstdio>
#include "game.h"
using namespace sv;
int main(int argc, char** argv) {
    std::string root = argc > 1 ? argv[1] : "out/3ds/stardew3ds/";
    GameData d; std::string err;
    if (!d.load(root, &err)) { printf("data: %s\n", err.c_str()); return 1; }
    TileMap m;
    if (!m.load(root + "maps/Farm.smap", &err)) { printf("map: %s\n", err.c_str()); return 1; }
    printf("map %dx%d, %zu tilesets, tex0=%s -> %s\n", m.width(), m.height(), m.tilesets.size(),
           m.tilesets[0].texture.c_str(), m.tilesets[0].textureFor(FALL).c_str());
    assert(m.animatedTile(0, 1, 0) == 1 && m.animatedTile(0, 1, 300) == 2);
    assert(m.flags(0,0) & CF_DIGGABLE);
    assert(m.flags(1,0) & CF_WATER);
    assert(m.flags(1,1) & CF_BLOCKED);      // building tile at index 7
    Game g(d, m);
    g.newGame(1234);
    // find a diggable, debris-free tile
    int tx=-1, ty=-1;
    for (int y=0;y<m.height()&&tx<0;y++) for (int x=0;x<m.width();x++)
        if ((m.flags(x,y)&CF_DIGGABLE) && g.debris(x,y)==DEB_NONE) { tx=x; ty=y; break; }
    assert(tx >= 0);
    // stand above it facing down
    g.px = tx*16+8; g.py = (ty-1)*16+14; g.facing = FACE_DOWN;
    int ax, ay; g.targetTile(ax, ay); assert(ax==tx && ay==ty);
    // slot 0 = Hoe (only tool in fake data), slot 1 = seeds
    assert(g.inv[0].type==SLOT_TOOL && g.inv[1].id==472 && g.inv[1].count==15);
    Input in; in.action = true;
    g.selected = 0; g.update(0.016f, in); assert(g.soil(tx,ty).tilled);
    g.toolTimer = 0; g.selected = 1; g.update(0.016f, in);
    assert(g.soil(tx,ty).seed == 472 && g.inv[1].count == 14);
    for (int dday=0; dday<4; dday++) {
        Soil s = g.soil(tx,ty); (void)s;
        const_cast<Soil&>(g.soil(tx,ty)).watered = true;   // no can in fake data
        g.goToBed();
        g.px = tx*16+8; g.py = (ty-1)*16+14; g.facing = FACE_DOWN;
    }
    assert(g.cropReady(tx,ty));
    printf("frame when ready=%d\n", g.cropFrame(tx,ty));
    g.selected = 2; g.update(0.016f, in);
    bool got=false; for (auto& s: g.inv) if (s.type==SLOT_OBJECT && s.id==24) got=true;
    assert(got && g.soil(tx,ty).seed == -1);
    assert(g.day == 5 && g.timeString()=="6:00 am");
    // shipping value
    g.shipped.push_back({SLOT_OBJECT, 24, 3}); int m0=g.money; g.goToBed(); assert(g.money==m0+105);
    // clock pass-out
    g.timeOfDay = DAY_END-10; Input none; g.update(7.01f, none); assert(g.passedOut && g.timeOfDay==DAY_START);
    // save/load
    assert(g.save("/tmp/sv.sav"));
    Game g2(d, m); assert(g2.load("/tmp/sv.sav"));
    assert(g2.day==g.day && g2.money==g.money && g2.soil(tx,ty).tilled==g.soil(tx,ty).tilled);
    for (int y=0;y<m.height();y++) for (int x=0;x<m.width();x++) assert(g2.debris(x,y)==g.debris(x,y));
    auto shop = g.shopStock(); assert(shop.size()==1 && shop[0].price==20);
    printf("all core tests passed\n");
}
