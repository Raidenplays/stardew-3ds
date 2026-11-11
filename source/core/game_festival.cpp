// Festivals: on the day, the festival's own map takes the place of the town / forest / beach, with
// everyone standing where that map's actor layers put them and saying their festival lines. Lewis
// starts the main event. Dates, hours, maps and lines all come from Data/Festivals.
#include "game.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <set>

namespace sv {

static const int TILE = 16;

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

static std::string clock12(int hhmm) {
    int h = hhmm / 100, m = hhmm % 100;
    char b[16];
    snprintf(b, sizeof b, "%d:%02d%s", h % 12 == 0 ? 12 : h % 12, m, h % 24 < 12 ? "am" : "pm");
    return b;
}

void Game::loadFestivals() {
    if (festivalsLoaded) return;
    festivalsLoaded = true;
    std::vector<std::string> lines;
    if (!data.vfs.readTable("data/festivals.dat", lines)) return;
    for (auto& l : lines) {
        auto f = tabs(l);
        if (f.size() < 12) continue;
        Festival fe;
        fe.key = f[0]; fe.season = atoi(f[1].c_str()); fe.day = atoi(f[2].c_str()); fe.name = f[3]; fe.loc = f[4];
        fe.start = atoi(f[5].c_str()); fe.end = atoi(f[6].c_str());
        fe.map[0] = f[7]; fe.map[1] = f[8];
        for (int k = 0; k < 2; k++) sscanf(f[9 + k].c_str(), "%d %d %d", &fe.sx[k], &fe.sy[k], &fe.sdir[k]);
        festivals.push_back(fe);
    }
}

const Festival* Game::festivalToday() {
    loadFestivals();
    for (auto& f : festivals)
        if (f.season == season && f.day == day && hasMap(f.map[0])) return &f;
    return nullptr;
}

// Called from warpTo: true if the festival took over (or turned you away).
bool Game::festivalWarp(const std::string& name) {
    if (cutscene.active) return false;   // a cutscene moving you around isn't you walking in
    if (festivalActive >= 0) {           // walking off the festival grounds ends your day there
        if (name != cur->name) { endFestival(); return true; }
        return false;
    }
    const Festival* f = festivalDone ? nullptr : festivalToday();
    if (!f || name != f->loc) return false;
    int now = (timeOfDay / 60) * 100 + timeOfDay % 60;
    if (now < f->start) {
        say("They're still setting up for the " + f->name + ". Come back at " + clock12(f->start) + ".");
        return true;
    }
    if (now >= f->end + 100) return false;   // over: the place is back to normal
    enterFestival(*f);
    return true;
}

void Game::enterFestival(const Festival& f) {
    int yr = year >= 2 && year % 2 == 0 && hasMap(f.map[1]) ? 1 : 0;
    int idx = (int)(&f - &festivals[0]);
    festivalActive = -1;                 // so the warp below goes through
    festivalDone = true;
    int sx = f.sx[yr] >= 0 ? f.sx[yr] : 10, sy = f.sy[yr] >= 0 ? f.sy[yr] : 10;
    if (!warpTo(f.map[yr], sx, sy, false)) { festivalDone = false; return; }
    festivalActive = idx;
    festLines.clear();
    eggHunt = 0;
    std::vector<std::string> lines;
    data.vfs.readTable("data/festivals/" + f.key + ".dat", lines);
    static const int face[4] = {FACE_UP, FACE_RIGHT, FACE_DOWN, FACE_LEFT};
    for (auto& n : npcs) { n.legs.clear(); n.leg = 0; n.sched.clear(); n.next = 0; n.anim = nullptr; n.moving = false; n.path.clear(); }
    for (auto& l : lines) {
        auto p = tabs(l);
        if (p[0] == "actor" && p.size() > 6 && atoi(p[1].c_str()) == yr + 1 && p[2] == "Set-Up") {
            for (auto& n : npcs)
                if (n.def->name == p[3]) {
                    n.loc = cur->name;
                    n.x = atoi(p[4].c_str()) * TILE + TILE / 2.0f;
                    n.y = atoi(p[5].c_str()) * TILE + TILE - 2.0f;
                    n.facing = face[atoi(p[6].c_str()) & 3];
                }
        } else if (p[0] == "line" && p.size() > 2) {
            festLines[p[1]] = p[2];
        }
    }
    for (auto& n : npcs)                  // whoever isn't here stays out of sight for the day
        if (n.loc != cur->name) n.loc = "_festival_away";
    say(f.name + "! Talk to Mayor Lewis when you're ready for the main event.");
}

bool Game::festivalTalk(NpcState& n) {
    if (festivalActive < 0) return false;
    const Festival& f = festivals[festivalActive];
    if (n.def->name == "Lewis" && eggHunt <= 0) {
        DialoguePage p;
        p.text = f.key == "fall27" || f.key == "fall16" || f.key == "winter25" ? "Had enough of the " + f.name + "? I can walk you home."
                                                                                : "Shall we start the main event, @?";
        size_t at = p.text.find('@');
        if (at != std::string::npos) p.text.replace(at, 1, me.name);
        DialogueChoice c;
        c.label = "Yes"; c.responseKey = "festival:go"; p.choices.push_back(c);
        c.label = "Not yet"; c.responseKey = ""; p.choices.push_back(c);
        dialogue = DialogueBox();
        dialogue.active = true;
        dialogue.npc = "Lewis";
        dialogue.pages.push_back(p);
        pendingChoice = "festival";
        return true;
    }
    bool y2 = year >= 2 && year % 2 == 0;
    const std::string& who = n.def->name;
    std::string key;
    if (who == spouse && festLines.count(who + "_spouse")) key = who + "_spouse";
    else if (y2 && festLines.count(who + "_y2")) key = who + "_y2";
    else if (festLines.count(who)) key = who;
    if (key.empty()) return false;
    auto pages = parseDialogue(festLines[key], me.name);
    if (pages.empty()) return false;
    if (!n.talkedToday) n.friendship = std::min(friendshipCap(n), n.friendship + 20);
    n.talkedToday = true;
    n.introduced = true;
    float dx = px - n.x, dy = py - n.y;
    n.facing = std::fabs(dx) > std::fabs(dy) ? (dx > 0 ? FACE_RIGHT : FACE_LEFT) : (dy > 0 ? FACE_DOWN : FACE_UP);
    showDialogue(n, pages);
    return true;
}

// Lewis says go: the festival's own "mainEvent" script plays with everyone in their MainEvent places,
// then the outcome (festivalOutcome). Without a script the outcome comes straight away.
void Game::festivalMain() {
    if (festivalActive < 0) return;
    const Festival& f = festivals[festivalActive];
    int yr = year >= 2 && year % 2 == 0 && hasMap(f.map[1]) ? 1 : 0;
    std::vector<std::string> lines;
    data.vfs.readTable("data/festivals/" + f.key + ".dat", lines);
    std::string script, actors, farmer;
    for (int pass = yr; pass >= 0 && script.empty(); pass--)
        for (auto& l : lines) {
            auto p = tabs(l);
            if (p[0] == "script" && p.size() > 2 && p[1] == (pass ? "mainEvent_y2" : "mainEvent")) script = p[2];
        }
    if (script.empty()) { festivalOutcome(); return; }
    for (auto& l : lines) {
        auto p = tabs(l);
        if (p[0] == "actor" && p.size() > 6 && atoi(p[1].c_str()) == yr + 1 && p[2] == "MainEvent")
            actors += " " + p[3] + " " + p[4] + " " + p[5] + " " + p[6];
    }
    size_t wf = script.find("warpFarmers ");
    int fx = (int)(px / TILE), fy = (int)(py / TILE), fd = 2;
    if (wf != std::string::npos) sscanf(script.c_str() + wf + 12, "%d %d %d", &fx, &fy, &fd);
    std::string full = "continue/" + std::to_string(fx) + " " + std::to_string(fy) + "/farmer " + std::to_string(fx) + " " +
                       std::to_string(fy) + " " + std::to_string(fd) + actors + "/" + script;
    for (auto& n : npcs) n.loc = "_festival_away";   // the scene's own actors stand in for them
    if (!startEvent(cur->name, "festival_" + f.key, full)) festivalOutcome();
}

void Game::festivalAfterScene() {
    if (festivalActive < 0) return;
    festivalOutcome();
}

// What each festival's main event comes to.
void Game::festivalOutcome() {
    if (festivalActive < 0) return;
    const Festival& f = festivals[festivalActive];
    auto everyone = [&](int pts) {
        for (auto& n : npcs)
            if (n.def->social) n.friendship = std::max(0, std::min(friendshipCap(n), n.friendship + pts));
    };
    std::string msg;
    if (f.key == "spring13") {           // the egg hunt: 50 seconds, nine eggs wins
        int placed = 0;
        const TileMap& m = cur->map;
        for (int tries = 0; tries < 4000 && placed < 26; tries++) {
            int x = 2 + (int)(rand32() % (uint32_t)std::max(1, m.width() - 4)), y = 2 + (int)(rand32() % (uint32_t)std::max(1, m.height() - 4));
            if (std::abs(x - (int)(px / TILE)) > 22 || std::abs(y - (int)(py / TILE)) > 16) continue;
            if (tileSolid(x, y) || m.tile(L_BACK, x, y) == NO_TILE || (m.flags(x, y) & CF_WATER)) continue;
            bool taken = false;
            for (auto& o : cur->objects) taken |= o.x == x && o.y == y;
            if (taken) continue;
            WorldObject o;
            o.x = x; o.y = y; o.itemId = 176;
            cur->objects.push_back(o);
            placed++;
        }
        eggHunt = 50.0f;
        eggsFound = 0;
        sfx("whistle");
        say("The egg hunt is on! Run over the eggs: 50 seconds!");
        return;
    } else if (f.key == "spring24") {    // the flower dance: a partner with 4 hearts or more
        NpcState* best = nullptr;
        for (auto& n : npcs)
            if (data.romanceable(n.def->name) && n.friendship >= 1000 && (!best || n.friendship > best->friendship ||
                                                                           dating.count(n.def->name) || n.def->name == spouse))
                if (!best || !(dating.count(best->def->name) || best->def->name == spouse)) best = &n;
        if (best) {
            best->friendship = std::min(friendshipCap(*best), best->friendship + 250);
            msg = "You danced with " + best->def->display + " under the spring blossoms. They won't forget it.";
        } else {
            msg = "Nobody wanted to dance with you this year... you watched from the side. (4 hearts with someone would help.)";
        }
    } else if (f.key == "summer11") {    // the luau: what you put in the soup
        Slot& s = inv[selected];
        const ObjectDef* o = s.type == SLOT_OBJECT ? data.object(s.id) : nullptr;
        if (!o || o->edibility == -300) {
            msg = "You added nothing to the potluck soup. The Governor found it... fine.";
        } else {
            int v = o->price * (s.quality >= 2 ? 2 : 1);
            if (o->edibility < 0) { everyone(-50); msg = "The Governor turned green. Your " + o->name + " ruined the soup!"; }
            else if (v >= 200) { everyone(120); msg = "\"The best soup I've ever tasted!\" Your " + o->name + " made the luau."; }
            else if (v >= 80) { everyone(60); msg = "The Governor enjoyed the soup. Your " + o->name + " went down well."; }
            else msg = "The soup was all right. Nobody noticed your " + o->name + ".";
            if (--s.count <= 0) s = Slot();
        }
    } else if (f.key == "summer28") {
        msg = "The lanterns drift out... and the moonlight jellies come, glowing, on their way south. Summer is over.";
    } else if (f.key == "winter8") {     // ice fishing
        int caught = 2 + skillLevel(SK_FISHING) / 2 + (int)(rand32() % 3);
        if (caught >= 5) {
            msg = "You caught " + std::to_string(caught) + " fish through the ice and won the contest!";
            if (!mailReceived.count("s3d_iceFestivalWin")) {
                mailReceived.insert("s3d_iceFestivalWin");
                int hat = data.toolByKey("Hat_17");
                if (hat >= 0) giveTool("Hat_17");
                for (int id : {687, 691, 690})
                    if (data.object(id)) addItem(id, 1);
            } else if (data.object(749)) addItem(749, 1);
        } else {
            msg = "You caught " + std::to_string(caught) + " fish through the ice. Willy took the prize this year.";
        }
    } else if (f.key == "fall16") {      // the fair: your grange display is the best nine things in your bag
        std::vector<int> pts;
        std::set<int> cats;
        for (int i = 0; i < invSlots; i++) {
            const Slot& s = inv[i];
            const ObjectDef* o = s.type == SLOT_OBJECT ? data.object(s.id) : nullptr;
            if (!o || o->price <= 0) continue;
            int v = itemPrice(s);
            pts.push_back((s.quality >= 4 ? 4 : s.quality + 1) + (v >= 400 ? 5 : v >= 200 ? 4 : v >= 90 ? 3 : v >= 20 ? 2 : 1));
            cats.insert(o->category);
        }
        std::sort(pts.rbegin(), pts.rend());
        int score = 14 + std::min(30, (int)cats.size() * 5);
        for (size_t i = 0; i < pts.size() && i < 9; i++) score += pts[i];
        int prize = score >= 90 ? 1000 : score >= 75 ? 500 : score >= 60 ? 200 : 50;
        money += prize * 2;
        msg = "Your grange display scored " + std::to_string(score) + " points" + (score >= 90 ? ": first place!" : ".") +
              " You traded your star tokens for " + std::to_string(prize * 2) + "g.";
        if (score >= 90 && !mailReceived.count("CF_Fair") && data.object(434)) {
            mailReceived.insert("CF_Fair");
            addItem(434, 1);
        }
    } else if (f.key == "winter25") {    // the secret gift
        std::vector<NpcState*> pool;
        for (auto& n : npcs)
            if (n.def->social && n.loc == cur->name) pool.push_back(&n);
        if (!pool.empty()) {
            NpcState* who = pool[(uint32_t)(year * 31 + 7) % pool.size()];
            Slot& s = inv[selected];
            if (s.type == SLOT_OBJECT && data.object(s.id)) {
                who->friendship = std::min(friendshipCap(*who), who->friendship + giftPoints(giftTaste(data, *who->def, s.id)) * 5);
                if (--s.count <= 0) s = Slot();
            }
            static const int gifts[] = {223, 234, 220, 243, 354, 395, 286};
            int g = gifts[rand32() % 7];
            if (data.object(g)) addItem(g, g == 286 ? 5 : 1);
            msg = "You and " + who->def->display + " traded secret gifts. Happy Feast of the Winter Star!";
        }
    } else if (f.key == "fall27") {
        msg = "The night grows late and the maze goes quiet...";
    }
    endFestival();
    if (!msg.empty()) say(msg);
}

void Game::festivalUpdate(float dt) {
    if (festivalActive < 0 || eggHunt <= 0) return;
    eggHunt -= dt;
    for (size_t i = 0; i < cur->objects.size(); i++) {
        const WorldObject& o = cur->objects[i];
        if (o.itemId != 176) continue;
        float dx = px - (o.x * TILE + 8), dy = (py - 4) - (o.y * TILE + 8);
        if (dx * dx + dy * dy < 12 * 12) {
            cur->objects.erase(cur->objects.begin() + i);
            i--;
            eggsFound++;
            sfx("coin");
            say(std::to_string(eggsFound) + (eggsFound == 1 ? " egg!" : " eggs!"));
        }
    }
    if (eggHunt > 0) return;
    int found = eggsFound;
    bool win = found >= 9;
    if (win) {
        if (!mailReceived.count("s3d_eggHuntWin") && data.toolByKey("Hat_4") >= 0) {
            mailReceived.insert("s3d_eggHuntWin");
            giveTool("Hat_4");
        } else money += 1000;
    }
    endFestival();
    say(win ? "Time! " + std::to_string(found) + " eggs: you won the egg hunt!" : "Time! " + std::to_string(found) +
                                                                                 " eggs. Abigail won again... nine would have done it.");
}

bool Game::visitFestival() {
    startDay();
    const Festival* fe = festivalToday();
    if (!fe) return false;
    timeOfDay = (fe->start / 100) * 60 + 30;
    warpTo(fe->loc, 20, 20, false);
    return festivalActive >= 0;
}

void Game::endFestival() {
    if (festivalActive < 0) return;
    festivalActive = -1;
    eggHunt = 0;
    festLines.clear();
    for (auto& n : npcs) {               // everyone's home for the night
        n.loc = n.def->homeMap;
        n.x = n.def->homeX * TILE + TILE / 2.0f;
        n.y = n.def->homeY * TILE + TILE - 2.0f;
        n.facing = n.def->homeDir;
    }
    timeOfDay = std::max(timeOfDay, 22 * 60);
    warpTo("Farm", data.layout.doorX, data.layout.doorY + 1, false);
    facing = FACE_DOWN;
    say("The festival is over. It's late: time to head to bed.");
}

} // namespace sv
