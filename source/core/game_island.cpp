// Ginger Island: golden walnuts, the parrots that take them, Qi's walnut room, and the casino in the desert.
#include "game.h"

#include <algorithm>
#include <cstdlib>
#include <functional>

namespace sv {

static const int TILE = 16;

namespace {
struct Unlock {
    const char* flag;      // the game's own mail flag for it
    const char* label;
    int cost;
    const char* map;       // map it changes ("" = just a way through)
    const char* patch;     // the overlay map that goes on top
    int x, y;              // where; -1 = wherever the ground lines up
};
const Unlock UNLOCKS[] = {
    {"Island_FirstParrot", "Clear the way north", 1, "", "", 0, 0},
    {"Island_Turtle", "Wake the turtle blocking the west", 10, "", "", 0, 0},
    {"Island_UpgradeBridge", "Mend the dig site bridge", 10, "IslandNorth", "Island_Bridge_Repaired", 31, 52},
    {"Island_UpgradeTrader", "Build the island trader's stall", 10, "IslandNorth", "Island_N_Trader", -1, -1},
    {"Island_VolcanoBridge", "Bridge the lava to the volcano", 5, "", "", 0, 0},
    {"Island_UpgradeHouse", "Restore the farmhouse", 20, "IslandWest", "Island_House_Restored", 74, 33},
    {"Island_W_Obelisk", "Raise the farm obelisk", 20, "IslandWest", "Island_W_Obelisk", 82, 33},
    {"Island_Resort", "Build the beach resort", 20, "IslandSouth", "Island_Resort", -1, -1},
};
// How many walnuts lie about on each map.
const struct { const char* map; int n; } GROUND[] = {
    {"IslandSouth", 12}, {"IslandNorth", 19}, {"IslandWest", 32}, {"IslandEast", 14}, {"IslandSouthEast", 14},
    {"IslandShrine", 4}, {"IslandFarmCave", 2}, {"IslandNorthCave1", 3}, {"IslandWestCave1", 3}, {"CaptainRoom", 2},
};
uint32_t mix(uint32_t a) { a ^= a >> 16; a *= 0x7feb352du; a ^= a >> 15; a *= 0x846ca68bu; a ^= a >> 16; return a; }
}

void Game::gotWalnut(int n, const std::string& why) {
    walnuts += n;
    walnutsFound += n;
    sfx("coin");
    say(why + " +" + std::to_string(n) + (n == 1 ? " golden walnut" : " golden walnuts") + " (" + std::to_string(walnutsFound) + "/130)");
}

// A small chance of a walnut from island work; each kind of work only has five to give.
void Game::walnutChance(int pool, int oneIn) {
    if (pool < 0 || pool >= 4 || walnutPools[pool] >= 5 || rand32() % (uint32_t)oneIn) return;
    static const char* const WHY[] = {"Tucked in the crop!", "Hooked one!", "It fell out of the rock!", "The monster dropped it!"};
    walnutPools[pool]++;
    gotWalnut(1, WHY[pool]);
}

// The walnuts lying about this map: the same spots every time, minus the ones you've picked up.
void Game::islandWalnuts() {
    int want = 0;
    for (auto& g : GROUND) if (cur->name == g.map) want = g.n;
    if (!want) return;
    const TileMap& m = cur->map;
    int w = m.width(), h = m.height();
    auto open = [&](int x, int y) {
        if (!m.inBounds(x, y) || m.tile(L_BACK, x, y) == NO_TILE) return false;
        uint8_t f = m.flags(x, y);
        return !(f & CF_WATER) && (!(f & CF_BLOCKED) || (f & CF_PASSABLE));
    };
    if (cur->walnutSpots.empty() && !cur->walnutInit) {
        cur->walnutInit = true;
        std::vector<uint8_t> seen((size_t)w * h, 0);
        std::vector<int> queue;
        for (auto& wp : m.warps) {                       // everywhere you can walk to from the ways in
            int x = std::max(0, std::min(w - 1, wp.x)), y = std::max(0, std::min(h - 1, wp.y));
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++)
                    if (open(x + dx, y + dy) && !seen[(size_t)(y + dy) * w + x + dx]) { seen[(size_t)(y + dy) * w + x + dx] = 1; queue.push_back((y + dy) * w + x + dx); }
        }
        for (size_t qi = 0; qi < queue.size(); qi++) {
            int x = queue[qi] % w, y = queue[qi] / w;
            static const int D[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
            for (auto& d : D) {
                int nx = x + d[0], ny = y + d[1];
                if (open(nx, ny) && !seen[(size_t)ny * w + nx]) { seen[(size_t)ny * w + nx] = 1; queue.push_back(ny * w + nx); }
            }
        }
        uint32_t salt = 0;
        for (char ch : cur->name) salt = salt * 31 + (uint8_t)ch;
        std::vector<std::pair<uint32_t, int>> ranked;
        for (int cell : queue) {
            int x = cell % w, y = cell / w;
            if (x < 2 || y < 2 || x >= w - 2 || y >= h - 2 || m.warpAt(x, y) || !m.action(x, y).empty()) continue;
            ranked.push_back({mix((uint32_t)cell * 2654435761u + salt), cell});
        }
        std::sort(ranked.begin(), ranked.end());
        for (size_t i = 0; i < ranked.size() && (int)cur->walnutSpots.size() < want; i++) {
            int x = ranked[i].second % w, y = ranked[i].second / w;
            bool close = false;                          // spread them out
            for (auto& s : cur->walnutSpots) close |= std::abs(s.first - x) + std::abs(s.second - y) < 5;
            if (!close) cur->walnutSpots.push_back({x, y});
        }
    }
    for (auto& s : cur->walnutSpots) {
        std::string key = cur->name + ":" + std::to_string(s.first) + ":" + std::to_string(s.second);
        if (walnutsGot.count(key) || objectAt(s.first, s.second) || tileSolid(s.first, s.second)) continue;
        WorldObject o;
        o.x = s.first; o.y = s.second; o.itemId = 73;
        cur->objects.push_back(o);
    }
}

bool Game::pickWalnut(int tx, int ty) {
    WorldObject* o = objectAt(tx, ty);
    if (!o || o->itemId != 73 || o->ore) return false;
    walnutsGot.insert(cur->name + ":" + std::to_string(tx) + ":" + std::to_string(ty));
    cur->objects.erase(cur->objects.begin() + (o - &cur->objects[0]));
    gotWalnut(1, "A golden walnut!");
    return true;
}

// What the parrots have built so far goes onto the map.
void Game::islandStamps() {
    for (auto& u : UNLOCKS) {
        if (!u.map[0] || cur->name != u.map || !mailReceived.count(u.flag) || cur->stamps.count(u.patch)) continue;
        std::string path = std::string("maps/") + u.patch + ".smap";
        if (!data.vfs.exists(path)) continue;
        TileMap p;
        if (!p.load(data.vfs.path(path), nullptr)) continue;
        int x = u.x, y = u.y;
        if (x < 0 && !cur->map.findFit(p, x, y)) continue;
        if (!cur->map.stamp(p, x, y)) continue;
        cur->stamps.insert(u.patch);
        // the counters people serve from aren't in the map data: the trader's front, the bar's back shelf
        if (std::string(u.patch) == "Island_N_Trader")
            for (int k = 2; k <= 6; k++) cur->map.setAction(x + k, y + 7, "IslandTrader");
        if (std::string(u.patch) == "Island_Resort")
            for (int k = 12; k <= 16; k++) cur->map.setAction(x + k, y + 3, "ResortBar");
    }
    if (cur->name == "IslandWest" && hasMap("QiNutRoom")) cur->map.setAction(20, 22, "QiDoor");
    if (cur->name == "IslandSouth" && !mailReceived.count("Island_arrived")) {
        mailReceived.insert("Island_arrived");
        gotWalnut(1, "A parrot drops something shiny at your feet.");
    }
}

static void ask(Game& g, const std::string& text, const std::vector<std::pair<std::string, std::string>>& choices, const char* what) {
    DialoguePage p;
    p.text = text;
    for (auto& c : choices) {
        DialogueChoice ch;
        ch.label = c.first; ch.responseKey = c.second;
        p.choices.push_back(ch);
    }
    g.dialogue = DialogueBox();
    g.dialogue.active = true;
    g.dialogue.pages.push_back(p);
    g.pendingChoice = what;
}

void Game::islandOffer(const std::string& flag) {
    for (auto& u : UNLOCKS)
        if (flag == u.flag) {
            std::string nuts = std::to_string(u.cost) + (u.cost == 1 ? " golden walnut" : " golden walnuts");
            if (walnuts < u.cost) { showMessage("Squawk! " + std::string(u.label) + "? Bring " + nuts + ". You have " + std::to_string(walnuts) + "."); return; }
            ask(*this, "Squawk! " + std::string(u.label) + " for " + nuts + "? You have " + std::to_string(walnuts) + ".",
                {{"Hand them over", std::string("island:") + u.flag}, {"Not now", ""}}, "island");
        }
}

// Leo's parrot keeps the list of everything the parrots can still do.
void Game::islandParrot() {
    std::vector<std::pair<std::string, std::string>> ch;
    for (auto& u : UNLOCKS)
        if (!mailReceived.count(u.flag) && ch.size() < 5) ch.push_back({std::string(u.label) + " (" + std::to_string(u.cost) + ")", std::string("island:") + u.flag});
    if (ch.empty()) { showMessage("Squawk! The island is whole again. You found " + std::to_string(walnutsFound) + " of 130 golden walnuts."); return; }
    ch.push_back({"Nothing yet", ""});
    ask(*this, "Squawk! Golden walnuts make parrots work. You have " + std::to_string(walnuts) + " (found " + std::to_string(walnutsFound) + " of 130).", ch, "island");
}

// Some ways across the island are shut until a parrot is paid. True = the way is shut (and the offer's up).
bool Game::islandGate(const std::string& dest) {
    if (!cur) return false;
    const char* flag = nullptr;
    if (cur->name == "IslandSouth" && dest == "IslandNorth") flag = "Island_FirstParrot";
    else if (cur->name == "IslandSouth" && dest == "IslandWest") flag = "Island_Turtle";
    if (!flag || mailReceived.count(flag)) return false;
    const TileMap& m = cur->map;                         // step back from the edge
    float lim = TILE * 1.6f;
    if (px < lim) px = lim;
    if (py < lim + TILE) py = lim + TILE;
    if (px > m.width() * TILE - lim) px = m.width() * TILE - lim;
    islandOffer(flag);
    return true;
}

bool Game::islandChoice(const std::string& what, const std::string& key) {
    if (what == "island") {
        if (key.rfind("island:", 0) != 0) return true;
        for (auto& u : UNLOCKS) {
            if (key.substr(7) != u.flag) continue;
            if (walnuts < u.cost) { say("Squawk! Not enough golden walnuts (" + std::to_string(u.cost) + ")."); return true; }
            walnuts -= u.cost;
            mailReceived.insert(u.flag);
            sfx("parrot");
            islandStamps();
            showMessage(std::string("The parrots get to work... ") + u.label + ": done!");
        }
        return true;
    }
    if (what == "casino") {
        if (key == "casino:coins") {
            if (money < 1000) { say("100 Qi coins cost 1,000g."); return true; }
            money -= 1000; clubCoins += 100;
            sfx("purchase");
            say("+100 Qi coins (" + std::to_string(clubCoins) + ")");
        } else if (key.rfind("casino:slots:", 0) == 0) {
            int bet = atoi(key.c_str() + 13);
            if (clubCoins < bet) { say("Not enough Qi coins."); return true; }
            clubCoins -= bet;
            static const char* const REEL[] = {"Cherry", "Melon", "Parsnip", "Shell", "Star", "Stardrop"};
            static const int PAY[] = {5, 30, 80, 120, 200, 500};
            int a = rand32() % 6, b = rand32() % 6, c = rand32() % 6;
            if (rand32() % 100 < 22) b = c = a;          // the house lets some through
            int win = a == b && b == c ? PAY[a] * bet / 10 : (a == 0 && b == 0) ? 3 * bet / 10 * 10 / 10 : a == 0 ? 2 * bet : 0;
            if (a == b && b == c) win = PAY[a] * bet; else if (a == 0 && b == 0) win = bet * 3; else if (a == 0) win = bet * 2;
            clubCoins += win;
            sfx(win ? "reward" : "cancel");
            showMessage(std::string(REEL[a]) + " | " + REEL[b] + " | " + REEL[c] + (win ? "#$b#You win " + std::to_string(win) + " Qi coins!" : "#$b#Nothing this time.") +
                        " You have " + std::to_string(clubCoins) + ".");
        } else if (key.rfind("casino:jack:", 0) == 0) {
            jackBet = atoi(key.c_str() + 12);
            if (clubCoins < jackBet) { say("Not enough Qi coins."); jackBet = 0; return true; }
            clubCoins -= jackBet;
            jackMine = 2 + (int)(rand32() % 10) + 1 + (int)(rand32() % 10);   // two cards, 1 to 10 or so
            jackDealer = 1 + (int)(rand32() % 11);
            calicoJack();
        } else if (key == "casino:hit") {
            jackMine += 1 + (int)(rand32() % 11);
            calicoJack();
        } else if (key == "casino:stand") {
            while (jackDealer < 18) jackDealer += 1 + (int)(rand32() % 11);
            bool win = jackDealer > 21 || jackMine > jackDealer, push = jackDealer == jackMine;
            if (win) clubCoins += jackBet * 2; else if (push) clubCoins += jackBet;
            sfx(win ? "reward" : "cancel");
            showMessage("You: " + std::to_string(jackMine) + "   Dealer: " + std::to_string(jackDealer) + (jackDealer > 21 ? " (bust)" : "") + "#$b#" +
                        (win ? "You win " + std::to_string(jackBet * 2) + " Qi coins!" : push ? "A tie. Your bet comes back." : "The dealer wins.") + " You have " + std::to_string(clubCoins) + ".");
            jackBet = 0;
        } else if (key == "casino:statue") {
            if (money < 1000000) { say("Not enough money"); return true; }
            Slot s; s.type = SLOT_BIG; s.id = 127; s.count = 1;
            if (!addSlot(s)) return true;
            money -= 1000000;
            sfx("purchase");
            say("The Statue of Endless Fortune is yours.");
        }
        return true;
    }
    return false;
}

// CalicoJack: get as close to 21 as you can without going over.
void Game::calicoJack() {
    if (jackMine > 21) {
        sfx("cancel");
        showMessage("You draw up to " + std::to_string(jackMine) + ": bust! You have " + std::to_string(clubCoins) + " Qi coins.");
        jackBet = 0;
        return;
    }
    if (jackMine == 21) { islandChoice("casino", "casino:stand"); return; }
    ask(*this, "CalicoJack. You: " + std::to_string(jackMine) + "   Dealer shows: " + std::to_string(jackDealer), {{"Hit", "casino:hit"}, {"Stand", "casino:stand"}}, "casino");
}

bool Game::islandAction(const std::string& verb, const std::string& rest) {
    if (verb == "Parrot") { islandParrot(); return true; }
    if (verb == "QiDoor") {
        if (walnutsFound < 100) { showMessage("A sealed door. A plaque reads: \"Only those who have found 100 golden walnuts may enter.\" You've found " + std::to_string(walnutsFound) + "."); return true; }
        sfx("doorClose");
        warpTo("QiNutRoom", 7, 8, false);
        return true;
    }
    if (verb == "QiChallengeBoard" || verb == "DropBox") { ordersBoard(true); return true; }
    if (verb == "QiCat") { showMessage("Meow. (The cat looks like it knows something you don't.)"); return true; }
    if (verb == "BuyQiCoins") {
        ask(*this, "Buy 100 Qi coins for 1,000g? You have " + std::to_string(clubCoins) + ".", {{"Buy", "casino:coins"}, {"No", ""}}, "casino");
        return true;
    }
    if (verb == "ClubShop") { if (!data.shops.count("Casino")) return false; openShop = "Casino"; return true; }
    if (verb == "ClubSlots") {
        ask(*this, "The slot machine. You have " + std::to_string(clubCoins) + " Qi coins.", {{"Bet 10", "casino:slots:10"}, {"Bet 100", "casino:slots:100"}, {"Walk away", ""}}, "casino");
        return true;
    }
    if (verb == "ClubCards") {
        int bet = atoi(rest.c_str());
        if (bet <= 0) bet = 100;
        ask(*this, "CalicoJack: " + std::to_string(bet) + " Qi coins a hand. You have " + std::to_string(clubCoins) + ".",
            {{"Deal me in", "casino:jack:" + std::to_string(bet)}, {"No thanks", ""}}, "casino");
        return true;
    }
    if (verb == "ClubSeller") {
        ask(*this, "\"Psst. I've got a Statue of Endless Fortune. One million g.\"", {{"Buy it (1,000,000g)", "casino:statue"}, {"No", ""}}, "casino");
        return true;
    }
    if (verb == "ClubComputer") { showMessage("Qi coins: " + std::to_string(clubCoins) + ". The house always wins... usually."); return true; }
    return false;
}

} // namespace sv
