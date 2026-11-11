// Tool upgrades at the blacksmith and charged hoe / watering can.
#include <algorithm>
#include <cctype>

#include "game.h"

namespace sv {

static const int UPGRADE_DAYS = 2;
static const float CHARGE_STEP = 0.6f;   // seconds per charge level

static int barForLevel(int level) {
    switch (level) {
        case 1: return 334;   // copper bar
        case 2: return 335;   // iron bar
        case 3: return 336;   // gold bar
        case 4: return 337;   // iridium bar
        default: return -1;
    }
}

std::vector<UpgradeOffer> Game::upgradeOffers() const {
    std::vector<UpgradeOffer> out;
    for (int i = 0; i < INV_SIZE; i++) {
        if (inv[i].type != SLOT_TOOL) continue;
        int next = data.toolUpgrade(inv[i].id);
        if (next < 0) continue;
        const ToolDef* t = data.tool(next);
        UpgradeOffer o;
        o.slot = i;
        o.next = next;
        o.price = t->price > 0 ? t->price : 2000 * t->upgrade;
        o.bar = barForLevel(t->upgrade);
        out.push_back(o);
    }
    return out;
}

bool Game::startUpgrade(const UpgradeOffer& o) {
    if (upgradeTool >= 0) {
        say("Clint is already working on a tool");
        return false;
    }
    if (o.slot < 0 || o.slot >= INV_SIZE || inv[o.slot].type != SLOT_TOOL ||
        data.toolUpgrade(inv[o.slot].id) != o.next)
        return false;
    if (money < o.price) {
        say("Not enough money");
        return false;
    }
    if (o.bar >= 0 && countOf(Ingredient{o.bar, o.bars, false}) < o.bars) {
        const ObjectDef* b = data.object(o.bar);
        say("Needs " + std::to_string(o.bars) + " " + (b ? b->name : std::string("bars")));
        return false;
    }
    money -= o.price;
    int need = o.bar >= 0 ? o.bars : 0;
    for (auto& s : inv)
        if (need && s.type == SLOT_OBJECT && s.id == o.bar) {
            int t = std::min(need, s.count);
            s.count -= t;
            need -= t;
            if (!s.count) s = Slot();
        }
    upgradeTool = o.next;
    upgradeReadyDay = absoluteDay() + UPGRADE_DAYS;
    inv[o.slot] = Slot();
    if (selected == o.slot) charging = false;
    say("Come back in " + std::to_string(UPGRADE_DAYS) + " days");
    return true;
}

bool Game::giveTool(const std::string& key) {
    for (size_t i = 0; i < data.tools.size(); i++)
        if (data.tools[i].key == key) {
            Slot t;
            t.type = SLOT_TOOL;
            t.id = (int)i;
            t.count = 1;
            if (!addSlot(t)) { say("No room for the " + data.tools[i].name); return false; }
            say("Got the " + data.tools[i].name + "!");
            return true;
        }
    return false;
}

void Game::awardPrize(const std::string& what) {
    if (what == "rod") {                 // Willy's old rod
        if (!giveTool("BambooPole")) {
            if (const ToolDef* t = data.toolByKind(TOOL_ROD)) giveTool(t->key);
        }
    } else if (what == "pan") {
        giveTool("Pan");
    } else if (!what.empty() && isdigit((unsigned char)what[0])) {
        addItem(atoi(what.c_str()), 1);
    }
}

bool Game::collectUpgrade() {
    if (upgradeTool < 0 || absoluteDay() < upgradeReadyDay) return false;
    Slot t;
    t.type = SLOT_TOOL;
    t.id = upgradeTool;
    t.count = 1;
    if (!addSlot(t)) return false;
    const ToolDef* d = data.tool(upgradeTool);
    say("Got your " + (d ? d->name : std::string("tool")) + " back!");
    upgradeTool = -1;
    return true;
}

int Game::toolLevel(ToolKind k) const {
    int best = -1;
    for (auto& s : inv)
        if (s.type == SLOT_TOOL) {
            const ToolDef* t = data.tool(s.id);
            if (t && t->kind == k) best = std::max(best, t->upgrade);
        }
    return best;
}

int Game::canCapacity() const { return CAN_CAPACITY + 15 * std::max(0, toolLevel(TOOL_CAN)); }

int Game::chargeLevel() const {
    if (!charging) return 0;
    const Slot& s = inv[selected];
    const ToolDef* t = s.type == SLOT_TOOL ? data.tool(s.id) : nullptr;
    int max = t ? t->upgrade : 0;
    return std::min(max, (int)(chargeTime / CHARGE_STEP));
}

// Tiles hit by a charged swing: a line ahead, then wider areas.
void Game::chargedTiles(int level, std::vector<std::pair<int, int>>& out) const {
    out.clear();
    int tx, ty;
    targetTile(tx, ty);
    int fx = 0, fy = 0;
    switch (facing) {
        case FACE_DOWN: fy = 1; break;
        case FACE_UP: fy = -1; break;
        case FACE_LEFT: fx = -1; break;
        default: fx = 1; break;
    }
    int len = 1, width = 1;
    switch (level) {
        case 1: len = 3; break;
        case 2: len = 5; break;
        case 3: len = 3; width = 3; break;
        case 4: len = 6; width = 3; break;
        default: break;
    }
    for (int w = -(width / 2); w <= width / 2; w++)
        for (int i = 0; i < len; i++)
            out.push_back({tx + fx * i + (fy ? w : 0), ty + fy * i + (fx ? w : 0)});
}

} // namespace sv
