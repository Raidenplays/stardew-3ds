// Boot-time mod screen: pick which converted mod layers load, and in what order.
//
// Shown automatically when mods/ has layers that mods.txt doesn't know about
// (new installs default to ON) or when an enabled mod's folder is gone.
// Hold SELECT while the game starts to open it any time.
//
// mods.txt keeps enabled ids in load order plus "#off <id>" for mods you
// turned off, so they aren't treated as new next boot.
#pragma once
#include <string>
#include <vector>

#include "../core/vfs.h"
#include "canvas.h"

namespace sv {

struct ModScreen {
    std::vector<ModInfo> mods;   // display = load order (bottom wins)
    int cursor = 0;
    int scroll = 0;
    bool changed = false;        // new or missing mods detected
    bool done = false;
    std::string backupNote;      // filled in after the backup check
};

// Builds the list from mods/ + mods.txt. Returns true if it should be shown
// without being asked for (new or missing mods).
bool modScreenInit(ModScreen& s, const Vfs& vfs);

enum ModKey { MK_UP, MK_DOWN, MK_TOGGLE, MK_MOVE_UP, MK_MOVE_DOWN, MK_ALL_ON, MK_ALL_OFF, MK_PLAY };
void modScreenKey(ModScreen& s, ModKey k);
void modScreenTouch(ModScreen& s, int px, int py);   // bottom screen buttons

std::vector<std::string> modScreenEnabled(const ModScreen& s);
bool modScreenSave(const ModScreen& s, const std::string& root);   // writes mods.txt

void drawModTop(Canvas& c, const ModScreen& s);
void drawModBottom(Canvas& c, const ModScreen& s);

} // namespace sv
