// The Saloon's arcade machines, played on the top screen.
//   Journey of the Prairie King: circle pad to move, A/B/X/Y to shoot right/down/up/left. Three stages.
//   Junimo Kart: A to jump the gaps (hold for a higher hop). Reach the end of the track.
// L leaves either one.
#include "game.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace sv {

void Game::arcadeStart(int which) {
    arcade = Arcade();
    arcade.game = which;
    arcade.lives = 3;
    arcade.px = 96; arcade.py = 96;
    arcade.stage = 1;
    arcade.timer = which == 1 ? 40.0f : which == 3 ? 30.0f : which == 4 ? 40.0f : 0.0f;
    if (which == 4) arcade.py = 60;
    arcade.cartY = 0;
    if (which == 2) kartTrack();
    sfx("coin");
}

// Lay a Junimo Kart track: runs of rail with gaps, a little higher or lower each time. Later tracks are
// longer, with shorter runs and wider gaps.
void Game::kartTrack() {
    int h = 3, st = arcade.stage;
    arcade.track.assign(12, h);
    arcade.dist = 0;
    arcade.vy = 0;
    while ((int)arcade.track.size() < 260 + st * 70) {
        int run = std::max(3, 5 - st + (int)(rand32() % (8 - st))), gap = 2 + (int)(rand32() % (st >= 3 ? 3 : 2));
        h = std::max(1, std::min(6, h + (int)(rand32() % 3) - 1));
        for (int i = 0; i < run; i++) arcade.track.push_back(h);
        for (int i = 0; i < gap; i++) arcade.track.push_back(0);
    }
    for (int i = 0; i < 14; i++) arcade.track.push_back(3);
    arcade.cartY = 3 * 16.0f;
}

void Game::arcadeEnd(bool won) {
    int which = arcade.game, score = arcade.score;
    arcade = Arcade();
    if (which == 3 || which == 4) {                      // the fair's booths pay in star tokens
        int tokens = which == 3 ? score * 6 : score * 10;
        starTokens += tokens;
        sfx("reward");
        showMessage(std::string(which == 3 ? "You hit " : "You caught ") + std::to_string(score) + (which == 3 ? " targets" : " fish") + ": " +
                    std::to_string(tokens) + " star tokens!");
        return;
    }
    if (which == 1) {
        if (won) mailReceived.insert("Beat_PK");
        say(won ? "You beat Journey of the Prairie King! Score " + std::to_string(score) : "Game over. Score " + std::to_string(score));
    } else {
        if (won) mailReceived.insert("JunimoKart");
        junimoBest = std::max(junimoBest, score);
        orderEvent("jk", 0, score, "");
        say((won ? "You reached the end of the line! " : "Crash! ") + std::string("Score ") + std::to_string(score));
    }
}

void Game::arcadeUpdate(float dt, const Input& in) {
    Arcade& a = arcade;
    if (in.prev) { arcadeEnd(false); return; }
    a.t += dt;
    if (a.game == 1) {
        if (a.pause > 0) { a.pause -= dt; return; }
        // move
        float len = std::sqrt(in.mx * in.mx + in.my * in.my);
        float walk = a.fast > 0 ? 110.0f : 70.0f;
        if (len > 0.2f) {
            a.px = std::max(8.0f, std::min(184.0f, a.px + in.mx / std::max(1.0f, len) * walk * dt));
            a.py = std::max(8.0f, std::min(184.0f, a.py + in.my / std::max(1.0f, len) * walk * dt));
        }
        if (a.rapid > 0) a.rapid -= dt;
        if (a.spread > 0) a.spread -= dt;
        if (a.fast > 0) a.fast -= dt;
        // shoot
        a.cool -= dt;
        if ((in.sx || in.sy) && a.cool <= 0) {
            Arcade::Shot s;
            s.x = a.px; s.y = a.py; s.vx = in.sx * 190.0f; s.vy = in.sy * 190.0f;
            a.shots.push_back(s);
            if (a.spread > 0)                               // two more, fanned out either side
                for (int k = -1; k <= 1; k += 2) {
                    Arcade::Shot e = s;
                    if (in.sx) e.vy += k * 60.0f; else e.vx += k * 60.0f;
                    a.shots.push_back(e);
                }
            a.cool = a.rapid > 0 ? 0.09f : 0.22f;
            sfx("Cowboy_gunshot");
        }
        for (auto& s : a.shots) { s.x += s.vx * dt; s.y += s.vy * dt; }
        a.shots.erase(std::remove_if(a.shots.begin(), a.shots.end(), [](const Arcade::Shot& s) { return s.x < 0 || s.x > 192 || s.y < 0 || s.y > 192; }), a.shots.end());
        // outlaws pour in from the four gates, faster each stage
        a.spawn -= dt;
        if (a.spawn <= 0 && a.timer > 2 && !a.boss) {
            Arcade::Foe f;
            int side = (int)(rand32() % 4);
            float along = 80.0f + (float)(rand32() % 33);
            f.x = side == 0 ? 4.0f : side == 1 ? 188.0f : along;
            f.y = side == 2 ? 4.0f : side == 3 ? 188.0f : along;
            f.kind = a.stage >= 2 && rand32() % 4 == 0 ? 1 : 0;
            f.hp = f.kind ? 3 : 1;
            a.foes.push_back(f);
            a.spawn = std::max(0.35f, 1.3f - a.stage * 0.25f - (40.0f - a.timer) * 0.01f);
        }
        for (auto& f : a.foes) {
            float dx = a.px - f.x, dy = a.py - f.y, d = std::sqrt(dx * dx + dy * dy) + 0.01f;
            float sp = (f.kind ? 20.0f : 30.0f) + a.stage * 4.0f;
            f.x += dx / d * sp * dt;
            f.y += dy / d * sp * dt;
        }
        for (auto& s : a.shots)
            for (auto& f : a.foes)
                if (f.hp > 0 && s.x > -500 && std::fabs(s.x - f.x) < 9 && std::fabs(s.y - f.y) < 9) {
                    s.x = -1000;
                    if (--f.hp <= 0) {
                        a.score += f.kind ? 30 : 10;
                        sfx("Cowboy_monsterDie");
                        if (rand32() % 100 < 9) {           // sometimes they drop something
                            static const int KINDS[] = {0, 0, 0, 1, 1, 2, 2, 4, 4, 5, 3};
                            a.drops.push_back({f.x, f.y, KINDS[rand32() % 11], 9.0f});
                        }
                    }
                }
        for (auto& d : a.drops) {
            d.life -= dt;
            if (d.life > 0 && std::fabs(d.x - a.px) < 10 && std::fabs(d.y - a.py) < 10) {
                d.life = 0;
                sfx("cowboy_powerup");
                if (d.kind == 0) a.score += 50;
                else if (d.kind == 1) a.rapid = 10.0f;
                else if (d.kind == 2) a.spread = 10.0f;
                else if (d.kind == 3) a.lives = std::min(5, a.lives + 1);
                else if (d.kind == 4) a.fast = 10.0f;
                else { for (auto& f : a.foes) { f.hp = 0; a.score += 10; } }
            }
        }
        a.drops.erase(std::remove_if(a.drops.begin(), a.drops.end(), [](const Arcade::Drop& d) { return d.life <= 0; }), a.drops.end());
        if (a.boss) {                                       // he paces the top of the field and fires down at you
            a.bx += a.bvx * dt;
            if (a.bx < 20 || a.bx > 172) { a.bvx = -a.bvx; a.bx = std::max(20.0f, std::min(172.0f, a.bx)); }
            a.bcool -= dt;
            if (a.bcool <= 0) {
                float dx = a.px - a.bx, dy = a.py - a.by, d = std::sqrt(dx * dx + dy * dy) + 0.01f;
                a.bossShots.push_back({a.bx, a.by + 8, dx / d * 110.0f, dy / d * 110.0f});
                a.bcool = a.bhp < 20 ? 0.45f : 0.8f;
                sfx("Cowboy_gunshot");
            }
            for (auto& s : a.shots)
                if (s.x > -500 && std::fabs(s.x - a.bx) < 14 && std::fabs(s.y - a.by) < 14) {
                    s.x = -1000;
                    if (--a.bhp <= 0) { a.score += 1000; sfx("Cowboy_monsterDie"); arcadeEnd(true); return; }
                }
            for (auto& s : a.bossShots) { s.x += s.vx * dt; s.y += s.vy * dt; }
            a.bossShots.erase(std::remove_if(a.bossShots.begin(), a.bossShots.end(), [](const Arcade::Shot& s) { return s.x < 0 || s.x > 192 || s.y < 0 || s.y > 192; }), a.bossShots.end());
            bool hit = std::fabs(a.bx - a.px) < 14 && std::fabs(a.by - a.py) < 14;
            for (auto& s : a.bossShots) hit |= std::fabs(s.x - a.px) < 6 && std::fabs(s.y - a.py) < 6;
            if (hit) {
                sfx("cowboy_dead");
                a.bossShots.clear();
                a.shots.clear();
                a.px = 96; a.py = 150;
                a.pause = 1.2f;
                if (--a.lives <= 0) { arcadeEnd(false); return; }
            }
            return;
        }
        a.foes.erase(std::remove_if(a.foes.begin(), a.foes.end(), [](const Arcade::Foe& f) { return f.hp <= 0; }), a.foes.end());
        for (auto& f : a.foes)
            if (std::fabs(f.x - a.px) < 9 && std::fabs(f.y - a.py) < 9) {   // caught
                sfx("cowboy_dead");
                a.foes.clear();
                a.shots.clear();
                a.px = a.py = 96;
                a.pause = 1.2f;
                if (--a.lives <= 0) { arcadeEnd(false); return; }
                break;
            }
        a.timer -= dt;
        if (a.timer <= 0 && a.foes.empty()) {
            if (a.stage >= 3) {                         // the last stage ends with the outlaw himself
                a.boss = true;
                a.bhp = 45;
                a.px = 96; a.py = 150;
                a.pause = 1.5f;
                a.score += 100;
                a.drops.clear();
                sfx("cowboy_powerup");
                return;
            }
            a.stage++;
            a.timer = 40.0f;
            a.pause = 1.5f;
            a.score += 100;
            sfx("cowboy_powerup");
        }
    } else if (a.game == 3 || a.game == 4) {
        a.timer -= dt;
        if (a.timer <= 0) { arcadeEnd(true); return; }
        a.cool -= dt;
        a.spawn -= dt;
        bool sling = a.game == 3;
        if (sling) {                                        // move the crosshair, A lets fly
            a.px = std::max(6.0f, std::min(186.0f, a.px + in.mx * 130.0f * dt));
            a.py = std::max(6.0f, std::min(170.0f, a.py + in.my * 130.0f * dt));
        } else {                                            // raise and lower the hook
            a.px = 96;
            a.py = std::max(40.0f, std::min(180.0f, a.py + in.my * 110.0f * dt));
        }
        if (a.spawn <= 0) {
            bool fromLeft = rand32() & 1;
            Arcade::Foe f;
            f.x = fromLeft ? -10.0f : 202.0f;
            f.y = sling ? 16.0f + (float)(rand32() % 130) : 50.0f + (float)(rand32() % 125);
            f.kind = (int)(rand32() % 4);
            f.hp = 1;
            f.vx = (fromLeft ? 1.0f : -1.0f) * (sling ? 55.0f + (float)(rand32() % 60) : 35.0f + (float)(rand32() % 50));
            a.foes.push_back(f);
            a.spawn = sling ? 0.55f : 0.7f;
        }
        for (auto& f : a.foes) f.x += f.vx * dt;
        if (in.action && a.cool <= 0) {
            a.cool = sling ? 0.3f : 0.45f;
            sfx(sling ? "slingshot" : "fishSlap");
            Arcade::Foe* best = nullptr;
            float bestD = sling ? 13.0f : 12.0f;
            for (auto& f : a.foes) {
                float d = std::max(std::fabs(f.x - a.px), std::fabs(f.y - a.py));
                if (f.hp > 0 && d < bestD) { bestD = d; best = &f; }
            }
            if (best) { best->hp = 0; a.score++; sfx("coin"); }
        }
        a.foes.erase(std::remove_if(a.foes.begin(), a.foes.end(), [](const Arcade::Foe& f) { return f.hp <= 0 || f.x < -14 || f.x > 206; }), a.foes.end());
    } else if (a.game == 2) {
        float speed = 70.0f + std::min(60.0f, a.dist / 40.0f);
        a.dist += speed * dt;
        int cell = (int)((a.dist + 40.0f) / 16.0f);
        int h = cell >= 0 && cell < (int)a.track.size() ? a.track[cell] : 3;
        float ground = h * 16.0f;
        bool onRail = h > 0 && a.cartY <= ground + 0.5f && a.vy <= 0;
        if (onRail) {
            if (a.cartY < ground - 6.0f) { arcadeEnd(false); return; }   // ran into the end of a higher rail
            a.cartY = ground;
            a.vy = 0;
            if (in.action) { a.vy = 150.0f; sfx("pickUpItem"); }
        } else {
            a.vy -= ((in.hold && a.vy > 0) ? 260.0f : 420.0f) * dt;   // holding A floats the hop
            a.cartY += a.vy * dt;
            if (h > 0 && a.vy <= 0 && a.cartY <= ground && a.cartY > ground - 8.0f) { a.cartY = ground; a.vy = 0; }
        }
        a.score = (int)(a.dist / 4.0f);
        if (a.cartY < -30.0f) { arcadeEnd(false); return; }               // down the hole
        a.score = (a.stage - 1) * 15000 + (int)(a.dist * 3.0f);
        if (cell >= (int)a.track.size() - 6) {
            if (a.stage >= 3) { a.score += 500; arcadeEnd(true); return; }
            a.stage++;                                      // on to the next track
            kartTrack();
            a.pause = 0;
            sfx("cowboy_powerup");
        }
    }
}

} // namespace sv
