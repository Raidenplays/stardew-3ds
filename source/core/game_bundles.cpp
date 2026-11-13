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

// The golden scroll on the wall of each room, while its bundles are unfinished (the Crafts Room's from
// the first visit; the rest once the Wizard has taught you to read them).
std::vector<Game::CcNote> Game::ccNotes() const {
    static const struct { const char* room; int x, y; } SPOTS[6] = {
        {"Pantry", 14, 5}, {"Crafts Room", 14, 23}, {"Fish Tank", 40, 10},
        {"Boiler Room", 63, 14}, {"Vault", 55, 6}, {"Bulletin Board", 46, 11}};
    std::vector<CcNote> out;
    if (!cur || cur->name != "CommunityCenter" || mailReceived.count("JojaMember")) return out;
    bool canRead = mailReceived.count("canReadJunimoText") || eventsSeen.count("112");
    for (auto& kv : bundleGiven) canRead |= !kv.second.empty();
    for (auto& sp : SPOTS) {
        std::string room = sp.room;
        if (mailReceived.count(roomFlagOf(room))) continue;   // that room is done
        if (!canRead && room != "Crafts Room") continue;
        if (room == "Bulletin Board" && !canRead) continue;
        out.push_back({room, sp.x, sp.y});
    }
    return out;
}

static const char* roomFlag(const std::string& room);
const char* Game::roomFlagOf(const std::string& room) { return roomFlag(room); }

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
    int n = 0;   // only good enough quality counts (silver 1, gold 2, iridium 4)
    for (auto& s : inv) if (s.type == SLOT_OBJECT && s.id == it.id && s.quality >= it.quality) n += s.count;
    return n;
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
        } else {
            int need = it.count;   // lowest good-enough quality first
            for (int q = it.quality; q <= 4 && need > 0; q++)
                for (auto& s : inv) {
                    if (need <= 0) break;
                    if (s.type != SLOT_OBJECT || s.id != it.id || s.quality != q) continue;
                    int k = std::min(need, s.count);
                    s.count -= k;
                    need -= k;
                    if (s.count <= 0) s = Slot();
                }
        }
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

namespace sv {

// ---------------------------------------------------------------- the museum

bool Game::museumWants(const Slot& s) const {
    return s.type == SLOT_OBJECT && data.museumItems.count(s.id) && !museumDonated.count(s.id);
}

bool Game::museumDonate(int slot) {
    if (slot < 0 || slot >= INV_SIZE || !museumWants(inv[slot])) return false;
    int id = inv[slot].id;
    museumDonated.insert(id);
    if (--inv[slot].count <= 0) inv[slot] = Slot();
    sfx("newArtifact");
    const ObjectDef* o = data.object(id);
    say("Gunther: Thank you for donating the " + (o ? o->name : std::string("piece")) + "!");
    museumRewardsCheck(true);
    return true;
}

void Game::museumRewardsCheck(bool give) {
    int arch = 0, min = 0;
    for (int id : museumDonated) {
        auto it = data.museumItems.find(id);
        if (it == data.museumItems.end()) continue;
        (it->second == 'A' ? arch : min)++;
    }
    int all = arch + min;
    for (auto& r : data.museumRewards) {
        std::string flag = "museum:" + r.key;
        if (mailReceived.count(flag)) continue;
        bool ok = true;
        for (auto& q : r.reqs) {
            if (q.first.rfind("id=", 0) == 0) ok &= museumDonated.count(atoi(q.first.c_str() + 3)) > 0;
            else if (q.first == "arch") ok &= arch >= q.second;
            else if (q.first == "min") ok &= min >= q.second;
            else ok &= q.second < 0 ? all >= (int)data.museumItems.size() : all >= q.second;   // -1: the whole collection
        }
        if (!ok) continue;
        mailReceived.insert(flag);
        if (!give) continue;
        const ObjectDef* o = data.object(r.id);
        if (r.kind == 'O' && o) { addItem(r.id, r.count); say("Gunther gave you " + o->name + " for your donations!"); }
        else if (r.kind == 'R' && o) { mailReceived.insert("craft:" + o->name); say("You learned to make " + o->name + "!"); }
        else if (r.kind == 'B' && data.big(r.id)) {
            Slot s; s.type = SLOT_BIG; s.id = r.id; s.count = r.count;
            if (addSlot(s)) say("Gunther gave you a " + data.big(r.id)->name + "!");
        } else if (r.kind == 'F' && placeFurnitureInHouse(r.id)) {
            auto fd = data.furniture.find(r.id);
            say("Gunther sent a " + (fd != data.furniture.end() ? fd->second.name : std::string("gift")) + " to your house!");
        }
    }
}

bool Game::placeFurnitureInHouse(int id) {
    auto fd = data.furniture.find(id);
    if (fd == data.furniture.end() || !hasMap("FarmHouse")) return false;
    Location* h = loadLocation("FarmHouse");
    if (!h) return false;
    const TileMap& m = h->map;
    int bw = fd->second.boxW, bh = fd->second.boxH;
    for (int y = 3; y + bh < m.height(); y++)
        for (int x = 1; x + bw < m.width(); x++) {
            bool ok = true;
            for (int dy = 0; dy < bh && ok; dy++)
                for (int dx = 0; dx < bw && ok; dx++) {
                    int tx = x + dx, ty = y + dy;
                    if (m.tile(L_BACK, tx, ty) == NO_TILE || (m.flags(tx, ty) & CF_BLOCKED) || m.tile(L_BUILDINGS, tx, ty) != NO_TILE ||
                        m.warpAt(tx, ty) || m.warpAt(tx, ty + 1))
                        ok = false;
                    for (auto& fp : furniture) {
                        if (fp.map != "FarmHouse") continue;
                        auto od = data.furniture.find(fp.id);
                        int ow = od != data.furniture.end() ? od->second.boxW : 1, oh = od != data.furniture.end() ? od->second.boxH : 1;
                        if (tx >= fp.x && tx < fp.x + ow && ty >= fp.y && ty < fp.y + oh) ok = false;
                    }
                }
            if (!ok) continue;
            FurniturePlacement p;
            p.map = "FarmHouse"; p.id = id; p.x = x; p.y = y;
            furniture.push_back(p);
            return true;
        }
    return false;
}

} // namespace sv
