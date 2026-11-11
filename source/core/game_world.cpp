// The rest of the valley's map actions: signs and shelves, shop counters, trash cans, the calendar,
// the minecarts, the bus to the desert, the sewer, the Skull Cavern door, the Community Center door.
#include "game.h"

#include <algorithm>
#include <cstdlib>
#include <sstream>

namespace sv {

static std::vector<std::string> tabs(const std::string& l) {
    std::vector<std::string> f;
    size_t i = 0;
    while (true) {
        size_t j = l.find('\t', i);
        f.push_back(l.substr(i, j == std::string::npos ? std::string::npos : j - i));
        if (j == std::string::npos) break;
        i = j + 1;
    }
    return f;
}

const std::string* Game::mapString(const std::string& key) {
    if (!mapStringsLoaded) {
        mapStringsLoaded = true;
        std::vector<std::string> lines;
        if (data.vfs.readTable("data/mapstrings.dat", lines))
            for (auto& l : lines) {
                size_t t = l.find('\t');
                if (t != std::string::npos) mapStrings[l.substr(0, t)] = l.substr(t + 1);
            }
    }
    auto it = mapStrings.find(key);
    return it == mapStrings.end() ? nullptr : &it->second;
}

void Game::showMessage(const std::string& text) {
    auto pages = parseDialogue(text, me.name);
    if (pages.empty()) return;
    dialogue = DialogueBox();
    dialogue.active = true;
    dialogue.pages = pages;
}

static std::string unquote(std::string s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '"')) s.erase(s.begin());
    while (!s.empty() && (s.back() == ' ' || s.back() == '"')) s.pop_back();
    return s;
}

bool Game::ccDone() const {
    for (const char* r : {"ccPantry", "ccCraftsRoom", "ccFishTank", "ccBoilerRoom", "ccBulletin", "ccVault"})
        if (!mailReceived.count(r)) return false;
    return true;
}

bool Game::worldAction(const std::string& verb, const std::string& rest) {
    std::istringstream ss(rest);
    if (verb == "Message" || verb == "Letter" || verb == "MessageOnce") {
        const std::string* s = mapString(unquote(rest));
        if (!s) return false;
        showMessage(*s);
        return true;
    }
    if (verb == "Notes") {               // the library's lost books
        const std::string* s = mapString("note:" + unquote(rest));
        if (!s) return false;
        showMessage(*s);
        return true;
    }
    // shop counters
    struct { const char* verb; const char* shop; } counters[] = {
        {"Carpenter", "Carpenter"}, {"AnimalShop", "AnimalShop"}, {"Saloon", "Saloon"}, {"HospitalShop", "Hospital"},
        {"JojaShop", "Joja"}, {"AdventureShop", "AdventureShop"}, {"IceCreamStand", "IceCreamStand"}, {"Blacksmith", "Blacksmith"},
        {"DesertVendor", "DesertTrade"}, {"Sandy", "Sandy"}, {"Bookseller", "Bookseller"}};
    for (auto& c : counters)
        if (verb == c.verb) {
            if (data.shops.find(c.shop) == data.shops.end() && std::string(c.shop) != "Carpenter" && std::string(c.shop) != "AnimalShop") return false;
            if (verb == "IceCreamStand" && season != SUMMER) { say("The ice cream stand is closed until summer."); return true; }
            if (verb == "Blacksmith" && crackGeode()) return true;
            openShop = c.shop;
            if (verb == "Blacksmith") collectUpgrade();
            return true;
        }
    if (verb == "Garbage") {             // rummaging through a trash can, once a day each
        std::string id = unquote(rest);
        if (garbageToday.count(id)) { say("You've been through this one today."); return true; }
        garbageToday.insert(id);
        sfx("trashcan");
        for (auto& n : npcs)             // nobody likes to see it (Linus doesn't mind)
            if (n.def->social && n.loc == cur->name && n.def->name != "Linus" &&
                std::abs(n.tileX() - (int)(px / 16)) < 7 && std::abs(n.tileY() - (int)(py / 16)) < 7)
                n.friendship = std::max(0, n.friendship - 25);
        if (rand32() % 100 >= 22) { say("Nothing but trash."); return true; }
        static const int junk[] = {168, 167, 170, 171, 172, 216, 153, 176, 78, 309, 310, 311};
        int item = junk[rand32() % 12];
        if (id.find("Saloon") != std::string::npos && rand32() % 3 == 0) item = 346;      // someone's leftover beer
        if (id.find("Blacksmith") != std::string::npos && rand32() % 3 == 0) item = 378;  // copper ore
        if (id.find("Museum") != std::string::npos && rand32() % 4 == 0) item = 535;      // a geode
        if (data.object(item) && addItem(item, 1)) itemFlies(item, px, py - 16);
        return true;
    }
    if (verb == "Billboard") {           // the calendar outside Pierre's
        loadFestivals();
        std::string out = std::string("This ") + seasonName(season) + ":";
        for (auto& f : festivals)
            if (f.season == season) out += " " + f.name + " on the " + std::to_string(f.day) + ".";
        std::string bd;
        for (auto& n : data.npcs)
            if (n.social && n.birthSeason == season && n.birthDay >= day && n.birthDay < day + 7)
                bd += (bd.empty() ? "" : ", ") + n.display + " (" + std::to_string(n.birthDay) + ")";
        out += bd.empty() ? "#$b#No birthdays this week." : "#$b#Birthdays this week: " + bd + ".";
        showMessage(out);
        return true;
    }
    if (verb == "WarpCommunityCenter") {
        if (!mailReceived.count("ccDoorUnlock") && !eventsSeen.count("611439")) { say("It's locked."); return true; }
        warpTo("CommunityCenter", 32, 23, true);
        return true;
    }
    if (verb == "EnterSewer") {
        if (!mailReceived.count("HasRustyKey")) { say("A heavy grate, locked. It would take an old key..."); return true; }
        sfx("stairsdown");
        warpTo("Sewer", 16, 11, false);
        return true;
    }
    if (verb == "SkullCave" || verb == "SkullDoor") {
        if (mineDeepest < 120) { say("A door with a skull-shaped keyhole. The key must be somewhere deep..."); return true; }
        enterMine(std::max(121, std::min(mineDeepest, 121)));
        return true;
    }
    if (verb == "MinecartTransport") {
        if (!mailReceived.count("ccBoilerRoom")) { say("The minecart is out of order. The boiler room that powers it is in ruins."); return true; }
        std::vector<std::string> lines;
        data.vfs.readTable("data/minecarts.dat", lines);
        std::string net, here;
        ss >> net >> here;
        DialoguePage p;
        p.text = "Choose a destination";
        for (auto& l : lines) {
            auto f = tabs(l);
            if (f.size() < 5 || (f[0] != net && !net.empty()) || f[2] == cur->name || !hasMap(f[2])) continue;
            DialogueChoice c;
            c.label = f[1] == "Bus" ? "Bus Stop" : f[1];
            c.responseKey = "cart:" + f[2] + ":" + f[3] + ":" + f[4];
            p.choices.push_back(c);
        }
        if (p.choices.empty()) { say("Nowhere to go from here."); return true; }
        DialogueChoice c;
        c.label = "Cancel";
        p.choices.push_back(c);
        dialogue = DialogueBox();
        dialogue.active = true;
        dialogue.pages.push_back(p);
        pendingChoice = "cart";
        return true;
    }
    if (verb == "BusTicket" || verb == "TicketMachine") {
        if (!mailReceived.count("ccVault")) { say("Out of service. The bus has been broken down for years."); return true; }
        if (!hasMap("Desert")) { say("The desert wasn't converted."); return true; }
        if (money < 500) { say("A ticket to the desert is 500g."); return true; }
        money -= 500;
        sfx("busDriveOff");
        warpTo("Desert", 16, 24, false);
        say("The bus rattles off to the Calico Desert...");
        return true;
    }
    if (verb == "DesertBus") {
        sfx("busDriveOff");
        warpTo("BusStop", 22, 11, false);
        return true;
    }
    if (verb == "Jukebox") { say("The jukebox hums. (Songs you've heard play around the valley.)"); return true; }
    if (verb == "ColaMachine") {
        if (money < 75) { say("Joja Cola: 75g."); return true; }
        if (data.object(167) && addItem(167, 1)) { money -= 75; sfx("purchase"); }
        return true;
    }
    if (verb == "ElliottPiano") { sfx("toyPiano"); return true; }
    if (verb == "Yoba") { say("A shrine to Yoba."); return true; }
    if (verb == "DwarfGrave") { say("Strange symbols are carved in the stone. Unreadable... for now."); return true; }
    if (verb == "Tent") { say("Linus's tent. Better to knock."); return true; }
    if (verb == "Arcade_Prairie" || verb == "Arcade_Minecart") { say("The arcade machine is out of order."); return true; }
    return false;
}

bool Game::worldChoice(const std::string& what, const std::string& key) {
    if (what == "cart" && key.rfind("cart:", 0) == 0) {
        std::string s = key.substr(5);
        size_t a = s.find(':'), b = s.rfind(':');
        if (a == std::string::npos || b == a) return true;
        sfx("stairsdown");
        warpTo(s.substr(0, a), atoi(s.substr(a + 1, b - a - 1).c_str()), atoi(s.substr(b + 1).c_str()), false);
        return true;
    }
    return false;
}

// Each morning: the rusty key after 60 museum donations, the skull key at the bottom of the mines.
void Game::worldMorning() {
    garbageToday.clear();
    if (museumDonated.size() >= 60 && !mailReceived.count("HasRustyKey")) {
        mailReceived.insert("HasRustyKey");
        if (morningNote.empty()) morningNote = "Gunther stopped by with a Rusty Key: \"For the sewers. A thank-you from the museum.\"";
    }
    if (mineDeepest >= 120 && !mailReceived.count("HasSkullKey")) mailReceived.insert("HasSkullKey");
}

} // namespace sv
