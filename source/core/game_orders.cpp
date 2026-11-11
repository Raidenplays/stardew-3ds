// Special orders: bigger jobs the townsfolk post on the board by Pierre's (from Fall 2 of your first
// year). Two are on offer each week; take one, do what it asks before time runs out, get paid.
#include "game.h"

#include <algorithm>
#include <cstdlib>

namespace sv {

static std::vector<std::string> splitBy(const std::string& l, char sep) {
    std::vector<std::string> f;
    size_t i = 0;
    while (true) {
        size_t j = l.find(sep, i);
        f.push_back(l.substr(i, j == std::string::npos ? std::string::npos : j - i));
        if (j == std::string::npos) break;
        i = j + 1;
    }
    return f;
}

void Game::loadOrders() {
    if (ordersLoaded) return;
    ordersLoaded = true;
    std::vector<std::string> lines;
    if (!data.vfs.readTable("data/specialorders.dat", lines)) return;
    for (auto& l : lines) {
        auto f = splitBy(l, '\t');
        if (f.size() < 10) continue;
        OrderDef d;
        d.key = f[0]; d.requester = f[1]; d.name = f[2]; d.text = f[3];
        d.days = atoi(f[4].c_str()); d.money = atoi(f[5].c_str()); d.season = atoi(f[6].c_str());
        d.repeatable = f[9] == "1";
        auto objs = splitBy(f[7], '|'), texts = splitBy(f[8], '|');
        for (size_t i = 0; i < objs.size(); i++) {
            auto p = splitBy(objs[i], ':');
            if (p.size() < 4) continue;
            OrderDef::Objective o;
            o.kind = p[0]; o.count = std::max(1, atoi(p[1].c_str())); o.target = p[2];
            for (auto& id : splitBy(p[3], ','))
                if (!id.empty()) o.items.insert(atoi(id.c_str()));
            o.text = i < texts.size() ? texts[i] : "";
            d.objectives.push_back(o);
        }
        if (!d.objectives.empty()) orderDefs.push_back(d);
    }
}

const Game::OrderDef* Game::orderDef(const std::string& key) {
    loadOrders();
    for (auto& d : orderDefs) if (d.key == key) return &d;
    return nullptr;
}

bool Game::ordersUnlocked() const { return absoluteDay() >= 58; }   // Fall 2, year 1

// This week's two offers: the same all week, different next week.
std::vector<const Game::OrderDef*> Game::ordersOffered() {
    loadOrders();
    std::vector<const OrderDef*> pool, out;
    std::set<std::string> seen;
    for (auto& d : orderDefs) {
        std::string base = d.key.substr(0, d.key.find('#'));
        if (d.season >= 0 && d.season != season) continue;
        if (!d.repeatable && mailReceived.count("so:" + base)) continue;
        pool.push_back(&d);
    }
    uint32_t r = (uint32_t)(absoluteDay() / 7) * 2654435761u + 12345u;
    for (int k = 0; k < 2 && !pool.empty(); k++) {
        r = r * 1664525u + 1013904223u;
        size_t at = (r >> 8) % pool.size();
        std::string base = pool[at]->key.substr(0, pool[at]->key.find('#'));
        out.push_back(pool[at]);
        pool.erase(std::remove_if(pool.begin(), pool.end(), [&](const OrderDef* d) { return d->key.substr(0, d->key.find('#')) == base; }), pool.end());
    }
    return out;
}

std::string Game::orderStatus() {
    const OrderDef* d = orderDef(orderKey);
    if (!d) return "";
    std::string s = d->name + " (" + std::to_string(orderDays) + (orderDays == 1 ? " day left)" : " days left)");
    for (size_t i = 0; i < d->objectives.size(); i++) {
        int have = i < orderProgress.size() ? orderProgress[i] : 0;
        s += "#$b#" + d->objectives[i].text + " " + std::to_string(std::min(have, d->objectives[i].count)) + "/" + std::to_string(d->objectives[i].count);
    }
    return s;
}

// The board: your current order (handing over anything it wants that you're carrying), or this week's offers.
void Game::ordersBoard() {
    if (!ordersUnlocked()) { say("Mayor Lewis is planning a board for bigger jobs here. Come back in the fall."); return; }
    if (!orderKey.empty()) {
        const OrderDef* d = orderDef(orderKey);
        if (!d) { orderKey.clear(); return; }
        orderProgress.resize(d->objectives.size());
        for (size_t i = 0; i < d->objectives.size(); i++) {      // drop off what you've brought
            const auto& o = d->objectives[i];
            if (o.kind != "donate" && o.kind != "collect") continue;
            for (int s = 0; s < invSlots && orderProgress[i] < o.count; s++) {
                Slot& sl = inv[s];
                if (sl.type != SLOT_OBJECT || !o.items.count(sl.id)) continue;
                int take = std::min(sl.count, o.count - orderProgress[i]);
                sl.count -= take;
                orderProgress[i] += take;
                if (!sl.count) sl = Slot();
            }
        }
        if (orderCheckDone()) return;
        showMessage(orderStatus());
        return;
    }
    auto offers = ordersOffered();
    if (offers.empty()) { say("No special orders this week."); return; }
    DialoguePage p;
    p.text = "Special orders this week:";
    for (auto* d : offers) {
        const NpcDef* who = nullptr;
        for (auto& n : data.npcs) if (n.name == d->requester) who = &n;
        DialogueChoice c;
        c.label = d->name + " (" + (who ? who->display : d->requester) + ", " + std::to_string(d->money) + "g)";
        c.responseKey = "so:" + d->key;
        p.choices.push_back(c);
    }
    DialogueChoice c;
    c.label = "Not this week";
    p.choices.push_back(c);
    dialogue = DialogueBox();
    dialogue.active = true;
    dialogue.pages.push_back(p);
    pendingChoice = "order";
}

bool Game::ordersChoice(const std::string& what, const std::string& key) {
    if (what != "order") return false;
    if (key.rfind("so:", 0) == 0) {
        const OrderDef* d = orderDef(key.substr(3));
        if (d) {
            orderKey = d->key;
            orderDays = d->days;
            orderProgress.assign(d->objectives.size(), 0);
            sfx("newArtifact");
            showMessage(d->text + "#$b#" + orderStatus());
        }
    }
    return true;
}

// Something happened that an order might be counting: kind is "fish", "ship", "slay", "deliver" or "floor".
void Game::orderEvent(const std::string& kind, int item, int count, const std::string& target) {
    if (orderKey.empty()) return;
    const OrderDef* d = orderDef(orderKey);
    if (!d) return;
    orderProgress.resize(d->objectives.size());
    for (size_t i = 0; i < d->objectives.size(); i++) {
        const auto& o = d->objectives[i];
        if (o.kind != kind || orderProgress[i] >= o.count) continue;
        if (kind == "floor") { orderProgress[i] = std::max(orderProgress[i], std::min(count, o.count)); continue; }
        if (kind == "slay") {
            if (!o.target.empty() && target.find(o.target) == std::string::npos) continue;
        } else {
            if (!o.items.count(item)) continue;
            if (kind == "deliver" && !o.target.empty() && o.target != target) continue;
        }
        orderProgress[i] = std::min(o.count, orderProgress[i] + count);
        if (kind == "deliver") break;   // one gift, one objective
    }
    orderCheckDone();
}

// Would this gift count toward an order (so it's handed over, not judged as a present)?
bool Game::orderWants(const std::string& npc, int item) {
    const OrderDef* d = orderKey.empty() ? nullptr : orderDef(orderKey);
    if (!d) return false;
    for (size_t i = 0; i < d->objectives.size(); i++) {
        const auto& o = d->objectives[i];
        if (o.kind == "deliver" && o.target == npc && o.items.count(item) && (i >= orderProgress.size() || orderProgress[i] < o.count)) return true;
    }
    return false;
}

bool Game::orderCheckDone() {
    const OrderDef* d = orderDef(orderKey);
    if (!d) return false;
    for (size_t i = 0; i < d->objectives.size(); i++)
        if (i >= orderProgress.size() || orderProgress[i] < d->objectives[i].count) return false;
    money += d->money;
    totalEarned += d->money;
    mailReceived.insert("so:" + d->key.substr(0, d->key.find('#')));
    if (NpcState* n = npcByNameMut(d->requester)) n->friendship = std::min(friendshipCap(*n), n->friendship + 250);
    sfx("questcomplete");
    say("Special order done: " + d->name + "! +" + std::to_string(d->money) + "g");
    orderKey.clear();
    orderProgress.clear();
    orderDays = 0;
    return true;
}

void Game::ordersNight() {
    if (orderKey.empty()) return;
    if (--orderDays <= 0) {
        const OrderDef* d = orderDef(orderKey);
        if (morningNote.empty()) morningNote = "Time ran out on the special order" + (d ? ": " + d->name + "." : std::string("."));
        orderKey.clear();
        orderProgress.clear();
    }
}

} // namespace sv
