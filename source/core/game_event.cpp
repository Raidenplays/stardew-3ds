// Cutscenes: runs event scripts from data/events/<map>.dat.
// Script format and precondition codes follow the public modding docs.
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "game.h"

namespace sv {

static const int TILE = 16;

// Splits a command on spaces, keeping "quoted text" (with \" escapes) as one token.
static std::vector<std::string> tokenize(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    bool inQ = false, any = false;
    for (size_t i = 0; i < s.size(); i++) {
        char c = s[i];
        if (inQ) {
            if (c == '\\' && i + 1 < s.size() && s[i + 1] == '"') { cur += '"'; i++; }
            else if (c == '"') inQ = false;
            else cur += c;
        } else if (c == '"') {
            inQ = true;
            any = true;
        } else if (c == ' ' || c == '\t') {
            if (!cur.empty() || any) out.push_back(cur);
            cur.clear();
            any = false;
        } else {
            cur += c;
        }
    }
    if (!cur.empty() || any) out.push_back(cur);
    return out;
}

static std::vector<std::string> splitSlash(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    bool inQ = false;
    for (char c : s) {
        if (c == '"') inQ = !inQ;
        if (c == '/' && !inQ) {
            out.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    out.push_back(cur);
    return out;
}

static bool isNum(const std::string& s) {
    if (s.empty()) return false;
    size_t i = (s[0] == '-') ? 1 : 0;
    if (i >= s.size()) return false;
    for (; i < s.size(); i++)
        if (!isdigit((unsigned char)s[i])) return false;
    return true;
}

static int toOurFacing(int d) {   // events use 0 up 1 right 2 down 3 left
    static const int m[4] = {2, 1, 0, 3};
    return m[((d % 4) + 4) % 4];
}

static bool truthy(const std::string& s) { return s == "true" || s == "continue"; }

// ------------------------------------------------------------ event files

static bool readEventFile(const Vfs& vfs, const std::string& location,
                          std::vector<std::pair<std::string, std::string>>& out) {
    std::vector<std::string> lines;
    if (!vfs.readTable("data/events/" + location + ".dat", lines)) return false;
    for (auto& line : lines) {
        size_t tab = line.find('\t');
        if (tab != std::string::npos) out.push_back({line.substr(0, tab), line.substr(tab + 1)});
    }
    return true;
}

// ------------------------------------------------------------ preconditions

bool Game::eventConditionsMet(const std::string& key) const {
    std::vector<std::string> parts = splitSlash(key);
    int now = (timeOfDay / 60) * 100 + timeOfDay % 60;
    int daysPlayed = (year - 1) * 112 + season * 28 + day;
    static const char* dows[7] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
    for (size_t i = 1; i < parts.size(); i++) {
        std::vector<std::string> w = tokenize(parts[i]);
        if (w.empty()) continue;
        std::string code = w[0];
        bool neg = false;
        if (!code.empty() && code[0] == '!') {
            neg = true;
            code = code.substr(1);
        }
        bool ok;
        if (code == "f") {
            ok = true;
            for (size_t k = 1; k + 1 < w.size(); k += 2) {
                const NpcState* n = npcByName(w[k]);
                if (!n || n->friendship < atoi(w[k + 1].c_str())) ok = false;
            }
        } else if (code == "t") {
            ok = w.size() >= 3 && now >= atoi(w[1].c_str()) && now < atoi(w[2].c_str());
        } else if (code == "w") {
            ok = w.size() >= 2 && ((w[1] == "rainy") == raining);
        } else if (code == "d") {          // NOT on any of these days
            ok = true;
            for (size_t k = 1; k < w.size(); k++)
                if (w[k] == dows[weekday()]) ok = false;
        } else if (code == "e") {
            ok = true;
            for (size_t k = 1; k < w.size(); k++)
                if (!eventsSeen.count(w[k])) ok = false;
        } else if (code == "k") {
            ok = true;
            for (size_t k = 1; k < w.size(); k++)
                if (eventsSeen.count(w[k])) ok = false;
        } else if (code == "z") {          // NOT this season
            ok = true;
            for (size_t k = 1; k < w.size(); k++)
                if (w[k] == seasonName(season)) ok = false;
        } else if (code == "y") {
            int y = w.size() > 1 ? atoi(w[1].c_str()) : 1;
            ok = y == 1 ? year == 1 : year >= y;
        } else if (code == "u") {
            ok = false;
            for (size_t k = 1; k < w.size(); k++)
                if (atoi(w[k].c_str()) == day) ok = true;
        } else if (code == "n" || code == "Hn" || code == "*n") {
            ok = w.size() > 1 && mailReceived.count(w[1]);
        } else if (code == "l" || code == "Hl" || code == "*l") {
            ok = w.size() > 1 && !mailReceived.count(w[1]);
        } else if (code == "p") {
            const NpcState* n = w.size() > 1 ? npcByName(w[1]) : nullptr;
            ok = n && n->loc == cur->name;
        } else if (code == "A" || code == "H" || code == "F" || code == "U" || code == "v") {
            ok = true;   // no special dialogue / we are the host / no festival / visible
        } else if (code == "o") {          // not married to them
            ok = w.size() < 2 || spouse != w[1];
        } else if (code == "O") {          // married to them
            ok = w.size() > 1 && spouse == w[1];
        } else if (code == "D") {          // dating them
            ok = w.size() > 1 && dating.count(w[1]) != 0;
        } else if (code == "L") {          // the farmhouse has been upgraded
            ok = houseLevel >= 2;
        } else if (code == "B") {          // a double bed
            ok = houseLevel >= 1;
        } else if (code == "c") {          // free bag slots
            int freeSlots = 0;
            for (int k = 0; k < invSlots; k++) freeSlots += inv[k].empty();
            ok = w.size() > 1 && freeSlots >= atoi(w[1].c_str());
        } else if (code == "C" || code == "X") {   // Community Center finished / not
            bool done = mailReceived.count("ccPantry") && mailReceived.count("ccCraftsRoom") && mailReceived.count("ccFishTank") &&
                        mailReceived.count("ccBoilerRoom") && mailReceived.count("ccBulletin") && mailReceived.count("ccVault");
            ok = (code == "C") == done;
        } else if (code == "J") {          // Joja's warehouse finished
            ok = mailReceived.count("JojaMember") && mailReceived.count("ccPantry") && mailReceived.count("ccCraftsRoom") &&
                 mailReceived.count("ccFishTank") && mailReceived.count("ccBoilerRoom") && mailReceived.count("ccVault");
        } else if (code == "N") {          // golden walnuts found
            ok = w.size() > 1 && walnutsFound >= atoi(w[1].c_str());
        } else if (code == "g") {
            ok = w.size() > 1 && w[1] == "male";   // the farmer sprite is the male base
        } else if (code == "b") {          // reached the bottom of the mines
            ok = mineDeepest >= 120;
        } else if (code == "m") {          // total earnings
            ok = w.size() > 1 && totalEarned >= atoll(w[1].c_str());
        } else if (code == "h") {          // pet preference: the cat's event is the one that plays
            ok = w.size() > 1 && (w[1] == "dog") == (me.petKind == 2) && pets.empty() && !mailReceived.count("hasPet") && !mailReceived.count("s3d_noPet");
        } else if (code == "Skill") {
            static const char* names[SK_COUNT] = {"Farming", "Fishing", "Foraging", "Mining", "Combat"};
            ok = false;
            for (int k = 0; k < SK_COUNT && w.size() > 2; k++)
                if (w[1] == names[k]) ok = skillLevel(k) >= atoi(w[2].c_str());
        } else if (code == "S") {
            ok = w.size() > 1 && mailReceived.count("secretNote:" + w[1]) != 0;
        } else if (code == "j") {
            ok = w.size() > 1 && daysPlayed > atoi(w[1].c_str());
        } else if (code == "M") {
            ok = w.size() > 1 && money >= atoi(w[1].c_str());
        } else if (code == "a") {
            ok = w.size() > 2 && (int)(px / TILE) == atoi(w[1].c_str()) &&
                 (int)((py - 4) / TILE) == atoi(w[2].c_str());
        } else if (code == "i") {
            ok = false;
            for (auto& s : inv)
                if (s.type == SLOT_OBJECT && w.size() > 1 && std::to_string(s.id) == w[1]) ok = true;
        } else if (code == "r") {
            ok = w.size() > 1 && (rngState % 1000) / 1000.0 < atof(w[1].c_str());
        } else {
            ok = false;  // not supported yet: never fire early
        }
        if (ok == neg) return false;
    }
    return true;
}

// ------------------------------------------------------------ start / end

EventActor* Game::eventActor(const std::string& name) {
    std::string n = name;
    if (n.rfind("farmer", 0) == 0) n = "farmer";
    for (auto& a : cutscene.actors)
        if (a.name == n) return &a;
    return nullptr;
}

bool Game::hasEvent(const std::string& loc, const std::string& id) const {
    std::vector<std::pair<std::string, std::string>> evs;
    if (!readEventFile(data.vfs, loc, evs)) return false;
    for (auto& e : evs)
        if (e.first.substr(0, e.first.find('/')) == id) return true;
    return false;
}

bool Game::checkEvents() {
    if (cutscene.active || !cur) return false;
    std::vector<std::pair<std::string, std::string>> evs;
    if (!readEventFile(data.vfs, cur->name, evs)) return false;
    for (auto& e : evs) {
        std::string id = e.first.substr(0, e.first.find('/'));
        if (!isNum(id) && e.first.find('/') == std::string::npos) continue;   // fork targets
        if (eventsSeen.count(id)) continue;
        if (!eventConditionsMet(e.first)) continue;
        return startEvent(cur->name, e.first, e.second);
    }
    return false;
}

bool Game::startOpening() {
    std::vector<std::pair<std::string, std::string>> evs;
    if (!readEventFile(data.vfs, "BusStop", evs)) return false;
    const std::pair<std::string, std::string>* pick = nullptr;
    for (auto& e : evs) {
        // the arrival is the bus stop event whose condition is "day 0"
        std::vector<std::string> pre = splitSlash(e.first);
        for (size_t i = 1; i < pre.size(); i++)
            if (pre[i] == "u 0") pick = &e;
        if (pick) break;
    }
    if (!pick || !hasMap("BusStop")) return false;
    if (!warpTo("BusStop", 10, 10)) return false;
    timeOfDay = DAY_START;
    return startEvent("BusStop", pick->first, pick->second);
}

bool Game::startEvent(const std::string& location, const std::string& key, const std::string& script) {
    std::vector<std::string> cmds = splitSlash(script);
    if (cmds.size() < 3) return false;
    Cutscene& cs = cutscene;
    cs = Cutscene();
    cs.active = true;
    cs.id = key.substr(0, key.find('/'));
    cs.location = location;
    cs.cmds = cmds;
    cs.ip = 3;
    cs.retLoc = cur->name;
    cs.retX = px;
    cs.retY = py;
    cs.retFacing = facing;
    cs.fade = 1;
    cs.fadeTarget = 0;
    {
        std::vector<std::string> mus = tokenize(cmds[0]);
        cs.music = mus.empty() ? "continue" : mus[0];
    }

    std::vector<std::string> vp = tokenize(cmds[1]);
    if (vp.size() >= 2 && isNum(vp[0]) && isNum(vp[1])) {
        cs.camX = atoi(vp[0].c_str()) * TILE + TILE / 2.0f;
        cs.camY = atoi(vp[1].c_str()) * TILE + TILE / 2.0f;
    } else {
        cs.camX = px;
        cs.camY = py;
    }
    std::vector<std::string> ac = tokenize(cmds[2]);
    for (size_t i = 0; i + 3 < ac.size(); i += 4) {
        if (!isNum(ac[i + 1]) || !isNum(ac[i + 2])) continue;
        EventActor a;
        a.name = ac[i].rfind("farmer", 0) == 0 ? "farmer" : ac[i];
        a.x = atoi(ac[i + 1].c_str()) * TILE + TILE / 2.0f;
        a.y = atoi(ac[i + 2].c_str()) * TILE + TILE - 2.0f;
        a.facing = toOurFacing(atoi(ac[i + 3].c_str()));
        cs.actors.push_back(a);
    }
    dialogue = DialogueBox();
    moving = false;
    toolTimer = 0;
    return true;
}

void Game::endEvent() {
    Cutscene& cs = cutscene;
    if (!cs.active) return;
    cs.active = false;
    if (cs.id.rfind("festival_", 0) == 0) {   // a festival's main event: stay where the scene left you
        if (EventActor* a = eventActor("farmer")) { px = a->x; py = a->y; facing = a->facing; }
        fade = 0;
        festivalAfterScene();
        return;
    }
    eventsSeen.insert(cs.id);
    if (cs.id == "112") mailReceived.insert("canReadJunimoText");      // the Wizard's forest magic
    if (cs.id == "611439") mailReceived.insert("ccDoorUnlock");
    if (cs.after == "beginGame") {   // the arrival is over: wake up at home, day 1
        timeOfDay = DAY_START;
        clockAcc = 0;
        wakeUp();
        npcsEnterLocation();
        return;
    }
    // put the player back
    if (cs.after == "warpOut" && !cur->map.warps.empty()) {
        Warp w = cur->map.warps[0];
        warpTo(w.target, w.tx, w.ty);
    } else if (cs.retLoc != cur->name) {
        warpTo(cs.retLoc, (int)(cs.retX / TILE), (int)((cs.retY - 4) / TILE));
    } else {
        px = cs.retX;
        py = cs.retY;
        facing = cs.retFacing;
        fade = 1.0f;
    }
    npcsEnterLocation();
    if (cs.after == "newDay") {
        passedOut = true;   // skip the "go home to sleep" check
        goToBed();
        passedOut = false;
    }
}

void Game::skipEvent() {
    if (!cutscene.active || !cutscene.skippable) return;
    // Apply friendship and mail changes the rest of the script would make.
    for (size_t i = cutscene.ip; i < cutscene.cmds.size(); i++) {
        std::vector<std::string> w = tokenize(cutscene.cmds[i]);
        if (w.empty()) continue;
        if (w[0] == "friendship" && w.size() > 2) {
            for (auto& n : npcs)
                if (n.def->name == w[1]) n.friendship = std::max(0, std::min(friendshipCap(n), n.friendship + atoi(w[2].c_str())));
        } else if ((w[0] == "mail" || w[0] == "addMailReceived") && w.size() > 1) {
            mailReceived.insert(w[1]);
        } else if (w[0] == "end" && w.size() > 1) {
            if (w[1] == "newDay") cutscene.after = "newDay";
            if (w[1] == "warpOut") cutscene.after = "warpOut";
        }
    }
    dialogue = DialogueBox();
    endEvent();
}

// ------------------------------------------------------------ running

void Game::eventUpdate(float dt) {
    Cutscene& cs = cutscene;
    for (auto& a : cs.actors) {
        if (a.moving) {
            float dx = a.tx - a.x, dy = a.ty - a.y, d = std::sqrt(dx * dx + dy * dy), st = a.speed * dt;
            if (d <= st) {
                a.x = a.tx;
                a.y = a.ty;
                a.moving = false;
                a.facing = a.endFacing;
                // advancedMove: carry on with the next leg of the route
                if (a.routeAt < a.route.size() || (a.routeLoop && !a.route.empty())) {
                    if (a.routeAt >= a.route.size()) a.routeAt = 0;
                    auto leg = a.route[a.routeAt++];
                    a.tx = a.x + leg.first * TILE;
                    a.ty = a.y + leg.second * TILE;
                    a.facing = std::abs(leg.first) > std::abs(leg.second) ? (leg.first > 0 ? FACE_RIGHT : FACE_LEFT)
                                                                           : (leg.second > 0 ? FACE_DOWN : FACE_UP);
                    a.endFacing = a.facing;
                    a.moving = leg.first || leg.second;
                }
            } else {
                a.x += dx / d * st;
                a.y += dy / d * st;
            }
            a.walkTime += dt;
        }
        if (!a.anim.empty()) a.animT += dt;
        if (a.jumpT >= 0) {
            a.jumpT += dt;
            if (a.jumpT > 0.45f) a.jumpT = -1;
        }
        if (a.emoteT > 0) a.emoteT -= dt;
        if (a.shakeT > 0) a.shakeT -= dt;
        if (a.bubbleT > 0) a.bubbleT -= dt;
    }
    if (cs.panT > 0) {
        cs.camX += cs.panVX * dt;
        cs.camY += cs.panVY * dt;
        cs.panT -= dt;
    }
    if (cs.fade < cs.fadeTarget) cs.fade = std::min(cs.fadeTarget, cs.fade + cs.fadeSpeed * dt);
    if (cs.fade > cs.fadeTarget) cs.fade = std::max(cs.fadeTarget, cs.fade - cs.fadeSpeed * dt);

    if (dialogue.active) return;
    if (cs.wait > 0) {
        cs.wait -= dt;
        return;
    }
    if (cs.waitMoves) {
        for (auto& a : cs.actors)
            if (a.moving) return;
        cs.waitMoves = false;
    }
    for (int guard = 0; guard < 64 && cs.active && cs.ip < cs.cmds.size(); guard++) {
        std::string cmd = cs.cmds[cs.ip++];
        if (!eventStep(cmd)) return;
    }
    if (cs.active && cs.ip >= cs.cmds.size()) endEvent();
}

static std::vector<std::pair<std::string, std::string>> g_forkCache;

bool Game::eventStep(const std::string& raw) {
    Cutscene& cs = cutscene;
    std::vector<std::string> w = tokenize(raw);
    if (w.empty()) return true;
    const std::string& c = w[0];
    auto arg = [&](size_t i) -> std::string { return i < w.size() ? w[i] : ""; };
    auto iarg = [&](size_t i) { return atoi(arg(i).c_str()); };

    if (c == "playMusic") {
        cs.music = arg(1);
        return true;
    }
    if (c == "stopMusic") {
        cs.music = "none";
        return true;
    }
    if (c == "playSound") {
        if (!arg(1).empty()) sounds.push_back(arg(1));
        return true;
    }
    if (c == "pause") {
        cs.wait = iarg(1) / 1000.0f;
        return false;
    }
    if (c == "faceDirection") {
        if (EventActor* a = eventActor(arg(1))) a->facing = toOurFacing(iarg(2));
        return true;
    }
    if (c == "speak" || c == "message" || c == "textAboveHead") {
        std::string who = c == "message" ? "" : arg(1);
        std::string text = c == "message" ? arg(1) : arg(2);
        if (c == "textAboveHead") {
            if (EventActor* a = eventActor(who)) {
                a->bubble = text;
                a->bubbleT = 3.0f;
            }
            return true;
        }
        std::vector<DialoguePage> pages = parseDialogue(text, me.name);
        if (pages.empty()) return true;
        dialogue.active = true;
        dialogue.npc = data.npc(who) ? who : "";
        dialogue.pages = pages;
        dialogue.page = 0;
        return false;
    }
    if (c == "move") {
        bool cont = truthy(w.back());
        for (size_t i = 1; i + 3 < w.size() + 1 && i + 3 < w.size() + (truthy(w.back()) || w.back() == "false" ? 0 : 1); i += 4) {
            if (!isNum(w[i + 1]) || !isNum(w[i + 2]) || !isNum(w[i + 3])) break;
            EventActor* a = eventActor(w[i]);
            if (!a) continue;
            a->tx = a->x + atoi(w[i + 1].c_str()) * TILE;
            a->ty = a->y + atoi(w[i + 2].c_str()) * TILE;
            a->endFacing = toOurFacing(atoi(w[i + 3].c_str()));
            int dx = atoi(w[i + 1].c_str()), dy = atoi(w[i + 2].c_str());
            if (dx || dy) {
                a->facing = std::abs(dx) > std::abs(dy) ? (dx > 0 ? FACE_RIGHT : FACE_LEFT)
                                                        : (dy > 0 ? FACE_DOWN : FACE_UP);
                a->moving = true;
                a->frame = -1;
                a->anim.clear();
            } else {
                a->facing = a->endFacing;
            }
        }
        if (!cont) cs.waitMoves = true;
        return cont;
    }
    if (c == "warp") {
        if (EventActor* a = eventActor(arg(1))) {
            a->x = iarg(2) * TILE + TILE / 2.0f;
            a->y = iarg(3) * TILE + TILE - 2.0f;
            a->moving = false;
        }
        return true;
    }
    if (c == "showFrame") {
        std::string who = isNum(arg(1)) ? "farmer" : arg(1);
        int f = isNum(arg(1)) ? iarg(1) : iarg(2);
        if (EventActor* a = eventActor(who)) {
            a->frame = f;
            a->anim.clear();
        }
        return true;
    }
    if (c == "animate") {
        if (EventActor* a = eventActor(arg(1))) {
            a->animLoop = arg(3) == "true";
            a->animMs = std::max(1, iarg(4));
            a->anim.clear();
            for (size_t i = 5; i < w.size(); i++)
                if (isNum(w[i])) a->anim.push_back(atoi(w[i].c_str()));
            a->animT = 0;
        }
        return true;
    }
    if (c == "stopAnimation") {
        if (EventActor* a = eventActor(arg(1))) {
            a->anim.clear();
            a->frame = w.size() > 2 ? iarg(2) : -1;
        }
        return true;
    }
    if (c == "jump") {
        if (EventActor* a = eventActor(arg(1))) a->jumpT = 0;
        return true;
    }
    if (c == "emote") {
        if (EventActor* a = eventActor(arg(1))) {
            a->emote = iarg(2);
            a->emoteT = 1.0f;
        }
        if (arg(3) != "true") cs.wait = 0.9f;
        return arg(3) == "true";
    }
    if (c == "positionOffset") {
        if (EventActor* a = eventActor(arg(1))) {
            a->x += iarg(2) / 4.0f;   // event pixels are at the PC's 4x zoom
            a->y += iarg(3) / 4.0f;
        }
        return true;
    }
    if (c == "speed") {
        if (EventActor* a = eventActor(arg(1))) a->speed = std::max(5, iarg(2)) * 15.0f;
        return true;
    }
    if (c == "viewport") {
        if (arg(1) == "move") {
            float secs = std::max(1, iarg(4)) / 1000.0f;
            cs.panVX = iarg(2) * 15.0f;
            cs.panVY = iarg(3) * 15.0f;
            cs.panT = secs;
            return true;
        }
        if (isNum(arg(1)) && isNum(arg(2))) {
            cs.camX = iarg(1) * TILE + TILE / 2.0f;
            cs.camY = iarg(2) * TILE + TILE / 2.0f;
            cs.panT = 0;
        } else if (EventActor* a = eventActor(arg(1))) {
            cs.camX = a->x;
            cs.camY = a->y - 16;
        }
        return true;
    }
    if (c == "globalFade" || (c == "fade" && arg(1) != "unfade")) {
        cs.fadeTarget = 1;
        cs.wait = 0.7f;
        return false;
    }
    if (c == "globalFadeToClear" || (c == "fade" && arg(1) == "unfade")) {
        cs.fadeTarget = 0;
        cs.wait = 0.7f;
        return false;
    }
    if (c == "friendship") {
        for (auto& n : npcs)
            if (n.def->name == arg(1)) n.friendship = std::max(0, std::min(friendshipCap(n), n.friendship + iarg(2)));
        return true;
    }
    if (c == "mail") {              // a letter for tomorrow morning
        if (data.mail.count(arg(1))) mailTomorrow.insert(arg(1));
        else mailReceived.insert(arg(1));
        return true;
    }
    if (c == "addMailReceived") {   // just a flag
        mailReceived.insert(arg(1));
        return true;
    }
    if (c == "itemAboveHead") {     // hold something up (Willy's rod, the copper pan...)
        cs.aboveHead = arg(1).empty() ? "item" : arg(1);
        sfx("getNewSpecialItem");
        return true;
    }
    if (c == "awardFestivalPrize") {
        awardPrize(arg(1));
        cs.aboveHead.clear();
        return true;
    }
    if (c == "addItem") {           // addItem <id> [count]
        std::string id = arg(1);
        if (id.size() > 3 && id[0] == '(') id = id.substr(id.find(')') + 1);
        int n = w.size() > 2 ? std::max(1, iarg(2)) : 1;
        if (!id.empty() && isdigit((unsigned char)id[0])) addItem(atoi(id.c_str()), n);
        return true;
    }
    if (c == "skippable") {
        cs.skippable = true;
        return true;
    }
    if (c == "question" || c == "quickQuestion") {
        std::string text = c == "question" ? arg(2) : arg(1);
        size_t brk = text.find("(break)");
        if (brk != std::string::npos) text = text.substr(0, brk);
        std::vector<std::string> parts;
        std::string cur2;
        for (char ch : text) {
            if (ch == '#') { parts.push_back(cur2); cur2.clear(); }
            else cur2 += ch;
        }
        parts.push_back(cur2);
        DialoguePage p;
        p.text = parts[0];
        for (size_t i = 1; i < parts.size(); i++) {
            DialogueChoice ch;
            ch.label = parts[i];
            p.choices.push_back(ch);
        }
        cs.forkWant = -1;
        if (c == "question" && arg(1).rfind("fork", 0) == 0) cs.forkWant = atoi(arg(1).substr(4).c_str());
        cs.forkFlag = false;
        cs.questionPending = !p.choices.empty();
        dialogue.active = true;
        dialogue.npc = "";
        dialogue.pages = {p};
        dialogue.page = 0;
        return false;
    }
    if (c == "fork" || c == "switchEvent") {
        std::string target = w.back();
        bool go = c == "switchEvent" || cs.forkFlag ||
                  (w.size() > 2 && (mailReceived.count(arg(1)) || eventsSeen.count(arg(1))));
        if (!go) return true;
        g_forkCache.clear();
        readEventFile(data.vfs, cs.location, g_forkCache);
        for (auto& e : g_forkCache)
            if (e.first == target || e.first.rfind(target + "/", 0) == 0) {
                cs.cmds = splitSlash(e.second);
                cs.ip = 0;
                cs.forkFlag = false;
                return true;
            }
        return true;
    }
    if (c == "cave" || c == "catQuestion") {   // Demetrius's cave, Marnie's stray
        DialoguePage p;
        DialogueChoice ch;
        if (c == "cave") {
            p.text = "What should Demetrius set up in your cave?";
            ch.label = "Mushrooms"; p.choices.push_back(ch);
            ch.label = "Fruit bats"; p.choices.push_back(ch);
        } else {
            // the one you picked when you made your farmer comes first; a farm from before that choice can still pick
            static const char* kinds[3] = {"The cat", "The dog", "The turtle"};
            p.text = "Marnie found a few strays. Give one a home?";
            int first = std::max(1, std::min(3, me.petKind));
            ch.label = kinds[first - 1]; ch.responseKey = "pet:adopt:" + std::to_string(first); p.choices.push_back(ch);
            for (int k = 1; k <= 3; k++)
                if (k != first && (k != 3 || data.vfs.exists("tex/Animals_turtle.s3t"))) {
                    ch.label = kinds[k - 1]; ch.responseKey = "pet:adopt:" + std::to_string(k); p.choices.push_back(ch);
                }
            ch.label = "No"; ch.responseKey = "pet:adopt:0"; p.choices.push_back(ch);
        }
        cs.special = c;
        cs.forkWant = 0;
        cs.forkFlag = false;
        cs.questionPending = true;
        dialogue.active = true;
        dialogue.npc = "";
        dialogue.pages = {p};
        dialogue.page = 0;
        return false;
    }
    if (c == "rustyKey") { mailReceived.insert("HasRustyKey"); return true; }
    if (c == "gainSkill") { gainXp(std::max(0, std::min((int)SK_COUNT - 1, iarg(1))), 200); return true; }
    if (c == "grandpaEvaluation" || c == "grandpaEvaluation2") { grandpaEvaluation(); return true; }
    if (c == "splitSpeak") {   // one line or the other, by the answer just given
        std::string text = arg(2);
        size_t bar = text.find('~');
        if (bar != std::string::npos) text = cs.forkFlag ? text.substr(0, bar) : text.substr(bar + 1);
        auto pages = parseDialogue(text, me.name);
        if (pages.empty()) return true;
        dialogue.active = true;
        dialogue.npc = arg(1);
        dialogue.pages = pages;
        dialogue.page = 0;
        return false;
    }
    // festival scenes
    if (c == "loadActors" || c == "waitForOtherPlayers" || c == "hideShadow" || c == "bgColor" || c == "stopAdvancedMoves" ||
        c == "changeMapTile" || c == "removeObject" || c == "changeSprite" || c == "changePortrait" || c == "translateName" ||
        c == "stopGlowing" || c == "farmerAnimation" || c == "hostMail" || c == "waitForTempSprite" || c == "grandpaCandles" ||
        c == "beginSimultaneousCommand" || c == "endSimultaneousCommand" || c == "eggHuntWinner" || c == "iceFishingWinner" ||
        c == "governorTaste" || c == "awardFestivalPrize2")
        return true;
    if (c == "warpFarmers") {   // the first spot is yours
        if (EventActor* a = eventActor("farmer")) {
            a->x = iarg(1) * TILE + TILE / 2.0f;
            a->y = iarg(2) * TILE + TILE - 2.0f;
            a->facing = toOurFacing(iarg(3));
        }
        return true;
    }
    if (c == "playerControl") {   // the scene hands over to you (the egg hunt, ice fishing)
        endEvent();
        return false;
    }
    if (c == "addQuest") { addQuest(arg(1)); return true; }
    if (c == "removeQuest") {
        for (size_t i = 0; i < quests.size(); i++)
            if (quests[i].id == arg(1)) { quests.erase(quests.begin() + i); break; }
        return true;
    }
    if (c == "addCraftingRecipe") { mailReceived.insert("craft:" + arg(1)); return true; }
    if (c == "addCookingRecipe") { mailReceived.insert("cook:" + arg(1)); return true; }
    if (c == "removeItem") {
        int id = data.objectIdFor(arg(1));
        if (id >= 0 && countItem(id) > 0) takeItem(id, std::max(1, iarg(2)));
        return true;
    }
    if (c == "money") { money = std::max(0, money + iarg(1)); return true; }
    if (c == "addObject" || c == "addBigProp" || c == "addLantern" || c == "addConversationTopic" || c == "makeInvisible" ||
        c == "screenFlash" || c == "eyes" || c == "glow" || c == "ambientLight" || c == "farmerEat" || c == "playPetSound" ||
        c == "specificTemporarySprite" || c == "temporarySprite" || c == "removeSprite" || c == "stopSwimming" ||
        c == "swimming" || c == "changeYSourceRectOffset" || c == "resetVariable" || c == "addWorldState" || c == "dump" ||
        c == "broadcastEvent" || c == "removeTile" || c == "null" || c == "doAction" || c == "updateMinigame" || c == "cutscene")
        return true;   // set dressing the 3DS scene does without
    if (c == "changeToTemporaryMap") {
        std::string loc = arg(1);
        if (!loc.empty() && hasMap(loc)) {
            warpTo(loc, 10, 10);
            cs.location = loc;
            fade = 0;
            cs.fadeTarget = 0;
        }
        return true;
    }
    if (c == "changeLocation") {
        std::string loc = arg(1);
        if (!loc.empty() && loc != cur->name) {
            warpTo(loc, (int)(cs.camX / TILE), (int)(cs.camY / TILE));   // events stay active
            cs.location = loc;
            fade = 0;
            cs.fadeTarget = 0;   // a "fade" before the move clears on arrival, like the game
        }
        return true;
    }
    if (c == "addTemporaryActor" || c == "addActor") {
        EventActor a;
        a.name = arg(1);
        int tx = c == "addActor" ? iarg(2) : iarg(4), ty = c == "addActor" ? iarg(3) : iarg(5);
        a.x = tx * TILE + TILE / 2.0f;
        a.y = ty * TILE + TILE - 2.0f;
        a.facing = toOurFacing(c == "addActor" ? iarg(4) : iarg(6));
        cs.actors.push_back(a);
        return true;
    }
    if (c == "removeTemporarySprites") return true;
    if (c == "shake") {
        if (EventActor* a = eventActor(arg(1))) a->shakeT = std::max(0.1f, iarg(2) / 1000.0f);
        return true;
    }
    if (c == "startJittering" || c == "stopJittering") {
        if (EventActor* a = eventActor("farmer")) a->shakeT = c == "startJittering" ? 60.0f : 0.0f;
        return true;
    }
    if (c == "halt") {   // everyone stops where they are
        for (auto& a : cs.actors) {
            a.moving = false;
            a.route.clear();
            a.tx = a.x;
            a.ty = a.y;
        }
        return true;
    }
    if (c == "proceedPosition") {   // wait for someone to finish walking
        cs.waitMoves = true;
        return false;
    }
    if (c == "advancedMove") {
        // advancedMove <who> <loop> <dx> <dy> ...: walk a route of tile steps,
        // without holding up the rest of the script
        if (EventActor* a = eventActor(arg(1))) {
            a->route.clear();
            a->routeAt = 0;
            a->routeLoop = truthy(arg(2));
            for (size_t i = 3; i + 1 < w.size(); i += 2) {
                if (!isNum(w[i]) || !isNum(w[i + 1])) break;
                int dx = atoi(w[i].c_str()), dy = atoi(w[i + 1].c_str());
                // a pair with one zero and one facing number + delay is a turn; keep only real steps
                if (dx && dy) continue;
                if (dx || dy) a->route.push_back({dx, dy});
            }
            if (!a->route.empty() && !a->moving) {
                auto leg = a->route[a->routeAt++];
                a->tx = a->x + leg.first * TILE;
                a->ty = a->y + leg.second * TILE;
                a->facing = std::abs(leg.first) > std::abs(leg.second) ? (leg.first > 0 ? FACE_RIGHT : FACE_LEFT)
                                                                       : (leg.second > 0 ? FACE_DOWN : FACE_UP);
                a->endFacing = a->facing;
                a->moving = true;
                a->frame = -1;
            }
        }
        return true;
    }
    if (c == "end") {
        std::string kind = arg(1);
        if (kind == "newDay") cs.after = "newDay";
        else if (kind == "beginGame") cs.after = "beginGame";
        else if (kind == "warpOut" || kind == "dialogueWarpOut") cs.after = "warpOut";
        else if (kind == "position" && w.size() > 3) {
            cs.retX = iarg(2) * TILE + TILE / 2.0f;
            cs.retY = iarg(3) * TILE + TILE - 2.0f;
            cs.retLoc = cur->name;
        }
        std::string lastLine;
        std::string lastNpc;
        if ((kind == "dialogue" || kind == "dialogueWarpOut") && w.size() > 3) {
            lastNpc = arg(2);
            lastLine = arg(3);
        }
        endEvent();
        if (!lastLine.empty()) {
            auto pages = parseDialogue(lastLine, me.name);
            if (!pages.empty()) {
                dialogue.active = true;
                dialogue.npc = lastNpc;
                dialogue.pages = pages;
                dialogue.page = 0;
            }
        }
        return false;
    }
    // Everything else (sounds, music, screen shake, temporary sprites, minigames...)
    // is skipped for now so the scene keeps going.
    return true;
}

} // namespace sv
