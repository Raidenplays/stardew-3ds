// Several farms on one SD card. Each is one save file: save.sav (the first, and the
// only one older builds knew), then save2.sav ... save12.sav. Beside each sits a tiny
// .info file (who, where, when) so the Load screen can list farms without loading them.
#pragma once
#include <string>
#include <vector>

namespace sv {

class Game;

const int MAX_SAVES = 12;

struct SaveSlot {
    int slot = 0;                 // 0..MAX_SAVES-1
    std::string path;
    std::string farmer, farm;
    int day = 1, season = 0, year = 1, money = 0;
    bool known = false;           // the .info was there (false: an old save not yet looked at)
};

std::string savePathFor(const std::string& root, int slot);
// Farms on the card, the one played last first, then in slot order.
std::vector<SaveSlot> listSaves(const std::string& root);
// The first slot with no farm in it, or -1 when all are taken.
int freeSaveSlot(const std::string& root);
void writeSaveInfo(const std::string& savePath, const Game& g);
// Looks at old saves that have no .info yet (loads each once) so they can be listed.
void describeOldSaves(Game& g, const std::string& root);
// Takes a farm off the list. The file is kept as <save>.deleted in case it was a mistake.
bool trashSave(const std::string& savePath);
void setLastSave(const std::string& root, const std::string& savePath);
// "Spring 21, Year 1"
std::string saveDateText(const SaveSlot& s);

}  // namespace sv
