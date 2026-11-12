// Fishing: hold A to charge a cast, wait for a bite, press A, then keep the
// fish inside your bar (hold A to rise) until the catch meter fills.
#include <algorithm>
#include <cmath>

#include "game.h"

namespace sv {

static const float BITE_WINDOW = 1.0f;
static const float DONE_TIME = 1.2f;
static const int CAST_ENERGY = 4;

static float frand(uint32_t r) { return (r % 10000) / 10000.0f; }

int Game::pickFish() {
    std::vector<const FishSpot*> spots;
    for (const char* key : {"", "Default"}) {
        std::string k = *key ? key : cur->name;
        auto it = data.fishSpots.find(k);
        if (it == data.fishSpots.end()) continue;
        for (auto& s : it->second) spots.push_back(&s);
    }
    std::stable_sort(spots.begin(), spots.end(),
                     [](const FishSpot* a, const FishSpot* b) { return a->precedence < b->precedence; });
    int level = skillLevel(SK_FISHING);
    int hhmm = (timeOfDay / 60) * 100 + timeOfDay % 60;
    for (const FishSpot* s : spots) {
        if (!(s->seasonMask & (1 << season)) || level < s->minLevel || s->items.empty()) continue;
        if (frand(rand32()) >= s->chance) continue;
        int id = s->items[rand32() % s->items.size()];
        auto fd = data.fish.find(id);
        if (fd != data.fish.end() && !s->ignoreFishRules) {
            const FishDef& f = fd->second;
            if (!(f.seasonMask & (1 << season)) || level < f.minLevel) continue;
            if ((f.weather == 1 && raining) || (f.weather == 2 && !raining)) continue;
            bool inTime = f.times.size() < 2;
            for (size_t i = 0; i + 1 < f.times.size(); i += 2)
                if (hhmm >= f.times[i] && hhmm < f.times[i + 1]) inTime = true;
            if (!inTime) continue;
            // harder fish bite less often
            if (frand(rand32()) > 1.0f - f.difficulty / 220.0f) continue;
        }
        if (data.object(id)) return id;
    }
    return -1;
}

void Game::startFishing() {
    fishing = Fishing();
    fishing.state = FS_CHARGE;
}

void Game::fishingUpdate(float dt, const Input& in) {
    Fishing& f = fishing;
    f.t += dt;
    moving = false;
    switch (f.state) {
        case FS_CHARGE: {
            // power swings up and down while A is held
            float cyc = std::fmod(f.t, 2.0f);
            f.power = cyc < 1 ? cyc : 2 - cyc;
            if (in.hold) return;
            int dx = facing == FACE_RIGHT ? 1 : facing == FACE_LEFT ? -1 : 0;
            int dy = facing == FACE_DOWN ? 1 : facing == FACE_UP ? -1 : 0;
            int ptx = (int)(px / 16), pty = (int)((py - 4) / 16);
            int dist = 1 + (int)std::lround(f.power * 4);
            bool found = false;
            for (int d = dist; d >= 1 && !found; d--) {
                int x = ptx + dx * d, y = pty + dy * d;
                if (cur->map.inBounds(x, y) && (cur->map.flags(x, y) & CF_WATER)) {
                    f.bobX = x;
                    f.bobY = y;
                    found = true;
                }
            }
            if (!found) {
                say("Cast into water to fish");
                f.state = FS_NONE;
                return;
            }
            energy = std::max(0, energy - CAST_ENERGY);
            f.state = FS_CAST;
            sfx("cast");
            f.t = 0;
            return;
        }
        case FS_CAST:
            if (f.t >= 0.5f) {
                f.state = FS_WAIT;
                f.t = 0;
                f.waitFor = 1.5f + frand(rand32()) * (7.0f - f.power * 3.0f);
            }
            return;
        case FS_WAIT:
            if (in.action) {   // reel in early
                f.state = FS_NONE;
                return;
            }
            if (f.t >= f.waitFor) {
                f.fish = pickFish();
                if (f.fish < 0) {   // nothing biting: keep waiting
                    f.t = 0;
                    f.waitFor = 2.0f + frand(rand32()) * 4.0f;
                    return;
                }
                f.state = FS_BITE;
                sfx("fishBite");
                f.t = 0;
            }
            return;
        case FS_BITE:
            if (in.action) {
                auto fd = data.fish.find(f.fish);
                if (fd == data.fish.end()) {   // junk and such: straight in
                    f.caught = true;
                    sfx("jingle1");
                    f.state = FS_DONE;
                    f.t = 0;
                    addItem(f.fish, 1);
                fishCaught[f.fish]++;
                orderEvent("fish", f.fish, 1, "");
                if (cur->name.rfind("Island", 0) == 0) walnutChance(1, 6);
                    questItem(f.fish, 1);
                    const ObjectDef* o = data.object(f.fish);
                    say("Caught " + (o ? o->name : std::string("something")));
                    gainXp(SK_FISHING, 3);
                    return;
                }
                f.state = FS_REEL;
                sfx("FishHit");
                f.t = 0;
                f.barSize = 0.18f + 0.016f * skillLevel(SK_FISHING);
                f.barPos = 0.0f;
                f.fishPos = f.fishTarget = 0.25f + frand(rand32()) * 0.3f;
                f.progress = 0.3f;
                f.perfect = true;
                return;
            }
            if (f.t > BITE_WINDOW) {
                say("It got away...");
                f.state = FS_NONE;
            }
            return;
        case FS_REEL: {
            const FishDef& d = data.fish.at(f.fish);
            float diff = d.difficulty / 100.0f;
            // the fish picks new spots; how depends on its motion type
            f.nextMove -= dt;
            if (f.nextMove <= 0) {
                float r = frand(rand32());
                switch (d.motion) {
                    case FM_DART: f.fishTarget = r; f.nextMove = 0.25f + frand(rand32()) * 0.6f; break;
                    case FM_SMOOTH: f.fishTarget = std::max(0.0f, std::min(1.0f, f.fishPos + (r - 0.5f) * 0.4f));
                                    f.nextMove = 0.6f + frand(rand32()) * 0.8f; break;
                    case FM_SINKER: f.fishTarget = r * r; f.nextMove = 0.5f + frand(rand32()); break;
                    case FM_FLOATER: f.fishTarget = 1 - r * r; f.nextMove = 0.5f + frand(rand32()); break;
                    default: f.fishTarget = r; f.nextMove = 0.4f + frand(rand32()) * 1.0f; break;
                }
            }
            float speed = 0.25f + diff * (d.motion == FM_DART ? 1.6f : 1.0f);
            float want = (f.fishTarget - f.fishPos) * 4.0f;
            f.fishVel += (std::max(-speed, std::min(speed, want)) - f.fishVel) * std::min(1.0f, dt * 6);
            f.fishPos = std::max(0.0f, std::min(1.0f, f.fishPos + f.fishVel * dt));
            // your bar: hold A to lift it, let go and it sinks
            f.barVel += (in.hold ? 1.6f : -1.6f) * dt;
            f.barVel = std::max(-0.9f, std::min(0.9f, f.barVel));
            f.barPos += f.barVel * dt;
            float top = 1.0f - f.barSize;
            if (f.barPos < 0) { f.barPos = 0; f.barVel = -f.barVel * 0.3f; }
            if (f.barPos > top) { f.barPos = top; f.barVel = -f.barVel * 0.3f; }
            bool inside = f.fishPos >= f.barPos && f.fishPos <= f.barPos + f.barSize;
            if (inside) f.progress += 0.35f * dt;
            else { f.progress -= (0.12f + diff * 0.2f) * dt; f.perfect = false; }
            if (f.progress >= 1.0f) {
                f.caught = true;
                sfx("jingle1");
                f.size = d.minSize + (int)(frand(rand32()) * (d.maxSize - d.minSize + 1));
                addItem(f.fish, 1);
                fishCaught[f.fish]++;
                orderEvent("fish", f.fish, 1, "");
                questItem(f.fish, 1);
                const ObjectDef* o = data.object(f.fish);
                say("Caught " + (o ? o->name : std::string("a fish")) + "! " + std::to_string(f.size) + " in" +
                    (f.perfect ? "  Perfect!" : ""));
                int xp = 3 + d.difficulty / 3;
                gainXp(SK_FISHING, f.perfect ? xp * 2 : xp);
                f.state = FS_DONE;
                f.t = 0;
            } else if (f.progress <= 0) {
                say("The fish escaped");
                f.caught = false;
                sfx("fishEscape");
                f.state = FS_DONE;
                f.t = 0;
            }
            return;
        }
        case FS_DONE:
            if (f.t >= DONE_TIME || in.action) f.state = FS_NONE;
            return;
        default:
            f.state = FS_NONE;
            return;
    }
}

} // namespace sv
