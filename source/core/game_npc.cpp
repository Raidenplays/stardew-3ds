// Villagers: daily schedules, walking between maps, talking and gifts.
#include <algorithm>
#include <cmath>

#include "game.h"

namespace sv {

static const int TILE = 16;
static const float NPC_SPEED_PX = 32.0f;      // on-screen walking speed
static const float NPC_SPEED_TILES = 2.0f;    // off-screen travel speed

static void placeAt(NpcState& n, int tx, int ty) {
    n.x = tx * TILE + TILE / 2.0f;
    n.y = ty * TILE + TILE - 2.0f;
}

ScheduleContext Game::scheduleContext(const NpcState& n) const {
    ScheduleContext c;
    c.season = season;
    c.day = day;
    c.weekday = weekday();
    c.raining = raining;
    c.hearts = n.hearts();
    return c;
}

const NpcState* Game::npcByName(const std::string& name) const {
    for (auto& n : npcs)
        if (n.def->name == name) return &n;
    return nullptr;
}

void Game::npcsStartDay() {
    if (npcs.size() != data.npcs.size()) {
        npcs.clear();
        for (auto& d : data.npcs) {
            NpcState n;
            n.def = &d;
            npcs.push_back(n);
        }
    }
    for (auto& n : npcs) {
        n.loc = n.def->homeMap;
        placeAt(n, n.def->homeX, n.def->homeY);
        n.facing = n.def->homeDir;
        n.sched = resolveSchedule(*n.def, scheduleContext(n));
        n.next = 0;
        n.legs.clear();
        n.leg = 0;
        n.path.clear();
        n.pathIdx = 0;
        n.moving = false;
        n.anim = nullptr;
        n.endAnim.clear();
        n.talkedToday = false;
        n.giftsToday = 0;
        if (weekday() == 0) n.giftsWeek = 0;
        if (n.def->name == spouse && hasMap("FarmHouse")) {   // your spouse lives with you now
            Location* h = loadLocation("FarmHouse");
            const Warp* w = h ? h->map.warpTo("Farm") : nullptr;
            n.loc = "FarmHouse";
            int sx = w ? w->x - 3 : 5, sy = w ? w->y - 4 : 5;
            if (h) {   // nearest open floor
                const TileMap& m = h->map;
                bool found = false;
                for (int r = 0; r < 8 && !found; r++)
                    for (int dy = -r; dy <= r && !found; dy++)
                        for (int dx = -r; dx <= r && !found; dx++) {
                            int x = sx + dx, y = sy + dy;
                            if (m.inBounds(x, y) && m.tile(L_BACK, x, y) != NO_TILE && !(m.flags(x, y) & CF_BLOCKED) &&
                                m.tile(L_BUILDINGS, x, y) == NO_TILE) { sx = x; sy = y; found = true; }
                        }
            }
            placeAt(n, sx, sy);
            n.facing = FACE_DOWN;
            n.sched.clear();
        }
    }
}

void Game::startAnim(NpcState& n) {
    n.anim = n.endAnim.empty() ? nullptr : data.npcAnim(n.endAnim);
    n.animTime = 0;
    n.animOut = false;
}

// Frame of the stop animation now, or -1 when it's over / none.
int NpcState::animFrame() const {
    if (!anim) return -1;
    const float FT = 0.125f;           // seconds per listed frame
    int i = (int)(animTime / FT);
    if (animOut) return i < (int)anim->outro.size() ? anim->outro[i] : -1;
    if (i < (int)anim->intro.size()) return anim->intro[i];
    i -= (int)anim->intro.size();
    if (anim->loop.empty()) return anim->intro.empty() ? -1 : anim->intro.back();
    return anim->loop[i % anim->loop.size()];
}

void Game::npcPlan(NpcState& n, const SchedEntry& e) {
    bool ok = false;
    std::vector<GraphEdge> route = routeBetween(data, n.loc, e.loc, &ok);
    n.legs.clear();
    n.leg = 0;
    n.legDone = 0;
    n.path.clear();
    n.pathIdx = 0;
    n.endFacing = e.facing;
    n.endAnim = e.anim;
    if (!ok) {
        // No known way there (map not converted?): just appear.
        n.loc = e.loc;
        placeAt(n, e.x, e.y);
        n.facing = e.facing;
        startAnim(n);
        return;
    }
    std::string m = n.loc;
    int fx = n.tileX(), fy = n.tileY();
    for (const GraphEdge& g : route) {
        NpcState::Leg L;
        L.loc = m;
        L.fx = fx; L.fy = fy;
        L.tx = g.x; L.ty = g.y;
        L.warp = true;
        L.wloc = g.target;
        L.wx = g.tx; L.wy = g.ty;
        n.legs.push_back(L);
        m = g.target;
        fx = g.tx; fy = g.ty;
    }
    NpcState::Leg last;
    last.loc = m;
    last.fx = fx; last.fy = fy;
    last.tx = e.x; last.ty = e.y;
    n.legs.push_back(last);
}

void Game::npcFinishLeg(NpcState& n) {
    if (n.leg >= n.legs.size()) return;
    const NpcState::Leg L = n.legs[n.leg];
    if (L.warp) {
        n.loc = L.wloc;
        placeAt(n, L.wx, L.wy);
    } else {
        placeAt(n, L.tx, L.ty);
        n.facing = n.endFacing;
        startAnim(n);
    }
    n.leg++;
    n.legDone = 0;
    n.path.clear();
    n.pathIdx = 0;
    n.moving = false;
    if (n.leg < n.legs.size()) {
        n.legs[n.leg].fx = n.tileX();
        n.legs[n.leg].fy = n.tileY();
    }
}

void Game::npcsUpdate(float dt) {
    for (auto& n : npcs) {
        if (n.anim) n.animTime += dt;
        if (n.anim && n.animOut) {          // let the outro finish before walking off
            if (n.animFrame() >= 0) continue;
            n.anim = nullptr;
        }
        if (n.anim && n.next < n.sched.size() && timeOfDay >= n.sched[n.next].time) {
            n.animOut = true;
            n.animTime = 0;
            if (n.animFrame() >= 0) continue;
            n.anim = nullptr;
        }
        while (n.next < n.sched.size() && timeOfDay >= n.sched[n.next].time) {
            npcPlan(n, n.sched[n.next]);
            n.next++;
        }
        if (n.leg >= n.legs.size()) {
            n.moving = false;
            continue;
        }
        NpcState::Leg& L = n.legs[n.leg];
        n.loc = L.loc;
        bool visible = cur && L.loc == cur->name;
        if (!visible) {
            n.moving = false;
            n.legDone += dt * NPC_SPEED_TILES;
            int len = std::abs(L.tx - L.fx) + std::abs(L.ty - L.fy);
            if (n.legDone >= len) npcFinishLeg(n);
            continue;
        }
        if (n.path.empty()) {
            n.path = findPath(cur->map, n.tileX(), n.tileY(), L.tx, L.ty);
            n.pathIdx = 0;
            if (n.path.empty()) {     // already there, or no way through: snap
                npcFinishLeg(n);
                continue;
            }
        }
        float step = NPC_SPEED_PX * dt;
        while (step > 0 && n.pathIdx < n.path.size()) {
            float gx = n.path[n.pathIdx].first * TILE + TILE / 2.0f;
            float gy = n.path[n.pathIdx].second * TILE + TILE - 2.0f;
            float dx = gx - n.x, dy = gy - n.y;
            float dist = std::sqrt(dx * dx + dy * dy);
            if (std::fabs(dx) > std::fabs(dy)) n.facing = dx > 0 ? FACE_RIGHT : FACE_LEFT;
            else if (dy != 0) n.facing = dy > 0 ? FACE_DOWN : FACE_UP;
            if (dist <= step) {
                n.x = gx;
                n.y = gy;
                step -= dist;
                n.pathIdx++;
            } else {
                n.x += dx / dist * step;
                n.y += dy / dist * step;
                step = 0;
            }
        }
        n.moving = true;
        n.walkTime += dt;
        if (n.pathIdx >= n.path.size()) npcFinishLeg(n);
    }
}

void Game::npcsEnterLocation() {
    for (auto& n : npcs) {
        if (n.leg >= n.legs.size()) continue;
        NpcState::Leg& L = n.legs[n.leg];
        if (L.loc != cur->name) continue;
        // Put them part-way along this leg, on an open tile.
        int len = std::abs(L.tx - L.fx) + std::abs(L.ty - L.fy);
        float f = len > 0 ? std::min(1.0f, n.legDone / len) : 1.0f;
        int tx = (int)std::lround(L.fx + (L.tx - L.fx) * f);
        int ty = (int)std::lround(L.fy + (L.ty - L.fy) * f);
        const TileMap& m = cur->map;
        tx = std::max(0, std::min(tx, m.width() - 1));
        ty = std::max(0, std::min(ty, m.height() - 1));
        for (int r = 0; r < 4; r++) {
            bool done = false;
            for (int dy = -r; dy <= r && !done; dy++)
                for (int dx = -r; dx <= r && !done; dx++) {
                    uint8_t fl = m.flags(tx + dx, ty + dy);
                    if (m.inBounds(tx + dx, ty + dy) && !((fl & CF_BLOCKED) && !(fl & CF_PASSABLE))) {
                        tx += dx;
                        ty += dy;
                        done = true;
                    }
                }
            if (done) break;
        }
        placeAt(n, tx, ty);
        n.path.clear();
        n.pathIdx = 0;
    }
}

NpcState* Game::npcAt(int tx, int ty) {
    float cx = tx * TILE + TILE / 2.0f, cy = ty * TILE + 12.0f;
    for (auto& n : npcs)
        if (n.loc == cur->name && std::fabs(n.x - cx) < 12 && std::fabs(n.y - cy) < 14) return &n;
    return nullptr;
}

void Game::showDialogue(const NpcState& n, const std::vector<DialoguePage>& pages) {
    dialogue.active = !pages.empty();
    dialogue.npc = n.def->name;
    dialogue.pages = pages;
    dialogue.page = 0;
}

// ---------------------------------------------------------------- romance

int Game::friendshipCap(const NpcState& n) const {
    if (n.def->name == spouse) return 3500;                              // 14 hearts
    if (data.romanceable(n.def->name) && !dating.count(n.def->name) && engagedTo != n.def->name) return 2000;   // 8 until dating
    return 2500;
}

// A bouquet (8+ hearts) starts dating; the mermaid's pendant (10 hearts, dating, a bigger house) is a proposal.
bool Game::romanceGift(NpcState& n, int slot) {
    Slot& sl = inv[slot];
    if (sl.type != SLOT_OBJECT || (sl.id != 458 && sl.id != 460)) return false;
    const std::string& who = n.def->name;
    DialoguePage p;
    p.portrait = 1;
    if (!data.romanceable(who)) {
        p.text = "Um... that's sweet, but no.";
        p.portrait = 2;
    } else if (sl.id == 458) {   // bouquet
        if (who == spouse || dating.count(who)) p.text = "We're already together, silly!";
        else if (n.hearts() < 8) { p.text = "Oh... I don't think we know each other well enough for that."; p.portrait = 2; }
        else {
            dating.insert(who);
            if (--sl.count <= 0) sl = Slot();
            p.text = "Oh! You mean... you want to be more than friends? ...Yes. I'd like that.";
            p.portrait = 4;
            sfx("give_gift");
        }
    } else {                     // the pendant
        if (!spouse.empty()) p.text = "...You're already married.";
        else if (!dating.count(who) || n.hearts() < 10) { p.text = "That's... a big step. Maybe someday."; p.portrait = 2; }
        else if (houseLevel < 1) p.text = "Yes! ...But where would we live? Your house needs to be bigger first.";
        else {
            engagedTo = who;
            weddingDay = absoluteDay() + 3;
            if (--sl.count <= 0) sl = Slot();
            p.text = "Yes! Of course, yes! ...The wedding will be in three days. I'll take care of everything!";
            p.portrait = 4;
            sfx("give_gift");
        }
    }
    showDialogue(n, {p});
    return true;
}

// Married life: the spouse's lines (Characters/Dialogue/MarriageDialogue*), by weather, place and time.
void Game::marriageTalk(NpcState& n, std::vector<DialoguePage>& pages) {
    std::string mode = raining ? "Rainy_Day" : cur && cur->name == "FarmHouse" ? "Indoor_Day" : "Outdoor";
    if (timeOfDay >= 18 * 60) mode = raining ? "Rainy_Night" : "Indoor_Night";
    std::vector<const std::string*> pick;
    for (auto& kv : n.def->dialogue) {
        const std::string& k = kv.first;
        std::string pre = "marriage:" + mode + "_";
        if (k.rfind(pre, 0) != 0) continue;
        std::string rest = k.substr(pre.size());
        if (rest == n.def->name || (!rest.empty() && isdigit((unsigned char)rest[0]))) pick.push_back(&kv.second);
    }
    if (pick.empty()) return;
    uint32_t h = (uint32_t)absoluteDay() * 2654435761u + (uint32_t)timeOfDay / 60;
    pages = parseDialogue(*pick[h % pick.size()], me.name);
}

void Game::talkTo(NpcState& n) {
    if (festivalTalk(n)) return;
    if (kidsSpouseTalk(n)) return;
    // turn to face the player
    float dx = px - n.x, dy = py - n.y;
    if (std::fabs(dx) > std::fabs(dy)) n.facing = dx > 0 ? FACE_RIGHT : FACE_LEFT;
    else n.facing = dy > 0 ? FACE_DOWN : FACE_UP;

    if (n.def->dialogue.count("fact_0")) {   // a talking prop (Raiden): a random fact, nothing else
        std::vector<const std::string*> facts;
        for (auto& kv : n.def->dialogue)
            if (kv.first.rfind("fact_", 0) == 0) facts.push_back(&kv.second);
        uint32_t r = rand32() % facts.size();
        if (facts.size() > 1 && r == n.lastFact) r = (r + 1) % facts.size();
        n.lastFact = r;
        auto pages = parseDialogue("Oh, hey! Did you know...$h#$b#" + *facts[r], me.name);
        showDialogue(n, pages);
        return;
    }
    std::string key = pickDialogueKey(*n.def, scheduleContext(n), n.introduced);
    std::vector<DialoguePage> pages;
    if (n.def->name == spouse) marriageTalk(n, pages);
    if (pages.empty() && !key.empty()) pages = parseDialogue(n.def->dialogue.at(key), me.name);
    if (isBirthday(*n.def) && !n.talkedToday) {
        DialoguePage p;
        p.text = "It's my birthday today!";
        p.portrait = 1;
        pages.insert(pages.begin(), p);
    }
    if (pages.empty()) {
        DialoguePage p;
        p.text = "Hi, " + me.name + ".";
        pages.push_back(p);
    }
    if (!n.talkedToday) n.friendship = std::min(friendshipCap(n), n.friendship + 20);
    n.talkedToday = true;
    bool first = !n.introduced;
    n.introduced = true;
    showDialogue(n, pages);
    if (first) questMet();
}

void Game::giveGift(NpcState& n, int slot) {
    Slot& sl = inv[slot];
    const ObjectDef* o = data.object(sl.id);
    if (!o) return;
    if (questDelivered(n.def->name, sl.id)) {   // quest deliveries always go through
        n.friendship = std::min(friendshipCap(n), n.friendship + 150);
        if (--sl.count <= 0) sl = Slot();
        return;
    }
    if (n.def->dialogue.count("fact_0")) { say(n.def->display + " is too busy racing to take that."); return; }
    if (romanceGift(n, slot)) return;
    bool bday = isBirthday(*n.def);
    if (n.giftsToday >= 1) {
        say("You already gave " + n.def->display + " a gift today");
        return;
    }
    if (n.giftsWeek >= 2 && !bday) {
        say("You've given " + n.def->display + " two gifts this week");
        return;
    }
    Taste t = giftTaste(data, *n.def, sl.id);
    int pts = giftPoints(t) * (bday ? 8 : 1);
    n.friendship = std::max(0, std::min(friendshipCap(n), n.friendship + pts));
    n.giftsToday++;
    n.giftsWeek++;
    n.introduced = true;
    if (--sl.count <= 0) sl = Slot();

    static const char* generic[TASTE_COUNT] = {
        "Oh wow, I love this! Thank you!", "Thanks, I like this.", "Um... thanks, I guess.",
        "What is this? Ugh.", "Thanks.",
    };
    static const int faces[TASTE_COUNT] = {4, 1, 2, 5, 0};
    DialoguePage p;
    std::string own = n.def->tasteText[t];
    std::vector<DialoguePage> pages;
    if (!own.empty()) pages = parseDialogue(own, me.name);
    if (pages.empty()) {
        p.text = generic[t];
        p.portrait = faces[t];
        pages.push_back(p);
    } else if (pages[0].portrait == 0) {
        pages[0].portrait = faces[t];
    }
    showDialogue(n, pages);
}

void Game::dialogueNext() {
    if (!dialogue.active) return;
    if (dialogue.page < dialogue.pages.size() && !dialogue.pages[dialogue.page].choices.empty()) return;
    dialogue.page++;
    if (dialogue.page >= dialogue.pages.size()) dialogue.active = false;
}

void Game::dialogueChoose(int i) {
    if (!dialogue.active || dialogue.page >= dialogue.pages.size()) return;
    DialoguePage& p = dialogue.pages[dialogue.page];
    if (i < 0 || i >= (int)p.choices.size()) return;
    DialogueChoice ch = p.choices[i];
    if (!pendingChoice.empty()) {   // a question the game asked (the elevator)
        std::string what = pendingChoice;
        pendingChoice.clear();
        p.choices.clear();
        dialogueNext();
        if (what == "festival" && ch.responseKey == "festival:go") { festivalMain(); return; }
        if (worldChoice(what, ch.responseKey)) return;
        if (petChoice(what, ch.responseKey)) return;
        if (kidsChoice(what, ch.responseKey)) return;
        if (what == "elevator" && ch.responseKey.rfind("mine:", 0) == 0) {
            int lv = atoi(ch.responseKey.c_str() + 5);
            if (lv <= 0) { sfx("stairsdown"); warpTo("Mine", 17, 4, false); }
            else enterMine(lv);
        }
        return;
    }
    if (cutscene.active && cutscene.questionPending) {
        cutscene.forkFlag = (i == cutscene.forkWant);
        cutscene.questionPending = false;
        if (cutscene.special == "cave") mailReceived.insert(i == 0 ? "caveMushrooms" : "caveBats");
        if (cutscene.special == "catQuestion") petChoice("pet", ch.responseKey);
        cutscene.special.clear();
    }
    std::vector<DialoguePage> reply;
    for (auto& n : npcs)
        if (n.def->name == dialogue.npc) {
            n.friendship = std::max(0, std::min(friendshipCap(n), n.friendship + ch.points));
            if (!ch.responseKey.empty()) {
                auto it = n.def->dialogue.find(ch.responseKey);
                if (it != n.def->dialogue.end()) reply = parseDialogue(it->second, me.name);
            }
        }
    if (reply.empty() && !ch.inlineReply.empty()) reply = parseDialogue(ch.inlineReply, me.name);
    p.choices.clear();
    dialogue.pages.insert(dialogue.pages.begin() + dialogue.page + 1, reply.begin(), reply.end());
    dialogueNext();
}

} // namespace sv
