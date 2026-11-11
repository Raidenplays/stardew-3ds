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
    if (verb == "Billboard") {           // the boards outside Pierre's: the calendar, and special orders
        if (ordersUnlocked() && unquote(rest) != "calendar") {
            DialoguePage p;
            p.text = "The town boards";
            DialogueChoice c;
            c.label = "Calendar"; c.responseKey = "board:calendar"; p.choices.push_back(c);
            c.label = orderKey.empty() ? "Special orders" : "Special order: check in"; c.responseKey = "board:orders"; p.choices.push_back(c);
            dialogue = DialogueBox();
            dialogue.active = true;
            dialogue.pages.push_back(p);
            pendingChoice = "board";
            return true;
        }
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
    if (verb == "BrokenBeachBridge") {
        if (mailReceived.count("beachBridgeFixed")) return false;
        if (countItem(388) < 300) { say("The bridge is out. 300 wood would fix it."); return true; }
        takeItem(388, 300);
        mailReceived.insert("beachBridgeFixed");
        applyMapFixes();
        sfx("axchop");
        say("You mended the bridge! The tide pools are open.");
        return true;
    }
    if (verb == "JoinJoja" || verb == "JojaCD") { jojaDesk(); return true; }
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
    if (verb == "Arcade_Prairie") { arcadeStart(1); return true; }
    if (verb == "Arcade_Minecart") { arcadeStart(2); return true; }
    return false;
}

bool Game::worldChoice(const std::string& what, const std::string& key) {
    if (what == "board") {
        if (key == "board:orders") ordersBoard();
        else if (key == "board:calendar") worldAction("Billboard", "calendar");
        return true;
    }
    if (what == "joja") {
        if (key == "joja:join") {
            if (money < 5000) { say("Not enough money"); return true; }
            money -= 5000;
            mailReceived.insert("JojaMember");
            sfx("purchase");
            say("Welcome to the Joja family! Come back to this desk to develop the town.");
        } else if (key.rfind("joja:", 0) == 0) {
            std::string rest = key.substr(5);
            size_t colon = rest.find(':');
            int cost = atoi(rest.substr(colon + 1).c_str());
            if (money < cost) { say("Not enough money"); return true; }
            money -= cost;
            mailReceived.insert(rest.substr(0, colon));
            mailReceived.insert("joja" + rest.substr(2, colon - 2));
            if (!mailReceived.count("ccBulletin")) {   // the last one finishes the job
                bool all = true;
                for (const char* r : {"ccPantry", "ccCraftsRoom", "ccFishTank", "ccBoilerRoom", "ccVault"}) all &= mailReceived.count(r) != 0;
                if (all) mailReceived.insert("ccBulletin");
            }
            sfx("purchase");
            say("Joja crews will have it done by morning.");
        }
        return true;
    }
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

// ---- the travelling cart: ten things from anywhere, at three to five times their worth
void Game::cartMorning() {
    cartStock.clear();
    if (!cartHere()) return;
    std::vector<int> pool;
    for (auto& kv : data.objects) {
        const ObjectDef& o = kv.second;
        if (o.price > 0 && o.category != 0 && o.category != -999 && o.id >= 16 && o.id < 790) pool.push_back(o.id);
    }
    std::sort(pool.begin(), pool.end());   // the same stock whatever order the table loaded in
    for (int k = 0; k < 10 && !pool.empty(); k++) {
        size_t at = rand32() % pool.size();
        const ObjectDef* o = data.object(pool[at]);
        int price = std::max(100 + (int)(rand32() % 10) * 100, o->price * (3 + (int)(rand32() % 3)));
        cartStock.push_back({o->id, price});
        pool.erase(pool.begin() + at);
    }
    if (data.object(347) && rand32() % 100 < 40) cartStock.push_back({347, 1000});   // sometimes a rare seed
}

// ---- repairs that change the map you're standing on
void Game::applyMapFixes() {
    if (!cur) return;
    if (cur->name == "Beach" && mailReceived.count("beachBridgeFixed"))
        for (int x = 58; x <= 61; x++) cur->map.setFlags(x, 13, 0);   // planks over the gap
    if (cur->name == "FarmHouse") {
        cellarX = cellarY = -1;
        if (houseLevel >= 3 && hasMap("Cellar")) {   // the trapdoor: the first clear floor left of the front door
            int dx, dy;
            if (houseDoor(houseLevel, dx, dy))
                for (int k = 2; k < 12 && cellarX < 0; k++)
                    for (int up = 2; up < 6 && cellarX < 0; up++)
                        if (!tileSolid(dx - k, dy - up) && cur->map.tile(L_BACK, dx - k, dy - up) != NO_TILE && !cur->map.warpAt(dx - k, dy - up)) {
                            bool taken = false;
                            for (auto& fp : furniture)
                                if (fp.map == "FarmHouse") {
                                    auto fd = data.furniture.find(fp.id);
                                    int fw = fd != data.furniture.end() ? fd->second.boxW : 1, fh = fd != data.furniture.end() ? fd->second.boxH : 1;
                                    taken |= dx - k >= fp.x && dx - k < fp.x + fw && dy - up >= fp.y && dy - up < fp.y + fh;
                                }
                            if (!taken) { cellarX = dx - k; cellarY = dy - up; }
                        }
        }
    }
}

// ---- the cellar comes with its casks
void Game::stockCellar() {
    if (!hasMap("Cellar") || !data.big(163)) return;
    Location* c = loadLocation("Cellar");
    if (!c) return;
    auto& list = placed["Cellar"];
    int n = 0;
    for (auto& p : list) n += p.big && p.id == 163;
    const TileMap& m = c->map;
    for (int y = 2; y < m.height() - 1 && n < 33; y += 2)
        for (int x = 1; x < m.width() - 1 && n < 33; x++) {
            if (m.tile(L_BACK, x, y) == NO_TILE || m.tile(L_BUILDINGS, x, y) != NO_TILE || (m.flags(x, y) & CF_BLOCKED) || m.warpAt(x, y)) continue;
            if (x % 6 == 0) continue;   // aisles to walk down
            bool taken = false;
            for (auto& p : list) taken |= p.x == x && p.y == y;
            if (taken) continue;
            Placed p;
            p.x = x; p.y = y; p.big = true; p.id = 163;
            list.push_back(p);
            n++;
        }
}

// Days in the cask to reach iridium (the wiki's figures); 0 = it doesn't age.
static int caskDays(int item) {
    switch (item) {
        case 348: return 56;   // wine
        case 303: return 34;   // pale ale
        case 346: return 28;   // beer
        case 459: return 28;   // mead
        case 424: return 14;   // cheese
        case 426: return 14;   // goat cheese
        default: return 0;
    }
}

bool Game::caskInteract(Placed& p) {
    if (!p.held.empty()) {             // take it out, as good as it's got
        Slot out = p.held;
        if (!addSlot(out)) return true;
        static const char* q[5] = {"", "silver ", "gold ", "", "iridium "};
        say(std::string("Took the ") + q[std::min(4, (int)out.quality)] + itemName(out) + " out of the cask.");
        p.held = Slot();
        p.input = -1;
        return true;
    }
    Slot& sl = inv[selected];
    if (sl.type != SLOT_OBJECT || !caskDays(sl.id)) { say("Casks age wine, beer, pale ale, mead and cheese."); return true; }
    if (!cur || cur->name != "Cellar") { say("Casks only work down in the cellar."); return true; }
    if (sl.quality >= 4) { say("That's already as good as it gets."); return true; }
    p.held = sl;
    p.held.count = 1;
    p.input = 0;                        // days aged
    p.rule = -1;
    if (--sl.count <= 0) sl = Slot();
    sfx("Ship");
    say("Left it to age. " + std::to_string(caskDays(p.held.id)) + " days to iridium.");
    return true;
}

void Game::casksAge() {
    for (auto& kv : placed)
        for (auto& p : kv.second) {
            if (!p.big || p.id != 163 || p.held.empty() || kv.first != "Cellar") continue;
            int total = caskDays(p.held.id);
            if (!total) continue;
            p.input = std::max(0, p.input) + 1;
            int q = p.input * 4 >= total * 4 ? 4 : p.input * 2 >= total ? 2 : p.input * 4 >= total ? 1 : 0;
            if (q > p.held.quality) p.held.quality = (uint8_t)q;
        }
}

// ---- secret notes: once you can find them, a small chance whenever you earn farming, foraging or mining experience
void Game::secretNoteChance(int skill) {
    if (skill == SK_FISHING || skill == SK_COMBAT || !data.object(79)) return;
    if (!eventsSeen.count("520702") && !(year > 1 || season == WINTER)) return;   // the magnifying glass
    if (rand32() % 1000 >= 12) return;
    if (addItem(79, 1)) say("You found a secret note!");
}

bool Game::readSecretNote(int slot) {
    std::vector<int> unread;
    for (int n = 1; n <= 40; n++)
        if (mapString("secret:" + std::to_string(n)) && !mailReceived.count("secretNote:" + std::to_string(n))) unread.push_back(n);
    if (unread.empty()) { say("You've read every secret there is."); return false; }
    int n = unread[rand32() % unread.size()];
    mailReceived.insert("secretNote:" + std::to_string(n));
    std::string text = *mapString("secret:" + std::to_string(n));
    for (size_t at; (at = text.find('^')) != std::string::npos;) text.replace(at, 1, " ");
    showMessage("Secret Note " + std::to_string(n) + ":#$b#" + text);
    if (--inv[slot].count <= 0) inv[slot] = Slot();
    return true;
}

// ---- Joja: a membership, then Morris sells the town's repairs one by one
void Game::jojaDesk() {
    if (ccDone() && !mailReceived.count("JojaMember")) { say("Morris has nothing to say to you."); return; }
    DialoguePage p;
    DialogueChoice c;
    if (!mailReceived.count("JojaMember")) {
        p.text = "Morris: A Joja membership is 5,000g. The old Community Center becomes a warehouse, and Joja develops the town for you.";
        c.label = "Join (5,000g)"; c.responseKey = "joja:join"; p.choices.push_back(c);
    } else {
        struct { const char* flag; const char* name; int cost; } dev[] = {
            {"ccVault", "Bus", 40000}, {"ccBoilerRoom", "Minecarts", 15000}, {"ccCraftsRoom", "Bridge", 25000},
            {"ccPantry", "Greenhouse", 35000}, {"ccFishTank", "Panning", 20000}};
        p.text = "Morris: The Joja Community Development Form. What shall we build?";
        for (auto& d : dev)
            if (!mailReceived.count(d.flag)) {
                c.label = std::string(d.name) + " (" + std::to_string(d.cost) + "g)";
                c.responseKey = std::string("joja:") + d.flag + ":" + std::to_string(d.cost);
                p.choices.push_back(c);
            }
        if (p.choices.empty()) { say("Morris: Joja has developed everything. Thank you for your business!"); return; }
    }
    c.label = "Not today"; c.responseKey = ""; p.choices.push_back(c);
    dialogue = DialogueBox();
    dialogue.active = true;
    dialogue.pages.push_back(p);
    pendingChoice = "joja";
}

} // namespace sv
