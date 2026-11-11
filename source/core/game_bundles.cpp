// The Community Center: bundles from Data/Bundles (or an imported save's own
// remix). Give items, finish bundles for their rewards, finish a room for its
// mail flag (ccPantry fixes the greenhouse...).
#include "game.h"

#include <cstdlib>
#include <sstream>

namespace sv {

std::vector<std::string> Game::bundleRooms() {
    return {"Pantry", "Crafts Room", "Fish Tank", "Boiler Room", "Bulletin Board", "Vault"};
}

static const char* roomFlag(const std::string& room) {
    if (room == "Pantry") return "ccPantry";
    if (room == "Crafts Room") return "ccCraftsRoom";
    if (room == "Fish Tank") return "ccFishTank";
    if (room == "Boiler Room") return "ccBoilerRoom";
    if (room == "Bulletin Board") return "ccBulletin";
    if (room == "Vault") return "ccVault";
    return "";
}

std::vector<Bundle> Game::bundles() const {
    std::vector<Bundle> out;
    const auto& defs = bundleDefs.empty() ? data.bundles : bundleDefs;
    for (auto& kv : defs) {
        Bundle b;
        b.key = kv.first;
        size_t sl = kv.first.find('/');
        if (sl == std::string::npos) continue;
        b.room = kv.first.substr(0, sl);
        b.index = atoi(kv.first.c_str() + sl + 1);
        std::vector<std::string> f;
        std::string cur;
        for (char ch : kv.second) {
            if (ch == '/') { f.push_back(cur); cur.clear(); }
            else cur += ch;
        }
        f.push_back(cur);
        if (f.size() < 3) continue;
        b.name = f[0];
        b.reward = f[1];
        std::istringstream ss(f[2]);
        std::string id;
        int count, quality;
        while (ss >> id >> count >> quality) {
            int iid = id == "-1" ? -1 : data.objectIdFor(id);
            if (iid != -1 && !data.object(iid) && iid >= 0) continue;   // not in this game's data
            b.items.push_back({iid, count, quality});
        }
        b.color = f.size() > 3 ? atoi(f[3].c_str()) : 0;
        b.needed = f.size() > 4 && !f[4].empty() ? atoi(f[4].c_str()) : (int)b.items.size();
        b.needed = std::min(b.needed, (int)b.items.size());
        if (f.size() > 6 && !f[6].empty()) b.name = f[6];
        if (!b.items.empty()) out.push_back(b);
    }
    return out;
}

int Game::bundleGivenCount(const Bundle& b) const {
    auto it = bundleGiven.find(b.index);
    if (it == bundleGiven.end()) return 0;
    int n = 0;
    for (size_t i = 0; i < it->second.size() && i < b.items.size(); i++) n += it->second[i] != 0;
    return n;
}

bool Game::roomDone(const std::string& room) const {
    bool any = false;
    for (auto& b : bundles())
        if (b.room == room) {
            any = true;
            if (!bundleDone(b)) return false;
        }
    return any;
}

int Game::bundleHave(const Bundle::Item& it) const {
    if (it.id == -1) return money;
    if (it.id < 0) {   // a category ("any fish")
        int n = 0;
        for (auto& s : inv)
            if (s.type == SLOT_OBJECT)
                if (const ObjectDef* o = data.object(s.id)) n += o->category == it.id ? s.count : 0;
        return n;
    }
    return countItem(it.id);
}

bool Game::bundleGive(int bundleIndex, int slot) {
    for (auto& b : bundles()) {
        if (b.index != bundleIndex) continue;
        if (slot < 0 || slot >= (int)b.items.size() || bundleDone(b)) return false;
        auto& given = bundleGiven[b.index];
        given.resize(b.items.size());
        if (given[slot]) return false;
        const Bundle::Item& it = b.items[slot];
        if (bundleHave(it) < it.count) {
            say(it.id == -1 ? "Not enough money." : "You don't have enough of that.");
            return false;
        }
        if (it.id == -1) money -= it.count;
        else if (it.id < 0) {
            int need = it.count;
            for (auto& s : inv) {
                if (need <= 0) break;
                const ObjectDef* o = s.type == SLOT_OBJECT ? data.object(s.id) : nullptr;
                if (!o || o->category != it.id) continue;
                int k = std::min(need, s.count);
                s.count -= k;
                need -= k;
                if (s.count <= 0) s = Slot();
            }
        } else takeItem(it.id, it.count);
        given[slot] = 1;
        sfx("newArtifact");
        if (!bundleDone(b)) return true;
        // finished: the reward ("O 465 20", "BO 10 1")
        sfx("discoverMineral");
        std::istringstream rs(b.reward);
        std::string kind, id;
        int n = 1;
        if (rs >> kind >> id) {
            rs >> n;
            if (kind == "O") { int oid = data.objectIdFor(id); if (data.object(oid)) addItem(oid, std::max(1, n)); }
            else if (kind == "BO") {
                Slot s;
                s.type = SLOT_BIG;
                s.id = atoi(id.c_str());
                s.count = std::max(1, n);
                if (data.big(s.id)) addSlot(s);
            }
        }
        say(b.name + " Bundle complete!");
        if (roomDone(b.room) && *roomFlag(b.room) && !mailReceived.count(roomFlag(b.room))) {
            mailReceived.insert(roomFlag(b.room));
            say("The " + b.room + " is restored!" + (b.room == "Pantry" ? " Your greenhouse is fixed." : ""));
        }
        return true;
    }
    return false;
}

} // namespace sv
