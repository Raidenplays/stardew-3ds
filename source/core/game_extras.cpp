// The odd corners: bedroom doors, Lewis's house, the sewer statue, the witch's hut, Qi's errands and the
// casino door, the movie theater, and the rest of Ginger Island (field office, gourmand frog, banana
// shrine, crystal cave, parrot express, the island bin and mailbox).
#include "game.h"

#include <algorithm>
#include <cstdlib>
#include <sstream>

namespace sv {

static const int TILE = 16;

static void ask2(Game& g, const std::string& text, const std::vector<std::pair<std::string, std::string>>& choices) {
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
    g.pendingChoice = "extra";
}

std::string Game::L(const std::string& key, const std::string& fallback, const std::string& a0, const std::string& a1) {
    std::string s = gameLine("loc:" + key, fallback, a0, a1);
    for (auto& ch : s)
        if (ch == '^') ch = ' ';                          // their line breaks: our box wraps by itself
    return s;
}

int Game::objectByName(const std::string& name) const {
    for (auto& kv : data.objects)
        if (kv.second.name == name) return kv.first;
    return -1;
}

// Walk into a map we only know by name: stand just inside the way back to where we are, or failing that on
// the open tile nearest the bottom middle.
bool Game::enterMap(const std::string& name) {
    if (!hasMap(name)) return false;
    TileMap m;
    if (!m.load(data.vfs.path("maps/" + name + ".smap"), nullptr)) return false;
    int bx = -1, by = -1;
    for (auto& w : m.warps)
        if (cur && w.target == cur->name) {
            bx = std::max(1, std::min(m.width() - 2, w.x));
            by = std::max(1, std::min(m.height() - 2, w.y >= m.height() - 1 ? m.height() - 2 : w.y + 1));
        }
    auto open = [&](int x, int y) {
        uint8_t f = m.flags(x, y);
        return m.inBounds(x, y) && m.tile(L_BACK, x, y) != NO_TILE && !(f & CF_WATER) && (!(f & CF_BLOCKED) || (f & CF_PASSABLE));
    };
    if (bx < 0 || !open(bx, by)) {
        int best = 1 << 30;
        for (int y = 0; y < m.height(); y++)
            for (int x = 0; x < m.width(); x++) {
                int d = std::abs(x - m.width() / 2) + (m.height() - 1 - y) * 2;
                if (open(x, y) && open(x, y - 1) && d < best) { best = d; bx = x; by = y; }
            }
    }
    if (bx < 0) return false;
    return warpTo(name, bx, by, true);
}

// A door you open by hand: step to its far side.
void Game::stepThroughDoor() {
    int tx = (int)(px / TILE), ty = (int)((py - 1) / TILE);
    static const int D[4][2] = {{0, 1}, {1, 0}, {0, -1}, {-1, 0}};   // down, right, up, left
    int dx = D[facing & 3][0], dy = D[facing & 3][1];
    for (int k = 2; k <= 3; k++) {
        int nx = tx + dx * k, ny = ty + dy * k;
        if (cur->map.inBounds(nx, ny) && !tileSolid(nx, ny)) {
            px = nx * TILE + TILE / 2.0f;
            py = ny * TILE + TILE - 2.0f;
            sfx("doorClose");
            return;
        }
    }
}

// Qi beans turn up while one of his crop challenges is on.
void Game::qiBeanChance(int oneIn) {
    if (orderKey.rfind("QiChallenge2#", 0) != 0 || rand32() % (uint32_t)oneIn || !data.object(890)) return;
    if (addItem(890, 1 + (int)(rand32() % 3))) say("Qi beans!");
}

// Fossils lie about the dig site once the bridge is mended: only kinds the field office still wants.
void Game::islandFossils() {
    if (!cur || cur->name != "IslandNorth" || !mailReceived.count("Island_UpgradeBridge")) return;
    uint32_t seed = (uint32_t)absoluteDay() * 2246822519u;
    int placed = 0;
    for (int id = 820; id <= 828 && placed < 3; id++) {
        if (mailReceived.count("fossil:" + std::to_string(id)) || countItem(id) > 0 || !data.object(id)) continue;
        seed = seed * 1664525u + 1013904223u + (uint32_t)id;
        if ((seed >> 8) % 3) continue;                    // not every kind every day
        for (int tries = 0; tries < 40; tries++) {
            seed = seed * 1664525u + 1013904223u;
            int x = 4 + (int)((seed >> 10) % 26), y = 44 + (int)((seed >> 20) % 20);
            if (!cur->map.inBounds(x, y) || tileSolid(x, y) || objectAt(x, y) || cur->map.tile(L_BACK, x, y) == NO_TILE || (cur->map.flags(x, y) & CF_WATER)) continue;
            WorldObject o;
            o.x = x; o.y = y; o.itemId = id;
            cur->objects.push_back(o);
            placed++;
            break;
        }
    }
}

bool Game::extraAction(const std::string& verb, const std::string& rest) {
    auto flag = [&](const std::string& f) { return mailReceived.count(f) != 0; };
    Slot& held = inv[selected];
    int heldId = held.type == SLOT_OBJECT ? held.id : -1;
    auto useHeld = [&](int n) { held.count -= n; if (held.count <= 0) held = Slot(); };

    // ---- things mods let you talk to from where you stand (SVE's Dusty in his pen) ----
    if (verb == "NPCSpeechMessageNoRadius" || verb == "NPCSpeechMessage") {
        std::istringstream ss(rest);
        std::string who, key;
        ss >> who >> key;
        const std::string* s = mapString(key);
        if (!s) return false;
        std::string text = *s;
        if (!text.empty() && text[0] == '*') text.erase(0, 1);                       // "*You give Dusty many pets$0"
        size_t d = text.rfind('$');
        if (d != std::string::npos && d + 2 >= text.size()) text.erase(d);
        if (who == "Dusty") sfx("dog_bark");
        showMessage(text + (who == "Dusty" ? "." : ""));
        return true;
    }

    // ---- doors ----
    if (verb == "Door") {
        std::istringstream ss(rest);
        std::string who, first;
        bool ok = true, any = false;
        while (ss >> who) {
            if (!any) { first = who; ok = false; }
            any = true;
            const NpcState* n = npcByName(who);
            if (n && (n->hearts() >= 2 || who == spouse)) ok = true;
        }
        if (!ok) {
            const NpcState* n = npcByName(first);
            std::string nm = n && n->def ? n->def->display : first;
            const std::string* gen = mapString("gender:" + first);
            say(L(gen && *gen == "Female" ? "DoorUnlock_NotFriend_Female" : "DoorUnlock_NotFriend_Male", nm + "'s room. You aren't close enough friends to go in.", nm));
            return true;
        }
        stepThroughDoor();
        return true;
    }
    if (verb == "ConditionalDoor") {
        if (!gameQuery(rest)) { say("It's locked."); return true; }
        stepThroughDoor();
        return true;
    }
    if (verb == "WarpMensLocker" || verb == "WarpWomensLocker") {
        std::istringstream ss(rest);
        int x = 0, y = 0;
        std::string map;
        ss >> x >> y >> map;
        if (map.empty() || !hasMap(map)) return false;
        warpTo(map, x, y, true);
        return true;
    }
    if (verb == "Warp_Sunroom_Door") {
        const NpcState* n = npcByName("Caroline");
        if (!n || n->hearts() < 2) { say(L("Caroline_Sunroom_Door", "Caroline only lets her friends enter here...")); return true; }
        return enterMap("Sunroom");
    }
    if (verb == "WizardHatch") {
        const NpcState* n = npcByName("Wizard");
        if (!n || n->hearts() < 4) { say("The hatch is sealed by magic. The Wizard doesn't know you well enough yet."); return true; }
        return enterMap("WizardHouseBasement");
    }

    // ---- Lewis's house ----
    if (verb == "PrizeMachine") {
        int ticket = objectByName("Prize Ticket");
        if (ticket < 0 || countItem(ticket) < 1) { showMessage("The prize machine. It takes prize tickets."); return true; }
        static const struct { const char* name; int n; } PRIZES[] = {
            {"Carrot Seeds", 12}, {"Mystery Box", 3}, {"Stardrop Tea", 1}, {"Deluxe Speed-Gro", 20}, {"Mystery Box", 4},
            {"Apple Sapling", 1}, {"Fairy Dust", 3}, {"Mystery Box", 5}, {"Diamond", 5}, {"Stardrop Tea", 1},
            {"Iridium Sprinkler", 2}, {"Mystery Box", 6}, {"Magic Rock Candy", 1}, {"Golden Mystery Box", 2}, {"Prismatic Shard", 1},
        };
        int& claimed = stats["ticketPrizesClaimed"];
        int item = -1, n = 1;
        for (int k = 0; k < 15 && item < 0; k++) {       // the next prize in line that this game has
            auto& p = PRIZES[(claimed + k) % 15];
            item = objectByName(p.name);
            n = p.n;
        }
        if (item < 0) { item = 72; n = 1; }
        if (!addItem(item, n)) return true;
        takeItem(ticket, 1);
        claimed++;
        sfx("reward");
        const ObjectDef* o = data.object(item);
        showMessage("The machine whirrs... " + std::to_string(n) + " " + (o ? o->name : "prize") + "!");
        return true;
    }
    if (verb == "LedgerBook") { showMessage(L("ManorHouse_LedgerBook_Singleplayer", "Pelican Town Ledger Book...")); return true; }
    if (verb == "LostAndFound") { showMessage(L("ManorHouse_LAF_Check", "The town's 'Lost and Found' box.")); return true; }
    if (verb == "DivorceBook") {
        if (spouse.empty()) { showMessage(L("ManorHouse_DivorceBook_NoSpouse", "Pelican Town Marriage Log... You aren't married.")); return true; }
        const NpcState* n = npcByName(spouse);
        (void)n;
        ask2(*this, L("ManorHouse_DivorceBook_Question", "File for divorce? There is a 50,000g fee."), {{"Yes", "extra:divorce"}, {"No", ""}});
        return true;
    }
    if (verb == "MayorFridge") {
        if (flag("qi:shell") && !flag("qi:beets")) {
            if (heldId == 284 && held.count >= 10) {
                useHeld(10);
                mailReceived.insert("qi:beets");
                sfx("Ship");
                showMessage(L("ManorHouse_MayorFridge_ConsumeBeets", "You quickly place the beets in the fridge. There's a note taped to the milk:") + "#$b#" +
                            L("ManorHouse_MayorFridge_MrQiNote", "'Good job, kid. Your final task: Give the sand dragon his last meal. -Mr. Qi'"));
            } else showMessage(L("ManorHouse_MayorFridge_Initial", "It's the mayor's fridge."));
            return true;
        }
        showMessage(L("ManorHouse_MayorFridge_Initial", "It's the mayor's fridge. It's mostly full of milk and iced tea."));
        return true;
    }

    // ---- Qi's errands: tunnel lockbox, railroad box, the fridge, the sand dragon ----
    if (verb == "TunnelSafe") {
        if (flag("qi:battery")) { showMessage(L("Railroad_Box_Initial", "It's an empty box.")); return true; }
        if (heldId == 787) {
            useHeld(1);
            mailReceived.insert("qi:battery");
            sfx("Ship");
            showMessage(L("Tunnel_TunnelSafe_ConsumeBattery", "It worked! The lock-box swings open. Inside, there's a note:") + "#$b#" +
                        L("Tunnel_TunnelSafe_MrQiNote", "'Your first task: Bring a rainbow shell to the train platform. Leave it in the box. -Mr. Qi'"));
        } else showMessage(L("Tunnel_TunnelSafe_Initial", "It's a battery powered lock-box. Unfortunately, there's no battery in it."));
        return true;
    }
    if (verb == "RailroadBox") {
        if (!flag("qi:battery") || flag("qi:shell")) { showMessage(L("Railroad_Box_Initial", "It's an empty box.")); return true; }
        if (heldId == 394) {
            useHeld(1);
            mailReceived.insert("qi:shell");
            sfx("Ship");
            showMessage(L("Railroad_Box_ConsumeShell", "You throw the shell into the box. A note falls down from the rafters above:") + "#$b#" +
                        L("Railroad_Box_MrQiNote", "'Well done. Your next task: Place 10 beets inside Mayor's fridge. -Mr. Qi'"));
        } else showMessage(L("Railroad_Box_Initial", "It's an empty box."));
        return true;
    }
    if (verb == "SandDragon") {
        if (flag("HasClubCard") || flag("qi:lumber") || !flag("qi:beets") || heldId != 768) {
            showMessage(L("Desert_SandDragon_Initial", "Its maw gapes at the desert sun. Even in death, it yearns for a taste."));
            return true;
        }
        useHeld(1);
        mailReceived.insert("qi:lumber");                 // the card is waiting in the lumber pile by the farmhouse
        sfx("reward");
        showMessage(L("Desert_SandDragon_ConsumeEssence", "You place the essence in the dragon's maw. There's a note hidden in the eye socket:") + "#$b#" +
                    L("Desert_SandDragon_MrQiNote", "'You've proven yourself, kid. Check out the pile of lumber next to your house. -Mr. Qi'"));
        return true;
    }
    if (verb == "WitchCaveBlock") {
        if (!flag("henchmanGone")) {
            if (heldId == 308) {
                useHeld(1);
                mailReceived.insert("henchmanGone");
                sfx("reward");
                showMessage("\"Void mayonnaise?! My favourite!\" The henchman scurries off with it. The way is open.");
            } else showMessage("\"Halt! The mistress doesn't want visitors.\" (He keeps glancing at your bag. Perhaps he's hungry for something... void?)");
            return true;
        }
        return enterMap("WitchSwamp");
    }

    // ---- the sewer statue and the witch's hut ----
    if (verb == "DogStatue") {
        if (professions.empty()) { showMessage("The Statue of Uncertainty. It has nothing to change for you yet."); return true; }
        ask2(*this, L("Sewer_DogStatue", "-Statue Of Uncertainty- The statue can help you change professions. Make an offering of 10,000g?"), {{"Yes", "extra:statue"}, {"No", ""}});
        return true;
    }
    if (verb == "EvilShrineLeft") {                      // selfishness: the children become doves
        if (kids.empty()) { showMessage(L("WitchHut_EvilShrineLeftInactive", "-Dark Shrine of Selfishness- The statue stares through you...")); return true; }
        if (countItem(74) < 1) {
            showMessage(L("WitchHut_EvilShrineLeft", "-Dark Shrine of Selfishness- Your children will turn into doves and fly away... Make an offering of one Prismatic Shard?") +
                        "#$b#" + L("WitchHut_NoOffering", "You don't have the necessary offering."));
            return true;
        }
        ask2(*this, L("WitchHut_EvilShrineLeft", "-Dark Shrine of Selfishness- Your children will turn into doves and fly away... Make an offering of one Prismatic Shard?"),
             {{"Yes", "extra:doves"}, {"No", ""}});
        return true;
    }
    if (verb == "EvilShrineCenter") {                    // memory: your exes forget
        bool ex = false;
        for (auto& f : mailReceived) ex |= f.rfind("divorced:", 0) == 0;
        if (!ex) { showMessage(L("WitchHut_EvilShrineCenterInactive", "-Dark Shrine of Memory- The statue stares through you...")); return true; }
        ask2(*this, L("WitchHut_EvilShrineCenter", "-Dark Shrine of Memory- The relationship between you and your ex-spouses will be wiped from memory. Make an offering of 30,000g?"),
             {{"Yes", "extra:memory"}, {"No", ""}});
        return true;
    }
    if (verb == "MonsterGrave") { showMessage(L("Backwoods_MonsterGrave", "A small grave.")); return true; }
    if (verb == "EmilyRoomObject") { showMessage(L("HaleyHouse_EmilyRoomObject", "There's a tape inside: 'Freeform Dance Rhythms'")); return true; }
    if (verb == "ElliottBook") { showMessage(L("ElliottHouse_ElliottBook_Blank", "Blank book")); return true; }
    if (verb == "NPCMessage") {                          // NPCMessage Sam "Data\ExtraDialogue:Key/Strings\Locations:Key"
        size_t c1 = rest.find(':');
        if (c1 == std::string::npos) return false;
        size_t e = rest.find_first_of("/\"", c1);
        std::string key = rest.substr(c1 + 1, e == std::string::npos ? std::string::npos : e - c1 - 1);
        const std::string* line = mapString("xd:" + key);
        if (!line) line = mapString("loc:" + key);
        if (!line) return false;
        std::string who = rest.substr(0, rest.find(' '));
        const NpcState* n = npcByName(who);
        std::string text = *line;
        size_t d = text.rfind('$');
        if (d != std::string::npos && d + 2 >= text.size()) text.erase(d);
        if (n && n->loc == cur->name) showMessage((n->def ? n->def->display : who) + ": \"" + text + "\"");
        else return false;                               // nobody's there to mind
        return true;
    }
    if (verb == "MagicInk") {
        if (flag("hasMagicInk")) return false;
        mailReceived.insert("hasMagicInk");
        sfx("reward");
        showMessage("You found the Wizard's magic ink! He'll be glad to have it back.");
        if (NpcState* w = npcByNameMut("Wizard")) w->friendship = std::min(friendshipCap(*w), w->friendship + 250);
        return true;
    }

    // ---- the movie theater ----
    static const char* const MOVIES[] = {"The Brave Little Sapling", "Journey Of The Prairie King: The Motion Picture", "Mysterium",
        "The Miracle At Coldstar Ranch", "Natural Wonders: Exploring Our Vibrant World", "Wumbus", "It Howls In The Rain", "The Zuzu City Express"};
    const char* movie = MOVIES[((year - 1) % 2) * 4 + season % 4];
    if (verb == "Theater_Poster") { showMessage(L("Theater_Poster_0", "Now Playing: \"{0}.\"", movie)); return true; }
    if (verb == "Theater_PosterComingSoon") { showMessage(L("Theater_Poster_Coming_Soon", "Coming soon: \"{0}.\"", MOVIES[(((year - 1) % 2) * 4 + season % 4 + 1) % 8])); return true; }
    if (verb == "Theater_BoxOffice") {
        if (data.shops.count("BoxOffice")) { openShop = "BoxOffice"; return true; }
        return false;
    }
    if (verb == "Theater_Entrance") {
        if (flag("movie:" + std::to_string(absoluteDay()))) { say("You've already seen today's movie."); return true; }
        if (countItem(809) < 1) { say("You need a movie ticket. The box office is right here."); return true; }
        return enterMap("MovieTheater");
    }
    if (verb == "Theater_Doors") {
        if (countItem(809) < 1) { say("You need a movie ticket."); return true; }
        ask2(*this, std::string("Watch \"") + movie + "\"? It runs about three hours.", {{"Go in", "extra:movie"}, {"Not yet", ""}});
        return true;
    }
    if (verb == "Concessions") {
        ask2(*this, "The concession stand.", {{"Popcorn (120g)", "extra:snack:120"}, {"Jawbreaker (250g)", "extra:snack:250"}, {"Nothing", ""}});
        return true;
    }
    if (verb == "CraneGame") {
        ask2(*this, "The crane game: 500g a try.", {{"Play (500g)", "extra:crane"}, {"No", ""}});
        return true;
    }

    // ---- Ginger Island ----
    if (verb == "Gourmand") {
        static const int WANT[] = {254, 262, 248};       // melon, wheat, garlic
        int at = flag("gourmand:2") ? 3 : flag("gourmand:1") ? 2 : flag("gourmand:0") ? 1 : 0;
        if (at >= 3) { showMessage("The gourmand frog pats its belly. \"No more! I'm stuffed. Thank you, little one.\""); return true; }
        const ObjectDef* o = data.object(WANT[at]);
        bool grown = false;
        auto loc = locations.find("IslandWest");
        if (loc != locations.end())
            for (auto& s : loc->second->soils) {
                const CropDef* c = s.seed >= 0 ? data.cropForSeed(s.seed) : nullptr;
                grown |= c && c->harvestId == WANT[at] && !s.dead && s.phase >= c->phaseDays.size();
            }
        if (!grown) { showMessage("\"I'm so hungry... grow me a " + std::string(o ? o->name : "crop") + " on the farm out there, and let me smell it when it's ripe!\""); return true; }
        mailReceived.insert("gourmand:" + std::to_string(at));
        gotWalnut(5, "\"Mmm! I can smell it from here!\"");
        return true;
    }
    if (verb == "BananaShrine") {
        if (flag("bananaShrine")) { showMessage("The shrine is empty. Something big is snoring nearby."); return true; }
        if (heldId != 91) { showMessage("A stone shrine shaped like an open hand. It smells faintly of fruit."); return true; }
        useHeld(1);
        mailReceived.insert("bananaShrine");
        gotWalnut(3, "A gorilla snatches the banana and tosses something back!");
        return true;
    }
    if (verb == "CrystalCaveActivate") {
        if (flag("crystalCave")) { showMessage("The crystals are quiet now."); return true; }
        crystalSeq.clear();
        crystalAt = 0;
        int len = 5;
        std::string said;
        for (int i = 0; i < len; i++) {
            crystalSeq.push_back(1 + (int)(rand32() % 5));
            said += (i ? ", " : "") + std::to_string(crystalSeq.back());
        }
        sfx("crystal");
        showMessage("The crystals chime in turn, counting from the left: " + said + ". Touch them in the same order.");
        return true;
    }
    if (verb == "Crystal") {
        int n = atoi(rest.c_str());
        if (flag("crystalCave") || crystalSeq.empty()) { sfx("crystal"); return true; }
        if (n != crystalSeq[crystalAt]) {
            crystalSeq.clear();
            sfx("cancel");
            say("A sour note. The crystals go dark: start again at the pedestal.");
            return true;
        }
        sfx("crystal");
        if (++crystalAt >= crystalSeq.size()) {
            crystalSeq.clear();
            mailReceived.insert("crystalCave");
            gotWalnut(3, "The cave rings out in harmony!");
        } else say(std::to_string(crystalAt) + " of 5...");
        return true;
    }
    if (verb == "FieldOfficeSurvey") {
        if (!flag("survey:flowers")) ask2(*this, "Island survey: how many purple flowers grow on the island?", {{"18", "extra:no"}, {"20", "extra:no"}, {"22", "extra:flowers"}, {"24", "extra:no"}});
        else if (!flag("survey:starfish")) ask2(*this, "Island survey: how many purple starfish are on the island?", {{"16", "extra:no"}, {"18", "extra:starfish"}, {"20", "extra:no"}, {"22", "extra:no"}});
        else showMessage("The survey sheet is filled in. Professor Snail thanks you.");
        return true;
    }
    if (verb == "FieldOfficeDesk") {
        int gave = 0, have = 0;
        for (int id = 820; id <= 828; id++) {
            std::string f = "fossil:" + std::to_string(id);
            if (!flag(f) && countItem(id) > 0) { takeItem(id, 1); mailReceived.insert(f); gave++; }
            have += flag(f);
        }
        if (gave) {
            bool done = have == 9 && !flag("fossil:all");
            if (done) mailReceived.insert("fossil:all");
            gotWalnut(gave + (done ? 2 : 0), done ? "The collection is complete!" : "Professor Snail is delighted.");
        } else showMessage(have >= 9 ? "The fossil collection is complete." : "The fossil desk: " + std::to_string(have) + " of 9 pieces donated. Fossils turn up at the dig site across the bridge.");
        return true;
    }
    if (verb == "IslandBin") { openBin((int)(px / TILE), (int)(py / TILE) - 1); return true; }
    if (verb == "IslandMailbox") { if (!openMail()) say("No mail"); return true; }
    return false;
}

bool Game::extraChoice(const std::string& what, const std::string& key) {
    if (what != "extra") return false;
    if (key == "extra:divorce") {
        if (money < 50000) { say("Not enough money"); return true; }
        money -= 50000;
        if (NpcState* n = npcByNameMut(spouse)) n->friendship = 0;
        mailReceived.insert("divorced:" + spouse);
        spouse.clear();
        babyDue = 0;
        babyAsk = false;
        showMessage(L("ManorHouse_DivorceBook_Filed", "Your divorce papers have been filed."));
    } else if (key == "extra:statue") {
        if (money < 10000) { say("Not enough money"); return true; }
        money -= 10000;
        professions.clear();
        for (int sk = 0; sk < 5; sk++)                    // tonight you pick again, for every level you've earned
            for (int lv = 5; lv <= 10; lv += 5)
                if (skillLevels[sk] >= lv) levelUps.push_back({sk, lv});
        sfx("reward");
        showMessage(L("Sewer_DogStatueFinished", "*woof* *woof* (It's done... now go to sleep...)"));
    } else if (key == "extra:doves") {
        if (countItem(74) < 1) return true;
        takeItem(74, 1);
        kids.clear();
        sfx("wand");
        showMessage("A flutter of wings, and they are gone.");
    } else if (key == "extra:memory") {
        if (money < 30000) { say("Not enough money"); return true; }
        money -= 30000;
        for (auto it = mailReceived.begin(); it != mailReceived.end();)
            if (it->rfind("divorced:", 0) == 0) it = mailReceived.erase(it); else ++it;
        sfx("wand");
        showMessage("The memories fade. They will greet you as a stranger.");
    } else if (key == "extra:movie") {
        if (countItem(809) < 1) return true;
        takeItem(809, 1);
        mailReceived.insert("movie:" + std::to_string(absoluteDay()));
        timeOfDay = std::min(25 * 60, timeOfDay + 180);
        static const char* const MOVIES[] = {"The Brave Little Sapling", "Journey Of The Prairie King: The Motion Picture", "Mysterium",
            "The Miracle At Coldstar Ranch", "Natural Wonders: Exploring Our Vibrant World", "Wumbus", "It Howls In The Rain", "The Zuzu City Express"};
        std::string with;
        for (auto it = mailReceived.begin(); it != mailReceived.end();)
            if (it->rfind("movieGuest:", 0) == 0) { with = it->substr(11); it = mailReceived.erase(it); } else ++it;
        NpcState* guest = with.empty() ? nullptr : npcByNameMut(with);
        if (guest) guest->friendship = std::min(friendshipCap(*guest), guest->friendship + 200);
        showMessage(std::string("The lights go down... \"") + MOVIES[((year - 1) % 2) * 4 + season % 4] + "\" was worth the ticket." +
                    (guest && guest->def ? "#$b#" + guest->def->display + " loved it, and loved the company." : ""));
        if (hasMap("Town")) enterMap("Town");
    } else if (key.rfind("extra:snack:", 0) == 0) {
        int cost = atoi(key.c_str() + 12);
        if (money < cost) { say("Not enough money"); return true; }
        money -= cost;
        energy = std::min((int)MAX_ENERGY, energy + cost / 4);
        sfx("eat");
        say("Tasty.");
    } else if (key == "extra:crane") {
        if (money < 500) { say("Not enough money"); return true; }
        money -= 500;
        if (rand32() % 100 < 45) {
            static const char* const WIN[] = {"Mystery Box", "Coffee", "Triple Shot Espresso", "Magic Rock Candy", "Diamond", "Movie Ticket"};
            int item = objectByName(WIN[rand32() % 6]);
            if (item < 0) item = 72;
            addItem(item, 1);
            sfx("reward");
            const ObjectDef* o = data.object(item);
            showMessage("The claw holds... You won " + std::string(o ? o->name : "a prize") + "!");
        } else { sfx("cancel"); showMessage("The claw slips. So close!"); }
    } else if (key == "extra:flowers" || key == "extra:starfish") {
        mailReceived.insert(key == "extra:flowers" ? "survey:flowers" : "survey:starfish");
        gotWalnut(1, "Correct! Professor Snail hands you a reward.");
    } else if (key == "extra:no") {
        say("\"Hmm, that doesn't match my notes. Count again and come back tomorrow.\"");
    } else if (key.rfind("extra:fly:", 0) == 0) {
        std::string dest = key.substr(10);
        sfx("parrot");
        enterMap(dest);
    }
    return true;
}

} // namespace sv
