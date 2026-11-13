#include "saves.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "game.h"

namespace sv {

static bool exists(const std::string& p) {
    FILE* f = fopen(p.c_str(), "rb");
    if (!f) return false;
    fclose(f);
    return true;
}

std::string savePathFor(const std::string& root, int slot) {
    if (slot <= 0) return root + "save.sav";
    return root + "save" + std::to_string(slot + 1) + ".sav";
}

static bool readInfo(SaveSlot& s) {
    FILE* f = fopen((s.path + ".info").c_str(), "rb");
    if (!f) return false;
    char buf[512] = {0};
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = 0;
    // farmer \t farm \t day \t season \t year \t money
    std::vector<std::string> part(1);
    for (char* p = buf; *p && *p != '\n' && *p != '\r'; p++) {
        if (*p == '\t') part.emplace_back();
        else part.back() += *p;
    }
    if (part.size() < 6) return false;
    s.farmer = part[0];
    s.farm = part[1];
    s.day = atoi(part[2].c_str());
    s.season = atoi(part[3].c_str());
    s.year = atoi(part[4].c_str());
    s.money = atoi(part[5].c_str());
    s.known = true;
    return true;
}

static std::string lastSaveName(const std::string& root) {
    FILE* f = fopen((root + "last_save.txt").c_str(), "rb");
    if (!f) return "";
    char buf[64] = {0};
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r' || buf[n - 1] == ' ')) buf[--n] = 0;
    return buf;
}

std::vector<SaveSlot> listSaves(const std::string& root) {
    std::vector<SaveSlot> out;
    std::string last = root + lastSaveName(root);
    for (int i = 0; i < MAX_SAVES; i++) {
        SaveSlot s;
        s.slot = i;
        s.path = savePathFor(root, i);
        if (!exists(s.path)) continue;
        readInfo(s);
        if (s.path == last) out.insert(out.begin(), s);
        else out.push_back(s);
    }
    return out;
}

int freeSaveSlot(const std::string& root) {
    for (int i = 0; i < MAX_SAVES; i++)
        if (!exists(savePathFor(root, i))) return i;
    return -1;
}

static std::string oneLine(std::string s) {
    for (char& ch : s)
        if (ch == '\t' || ch == '\n' || ch == '\r') ch = ' ';
    return s;
}

void writeSaveInfo(const std::string& savePath, const Game& g) {
    FILE* f = fopen((savePath + ".info").c_str(), "wb");
    if (!f) return;
    fprintf(f, "%s\t%s\t%d\t%d\t%d\t%d\n", oneLine(g.me.name).c_str(), oneLine(g.me.farm).c_str(),
            g.day, g.season, g.year, g.money);
    fclose(f);
}

void describeOldSaves(Game& g, const std::string& root) {
    for (int i = 0; i < MAX_SAVES; i++) {
        std::string p = savePathFor(root, i);
        if (!exists(p) || exists(p + ".info")) continue;
        if (g.load(p)) writeSaveInfo(p, g);
    }
}

bool trashSave(const std::string& savePath) {
    std::string gone = savePath + ".deleted";
    remove(gone.c_str());
    if (rename(savePath.c_str(), gone.c_str()) != 0) return false;
    remove((savePath + ".info").c_str());
    remove((savePath + ".mods").c_str());
    return true;
}

void setLastSave(const std::string& root, const std::string& savePath) {
    FILE* f = fopen((root + "last_save.txt").c_str(), "wb");
    if (!f) return;
    size_t cut = savePath.find_last_of('/');
    fprintf(f, "%s\n", (cut == std::string::npos ? savePath : savePath.substr(cut + 1)).c_str());
    fclose(f);
}

std::string saveDateText(const SaveSlot& s) {
    static const char* const NAMES[4] = {"Spring", "Summer", "Fall", "Winter"};
    char buf[64];
    snprintf(buf, sizeof buf, "%s %d, Year %d", NAMES[s.season < 0 || s.season > 3 ? 0 : s.season], s.day, s.year);
    return buf;
}

}  // namespace sv
