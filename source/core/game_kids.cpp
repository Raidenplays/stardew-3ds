// Children. Married, with the nursery built and your spouse at ten hearts, some nights they ask about
// a baby; two weeks later it arrives. A newborn sleeps in the crib, then sits up, then crawls around
// the house, then walks. Two at most, the second once the first is walking.
#include "game.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace sv {

static const int TILE = 16;
static const int CRIB_X = 31, CRIB_Y = 14;   // the crib in the upgraded farmhouse's nursery

// The game's own line (Strings/Events, StringsFromCSFiles) with {0} and {1} filled in; ours if it's missing.
std::string Game::gameLine(const std::string& key, const std::string& fallback, const std::string& a0, const std::string& a1) {
    const std::string* s = mapString(key);
    std::string out = s ? *s : fallback;
    for (int k = 0; k < 2; k++) {
        std::string tok = k ? "{1}" : "{0}";
        size_t at;
        while ((at = out.find(tok)) != std::string::npos) out.replace(at, 3, k ? a1 : a0);
    }
    return out;
}

bool Game::spouseIsMale() {
    const std::string* g = mapString("gender:" + spouse);
    return g && *g == "Male";
}

// Two child beds in the nursery (the game's own Child Bed furniture), once the house has one.
void Game::ensureChildBeds() {
    if (houseLevel < 2 || mailReceived.count("s3d_childBeds") || !data.furniture.count(2076)) return;
    mailReceived.insert("s3d_childBeds");
    int have = 0;
    for (auto& fp : furniture) have += fp.map == "FarmHouse" && fp.id == 2076;
    for (int i = have; i < 2; i++) {
        FurniturePlacement fp;
        fp.map = "FarmHouse";
        fp.id = 2076;
        fp.x = CRIB_X + 4 + i * 3;
        fp.y = CRIB_Y;
        furniture.push_back(fp);
    }
}

// The idx-th child's bed (top-left tile of the Child Bed), if there is one.
bool Game::childBed(int idx, int& bx, int& by) const {
    int n = 0;
    for (auto& fp : furniture)
        if (fp.map == "FarmHouse" && fp.id == 2076 && n++ == idx) { bx = fp.x; by = fp.y; return true; }
    return false;
}

int Game::kidStage(const Child& k) const {
    return k.daysOld < 13 ? 0 : k.daysOld < 27 ? 1 : k.daysOld < 55 ? 2 : 3;   // newborn, baby, crawler, toddler
}

void Game::kidPlace(Child& k, int idx) {
    int st = kidStage(k);
    bool night = timeOfDay >= 20 * 60;
    int bx, by;
    if (st >= 2 && night && childBed(idx, bx, by)) {   // tucked into their own bed
        k.x = bx * TILE + 16.0f;
        k.y = by * TILE + 9.0f;
        k.state = 2;
    } else if (st <= 1 || night) {           // in the crib (no bed: beside it)
        k.x = (CRIB_X + (st <= 1 ? 0 : idx == 0 ? -2 : 3)) * TILE + 8.0f + (st <= 1 ? idx * 10.0f - 7.0f : 0.0f);
        k.y = (CRIB_Y + (st <= 1 ? 0 : 2)) * TILE + (st <= 1 ? 4.0f : 12.0f);
        k.state = st <= 1 ? 0 : 2;
    } else {
        k.x = (CRIB_X - 1 + idx * 3) * TILE + 8.0f;
        k.y = (CRIB_Y + 3) * TILE + 12.0f;
        k.state = 0;
    }
    k.timer = 2.0f + idx;
    k.facing = FACE_DOWN;
}

bool Game::canHaveBaby() const {
    if (spouse.empty() || houseLevel < 2 || babyDue >= 0 || kids.size() >= 2) return false;
    if (!kids.empty() && kidStage(kids[0]) < 3) return false;
    const NpcState* s = npcByName(spouse);
    return s && s->friendship >= 2500;
}

// Overnight: everyone a day older; a baby on the way arrives; maybe your spouse asks.
void Game::kidsNight() {
    for (auto& k : kids) { k.daysOld++; k.talked = false; }
    if (babyDue > 0 && --babyDue == 0) {
        babyDue = -1;
        Child k;
        k.girl = kids.empty() ? (rand32() % 2 == 0) : !kids[0].girl;   // the second is the other
        k.dark = false;
        static const char* boys[] = {"Rowan", "Jasper", "Finn", "Theo"}, *girls[] = {"Hazel", "Ivy", "Clover", "Wren"};
        k.name = (k.girl ? girls : boys)[rand32() % 4];
        if (keyboard) {
            std::string n = keyboard(k.girl ? "A baby girl! Her name?" : "A baby boy! His name?", k.name, 12);
            if (!n.empty()) k.name = n;
        }
        kids.push_back(k);
        const NpcState* sp = npcByName(spouse);
        std::string who = sp ? sp->def->display : spouse, kind = k.girl ? "girl" : "boy";
        morningNote = (spouseIsMale() ? gameLine("ev:BirthMessage_Adoption", "During the night, the adoption agency dropped off your new baby {0}.", kind, who)
                                      : gameLine("ev:BirthMessage_SpouseMother", "During the night, {1} gave birth to a baby {0}.", kind, who)) +
                      " Welcome, " + k.name + "!";
        if (NpcState* s = npcByNameMut(spouse)) s->friendship = std::min(friendshipCap(*s), s->friendship + 250);
    } else if (canHaveBaby() && rand32() % 100 < 5) {
        babyAsk = true;
    }
    for (size_t i = 0; i < kids.size(); i++) {
        int st = kidStage(kids[i]);
        if (morningNote.empty() && (kids[i].daysOld == 27 || kids[i].daysOld == 55))
            morningNote = kids[i].name + (st == 2 ? " has started to crawl!" : " is walking now!");
    }
}

// While a baby's on the way, your spouse's first words each day are about it.
bool Game::kidsSpouseTalk(NpcState& n) {
    if (babyDue <= 0 || n.def->name != spouse || n.talkedToday) return false;
    std::string line = spouseIsMale() ? gameLine("cs:NPC.cs.4440", "{0}, I hope our adoption request gets approved. I want a baby.$l", me.name, "")
                       : babyDue <= 4 ? gameLine("cs:NPC.cs.4444", "{0}, we're going to have a baby soon.$l", me.name, "")
                                      : gameLine("cs:NPC.cs.4439", "I wonder how much the new baby will change things?$s", me.name, "");
    auto pages = parseDialogue(line, me.name);
    if (pages.empty()) return false;
    n.friendship = std::min(friendshipCap(n), n.friendship + 20);
    n.talkedToday = true;
    showDialogue(n, pages);
    return true;
}

void Game::kidsMorning() {
    ensureChildBeds();
    for (size_t i = 0; i < kids.size(); i++) kidPlace(kids[i], (int)i);
}

// Your spouse brings it up the morning after (in the farmhouse, once you're up).
void Game::kidsAsk() {
    if (!babyAsk || !cur || cur->name != "FarmHouse" || dialogue.active || cutscene.active) return;
    babyAsk = false;
    if (!canHaveBaby()) return;
    const NpcState* s = npcByName(spouse);
    DialoguePage p;
    p.text = spouseIsMale() ? gameLine("ev:HaveBabyQuestion_Adoption", "Should we adopt a baby, {0}?", me.name, "")
                            : gameLine("ev:HaveBabyQuestion", "Should we have a baby, {0}?", me.name, "");
    DialogueChoice c;
    c.label = "Yes"; c.responseKey = "baby:yes"; p.choices.push_back(c);
    c.label = "Not now"; c.responseKey = ""; p.choices.push_back(c);
    dialogue = DialogueBox();
    dialogue.active = true;
    dialogue.npc = s ? s->def->name : "";
    dialogue.pages.push_back(p);
    pendingChoice = "baby";
}

bool Game::kidsChoice(const std::string& what, const std::string& key) {
    if (what != "baby") return false;
    if (key == "baby:yes") {
        babyDue = 14;
        // their answer, in the game's words
        std::string line = spouseIsMale() ? gameLine("cs:NPC.cs.4441", "{0}, I filed our adoption papers. Now all we can do is cross our fingers and wait.$l", me.name, "")
                                          : gameLine("cs:NPC.cs.4445", "{0}, I'm pregnant. Isn't it wonderful?$l", me.name, "");
        auto pages = parseDialogue(line, me.name);
        if (!pages.empty()) {
            dialogue = DialogueBox();
            dialogue.active = true;
            dialogue.npc = spouse;
            dialogue.pages = pages;
        }
    }
    return true;
}

void Game::kidsUpdate(float dt) {
    kidsAsk();
    if (!cur || cur->name != "FarmHouse" || kids.empty()) return;
    bool night = timeOfDay >= 20 * 60;
    for (size_t i = 0; i < kids.size(); i++) {
        Child& k = kids[i];
        if (k.heart > 0) k.heart -= dt;
        k.anim += dt;
        int st = kidStage(k);
        if (st <= 1) continue;                                   // in the crib
        if (night) { if (k.state != 2) kidPlace(k, (int)i); continue; }
        if (k.state == 2) kidPlace(k, (int)i);                   // morning: up and about
        k.timer -= dt;
        if (k.state == 1) {
            float dx = k.tx - k.x, dy = k.ty - k.y, d = std::sqrt(dx * dx + dy * dy);
            if (d < 1.5f || k.timer <= 0) { k.state = 0; k.timer = 2 + (float)(rand32() % 50) / 10.0f; continue; }
            float sp = (st == 2 ? 14.0f : 30.0f) * dt;
            float nx = k.x + dx / d * sp, ny = k.y + dy / d * sp;
            if (tileSolid((int)std::floor((nx + (dx > 0 ? 4 : -4)) / TILE), (int)std::floor((ny - 3) / TILE))) { k.state = 0; k.timer = 1.5f; continue; }
            k.x = nx; k.y = ny;
            k.facing = std::fabs(dx) > std::fabs(dy) ? (dx > 0 ? FACE_RIGHT : FACE_LEFT) : (dy > 0 ? FACE_DOWN : FACE_UP);
            continue;
        }
        if (k.timer > 0) continue;
        // toddlers run up to you when you're close; otherwise they roam the house
        float pdx = px - k.x, pdy = py - k.y, pd2 = pdx * pdx + pdy * pdy;
        if (st == 3 && pd2 < 80.0f * 80.0f && pd2 > 22.0f * 22.0f && rand32() % 2) { k.tx = px - (pdx > 0 ? 14 : -14); k.ty = py; }
        else { k.tx = k.x + (float)((int)(rand32() % 81) - 40); k.ty = k.y + (float)((int)(rand32() % 65) - 32); }
        k.state = 1;
        k.timer = 5;
    }
}

bool Game::kidInteract(int tx, int ty) {
    if (!cur || cur->name != "FarmHouse") return false;
    for (auto& k : kids) {
        int st = kidStage(k);
        int kx = (int)std::floor(k.x / TILE), ky = (int)std::floor((k.y - 4) / TILE);
        bool hit = st <= 1 ? (std::abs(tx - CRIB_X) <= 1 && std::abs(ty - CRIB_Y) <= 1) : (tx == kx && (ty == ky || ty == ky - 1));
        if (!hit) continue;
        k.heart = 1.5f;
        if (k.state == 1) { k.state = 0; k.timer = 3; }
        if (!k.talked) {
            k.talked = true;
            k.friendship = std::min(2500, k.friendship + 20);
        }
        static const char* lines[4] = {" is fast asleep.", " gurgles and grabs your finger.", " crawls over and tugs your boot.", " hugs your leg."};
        say(k.name + (timeOfDay >= 20 * 60 && st >= 2 ? " is sound asleep." : lines[st]) + " (" + std::to_string(k.friendship / 250) + " hearts)");
        sfx(st <= 1 ? "dwop" : "smallSelect");
        return true;
    }
    return false;
}

} // namespace sv
