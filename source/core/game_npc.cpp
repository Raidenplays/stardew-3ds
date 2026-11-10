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
        n.talkedToday = false;
        n.giftsToday = 0;
        if (weekday() == 0) n.giftsWeek = 0;
    }
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
    if (!ok) {
        // No known way there (map not converted?): just appear.
        n.loc = e.loc;
        placeAt(n, e.x, e.y);
        n.facing = e.facing;
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

void Game::talkTo(NpcState& n) {
    // turn to face the player
    float dx = px - n.x, dy = py - n.y;
    if (std::fabs(dx) > std::fabs(dy)) n.facing = dx > 0 ? FACE_RIGHT : FACE_LEFT;
    else n.facing = dy > 0 ? FACE_DOWN : FACE_UP;

    std::string key = pickDialogueKey(*n.def, scheduleContext(n), n.introduced);
    std::vector<DialoguePage> pages;
    if (!key.empty()) pages = parseDialogue(n.def->dialogue.at(key), me.name);
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
    if (!n.talkedToday) n.friendship = std::min(2500, n.friendship + 20);
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
        n.friendship = std::min(2500, n.friendship + 150);
        if (--sl.count <= 0) sl = Slot();
        return;
    }
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
    n.friendship = std::max(0, std::min(2500, n.friendship + pts));
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
    if (cutscene.active && cutscene.questionPending) {
        cutscene.forkFlag = (i == cutscene.forkWant);
        cutscene.questionPending = false;
    }
    std::vector<DialoguePage> reply;
    for (auto& n : npcs)
        if (n.def->name == dialogue.npc) {
            n.friendship = std::max(0, std::min(2500, n.friendship + ch.points));
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
