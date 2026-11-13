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
    if (cur && cur->name == "AbandonedJojaMart") {   // the Missing Bundle, once the center is restored
        if (roomOpen("Abandoned Joja Mart") && !roomDone("Abandoned Joja Mart") && !mailReceived.count("ccMovieTheater"))
            out.push_back({"Abandoned Joja Mart", 8, 8});
        return out;
    }
    if (!cur || cur->name != "CommunityCenter" || mailReceived.count("JojaMember")) return out;
    bool canRead = canReadJunimo();
    for (auto& sp : SPOTS) {
        std::string room = sp.room;
        if (mailReceived.count(roomFlagOf(room)) || roomDone(room)) continue;   // that room is done
        if (!canRead ? room != "Crafts Room" : !roomOpen(room)) continue;
        out.push_back({room, sp.x, sp.y});
    }
    return out;
}

bool Game::canReadJunimo() const {
    if (mailReceived.count("canReadJunimoText") || eventsSeen.count("112")) return true;
    for (auto& kv : bundleGiven) if (!kv.second.empty()) return true;   // an older save already working on them
    return false;
}

int Game::bundlesDone() const {
    int n = 0;
    for (auto& b : bundles()) if (b.room != "Abandoned Joja Mart") n += bundleDone(b);
    return n;
}

// The Crafts Room is open from the start; the Pantry and Fish Tank after one bundle, the Boiler Room after
// two, the Bulletin Board after three and the Vault after four (the Missing Bundle once the rest are done).
bool Game::roomOpen(const std::string& room) const {
    if (mailReceived.count(roomFlagOf(room))) return true;
    int done = bundlesDone();
    if (room == "Crafts Room") return true;
    if (room == "Pantry" || room == "Fish Tank") return done >= 1;
    if (room == "Boiler Room") return done >= 2;
    if (room == "Bulletin Board") return done >= 3;
    if (room == "Vault") return done >= 4;
    if (room == "Abandoned Joja Mart") return ccDone() && !mailReceived.count("JojaMember") && eventsSeen.count("191393");
    return true;
}

// Which room each tile of the Community Center belongs to: restoring a room takes its tiles from the
// refurbished map. (The main hall goes with the Bulletin Board.)
static const char* ccRoomAt(int x, int y) {
    if (x >= 36 && x <= 44 && y >= 4 && y <= 11) return "Fish Tank";
    if (x >= 46 && x <= 61 && y <= 8) return "Vault";
    if (x >= 54 && y >= 9) return "Boiler Room";
    if (x <= 21 && y <= 12) return "Pantry";
    if (x <= 21) return "Crafts Room";
    return "Bulletin Board";
}

void Game::ccRestore() {
    if (!cur || cur->name != "CommunityCenter" || mailReceived.count("JojaMember")) return;
    std::vector<std::string> rooms;
    for (auto& r : bundleRooms())
        if ((mailReceived.count(roomFlagOf(r)) || roomDone(r)) && !cur->stamps.count("cc:" + r)) rooms.push_back(r);
    if (rooms.empty() || !data.vfs.exists("maps/CommunityCenter_Refurbished.smap")) return;
    TileMap ref;
    if (!ref.load(data.vfs.path("maps/CommunityCenter_Refurbished.smap"), nullptr)) return;
    int w = cur->map.width(), h = cur->map.height();
    if (ref.width() != w || ref.height() != h) return;
    std::vector<uint8_t> mask((size_t)w * h, 0);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            for (auto& r : rooms) if (r == ccRoomAt(x, y)) mask[(size_t)y * w + x] = 1;
    if (!cur->map.copyCells(ref, mask)) return;
    for (auto& r : rooms) cur->stamps.insert("cc:" + r);
}

// Overnight the junimos finish what a restored room does for the valley.
void Game::ccMorning() {
    auto rooms = bundleRooms();
    rooms.push_back("Abandoned Joja Mart");
    for (auto& r : rooms) {
        const char* fl = roomFlagOf(r);
        if (!*fl || mailReceived.count(fl) || !roomDone(r)) continue;
        mailReceived.insert(fl);
        if (r == "Bulletin Board")   // 500 friendship with everyone you've met who isn't someone to date
            for (auto& n : npcs)
                if (n.def && n.introduced && !data.romanceable(n.def->name))
                    n.friendship = std::min(n.friendship + 500, friendshipCap(n));
        if (morningNote.empty()) {
            static const struct { const char* room; const char* note; } NOTES[] = {
                {"Pantry", "The junimos have been busy: your greenhouse is fixed."},
                {"Crafts Room", "The junimos have been busy: the bridge to the quarry is mended."},
                {"Fish Tank", "The junimos have been busy: the glittering boulder in the mountain stream is gone."},
                {"Boiler Room", "The junimos have been busy: the minecarts are running."},
                {"Bulletin Board", "The junimos have been busy: the townsfolk seem friendlier."},
                {"Vault", "The junimos have been busy: the bus is fixed."},
                {"Abandoned Joja Mart", "The junimos have been busy: a movie theater stands where JojaMart was."}};
            for (auto& nt : NOTES) if (r == nt.room) morningNote = nt.note;
        }
    }
    collectBundleRewards();
}

bool Game::collectBundleRewards() {
    bool any = false;
    while (!bundleRewards.empty()) {
        if (!addSlot(bundleRewards.front())) break;
        bundleRewards.erase(bundleRewards.begin());
        any = true;
    }
    return any;
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
    if (room == "Abandoned Joja Mart") return "ccMovieTheater";
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
            Slot s;
            s.count = std::max(1, n);
            if (kind == "O" || kind == "R") {   // an object, or a ring
                s.type = SLOT_OBJECT;
                s.id = data.objectIdFor(id);
                if (!data.object(s.id)) s = Slot();
            } else if (kind == "BO") {
                s.type = SLOT_BIG;
                s.id = atoi((id.rfind("(BC)", 0) == 0 ? id.substr(4) : id).c_str());
                if (!data.big(s.id)) s = Slot();
            }
            if (!s.empty()) bundleRewards.push_back(s);
        }
        say(b.name + " Bundle complete!");
        if (!collectBundleRewards() && !bundleRewards.empty())
            say("Your pack is full: the reward waits in the bundle list.");
        if (roomDone(b.room) && *roomFlag(b.room) && !mailReceived.count(roomFlag(b.room))) {
            say("The " + b.room + " is restored! The junimos will finish the job tonight.");
            ccRestore();
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
