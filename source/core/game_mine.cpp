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
    if (hasLadder) {   // a rest floor: already has its way down (and maybe a treasure chest)
        bool reward = level == 20 || level == 60 || level == 90 || level == 100;
        if (reward && !mailReceived.count("s3d_minechest_" + std::to_string(level))) {
            int cx = m.width() / 2, cy = m.height() / 2;
            for (int r = 0; r < 10 && loc.chestX < 0; r++)
                for (int dy = -r; dy <= r && loc.chestX < 0; dy++)
                    for (int dx = -r; dx <= r && loc.chestX < 0; dx++) {
                        int x = cx + dx, y = cy + dy;
                        if (m.inBounds(x, y) && m.tile(L_BACK, x, y) != NO_TILE && m.tile(L_BUILDINGS, x, y) == NO_TILE &&
                            m.tile(L_FRONT, x, y) == NO_TILE && !(m.flags(x, y) & CF_BLOCKED)) { loc.chestX = x; loc.chestY = y; }
                    }
        }
        return;
    }

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
            if (level > 120 && roll < std::min(120, 15 + (level - 120) * 2)) {
                o.itemId = 765;                                                       // iridium, more the deeper you go
            } else if (roll < oreChance) {
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
        // who lives at this depth
        int roll = (int)(rand32() % 100);
        bool bug = false;
        if (level < 40) {
            if (level > 1 && roll < 25) { mo.kind = "Bug"; bug = true; mo.ai = 1; }
            else if (level > 30 && roll < 55) { mo.kind = "Bat"; mo.ai = 2; }
            else if (level < 30 && roll < 45) { mo.kind = "Rock Crab"; mo.ai = 4; mo.hidden = true; }
            else mo.kind = "Green Slime";
        } else if (level < 80) {
            if (roll < 30) { mo.kind = "Dust Spirit"; mo.ai = 3; }
            else if (roll < 50) { mo.kind = "Frost Bat"; mo.ai = 2; }
            else mo.kind = "Frost Jelly";
        } else {
            if (roll < 35) { mo.kind = "Lava Bat"; mo.ai = 2; }
            else mo.kind = "Sludge";
        }
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
    if (speedBoost > 0) speedBoost -= dt;
    if (warriorBoost > 0) warriorBoost -= dt;
    if (!cur || cur->monsters.empty() || cutscene.active || dialogue.active) return;
    auto solidAt = [&](float x, float y) { return tileSolid((int)std::floor(x / TILE), (int)std::floor(y / TILE)); };
    for (auto& mo : cur->monsters) {
        mo.anim += dt;
        if (mo.hurt > 0) mo.hurt -= dt;
        float dx = px - mo.x, dy = py - mo.y;
        float dist = std::sqrt(dx * dx + dy * dy);
        if (mo.ai == 2) {   // bats: straight at you through anything, once they notice
            if (dist < 9 * TILE && dist > 1) {
                float sp = mo.kind == "Bat" ? 55.0f : 70.0f;
                mo.vx += (dx / dist * sp - mo.vx) * std::min(1.0f, dt * 2.5f);
                mo.vy += (dy / dist * sp - mo.vy) * std::min(1.0f, dt * 2.5f);
            } else {
                mo.vx *= 0.95f;
                mo.vy *= 0.95f;
            }
            mo.x += (mo.vx + mo.kx) * dt;
            mo.y += (mo.vy + mo.ky) * dt;
            mo.kx *= std::pow(0.02f, dt);
            mo.ky *= std::pow(0.02f, dt);
            if (dist < 12 && invuln <= 0 && mo.hurt <= 0 && !(mo.ai == 0 && wearing("Slime Charmer Ring"))) hurtPlayer(mo.damage, mo.x, mo.y);
            continue;
        }
        if (mo.ai == 4 && mo.hidden) {   // a rock crab: just a rock, until you come too close
            if (dist < 20) { mo.hidden = false; mo.think = 0; }
            else continue;
        }
        if (!mo.bug) {   // slimes hop at you when you're near
            mo.think -= dt;
            if (mo.think <= 0) {
                if (dist < 8 * TILE && dist > 1) {
                    const MonsterDef* d = data.monster(mo.kind);
                    float sp = (22.0f + (d ? d->speed : 2) * 12.0f) * (mo.ai == 3 ? 1.6f : mo.ai == 4 ? 0.6f : 1.0f);
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
        if (dist < 11 && invuln <= 0 && mo.hurt <= 0 && !(mo.ai == 0 && wearing("Slime Charmer Ring"))) hurtPlayer(mo.damage, mo.x, mo.y);
    }
}

void Game::hurtPlayer(int damage, float fromX, float fromY) {
    if (wearing("Yoba's Blessing") && rand32() % 100 < 10) { invuln = 5.0f; sfx("yoba"); return; }
    health -= std::max(1, damage - defense());
    invuln = wearing("Ring of Yoba") && rand32() % 100 < 10 ? 5.0f : 1.2f;
    if (wearing("Thorns Ring")) {   // whoever hit you feels it too
        for (auto& mo : cur->monsters) {
            float ddx = mo.x - fromX, ddy = mo.y - fromY;
            if (ddx * ddx + ddy * ddy < 4) { mo.hp -= std::max(1, damage); mo.hurt = 0.3f; }
        }
    }
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
        if (warriorBoost > 0) dmg += 10;
        if (wearing("Ruby Ring")) dmg += (dmg + 9) / 10;
        if (wearing("Iridium Band")) dmg += (dmg + 9) / 10;
        if (wearing("Aquamarine Ring") || wearing("Lucky Ring") ? rand32() % 100 < 12 : rand32() % 100 < 2) {
            dmg *= wearing("Jade Ring") ? 4 : 3;   // a critical hit
            sfx("crit");
        }
        mo.hp -= std::max(1, dmg);
        mo.hurt = 0.3f;
        mo.hidden = false;
        float kb = wearing("Amethyst Ring") ? 190.0f : 160.0f;
        mo.kx = fx[f] * kb;
        mo.ky = fy[f] * kb;
        if (!mo.bug) mo.think = 0.6f;
        sfx(mo.bug ? "hitEnemy" : "slimeHit");
        if (mo.hp > 0) continue;
        // defeated: a puff and its drops
        const MonsterDef* d = data.monster(mo.kind);
        bool slime = mo.ai == 0;
        sfx(slime ? "slimedead" : "monsterdead");
        if (mo.bug) burst("Characters_Monsters_Bug", 0, 0, 16, 16, mo.x, mo.y - 8, 8);
        else if (slime) burst("Characters_Monsters_Green_Slime", 0, 0, 16, 24, mo.x, mo.y - 10, 8);
        else {
            std::string t = "Characters_Monsters_" + mo.kind;
            for (auto& ch : t) if (ch == ' ') ch = '_';
            burst(t, 16, 0, 16, 24, mo.x, mo.y - 12, 8);
        }
        orderEvent("slay", 0, 1, mo.kind);
        if (wearing("Vampire Ring")) health = std::min(maxHealth, health + 2);
        if (wearing("Soul Sapper Ring")) energy = std::min(MAX_ENERGY, energy + 4);
        if (wearing("Savage Ring")) speedBoost = 3.0f;
        if (wearing("Warrior Ring") && rand32() % 10 == 0) warriorBoost = 5.0f;
        if (d) {
            int rolls = wearing("Burglar's Ring") ? 2 : 1;
            for (int roll = 0; roll < rolls; roll++)
                for (auto& dr : d->drops)
                    if ((rand32() % 1000) / 1000.0f < dr.second && data.object(dr.first) && addItem(dr.first, 1))
                        itemFlies(dr.first, mo.x, mo.y - 8);
            gainXp(SK_COMBAT, d->xp);
        }
        cur->monsters.erase(cur->monsters.begin() + i);
        i--;
    }
}

// Clint cracks the geode in your hand: half the time plain resources, otherwise a mineral.
bool Game::crackGeode() {
    Slot& s = inv[selected];
    if (s.type != SLOT_OBJECT) return false;
    auto it = data.geodes.find(s.id);
    if (it == data.geodes.end()) return false;
    if (money < 25) { say("Opening a geode costs 25g."); return true; }
    const GameData::GeodeDef& gd = it->second;
    int item = -1, n = 1;
    if (gd.basics && (rand32() % 2 || gd.items.empty())) {
        static const int plain[3][5] = {{390, 330, 378, 382, 86}, {390, 330, 380, 382, 84}, {390, 380, 384, 382, 82}};
        int row = s.id == 536 ? 1 : s.id == 537 ? 2 : s.id == 749 ? (int)(rand32() % 3) : 0;
        item = plain[row][rand32() % 5];
        n = item == 390 || item == 330 ? 1 + (int)(rand32() % 5) : item == 382 || item >= 378 ? 1 + (int)(rand32() % 3) : 1;
    } else if (!gd.items.empty()) {
        for (int tries = 0; tries < 8 && (item < 0 || !data.object(item)); tries++) item = gd.items[rand32() % gd.items.size()];
    }
    if (item < 0 || !data.object(item)) return false;
    money -= 25;
    if (--s.count <= 0) s = Slot();
    sfx("hammer");
    if (addItem(item, n)) itemFlies(item, px, py - 24, n);
    say("The geode had " + (n > 1 ? std::to_string(n) + " " : std::string("")) + data.object(item)->name + " inside!");
    return true;
}

void Game::enterMine(int level) {
    if (level <= 0) {
        warpTo("Mine", ENTRANCE_X, ENTRANCE_Y, false);
        return;
    }
    bool skull = level > 120;            // the Skull Cavern: floors past the bottom, from the desert
    if (!skull) level = std::min(level, 120);
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
    orderEvent("floor", 0, level, "");
    say(skull ? "Skull Cavern, floor " + std::to_string(level - 120) : "Floor " + std::to_string(level));
    if (level > mineDeepest && !skull) {
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
            if (!armed) say("Marlon left you a Rusty Sword!");
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
    if (tx == cur->chestX && ty == cur->chestY) {   // a treasure chest
        int lv = cur->mineLevel;
        bool ok = false;
        if (lv == 100) ok = addItem(434, 1);   // a stardrop
        else ok = giveTool(lv == 20 ? "Weapon_11" : lv == 60 ? "Weapon_21" : "Weapon_8");
        if (ok) {
            sfx("openChest");
            mailReceived.insert("s3d_minechest_" + std::to_string(lv));
            cur->chestX = cur->chestY = -1;
        }
        return true;
    }
    uint16_t t = cur->map.tile(L_BUILDINGS, tx, ty);
    if (t == LADDER_DOWN) { enterMine(cur->mineLevel + 1); return true; }
    if (t == LADDER_UP && cur->mineLevel > 120) {
        sfx("stairsdown");
        if (hasMap("SkullCave")) warpTo("SkullCave", 3, 5, false);
        else warpTo("Desert", 8, 7, false);
        return true;
    }
    if (t == LADDER_UP) { sfx("stairsdown"); warpTo("Mine", ENTRANCE_X, ENTRANCE_Y, false); return true; }
    if (t == ELEVATOR && cur->mineLevel > 120) { say("No elevator down here."); return true; }
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
