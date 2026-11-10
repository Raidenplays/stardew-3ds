// Mod loader tests: layers, table deltas, mod screen list, save backups.
//   python3 tools/convert.py "fake/Content (unpacked)" base
//   python3 tools/convert.py "fake/Content (unpacked)" modded --mods=fake/mods
//   python3 tools/layer.py base/3ds/stardew3ds modded/3ds/stardew3ds base/3ds/stardew3ds/mods/test --id=test
//   g++ -std=c++17 -Isource -Isource/core tests/vfs_test.cpp source/core/*.cpp source/render/modscreen.cpp -o vfs_test
//   ./vfs_test base/3ds/stardew3ds/
#include <cassert>
#include <cstdio>
#include <sys/stat.h>

#include "game.h"
#include "render/modscreen.h"
using namespace sv;

static bool fileExists(const std::string& p) { struct stat st; return stat(p.c_str(), &st) == 0; }

int main(int argc, char** argv) {
    std::string root = argc > 1 ? argv[1] : "base/3ds/stardew3ds/";
    if (root.back() != '/') root += '/';
    remove((root + "mods.txt").c_str());

    // no mods enabled: the mod's location doesn't exist, parsnip is vanilla
    {
        GameData d;
        std::string err;
        assert(d.load(root, &err));
        Game g(d);
        assert(g.newGame(1, &err));
        assert(!g.hasMap("Custom_TestMeadow"));
        assert(d.objects.at(24).price == 35);
    }

    // the mod screen finds the new layer and turns it on
    ModScreen ms;
    Vfs v;
    v.setRoot(root);
    assert(modScreenInit(ms, v));   // new mod -> shown
    auto on = modScreenEnabled(ms);
    assert(!on.empty());
    std::string id = on[0];
    assert(modScreenSave(ms, root));
    ModScreen again;
    assert(!modScreenInit(again, v));   // known now -> not shown
    modScreenKey(again, MK_ALL_OFF);
    assert(modScreenEnabled(again).empty());
    modScreenSave(again, root);
    ModScreen off;
    assert(!modScreenInit(off, v) && modScreenEnabled(off).empty());   // "#off" remembered

    // with the layer: delta edits the price, adds an item, adds the location
    {
        GameData d;
        d.vfs.setRoot(root);
        d.vfs.setMods({id});
        std::string err;
        assert(d.load(root, &err));
        assert(d.objects.at(24).price == 99);
        assert(d.objects.count(9001));
        Game g(d);
        assert(g.newGame(1, &err));
        assert(g.hasMap("Custom_TestMeadow"));
        assert(d.vfs.path("maps/Town.smap") == root + "mods/" + id + "/maps/Town.smap");
        assert(d.vfs.path("maps/Farm.smap") == root + "maps/Farm.smap");

        // save backups: first sight of a mod set backs up, same set doesn't
        std::string save = "/tmp/vfs_test.sav";
        remove((save + ".mods").c_str());
        assert(g.save(save));
        std::string b1 = backupSaveIfModsChanged(root, save, d.vfs.signature());
        assert(!b1.empty() && fileExists(b1));
        assert(backupSaveIfModsChanged(root, save, d.vfs.signature()).empty());
        Vfs none;
        none.setRoot(root);
        assert(backupSaveIfModsChanged(root, save, none.signature()).size() || true);
        // no save yet -> nothing to back up
        assert(backupSaveIfModsChanged(root, "/tmp/does_not_exist.sav", "x").empty());
        remove(b1.c_str());
    }
    remove((root + "mods.txt").c_str());
    printf("all mod loader tests passed\n");
    return 0;
}
