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

// The cave Demetrius set up: mushrooms every other day, or whatever fruit the bats drop.
void Game::caveNight() {
    bool mush = mailReceived.count("caveMushrooms"), bats = mailReceived.count("caveBats");
    if ((!mush && !bats) || !cur || cur->name != "FarmCave") return;
    // filled when you walk in, for the days since you last looked (up to four)
    int last = -1;
    for (auto it = mailReceived.begin(); it != mailReceived.end();)
        if (it->rfind("s3d_caveDay:", 0) == 0) { last = atoi(it->c_str() + 12); it = mailReceived.erase(it); }
        else ++it;
    mailReceived.insert("s3d_caveDay:" + std::to_string(absoluteDay()));
    int days = last < 0 ? 1 : std::min(4, absoluteDay() - last);
    Location* cave = cur;
    if (days <= 0 || cave->objects.size() >= 12) return;
    static const int shrooms[] = {404, 420, 422, 257, 281}, fruit[] = {296, 410, 613, 634, 635, 636, 637, 638, 406};
    int n = mush ? days * 2 : days + (int)(rand32() % 2);
    const TileMap& m = cave->map;
    for (int k = 0, tries = 0; k < n && tries < 200; tries++) {
        int x = 2 + (int)(rand32() % (uint32_t)std::max(1, m.width() - 4)), y = 3 + (int)(rand32() % (uint32_t)std::max(1, m.height() - 5));
        if (m.tile(L_BACK, x, y) == NO_TILE || m.tile(L_BUILDINGS, x, y) != NO_TILE || (m.flags(x, y) & CF_BLOCKED) || m.warpAt(x, y)) continue;
        bool taken = false;
        for (auto& o : cave->objects) taken |= o.x == x && o.y == y;
        if (taken) continue;
        WorldObject o;
        o.x = x; o.y = y;
        o.itemId = mush ? shrooms[rand32() % 5] : fruit[rand32() % 9];
        if (!data.object(o.itemId)) continue;
        cave->objects.push_back(o);
        k++;
    }
}

// Grandpa's shrine, the start of year 3: candles for how the farm has done.
void Game::grandpaEvaluation() {
    int pts = 0;
    for (long long step : {50000LL, 100000LL, 200000LL, 300000LL, 500000LL, 1000000LL})
        if (totalEarned >= step) pts++;
    if (totalEarned >= 1000000) pts++;
    int levels = 0;
    for (int k = 0; k < SK_COUNT; k++) levels += skillLevel(k);
    pts += levels >= 50 ? 2 : levels >= 30 ? 1 : 0;
    int friends = 0;
    for (auto& n : npcs) friends += n.friendship >= 2000;
    pts += friends >= 10 ? 2 : friends >= 5 ? 1 : 0;
    if (!spouse.empty() && houseLevel >= 2) pts++;
    if (ccDone()) pts += 3;
    if (mineDeepest >= 120) pts++;
    if (mailReceived.count("HasRustyKey")) pts++;
    if (museumDonated.size() >= 60) pts++;
    int candles = pts >= 12 ? 4 : pts >= 8 ? 3 : pts >= 4 ? 2 : 1;
    mailReceived.insert("s3d_grandpa" + std::to_string(candles));
    if (candles == 4 && !mailReceived.count("s3d_perfectionStatue") && data.big(160)) {
        mailReceived.insert("s3d_perfectionStatue");
        Slot s;
        s.type = SLOT_BIG; s.id = 160; s.count = 1;
        addSlot(s);
    }
    say("Grandpa's shrine: " + std::to_string(candles) + (candles == 1 ? " candle is" : " candles are") + " lit (" +
        std::to_string(pts) + " points)." + (candles == 4 ? " He couldn't be prouder." : ""));
}

} // namespace sv
