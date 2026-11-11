// The Mines: floors from Maps/Mines/<n>.tmx, filled with rocks and ore each
// visit; break rocks to find the ladder down. Every 5th floor has the elevator.
#include "game.h"

#include <cmath>
#include <cstdlib>

namespace sv {

static const int TILE = 16;
// tiles on the mine sheet (Buildings layer)
static const uint16_t LADDER_UP = 115, LADDER_DOWN = 173, ELEVATOR = 112;
// the entrance map: the ladder down and the elevator
static const int ENTRANCE_X = 23, ENTRANCE_Y = 10, LIFT_X = 17, LIFT_Y = 4;

int Game::mineLevelOf(const std::string& name) {
    static const char P[] = "UndergroundMine";
    if (name.compare(0, sizeof P - 1, P) != 0 || name.size() == sizeof P - 1) return 0;
    for (size_t i = sizeof P - 1; i < name.size(); i++)
        if (name[i] < '0' || name[i] > '9') return 0;
    return atoi(name.c_str() + sizeof P - 1);
}

// Fill a fresh floor: rocks on open ground, ore by depth.
void Game::mineGenerate(Location& loc) {
    int level = loc.mineLevel;
    TileMap& m = loc.map;
    // deeper floors use the frozen and the lava look (same layout, other sheet)
    const char* look = level >= 80 && level < 120 ? "_lava" : level >= 40 && level < 80 ? "_frost" : nullptr;
    if (look)
        for (auto& ts : m.tilesets)
            if (ts.texture == "Maps_Mines_mine" && data.vfs.exists(std::string("tex/Maps_Mines_mine") + look + ".s3t"))
                ts.texture = std::string("Maps_Mines_mine") + look;

    int ux = -100, uy = -100;
    bool hasLadder = false;
    for (int y = 0; y < m.height(); y++)
        for (int x = 0; x < m.width(); x++) {
            uint16_t t = m.tile(L_BUILDINGS, x, y);
            if (t == LADDER_UP) { ux = x; uy = y; }
            if (t == LADDER_DOWN) hasLadder = true;
        }
    if (hasLadder) return;   // a rest floor: already has its way down

    static const int brown[6] = {32, 34, 36, 38, 40, 42}, ice[4] = {48, 50, 52, 54}, lava[3] = {56, 58, 32};
    loc.objects.clear();
    for (int y = 0; y < m.height(); y++)
        for (int x = 0; x < m.width(); x++) {
            if (m.tile(L_BACK, x, y) == NO_TILE || m.tile(L_BUILDINGS, x, y) != NO_TILE) continue;
            // not under the black Front tiles that hide the rock walls (or behind anything else)
            if (m.tile(L_FRONT, x, y) != NO_TILE || m.tile(L_ALWAYSFRONT, x, y) != NO_TILE) continue;
            if (y + 1 < m.height() && m.tile(L_FRONT, x, y + 1) != NO_TILE && m.tile(L_BUILDINGS, x, y + 1) == NO_TILE &&
                m.tile(L_BACK, x, y + 1) == NO_TILE) continue;
            uint8_t f = m.flags(x, y);
            if ((f & CF_BLOCKED) || (f & CF_WATER)) continue;
            if (std::abs(x - ux) <= 1 && y >= uy && y <= uy + 2) continue;   // keep the way in clear
            uint32_t r = rand32();
            if (r % 100 >= 16) continue;
            WorldObject o;
            o.x = x;
            o.y = y;
            o.ore = true;
            int roll = (int)((r >> 8) % 1000);
            int deep = level % 40;     // ore gets commoner further down each section
            int oreChance = 40 + deep * 2;
            if (roll < oreChance) {
                o.itemId = level >= 80 ? 764 : level >= 40 ? 290 : 751;               // gold / iron / copper
            } else if (roll < oreChance + 20 && level >= 40) {
                o.itemId = level >= 80 ? 290 : 751;
            } else if (roll < oreChance + 35) {
                o.itemId = level >= 80 ? 77 : level >= 40 ? 76 : 75;                  // geode stones
            } else if (roll < oreChance + 40) {
                static const int gems[7] = {2, 4, 6, 8, 10, 12, 14};                  // gem nodes
                o.itemId = gems[(r >> 20) % 7];
            } else {
                o.itemId = level >= 80 ? lava[(r >> 20) % 3] : level >= 40 ? ice[(r >> 20) % 4] : brown[(r >> 20) % 6];
            }
            loc.objects.push_back(o);
        }
    mineSpawnMonsters(loc);
}

// A few monsters per floor: slimes (green up top, frost jelly, red sludge deeper)
// and bugs on the first section.
void Game::mineSpawnMonsters(Location& loc) {
    loc.monsters.clear();
    int level = loc.mineLevel;
    if (level % 5 == 0 || level >= 120) return;   // elevator / rest floors are quiet
    const TileMap& m = loc.map;
    int ux = -100, uy = -100;
    for (int y = 0; y < m.height(); y++)
        for (int x = 0; x < m.width(); x++)
            if (m.tile(L_BUILDINGS, x, y) == LADDER_UP) { ux = x; uy = y; }
    std::vector<std::pair<int, int>> open;
    for (int y = 0; y < m.height(); y++)
        for (int x = 0; x < m.width(); x++) {
            if (m.tile(L_BACK, x, y) == NO_TILE || m.tile(L_BUILDINGS, x, y) != NO_TILE) continue;
            if (m.tile(L_FRONT, x, y) != NO_TILE || (m.flags(x, y) & (CF_BLOCKED | CF_WATER))) continue;
            if (std::abs(x - ux) + std::abs(y - uy) < 7) continue;   // not right where you come in
            bool rock = false;
            for (auto& o : loc.objects) if (o.x == x && o.y == y) { rock = true; break; }
            if (!rock) open.push_back({x, y});
        }
    if (open.empty()) return;
    int n = std::min(12, 2 + (int)open.size() / 60 + (level % 40) / 10);
    for (int i = 0; i < n; i++) {
        auto c = open[rand32() % open.size()];
        Location::Monster mo;
        bool bug = level < 40 && level > 1 && rand32() % 100 < 35;
        mo.kind = bug ? "Bug" : level >= 80 ? "Sludge" : level >= 40 ? "Frost Jelly" : "Green Slime";
        const MonsterDef* d = data.monster(mo.kind);
        mo.hp = d ? d->health : 20;
        mo.damage = d ? d->damage : 5;
        mo.bug = bug;
        mo.tint = mo.kind == "Sludge" ? 0xE0483CFFu : mo.kind == "Frost Jelly" ? 0x64C8FFFFu : 0x60E048FFu;
        mo.x = c.first * TILE + 8.0f;
        mo.y = c.second * TILE + 14.0f;
        mo.think = (rand32() % 100) / 100.0f;
        if (bug) {
            static const float dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
            int k = rand32() % 4;
            mo.vx = dirs[k][0] * 40;
            mo.vy = dirs[k][1] * 40;
        }
        loc.monsters.push_back(mo);
    }
}

void Game::monstersUpdate(float dt) {
    if (invuln > 0) invuln -= dt;
    if (!cur || cur->monsters.empty() || cutscene.active || dialogue.active) return;
    auto solidAt = [&](float x, float y) { return tileSolid((int)std::floor(x / TILE), (int)std::floor(y / TILE)); };
    for (auto& mo : cur->monsters) {
        mo.anim += dt;
        if (mo.hurt > 0) mo.hurt -= dt;
        float dx = px - mo.x, dy = py - mo.y;
        float dist = std::sqrt(dx * dx + dy * dy);
        if (!mo.bug) {   // slimes hop at you when you're near
            mo.think -= dt;
            if (mo.think <= 0) {
                if (dist < 8 * TILE && dist > 1) {
                    const MonsterDef* d = data.monster(mo.kind);
                    float sp = 22.0f + (d ? d->speed : 2) * 12.0f;
                    mo.vx = dx / dist * sp;
                    mo.vy = dy / dist * sp;
                } else {
                    float a = (rand32() % 628) / 100.0f;
                    mo.vx = std::cos(a) * 15;
                    mo.vy = std::sin(a) * 15;
                }
                mo.think = 0.9f + (rand32() % 60) / 100.0f;
            } else if (mo.think < 0.4f) {
                mo.vx *= 0.85f;   // settling between hops
                mo.vy *= 0.85f;
            }
        }
        float mx = (mo.vx + mo.kx) * dt, my = (mo.vy + mo.ky) * dt;
        mo.kx *= std::pow(0.02f, dt);
        mo.ky *= std::pow(0.02f, dt);
        if (!solidAt(mo.x + mx + (mx > 0 ? 5 : -5), mo.y - 3)) mo.x += mx;
        else { if (mo.bug) mo.vx = -mo.vx; mo.kx = 0; }
        if (!solidAt(mo.x, mo.y + my + (my > 0 ? 1 : -6))) mo.y += my;
        else { if (mo.bug) mo.vy = -mo.vy; mo.ky = 0; }
        if (dist < 11 && invuln <= 0 && mo.hurt <= 0) hurtPlayer(mo.damage, mo.x, mo.y);
    }
}

void Game::hurtPlayer(int damage, float fromX, float fromY) {
    health -= std::max(1, damage);
    invuln = 1.2f;
    sfx("ow");
    float dx = px - fromX, dy = py - fromY, d = std::sqrt(dx * dx + dy * dy);
    if (d > 0.1f)
        for (int k = 0; k < 12; k++) {   // shoved back, a little at a time so walls stop it
            float nx = px + dx / d * 2, ny = py + dy / d * 2;
            if (blockedPx(nx, ny)) break;
            px = nx;
            py = ny;
        }
    if (health <= 0) {
        health = maxHealth / 2;
        energy = std::max(energy, 20);
        say("You were knocked out... and woke up at the mine entrance.");
        enterMine(0);
    }
}

void Game::swingWeapon(const ToolDef& w) {
    sfx("swordswipe");
    if (!cur) return;
    static const int fx[4] = {0, 1, 0, -1}, fy[4] = {1, 0, -1, 0};
    int f = facing & 3;
    float hx = px + fx[f] * 14.0f, hy = py - 8 + fy[f] * 14.0f;
    for (size_t i = 0; i < cur->monsters.size(); i++) {
        Location::Monster& mo = cur->monsters[i];
        float dx = mo.x - hx, dy = (mo.y - 8) - hy;
        if (dx * dx + dy * dy > 20.0f * 20.0f || mo.hurt > 0.15f) continue;
        int dmg = w.minDamage + (int)(rand32() % (uint32_t)std::max(1, w.maxDamage - w.minDamage + 1));
        mo.hp -= std::max(1, dmg);
        mo.hurt = 0.3f;
        mo.kx = fx[f] * 160.0f;
        mo.ky = fy[f] * 160.0f;
        if (!mo.bug) mo.think = 0.6f;
        sfx(mo.bug ? "hitEnemy" : "slimeHit");
        if (mo.hp > 0) continue;
        // defeated: a puff and its drops
        const MonsterDef* d = data.monster(mo.kind);
        sfx(mo.bug ? "monsterdead" : "slimedead");
        if (mo.bug) burst("Characters_Monsters_Bug", 0, 0, 16, 16, mo.x, mo.y - 8, 8);
        else burst("Characters_Monsters_Green_Slime", 0, 0, 16, 24, mo.x, mo.y - 10, 8);
        if (d) {
            for (auto& dr : d->drops)
                if ((rand32() % 1000) / 1000.0f < dr.second && data.object(dr.first) && addItem(dr.first, 1))
                    itemFlies(dr.first, mo.x, mo.y - 8);
            gainXp(SK_COMBAT, d->xp);
        }
        cur->monsters.erase(cur->monsters.begin() + i);
        i--;
    }
}

void Game::enterMine(int level) {
    if (level <= 0) {
        warpTo("Mine", ENTRANCE_X, ENTRANCE_Y, false);
        return;
    }
    level = std::min(level, 120);
    std::string name = "UndergroundMine" + std::to_string(level);
    Location* loc = loadLocation(name);
    if (!loc) {
        say("The mine floors weren't converted (re-run the converter).");
        return;
    }
    int sx = loc->map.width() / 2, sy = loc->map.height() / 2;
    for (int y = 0; y < loc->map.height(); y++)
        for (int x = 0; x < loc->map.width(); x++)
            if (loc->map.tile(L_BUILDINGS, x, y) == LADDER_UP) { sx = x; sy = y + 1; }
    sfx("stairsdown");
    warpTo(name, sx, sy, false);
    say("Floor " + std::to_string(level));
    if (level > mineDeepest) {
        mineDeepest = level;
        if (level % 5 == 0) say("Elevator unlocked: floor " + std::to_string(level));
    }
    if (!eventsSeen.count("s3d_rustySword")) {   // Marlon's welcome: a sword the first time down
        bool armed = false;
        for (auto& sl : inv)
            if (sl.type == SLOT_TOOL)
                if (const ToolDef* t = data.tool(sl.id)) armed |= t->kind == TOOL_WEAPON;
        if (armed || (data.toolByKey("Weapon_0") >= 0 && giveTool("Weapon_0"))) {
            eventsSeen.insert("s3d_rustySword");
            if (!armed) say("Marlon left you a Rusty Sword: there are monsters down here!");
        }
    }
}

void Game::openElevator() {
    int top = (mineDeepest / 5) * 5;
    int here = cur ? cur->mineLevel : 0;
    DialoguePage p;
    p.text = "Which floor?";
    DialogueChoice c;
    if (here > 0) { c.label = "Back to the top"; c.responseKey = "mine:0"; p.choices.push_back(c); }
    if (top >= 5 && top != here) { c.label = "Floor " + std::to_string(top); c.responseKey = "mine:" + std::to_string(top); p.choices.push_back(c); }
    if (here == 0) { c.label = "Floor 1"; c.responseKey = "mine:1"; p.choices.push_back(c); }
    c.label = "Cancel";
    c.responseKey = "";
    p.choices.push_back(c);
    dialogue = DialogueBox();
    dialogue.active = true;
    dialogue.pages.push_back(p);
    pendingChoice = "elevator";
}

// A for the ladders and the elevator inside the mine.
bool Game::mineInteract(int tx, int ty) {
    if (!cur || !cur->mineLevel || !cur->map.inBounds(tx, ty)) return false;
    uint16_t t = cur->map.tile(L_BUILDINGS, tx, ty);
    if (t == LADDER_DOWN) { enterMine(cur->mineLevel + 1); return true; }
    if (t == LADDER_UP) { sfx("stairsdown"); warpTo("Mine", ENTRANCE_X, ENTRANCE_Y, false); return true; }
    if (t == ELEVATOR) { openElevator(); return true; }
    return false;
}

// After a rock breaks in the mine: maybe coal, maybe the ladder down.
void Game::mineRockBroken(int tx, int ty, int node) {
    if (!cur || !cur->mineLevel) return;
    bool plain = oreNodeDrop(node) == 390 && node != 75 && node != 76 && node != 77;
    if (plain && rand32() % 100 < 5 && addItem(382, 1)) itemFlies(382, tx * TILE + 8.0f, ty * TILE + 8.0f);   // coal
    int left = 0;
    for (auto& o : cur->objects) left += o.ore;
    bool ladder = false;
    for (int y = 0; y < cur->map.height() && !ladder; y++)
        for (int x = 0; x < cur->map.width(); x++)
            if (cur->map.tile(L_BUILDINGS, x, y) == LADDER_DOWN) { ladder = true; break; }
    if (ladder || cur->mineLevel >= 120) return;
    float chance = 0.04f + 1.5f / std::max(1, left + 1);
    if ((rand32() % 10000) / 10000.0f < chance) {
        cur->map.setTile(L_BUILDINGS, tx, ty, LADDER_DOWN, cur->map.tileset(L_BACK, tx, ty));
        sfx("hammer");
        say("A ladder down!");
    }
}

} // namespace sv
