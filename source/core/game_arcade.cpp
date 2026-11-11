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
    arcade.timer = which == 1 ? 40.0f : 0.0f;
    arcade.cartY = 0;
    if (which == 2) {            // lay the track: runs of rail with gaps, a little higher or lower each time
        int h = 3;
        arcade.track.assign(12, h);
        while (arcade.track.size() < 400) {
            int run = 4 + (int)(rand32() % 7), gap = 2 + (int)(rand32() % 2);
            h = std::max(1, std::min(6, h + (int)(rand32() % 3) - 1));
            for (int i = 0; i < run; i++) arcade.track.push_back(h);
            for (int i = 0; i < gap; i++) arcade.track.push_back(0);
        }
        for (int i = 0; i < 14; i++) arcade.track.push_back(3);
        arcade.cartY = 3 * 16.0f;
    }
    sfx("coin");
}

void Game::arcadeEnd(bool won) {
    int which = arcade.game, score = arcade.score;
    arcade = Arcade();
    if (which == 1) {
        if (won) mailReceived.insert("Beat_PK");
        say(won ? "You beat Journey of the Prairie King! Score " + std::to_string(score) : "Game over. Score " + std::to_string(score));
    } else {
        if (won) mailReceived.insert("JunimoKart");
        junimoBest = std::max(junimoBest, score);
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
        if (len > 0.2f) {
            a.px = std::max(8.0f, std::min(184.0f, a.px + in.mx / std::max(1.0f, len) * 70.0f * dt));
            a.py = std::max(8.0f, std::min(184.0f, a.py + in.my / std::max(1.0f, len) * 70.0f * dt));
        }
        // shoot
        a.cool -= dt;
        if ((in.sx || in.sy) && a.cool <= 0) {
            Arcade::Shot s;
            s.x = a.px; s.y = a.py; s.vx = in.sx * 190.0f; s.vy = in.sy * 190.0f;
            a.shots.push_back(s);
            a.cool = 0.22f;
            sfx("Cowboy_gunshot");
        }
        for (auto& s : a.shots) { s.x += s.vx * dt; s.y += s.vy * dt; }
        a.shots.erase(std::remove_if(a.shots.begin(), a.shots.end(), [](const Arcade::Shot& s) { return s.x < 0 || s.x > 192 || s.y < 0 || s.y > 192; }), a.shots.end());
        // outlaws pour in from the four gates, faster each stage
        a.spawn -= dt;
        if (a.spawn <= 0 && a.timer > 2) {
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
                    if (--f.hp <= 0) { a.score += f.kind ? 30 : 10; sfx("Cowboy_monsterDie"); }
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
            if (a.stage >= 3) { a.score += 500; arcadeEnd(true); return; }
            a.stage++;
            a.timer = 40.0f;
            a.pause = 1.5f;
            a.score += 100;
            sfx("cowboy_powerup");
        }
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
        if (cell >= (int)a.track.size() - 6) { a.score += 500; arcadeEnd(true); return; }
    }
}

} // namespace sv
