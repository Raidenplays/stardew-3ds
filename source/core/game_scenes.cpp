// Scenes the game starts itself rather than by walking into them: the wedding ceremony, being knocked
// out, leaving the island, the summit. Each one is the game's own event script, read from the
// converted data files, with the day's details filled in.
#include "game.h"

#include <cstdio>
#include <cstdlib>

namespace sv {

static const int TILE = 16;

static std::vector<std::vector<std::string>> readTable(const std::string& path) {
    std::vector<std::vector<std::string>> rows;
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return rows;
    std::string line;
    int ch;
    auto flush = [&]() {
        if (line.empty()) return;
        std::vector<std::string> cols(1);
        for (char c : line) {
            if (c == '\t') cols.emplace_back();
            else if (c != '\r') cols.back() += c;
        }
        rows.push_back(cols);
        line.clear();
    };
    while ((ch = fgetc(f)) != EOF) {
        if (ch == '\n') flush();
        else line += (char)ch;
    }
    flush();
    fclose(f);
    return rows;
}

static void replaceAll(std::string& s, const std::string& what, const std::string& with) {
    if (what.empty()) return;
    for (size_t at = s.find(what); at != std::string::npos; at = s.find(what, at + with.size())) s.replace(at, what.size(), with);
}

// "spouse" as a whole word (the actor), not inside "%spouse" or another name
static void nameSpouse(std::string& s, const std::string& name) {
    for (size_t at = s.find("spouse"); at != std::string::npos; at = s.find("spouse", at + 1)) {
        bool startOk = at == 0 || s[at - 1] == ' ' || s[at - 1] == '/';
        bool endOk = at + 6 >= s.size() || s[at + 6] == ' ' || s[at + 6] == '/';
        if (startOk && endOk) s.replace(at, 6, name);
    }
}

bool Game::startWedding() {
    if (spouse.empty() || !hasMap("Town")) return false;
    auto rows = readTable(data.vfs.path("data/wedding.dat"));
    bool male = false;
    for (auto& r : rows)
        if (r.size() > 2 && r[0] == "gender" && r[1] == spouse) male = r[2] == "m";
    std::string script, guests, party;
    for (auto& r : rows) {
        if (r.size() > 2 && r[0] == "script" && r[1] == (male ? "m" : "f")) script = r[2];
        if (r.size() > 3 && r[0] == "guest" && r[1] != spouse && npcByName(r[1])) {
            const std::string& cond = r[2];   // "", "SEASON winter" or "!SEASON winter"
            if (!cond.empty() && !gameQuery(cond)) continue;
            guests += " " + r[3];
            if (r.size() > 4 && !r[4].empty()) party += r[4] + "/";
        }
    }
    if (script.empty()) return false;
    std::string sn = seasonName(season);
    replaceAll(script, "{SetupContextualWeddingAttendees}", guests);
    replaceAll(script, "{ContextualWeddingCelebrations}", party);
    replaceAll(script, "{DayOfMonth}", std::to_string(day));
    replaceAll(script, "{Season}", sn);
    replaceAll(script, "%spouse", spouse);
    replaceAll(script, " otherFarmers 29 69 0", "");
    nameSpouse(script, spouse);
    teleport("Town", 27, 63);
    if (cutscene.active) skipEvent();
    if (!startEvent("Town", "wedding", script)) return false;
    // afterwards: home, on the porch, married
    cutscene.retLoc = "Farm";
    cutscene.retX = 64 * TILE + TILE / 2.0f;
    cutscene.retY = 15 * TILE + TILE - 2.0f;
    return true;
}

bool Game::startNamedEvent(const std::string& loc, const std::string& id) {
    if (!hasMap(loc)) return false;
    FILE* f = fopen(data.vfs.path("data/events/" + loc + ".dat").c_str(), "rb");
    if (!f) return false;
    std::string line, key, script;
    int ch;
    auto check = [&]() {
        size_t tab = line.find('\t');
        if (tab != std::string::npos && script.empty()) {
            std::string k = line.substr(0, tab);
            if (k.substr(0, k.find('/')) == id) { key = k; script = line.substr(tab + 1); }
        }
        line.clear();
    };
    while ((ch = fgetc(f)) != EOF) {
        if (ch == '\n') check();
        else if (ch != '\r') line += (char)ch;
    }
    check();
    fclose(f);
    if (script.empty()) return false;
    for (const char* tok : {"{0}", "{1}", "{2}"}) replaceAll(script, tok, me.name);   // "...Farmer?"
    if (!cur || cur->name != loc) teleport(loc, 10, 10);
    if (cutscene.active) skipEvent();
    return startEvent(loc, key, script);
}

// Knocked out: you come round in Harvey's clinic, or at the top of the mines if that is where it
// happened, or on the island's beach. The scene and what it costs are the game's own.
bool Game::knockedOut() {
    std::string here = cur ? cur->name : "";
    bool mine = here.rfind("Mine", 0) == 0 || here.rfind("UndergroundMine", 0) == 0;
    bool island = here.rfind("Island", 0) == 0 || here.rfind("Volcano", 0) == 0 || here == "Caldera";
    // a mod's own places first: the map you fell in, or the outpost its monsters' lands send you back to
    bool modded = false;
    if (here.find("Badlands") != std::string::npos || here.find("CastleVillage") != std::string::npos)
        modded = startNamedEvent("Custom_CastleVillageOutpost", "PlayerKilled");
    else if (here.find("Highlands") != std::string::npos)
        modded = startNamedEvent("Custom_HighlandsOutpost", "PlayerKilled");
    else if (here.rfind("Custom_", 0) == 0 || here == "DesertFestival")
        modded = startNamedEvent(here, "PlayerKilled");
    bool ok = modded || (island && startNamedEvent("IslandSouth", "PlayerKilled")) || (mine && startNamedEvent("Mine", "PlayerKilled")) ||
              startNamedEvent("Hospital", "PlayerKilled");
    if (!ok) return false;
    for (auto& a : cutscene.actors)   // you get up where the scene had you lying
        if (a.name == "farmer") { cutscene.retLoc = cur->name; cutscene.retX = a.x; cutscene.retY = a.y; }
    return true;
}

// The summit, the first time you reach it: whoever is closest to you (your spouse, or Lewis, or Morris
// if the town went with Joja) is there for the view. Their lines are the game's (Data/ExtraDialogue).
bool Game::startSummit() {
    if (!cur || cur->name != "Summit") return false;
    bool joja = mailReceived.count("JojaMember") != 0;
    std::string who = !spouse.empty() && npcByName(spouse) ? spouse : joja && npcByName("Morris") ? "Morris" : "Lewis";
    std::string tag = who == spouse ? "Spouse" : who;
    auto line = [&](const std::string& key) {
        std::string s = gameLine("xd:SummitEvent_" + key, "", "", "");
        replaceAll(s, "%farm", me.farm);
        replaceAll(s, "%year", std::to_string(year));
        replaceAll(s, "\"", "'");
        replaceAll(s, "/", " ");
        return s;
    };
    int tx = (int)(px / TILE), ty = (int)((py - 4) / TILE);
    std::string sc = "none/-1000 -1000/farmer " + std::to_string(tx) + " " + std::to_string(ty) + " 0 " + who + " " +
                     std::to_string(tx + 1) + " " + std::to_string(ty) + " 0/skippable/viewport " + std::to_string(tx) + " " +
                     std::to_string(ty) + " true/pause 2500";
    auto speak = [&](const std::string& key) {
        std::string s = line(key);
        if (!s.empty()) sc += "/speak " + who + " \"" + s + "\"/pause 800";
    };
    speak("Intro_" + tag);
    if (tag == "Spouse") speak("Intro2_Spouse");
    speak("Dialogue1_" + tag);
    if (tag == "Spouse") speak("Dialogue1B_Spouse");
    speak("Dialogue2_" + tag);
    if (tag == "Spouse") speak("Dialogue3_" + who);
    else speak("Outro_" + tag);
    sc += "/pause 1500/globalFade/viewport -1000 -1000";
    for (const char* k : {"closingmessage", "closingmessage2", "closingmessage3", "closingmessage4", "closingmessage5"}) {
        std::string s = line(k);
        if (!s.empty()) sc += "/message \"" + s + "\"/pause 400";
    }
    sc += "/end";
    return startEvent("Summit", "summit", sc);
}

}  // namespace sv
