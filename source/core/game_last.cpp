// The last pieces: the mastery cave, the raccoon, the Desert Festival, the Trout Derby, SquidFest, the
// night market, the summit, Emily's sewing machine and dye pots, the Wizard's shrine, night terrors.
#include "game.h"

#include <algorithm>
#include <cstdlib>
#include <sstream>

namespace sv {

static const int TILE = 16;

static void ask3(Game& g, const std::string& text, const std::vector<std::pair<std::string, std::string>>& choices) {
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
    g.pendingChoice = "last";
}

bool Game::desertFest() const { return season == SPRING && day >= 15 && day <= 17; }
bool Game::troutDerby() const { return season == SUMMER && day >= 20 && day <= 21; }
bool Game::squidFest() const { return season == WINTER && day >= 12 && day <= 13; }
bool Game::nightMarket() const { return season == WINTER && day >= 15 && day <= 17; }

// Some places wear a different map on certain days.
std::string Game::mapForToday(const std::string& name) const {
    if (name == "Desert" && desertFest() && hasMap("DesertFestival")) return "DesertFestival";
    if (name == "Beach" && nightMarket() && timeOfDay >= 17 * 60 && hasMap("BeachNightMarket")) return "BeachNightMarket";
    return name;
}

int Game::giveNamed(const char* name, int n) {
    int id = objectByName(name);
    if (id < 0) return -1;
    return addItem(id, n) ? id : -1;
}

// How finished the valley is, out of 100 (Qi keeps count; the summit opens at 100).
int Game::perfection() {
    float total = 0;
    auto part = [&](int page, float weight) {
        auto items = collectionItems(page);
        if (items.empty()) { total += weight; return; }
        int have = 0;
        for (int id : items) have += collectionCount(page, id) > 0;
        total += weight * have / (float)items.size();
    };
    part(0, 15);                                         // shipped
    part(1, 10);                                         // fish
    part(3, 10);                                         // cooking
    part(2, 5);                                          // museum
    int known = 0, craftable = 0;
    for (auto& r : data.recipes)
        if (!r.cooking) { craftable++; known += craftedEver.count(r.outId) != 0; }
    total += craftable ? 10.0f * known / craftable : 10.0f;
    int levels = 0;
    for (int sk = 0; sk < 5; sk++) levels += std::min(10, skillLevels[sk]);
    total += 5.0f * levels / 50.0f;
    int friends = 0, people = 0;
    for (auto& n : npcs)
        if (n.def && !n.def->dialogue.empty()) { people++; friends += n.hearts() >= 8; }
    total += people ? 11.0f * friends / people : 11.0f;
    total += 5.0f * std::min(130, walnutsFound) / 130.0f;
    int obelisks = 0;
    for (auto& b : buildings) obelisks += b.type.find("Obelisk") != std::string::npos || b.type == "Gold Clock";
    total += 14.0f * std::min(5, obelisks) / 5.0f;       // the four obelisks and the gold clock
    total += 5.0f * std::min(100, stats["monstersKilled"] / 10) / 100.0f;
    int stardrops = (int)mailReceived.count("CF_Fair") + (int)mailReceived.count("CF_Mines") + (int)mailReceived.count("CF_Spouse") +
                    (int)mailReceived.count("CF_Sewer") + (int)mailReceived.count("CF_Statue") + (int)mailReceived.count("CF_Fish") +
                    (int)mailReceived.count("museumComplete");
    total += 10.0f * std::min(7, stardrops) / 7.0f;
    return (int)(total + 0.01f);
}

int Game::masteryLevel() {
    static const int NEED[] = {10000, 25000, 45000, 70000, 100000};
    int lv = 0;
    for (int n : NEED) lv += stats["masteryXp"] >= n;
    return lv;
}

void Game::lastXp(int skill, int xp) {
    (void)skill;
    for (int sk = 0; sk < 5; sk++)
        if (skillLevels[sk] < 10) return;
    int before = masteryLevel();
    stats["masteryXp"] += xp;
    if (masteryLevel() > before) say("Your mastery grew! Visit the cave in the forest.");
}

// What the season's little festivals and the raccoon change on the map you just walked into.
void Game::lastMapFixes() {
    if (!cur) return;
    auto stamp = [&](const char* patch, int x, int y) {
        if (cur->stamps.count(patch)) return false;
        std::string path = std::string("maps/") + patch + ".smap";
        TileMap p;
        if (!data.vfs.exists(path) || !p.load(data.vfs.path(path), nullptr) || !cur->map.stamp(p, x, y)) return false;
        cur->stamps.insert(patch);
        return true;
    };
    if (cur->name == "Forest") {
        if (troutDerby()) stamp("Forest_FishingDerby", 63, 43);
        if (mailReceived.count("raccoonFixed")) {
            if (stamp("Forest_RaccoonHouse", 53, 2))
                for (int k = 2; k <= 4; k++) cur->map.setAction(53 + k, 6, "Raccoon");
        } else if (mailReceived.count("raccoonTreeFallen")) stamp("Forest_RaccoonStump", 53, 2);
    }
    if (cur->name == "Beach" && squidFest()) stamp("Beach_SquidFest", 11, 3);
    if (cur->name == "Submarine" && stats["submarine"] != absoluteDay())
        ask3(*this, L("Submarine_SubmergeQuestion", "Captain: Ahoy there! Want to take the deep-sea fishing tour? It'll cost ya 1,000g."), {{"Yes", "last:sub:yes"}, {"No", "last:sub:no"}});
    if (cur->name == "MermaidHouse")
        ask3(*this, "The mermaid sings, slow and strange, and the lanterns sway." + std::string(mailReceived.count("gotPearl") ? "" : " Five clam shells sit in a row before the stage."),
             mailReceived.count("gotPearl") ? std::vector<std::pair<std::string, std::string>>{{"Listen a while", ""}}
                                            : std::vector<std::pair<std::string, std::string>>{{"Play the shells", "last:shell:0"}, {"Just listen", ""}});
    if (cur->name == "Farm" && mailReceived.count("qi:lumber") && !mailReceived.count("HasClubCard")) {
        mailReceived.insert("HasClubCard");
        sfx("reward");
        showMessage("Something glints in the pile of lumber next to your house... You found the Club Card!");
    }
    if (cur->name == "Farm" && mailReceived.count("nightTerrors") && timeOfDay >= 19 * 60 && cur->monsters.empty()) {
        static const char* const KINDS[] = {"Bat", "Green Slime", "Shadow Brute"};
        for (int k = 0; k < 4; k++) {
            int x = (int)(px / TILE) + (int)(rand32() % 17) - 8, y = (int)(py / TILE) + (int)(rand32() % 13) - 6;
            const MonsterDef* d = data.monster(KINDS[rand32() % 3]);
            if (!d || !cur->map.inBounds(x, y) || tileSolid(x, y) || std::abs(x * TILE - (int)px) < 4 * TILE) continue;
            Location::Monster mo;
            mo.kind = d->name;
            mo.hp = d->health;
            mo.damage = d->damage;
            mo.x = x * TILE + 8.0f;
            mo.y = y * TILE + 14.0f;
            cur->monsters.push_back(mo);
        }
    }
}

void Game::lastMorning() {
    if (!mailReceived.count("raccoonTreeFallen") && absoluteDay() > 28 + 3) {
        mailReceived.insert("raccoonTreeFallen");
        if (morningNote.empty()) morningNote = "A fierce wind blew through the valley last night. Something big came down in the forest...";
    }
    if (desertFest() && morningNote.empty() && day == 15) morningNote = "The Desert Festival is on in the Calico Desert for three days!";
    if (troutDerby() && morningNote.empty() && day == 20) morningNote = "The Trout Derby is on by the forest river today and tomorrow.";
    if (squidFest() && morningNote.empty() && day == 12) morningNote = "SquidFest is on at the beach today and tomorrow.";
    if (nightMarket() && morningNote.empty() && day == 15) morningNote = "The Night Market is at the beach from 5pm, tonight and the next two nights.";
}

// Hooks from fishing and the mines.
void Game::lastCaught(int fish) {
    if (fish == 138 && cur->name == "Forest" && troutDerby() && rand32() % 3 == 0)
        if (giveNamed("Golden Tag", 1) >= 0) say("This one has a golden tag!");
    if (fish == 151 && cur->name == "Beach" && squidFest()) {
        int n = ++stats["squid:" + std::to_string(absoluteDay())];
        say("Squid caught today: " + std::to_string(n));
    }
}

void Game::lastMineFloor(int level) {
    if (!desertFest() || level <= 120 || level > 300) return;
    int& best = stats["desertFloor:" + std::to_string(absoluteDay())];
    best = std::max(best, level - 120);
    if (giveNamed("Calico Egg", 2) >= 0) say("+2 calico eggs");
}

void Game::lastSlain() {
    if (desertFest() && cur->mineLevel > 120 && cur->mineLevel <= 300 && rand32() % 6 == 0) giveNamed("Calico Egg", 1);
}

static int countCategory(const Game& g, int cat) {
    int n = 0;
    for (int i = 0; i < g.invSlots; i++) {
        const Slot& s = g.inv[i];
        const ObjectDef* o = s.type == SLOT_OBJECT ? g.data.object(s.id) : nullptr;
        if (o && o->category == cat) n += s.count;
    }
    return n;
}
static void takeCategory(Game& g, int cat, int n) {
    for (int i = 0; i < g.invSlots && n > 0; i++) {
        Slot& s = g.inv[i];
        const ObjectDef* o = s.type == SLOT_OBJECT ? g.data.object(s.id) : nullptr;
        if (!o || o->category != cat) continue;
        int t = std::min(n, s.count);
        s.count -= t;
        n -= t;
        if (s.count <= 0) s = Slot();
    }
}

namespace {
const struct { int cat; int n; const char* what; } RACCOON[] = {
    {-26, 2, "artisan goods (jelly, pickles, wine, cheese...)"}, {-4, 5, "fish"}, {-75, 8, "vegetables"}, {-79, 8, "fruit"}, {-81, 10, "foraged things"}};
const struct { const char* q; const char* a[3]; int right; } QUIZ[] = {
    {"Which season do melons grow in?", {"Spring", "Summer", "Fall"}, 1},
    {"Who runs the Stardrop Saloon?", {"Gus", "Pierre", "Clint"}, 0},
    {"How many floors does the mine near town have?", {"100", "120", "150"}, 1},
    {"Which of these is a fall crop?", {"Pumpkin", "Parsnip", "Blueberry"}, 0},
    {"What does a furnace need besides ore?", {"Wood", "Coal", "Stone"}, 1},
    {"Who lives in a tent by the mountain lake?", {"Linus", "Willy", "Shane"}, 0},
    {"Which fish is caught in the desert pond?", {"Sandfish", "Tuna", "Carp"}, 0},
    {"What animal gives truffles?", {"Goat", "Pig", "Rabbit"}, 1},
};
}

bool Game::lastAction(const std::string& verb, const std::string& rest) {
    (void)rest;
    auto flag = [&](const std::string& f) { return mailReceived.count(f) != 0; };
    Slot& held = inv[selected];
    int heldId = held.type == SLOT_OBJECT ? held.id : -1;
    std::string today = std::to_string(absoluteDay());
    int eggId = objectByName("Calico Egg");

    // ---- mastery cave ----
    if (verb == "MasteryRoom") {
        for (int sk = 0; sk < 5; sk++)
            if (skillLevels[sk] < 10) { showMessage("A sealed door, carved with five symbols. Only a master of every skill may enter."); return true; }
        return enterMap("MasteryCave");
    }
    if (verb == "MasteryCave_Pedestal") {
        int lv = masteryLevel(), spent = 0;
        for (const char* s : {"Farming", "Mining", "Foraging", "Fishing", "Combat"}) spent += flag(std::string("mastery:") + s);
        showMessage("Mastery: level " + std::to_string(lv) + " of 5 (" + std::to_string(stats["masteryXp"]) + " points). " +
                    (lv > spent ? std::to_string(lv - spent) + " pillar" + (lv - spent > 1 ? "s" : "") + " can be claimed." : "Keep working to earn the next."));
        return true;
    }
    if (verb.rfind("MasteryCave_", 0) == 0) {
        std::string skill = verb.substr(12);
        if (flag("mastery:" + skill)) { showMessage("You have mastered " + skill + "."); return true; }
        int spent = 0;
        for (const char* s : {"Farming", "Mining", "Foraging", "Fishing", "Combat"}) spent += flag(std::string("mastery:") + s);
        if (masteryLevel() <= spent) { showMessage("The pillar of " + skill + ". You need more mastery to claim it."); return true; }
        ask3(*this, "Claim the mastery of " + skill + "?", {{"Claim it", "last:mastery:" + skill}, {"Not yet", ""}});
        return true;
    }
    if (verb == "LeoParrot") { sfx("parrot"); say("Squawk!"); return true; }
    if (verb == "GrandpaMasteryNote") { showMessage("A note in Grandpa's hand: \"If you're reading this, you've gone further than I ever did. I'm proud of you.\""); return true; }

    // ---- the raccoon ----
    if (verb == "FixRaccoonStump") {
        if (flag("raccoonFixed")) return false;
        ask3(*this, "A giant stump, hollow inside. With 100 hardwood it could be made into a home. You have " + std::to_string(countItem(709)) + ".",
             {{"Build it (100 hardwood)", "last:stump"}, {"Leave it", ""}});
        return true;
    }
    if (verb == "Raccoon") {
        int done = stats["raccoonRequests"];
        auto& r = RACCOON[done % 5];
        std::vector<std::pair<std::string, std::string>> ch = {{"Hand them over", "last:raccoon"}};
        if (done > 0 && data.shops.count("Raccoon")) ch.push_back({"See what Mrs. Raccoon is selling", "last:raccoonshop"});
        ch.push_back({"Not now", ""});
        ask3(*this, "The raccoon chitters and draws in the dirt: " + std::to_string(r.n) + " " + r.what + ". You have " + std::to_string(countCategory(*this, r.cat)) + ".", ch);
        return true;
    }

    // ---- trout derby and squidfest ----
    if (verb == "FishingDerbySign") { showMessage("Trout Derby: Summer 20 and 21. Catch rainbow trout here for golden tags, and trade the tags at the booth."); return true; }
    if (verb == "TroutDerbyBooth") {
        int tag = objectByName("Golden Tag");
        if (tag < 0 || countItem(tag) < 1) { showMessage("\"Bring me a golden tag off a rainbow trout and you can pick a prize!\""); return true; }
        static const struct { const char* name; int n; } PRIZES[] = {{"Mystery Box", 3}, {"Deluxe Bait", 20}, {"Triple Shot Espresso", 1}, {"Diamond", 1},
            {"Quality Fertilizer", 15}, {"Mystery Box", 4}, {"Warp Totem: Farm", 3}, {"Golden Mystery Box", 1}};
        int& n = stats["derbyPrizes"];
        int got = -1, count = 1;
        for (int k = 0; k < 8 && got < 0; k++) { auto& p = PRIZES[(n + k) % 8]; got = objectByName(p.name); count = p.n; }
        if (got < 0) { got = 72; count = 1; }
        if (!addItem(got, count)) return true;
        takeItem(tag, 1);
        n++;
        sfx("reward");
        const ObjectDef* o = data.object(got);
        showMessage("\"A golden tag! Here you go.\" You got " + std::to_string(count) + " " + (o ? o->name : "prize") + ".");
        return true;
    }
    if (verb == "SquidFestBooth") {
        if (!squidFest()) return false;
        static const int NEED[2][4] = {{1, 3, 5, 8}, {2, 5, 7, 10}};
        static const struct { const char* name; int n; } PRIZES[4] = {{"Deluxe Bait", 15}, {"Triple Shot Espresso", 1}, {"Mystery Box", 3}, {"Golden Mystery Box", 1}};
        int d = day - 12, caught = stats["squid:" + today];
        int& claimed = stats["squidClaim:" + today];
        if (claimed < 4 && caught >= NEED[d][claimed]) {
            auto& p = PRIZES[claimed];
            int got = objectByName(p.name);
            if (got < 0) got = 166;
            if (!addItem(got, got == 166 ? 1 : p.n)) return true;
            claimed++;
            sfx("reward");
            const ObjectDef* o = data.object(got);
            showMessage("\"" + std::to_string(caught) + " squid! That earns a prize.\" You got " + (o ? o->name : "a prize") + ".");
        } else if (claimed >= 4) showMessage("\"You've won everything there is to win today!\"");
        else showMessage("SquidFest: " + std::to_string(caught) + " squid caught today. The next prize is at " + std::to_string(NEED[d][claimed]) + ".");
        return true;
    }

    // ---- the desert festival ----
    if (verb.rfind("Desert", 0) == 0 && verb != "DesertBus") {
        if (!desertFest()) return false;
        int eggs = eggId >= 0 ? countItem(eggId) : 0;
        if (verb == "DesertEggShop") { if (!data.shops.count("DesertFestival_EggShop")) return false; openShop = "DesertFestival_EggShop"; return true; }
        if (verb == "DesertVendor") {                    // two villagers keep stalls, different ones each day
            std::vector<std::string> stalls;
            for (auto& kv : data.shops)
                if (kv.first.rfind("DesertFestival_", 0) == 0 && kv.first != "DesertFestival_EggShop") stalls.push_back(kv.first);
            std::sort(stalls.begin(), stalls.end());
            if (stalls.empty()) return false;
            size_t pick = ((size_t)absoluteDay() * 7 + (px > 20 * TILE ? 3 : 0)) % stalls.size();
            openShop = stalls[pick];
            return true;
        }
        if (verb == "DesertFestivalMineExplanation" || verb == "DesertMarlon") {
            showMessage("Marlon: \"During the festival the Skull Cavern is full of calico eggs. Every floor you go down earns some, and the monsters carry them too. "
                        "Gil will pay out more for how deep you get.\"");
            return true;
        }
        if (verb == "DesertGil") {
            int deep = stats["desertFloor:" + today];
            int& paid = stats["desertPaid:" + today];
            int owed = deep / 5 * 10 - paid;
            if (owed > 0 && eggId >= 0 && addItem(eggId, owed)) {
                paid += owed;
                sfx("reward");
                showMessage("Gil: \"Floor " + std::to_string(deep) + " today? Not bad.\" He counts out " + std::to_string(owed) + " calico eggs.");
            } else showMessage("Gil: \"Deepest today: floor " + std::to_string(deep) + ". I pay 10 eggs for every 5 floors. Zzz...\"");
            return true;
        }
        if (verb == "DesertFood") {
            if (stats["desertMeal"] == absoluteDay()) { showMessage("The chef: \"One plate a day, friend. Come back tomorrow.\""); return true; }
            static const char* const DISH[] = {"Spicy Eel", "Pepper Poppers", "Crab Cakes", "Lucky Lunch", "Tom Kha Soup", "Fried Mushroom"};
            int id = objectByName(DISH[rand32() % 6]);
            if (id < 0) id = 194;
            if (!addItem(id, 1)) return true;
            stats["desertMeal"] = absoluteDay();
            sfx("coin");
            const ObjectDef* o = data.object(id);
            showMessage("The chef tosses a few things in a pan and hands you a plate: " + std::string(o ? o->name : "something hot") + ".");
            return true;
        }
        if (verb == "DesertScholar") {
            int& asked = stats["quiz:" + today];
            if (asked >= 3) { showMessage("The scholar: \"That's all my questions for today.\""); return true; }
            auto& q = QUIZ[(absoluteDay() * 3 + asked) % 8];
            std::vector<std::pair<std::string, std::string>> ch;
            for (int k = 0; k < 3; k++) ch.push_back({q.a[k], k == q.right ? "last:quiz:yes" : "last:quiz:no"});
            ask3(*this, std::string("The scholar: \"A question, for 10 calico eggs. ") + q.q + "\"", ch);
            return true;
        }
        if (verb == "DesertRacerMan") {
            ask3(*this, "The races! Bet 10 calico eggs on a racer; the winner pays triple. You have " + std::to_string(eggs) + ".",
                 {{"The cactus", "last:race:0"}, {"The crab", "last:race:1"}, {"The raccoon", "last:race:2"}, {"No bet", ""}});
            return true;
        }
        if (verb == "DesertShadyGuy") {
            ask3(*this, "\"Psst. 20 calico eggs and I'll give you... something.\" You have " + std::to_string(eggs) + ".", {{"Deal (20 eggs)", "last:shady"}, {"No thanks", ""}});
            return true;
        }
        if (verb == "DesertCactusMan") {
            if (stats["cactusYear"] == year) { showMessage("\"Take good care of the one I gave you. Come back next year.\""); return true; }
            int id = -1;
            for (auto& kv : data.furniture)
                if (kv.second.name.find("Cactus") != std::string::npos && (id < 0 || rand32() % 3 == 0)) id = kv.first;
            if (id < 0 || !placeFurnitureInHouse(id)) { showMessage("\"I'd give you a cactus, but there's no room in your house for it.\""); return true; }
            stats["cactusYear"] = year;
            sfx("reward");
            showMessage("\"Every visitor gets a free cactus! I've sent one to your house.\"");
            return true;
        }
        if (verb == "DesertFishingBoard") {
            int pay = 0;
            if (countItem(165) > 0) { takeItem(165, 1); pay = 50; }
            else if (countItem(164) > 0) { takeItem(164, 1); pay = 25; }
            if (pay && eggId >= 0) { addItem(eggId, pay); sfx("reward"); showMessage("Willy's board: fish handed in. +" + std::to_string(pay) + " calico eggs."); }
            else showMessage("Willy's board: \"Bring a sandfish (25 eggs) or a scorpion carp (50 eggs) from the pond.\"");
            return true;
        }
        return false;
    }

    // ---- the summit ----
    if (verb == "SummitBoulder") {
        int p = perfection();
        if (p < 100) { showMessage(L("SummitBoulder", "It seems impossible to budge...") + " (" + std::to_string(p) + "%)"); return true; }
        if (!enterMap("Summit")) return false;
        if (!mailReceived.count("Summit_event")) {   // the first time: someone is waiting at the top
            mailReceived.insert("Summit_event");
            startSummit();
        }
        return true;
    }

    // ---- Emily's sewing machine and dye pots, the Wizard's shrine ----
    if (verb == "Tailoring") {
        if (held.type != SLOT_OBJECT || heldId == 428 || countItem(428) < 1) { showMessage(L("HaleyHouse_SewingMachine", "It's a sewing machine...") + "#$b#(Hold an item, with cloth in your bag: the item decides the shirt.)"); return true; }
        takeItem(428, 1);
        me.outfit.shirt = (int)(((uint32_t)heldId * 2654435761u) >> 8) % 112;
        if (--held.count <= 0) held = Slot();
        sfx("reward");
        say("You sewed a new shirt and put it on.");
        return true;
    }
    if (verb == "DyePot") {
        if (held.type != SLOT_OBJECT) { showMessage(L("HaleyHouse_DyePot", "An array of brightly colored dyes.") + "#$b#(Hold something colourful to dye your trousers.)"); return true; }
        uint32_t h = (uint32_t)heldId * 2654435761u;
        static const uint8_t DYES[8][3] = {{200, 40, 40}, {230, 130, 30}, {220, 200, 50}, {60, 160, 60}, {46, 85, 183}, {120, 60, 170}, {220, 120, 170}, {60, 60, 60}};
        const uint8_t* d = DYES[(h >> 10) % 8];
        me.outfit.pantsR = d[0]; me.outfit.pantsG = d[1]; me.outfit.pantsB = d[2];
        if (--held.count <= 0) held = Slot();
        sfx("slosh");
        say("Your trousers take the colour.");
        return true;
    }
    if (verb == "WizardShrine") {
        ask3(*this, L("WizardTower_WizardShrine", "The Shrine Of Illusions. Make an offering of 500g to change your appearance?"),
             {{"Next hairstyle", "last:look:hair"}, {"Next shirt", "last:look:shirt"}, {"Leave", ""}});
        return true;
    }
    if (verb == "EvilShrineRight") {
        std::string text = flag("nightTerrors") ? L("WitchHut_EvilShrineRightDeActivate", "-Dark Shrine of Night Terrors- The seal of protection will be restored. Make an offering of one Strange Bun?")
                                                : L("WitchHut_EvilShrineRightActivate", "-Dark Shrine of Night Terrors- Monsters will appear on your farm at night. Make an offering of one Strange Bun?");
        if (heldId != 203) { showMessage(text + "#$b#" + L("WitchHut_NoOffering", "You don't have the necessary offering.")); return true; }
        if (--held.count <= 0) held = Slot();
        if (flag("nightTerrors")) mailReceived.erase("nightTerrors"); else mailReceived.insert("nightTerrors");
        sfx("wand");
        showMessage(flag("nightTerrors") ? "A cold wind. Your farm will not be safe after dark." : "The air clears. Your farm is safe again.");
        return true;
    }
    return false;
}

bool Game::lastChoice(const std::string& what, const std::string& key) {
    if (what != "last") return false;
    int eggId = objectByName("Calico Egg");
    std::string today = std::to_string(absoluteDay());
    if (key.rfind("last:mastery:", 0) == 0) {
        std::string skill = key.substr(13);
        mailReceived.insert("mastery:" + skill);
        auto big = [&](const char* name) {
            for (auto& kv : data.bigs)
                if (kv.second.name == name) { Slot s; s.type = SLOT_BIG; s.id = kv.first; s.count = 1; addSlot(s); }
        };
        if (skill == "Farming") { giveTool("IridiumScythe"); big("Statue Of Blessings"); }
        else if (skill == "Mining") { big("Statue Of The Dwarf King"); big("Heavy Furnace"); }
        else if (skill == "Foraging") { giveNamed("Mystic Tree Seed", 3); giveNamed("Treasure Totem", 5); }
        else if (skill == "Fishing") { giveTool("AdvancedIridiumRod"); giveNamed("Challenge Bait", 20); }
        else { big("Anvil"); big("Mini-Forge"); }
        sfx("reward");
        showMessage("The pillar glows. You have mastered " + skill + "! Its gifts are in your bag.");
    } else if (key == "last:stump") {
        if (countItem(709) < 100) { say("You need 100 hardwood."); return true; }
        takeItem(709, 100);
        mailReceived.insert("raccoonFixed");
        cur->stamps.erase("Forest_RaccoonHouse");
        lastMapFixes();
        sfx("axchop");
        showMessage("You fit the stump out as a snug little house. Something is watching from the bushes...");
    } else if (key == "last:raccoon") {
        int& done = stats["raccoonRequests"];
        auto& r = RACCOON[done % 5];
        if (countCategory(*this, r.cat) < r.n) { say("You don't have enough of those."); return true; }
        takeCategory(*this, r.cat, r.n);
        static const char* const SEEDS[] = {"Carrot Seeds", "Summer Squash Seeds", "Broccoli Seeds", "Powdermelon Seeds"};
        if (giveNamed(SEEDS[done % 4], 25) < 0) addItem(770, 10);
        giveNamed("Mystery Box", 2 + done / 2);
        done++;
        sfx("reward");
        showMessage("The raccoon does a happy little dance and pushes a bundle towards you.");
    } else if (key == "last:raccoonshop") {
        openShop = "Raccoon";
    } else if (key == "last:quiz:yes" || key == "last:quiz:no") {
        stats["quiz:" + today]++;
        if (key == "last:quiz:yes" && eggId >= 0) { addItem(eggId, 10); sfx("reward"); say("Correct! +10 calico eggs"); }
        else { sfx("cancel"); say("\"Not quite.\""); }
    } else if (key.rfind("last:race:", 0) == 0) {
        if (eggId < 0 || countItem(eggId) < 10) { say("You need 10 calico eggs to bet."); return true; }
        takeItem(eggId, 10);
        int pick = atoi(key.c_str() + 10), winner = (int)(rand32() % 3);
        static const char* const NAMES[] = {"the cactus", "the crab", "the raccoon"};
        if (pick == winner) { addItem(eggId, 30); sfx("reward"); }
        else sfx("cancel");
        showMessage(std::string("They're off! ...and ") + NAMES[winner] + " takes it." + (pick == winner ? " You win 30 calico eggs!" : " Your eggs are gone."));
    } else if (key == "last:shady") {
        if (eggId < 0 || countItem(eggId) < 20) { say("You need 20 calico eggs."); return true; }
        takeItem(eggId, 20);
        static const char* const LOOT[] = {"Mystery Box", "Iridium Bar", "Diamond", "Coal", "Triple Shot Espresso", "Fairy Dust", "Trash"};
        int id = objectByName(LOOT[rand32() % 7]);
        if (id < 0) id = 168;
        addItem(id, 1);
        const ObjectDef* o = data.object(id);
        showMessage("He slips something into your hand and vanishes: " + std::string(o ? o->name : "something") + ".");
    } else if (key.rfind("last:look:", 0) == 0) {
        if (money < 500) { say("Not enough money"); return true; }
        money -= 500;
        if (key == "last:look:hair") me.outfit.hair = (me.outfit.hair + 1) % 32;
        else me.outfit.shirt = (me.outfit.shirt + 1) % 112;
        sfx("wand");
        say(key == "last:look:hair" ? "Your hair shimmers and changes." : "Your shirt shimmers and changes.");
    } else if (key == "last:sub:yes" || key == "last:sub:no") {
        if (key == "last:sub:yes" && money >= 1000) {
            money -= 1000;
            stats["submarine"] = absoluteDay();
            sfx("pullItemFromWater");
            say("The submarine sinks into the dark. Cast a line through the hatch.");
        } else {
            if (key == "last:sub:yes") say("Not enough money");
            enterMap("BeachNightMarket");
        }
    } else if (key.rfind("last:shell:", 0) == 0) {       // the song's notes, lowest shell to highest: 1 5 4 2 3
        static const int SONG[5] = {1, 5, 4, 2, 3};
        int pick = atoi(key.c_str() + 11);
        if (pick == 0) { crystalSeq.clear(); }
        else crystalSeq.push_back(pick);
        size_t at = crystalSeq.size();
        bool ok = true;
        for (size_t k = 0; k < at; k++) ok &= crystalSeq[k] == SONG[k];
        if (!ok) { crystalSeq.clear(); sfx("cancel"); say("A sour note. The mermaid frowns."); return true; }
        if (at >= 5) {
            crystalSeq.clear();
            mailReceived.insert("gotPearl");
            addItem(797, 1);
            sfx("reward");
            showMessage("The shells ring out her song. The mermaid smiles and tosses you a pearl!");
            return true;
        }
        if (at) sfx("crystal");
        ask3(*this, "Shell " + std::to_string(at + 1) + " of 5: which one?", {{"1", "last:shell:1"}, {"2", "last:shell:2"}, {"3", "last:shell:3"}, {"4", "last:shell:4"}, {"5", "last:shell:5"}});
    } else if (key.rfind("last:market:", 0) == 0) {
        std::string shop = key.substr(12);
        if (data.shops.count(shop) || shop == "Traveler") openShop = shop;
    }
    return true;
}

// Things with no action on the map: the night market's boats, the bus parked in the desert.
bool Game::lastInteract(int tx, int ty) {
    if (!cur || !cur->map.inBounds(tx, ty) || !cur->map.action(tx, ty).empty() || !tileSolid(tx, ty)) return false;
    if (cur->name == "BeachNightMarket" && ty >= 28) {
        std::string magic = "Festival_NightMarket_MagicBoat_Day" + std::to_string(std::max(1, std::min(3, day - 14)));
        ask3(*this, "The Night Market. Which boat?", {{"The magic shop boat", "last:market:" + magic}, {"The decoration boat", "last:market:Festival_NightMarket_DecorationBoat"},
                                                      {"The travelling merchant", "last:market:Traveler"}, {"Just looking", ""}});
        return true;
    }
    if ((cur->name == "Desert" || cur->name == "DesertFestival") && std::abs(tx - 18) <= 5 && std::abs(ty - 25) <= 3) {
        sfx("busDriveOff");
        warpTo("BusStop", 22, 11, false);
        return true;
    }
    return false;
}

} // namespace sv
