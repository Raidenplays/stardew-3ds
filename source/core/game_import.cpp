// A PC save brought over by tools/saveimport.py (import.txt): the farm is
// rebuilt from it on top of a fresh game, then saved the 3DS way.
#include "game.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace sv {

static std::vector<std::string> fields(const std::string& line) {
    std::vector<std::string> f;
    size_t s = 0;
    for (;;) {
        size_t t = line.find('\t', s);
        f.push_back(line.substr(s, t == std::string::npos ? std::string::npos : t - s));
        if (t == std::string::npos) break;
        s = t + 1;
    }
    return f;
}

static int I(const std::vector<std::string>& f, size_t i, int def = 0) {
    return i < f.size() ? atoi(f[i].c_str()) : def;
}

bool Game::applyImport(const std::string& path, std::string* summary) {
    FILE* fp = fopen(path.c_str(), "r");
    if (!fp) return false;
    std::vector<std::vector<std::string>> lines;
    char buf[1024];
    while (fgets(buf, sizeof buf, fp)) {
        std::string l(buf);
        while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
        if (!l.empty()) lines.push_back(fields(l));
    }
    fclose(fp);
    if (lines.empty() || lines[0][0] != "version") return false;

    // the right kind of farm first (Riverland, Forest, ...): the world is started again on that map
    for (auto& f : lines)
        if (f[0] == "farmtype" && I(f, 1) != me.farmType && I(f, 1) >= 0 && I(f, 1) < (int)data.farmTypes.size()) {
            Character who = me;
            who.farmType = I(f, 1);
            if (!newGame(rngState, who)) return false;
        }

    // the PC farm replaces the starting one
    int fw = farm->map.width();
    for (auto& d : farm->debris) d = DEB_NONE;
    for (auto& g : farm->grass) g = 0;
    for (auto& s : farm->soils) s = Soil();
    farm->trees.clear();
    farm->clumps.clear();
    farm->fruitTrees.clear();
    placed.clear();
    for (auto& s : inv) s = Slot();
    buildings.clear();
    animals.clear();
    bundleDefs.clear();
    bundleGiven.clear();
    std::map<int, int> bid;   // import index -> building id
    bool importedFurniture = false;
    int counts[8] = {};

    auto locFor = [&](const std::string& name) -> Location* {
        if (name.size() > 2 && name[0] == '@' && name[1] == 'B') {
            auto it = bid.find(atoi(name.c_str() + 2));
            if (it == bid.end()) return nullptr;
            FarmBuilding* b = buildingById(it->second);
            return b ? loadLocation(interiorName(*b)) : nullptr;
        }
        if (name == "Greenhouse")
            for (auto& b : buildings)
                if (b.type == "Greenhouse") return loadLocation(interiorName(b));
        return hasMap(name) ? loadLocation(name) : nullptr;
    };
    auto slotFrom = [&](const std::vector<std::string>& f, size_t i, Slot& s) -> bool {
        if (i >= f.size()) return false;
        const std::string& k = f[i];
        s = Slot();
        if (k == "O" && data.object(I(f, i + 1))) {
            s.type = SLOT_OBJECT; s.id = I(f, i + 1); s.count = std::max(1, I(f, i + 2, 1));
            s.quality = (uint8_t)std::max(0, std::min(4, I(f, i + 3)));
            return true;
        }
        if (k == "B" && data.big(I(f, i + 1))) {
            s.type = SLOT_BIG; s.id = I(f, i + 1); s.count = std::max(1, I(f, i + 2, 1));
            return true;
        }
        int t = -1;
        if (k == "W" && i + 1 < f.size()) {
            const std::string& w = f[i + 1];
            t = data.toolByKey(w == "47" ? "Scythe" : w == "53" ? "GoldenScythe" : w == "66" ? "IridiumScythe" : "Weapon_" + w);
            if (t < 0 && (w == "47" || w == "53" || w == "66"))
                if (const ToolDef* d = data.toolByKind(TOOL_SCYTHE)) t = (int)(d - &data.tools[0]);
        } else if (k == "T" && i + 1 < f.size()) {
            const std::string& kind = f[i + 1];
            int lvl = I(f, i + 2);
            ToolKind tk = kind == "Axe" ? TOOL_AXE : kind == "Pickaxe" ? TOOL_PICKAXE : kind == "Hoe" ? TOOL_HOE
                        : kind == "WateringCan" ? TOOL_CAN : kind == "FishingRod" ? TOOL_ROD : TOOL_NONE;
            if (tk != TOOL_NONE) {
                for (size_t j = 0; j < data.tools.size(); j++)
                    if (data.tools[j].kind == tk && data.tools[j].upgrade == lvl) { t = (int)j; break; }
                if (t < 0)
                    if (const ToolDef* d = data.toolByKind(tk)) t = (int)(d - &data.tools[0]);
            } else {
                t = data.toolByKey(kind);
            }
        }
        if (t < 0) return false;
        s.type = SLOT_TOOL; s.id = t; s.count = 1;
        return true;
    };

    invSlots = INV_SIZE;   // unless the save says its backpack is smaller
    for (auto& f : lines) {
        const std::string& k = f[0];
        if (k == "name" && f.size() > 1) me.name = f[1];
        else if (k == "farm" && f.size() > 1) me.farm = f[1];
        else if (k == "equip" && f.size() > 3) {
            Slot sl;
            int e = I(f, 1);
            if (e >= 0 && e < EQ_COUNT && slotFrom(f, 2, sl)) {
                int kind = equipKind(sl);
                if (kind == e || (kind == EQ_RING1 && e == EQ_RING2)) { sl.count = 1; equip[e] = sl; }
            }
        }
        else if (k == "look" && f.size() > 8) {
            me.outfit.hair = I(f, 1) % 32;
            me.outfit.shirt = I(f, 2) % 112;
            me.outfit.pantsR = (uint8_t)I(f, 3); me.outfit.pantsG = (uint8_t)I(f, 4); me.outfit.pantsB = (uint8_t)I(f, 5);
            me.outfit.hairR = (uint8_t)I(f, 6); me.outfit.hairG = (uint8_t)I(f, 7); me.outfit.hairB = (uint8_t)I(f, 8);
        } else if (k == "favorite" && f.size() > 1) me.favorite = f[1];
        else if (k == "gender" && f.size() > 1) me.outfit.female = f[1] == "female";
        else if (k == "maxstamina") {   // Stardrops eaten on the PC
            maxEnergy = std::max((int)MAX_ENERGY, std::min(MAX_ENERGY + STARDROP_ENERGY * MAX_STARDROPS, I(f, 1)));
            energy = maxEnergy;
        } else if (k == "maxhealth") {
            maxHealth = std::max(100, std::min(250, I(f, 1)));
            health = maxHealth;
        } else if (k == "houseupgrade") houseUpgradeDays = std::max(1, I(f, 1));
        else if (k == "toolupgrade") {   // a tool left with Clint
            Slot s;
            if (slotFrom(f, 2, s) && s.type == SLOT_TOOL) { upgradeTool = s.id; upgradeReadyDay = absoluteDay() + std::max(1, I(f, 1)); }
        } else if (k == "quest" && f.size() > 1) {
            bool have = questsFinished.count(f[1]) > 0;
            for (auto& q : quests) have = have || q.id == f[1];
            if (!have && data.quest(f[1])) { QuestState st; st.id = f[1]; quests.push_back(st); }
        } else if (k == "mailbox" && f.size() > 1) {
            if (data.mail.count(f[1])) mailbox.push_back(f[1]);
        }
        else if (k == "money") money = I(f, 1);
        else if (k == "date") {
            season = std::max(0, std::min(3, I(f, 1)));
            day = std::max(1, std::min(28, I(f, 2, 1)));
            year = std::max(1, I(f, 3, 1));
        } else if (k == "xp" && I(f, 1) >= 0 && I(f, 1) < SK_COUNT) {
            skillXp[I(f, 1)] = I(f, 2);
            skillLevels[I(f, 1)] = xpLevel(I(f, 1));   // levels you already had: no level-up screens again
        } else if (k == "profession") professions.insert(I(f, 1));
        else if (k == "inv") {
            int slot = I(f, 1);
            Slot s;
            if (slot >= 0 && slot < INV_SIZE && slotFrom(f, 2, s)) { inv[slot] = s; counts[0]++; }
        } else if (k == "friend" && f.size() > 1) {
            for (auto& n : npcs) if (n.def && n.def->name == f[1]) n.friendship = std::min(3500, I(f, 2));
        } else if (k == "dating" && f.size() > 1) dating.insert(f[1]);
        else if (k == "engaged" && f.size() > 1) { engagedTo = f[1]; weddingDay = absoluteDay() + 1; }
        else if (k == "spouse" && f.size() > 1 && !f[1].empty()) { spouse = f[1]; dating.erase(spouse); 
        } else if (k == "mail" && f.size() > 1) mailReceived.insert(f[1]);
        else if (k == "event" && f.size() > 1) eventsSeen.insert(f[1]);
        else if (k == "mine") mineDeepest = I(f, 1);
        else if (k == "pet" && f.size() > 4) {
            Pet pet;
            pet.type = f[1] == "Dog" ? 2 : f[1] == "Turtle" ? 3 : 1;
            pet.breed = std::max(0, std::min(4, I(f, 2)));
            pet.name = f[3].empty() ? (pet.type == 2 ? "Buddy" : pet.type == 3 ? "Shelly" : "Whiskers") : f[3];
            pet.friendship = std::max(0, std::min(1000, I(f, 4)));
            if (pets.empty()) { me.petKind = pet.type; me.petBreed = pet.breed; }
            pets.push_back(pet);
            mailReceived.insert("hasPet");
        }
        else if (k == "kid" && f.size() > 4 && kids.size() < 2) {
            Child c;
            c.name = f[1].empty() ? "Kid" : f[1];
            c.girl = f[2] == "1"; c.dark = f[3] == "1";
            c.daysOld = std::max(0, I(f, 4));
            c.friendship = f.size() > 5 ? std::max(0, std::min(2500, I(f, 5))) : 0;
            kids.push_back(c);
        }
        else if (k == "babydue") babyDue = I(f, 1) > 0 ? I(f, 1) : -1;
        else if (k == "shippedever" && f.size() > 2) shippedEver[I(f, 1)] = I(f, 2);
        else if (k == "fishever" && f.size() > 2) fishCaught[I(f, 1)] = I(f, 2);
        else if (k == "walnuts" && f.size() > 2) { walnuts = I(f, 1); walnutsFound = I(f, 2); }
        else if (k == "clubcoins") clubCoins = I(f, 1);
        else if (k == "cookedever" && f.size() > 2) cookedEver[I(f, 1)] = I(f, 2);
        else if (k == "earned") totalEarned = std::max(totalEarned, (long long)I(f, 1));
        else if (k == "backpack") invSlots = I(f, 1) >= 36 ? 36 : I(f, 1) >= 24 ? 24 : 12;
        else if (k == "house") { houseLevel = std::max(0, std::min(3, I(f, 1))); locations.erase("FarmHouse"); }
        else if (k == "craftrecipe" && f.size() > 1) mailReceived.insert("craft:" + f[1]);
        else if (k == "cookrecipe" && f.size() > 1) mailReceived.insert("cook:" + f[1]);
        else if (k == "building" && f.size() > 5 && data.buildingType(f[1])) {
            FarmBuilding b;
            b.id = nextBuildingId++;
            b.type = f[1];
            b.x = I(f, 2);
            b.y = I(f, 3);
            b.daysLeft = I(f, 4);
            buildings.push_back(b);
            bid[I(f, 5)] = b.id;
            counts[1]++;
        } else if (k == "pond" && f.size() > 3) {
            auto it = bid.find(I(f, 1));
            if (it == bid.end()) continue;
            if (FarmBuilding* b = buildingById(it->second))
                if (data.fishPond(I(f, 2))) { b->pondFish = I(f, 2); b->pondPop = std::max(1, std::min(10, I(f, 3))); }
        } else if (k == "animal" && f.size() > 5 && data.animalType(f[1])) {
            auto it = bid.find(I(f, 3));
            if (it == bid.end()) continue;
            FarmAnimal a;
            a.type = f[1]; a.name = f[2]; a.home = it->second; a.age = I(f, 4); a.friendship = I(f, 5);
            a.x = -1;
            animals.push_back(a);
            counts[2]++;
        } else if (k == "animalout" && f.size() > 4 && data.animalType(f[1])) {
            const AnimalType* at = data.animalType(f[1]);
            int home = roomFor(*at);
            if (!home) continue;
            FarmAnimal a;
            a.type = f[1]; a.name = f[2]; a.home = home; a.age = I(f, 3); a.friendship = I(f, 4);
            a.x = -1;
            animals.push_back(a);
            counts[2]++;
        } else if (k == "debris" && f.size() > 4 && f[1] == "Farm") {
            int x = I(f, 2), y = I(f, 3);
            if (!farm->map.inBounds(x, y)) continue;
            size_t i = (size_t)y * fw + x;
            farm->debris[i] = f[4] == "W" ? DEB_WEED : f[4] == "S" ? DEB_STONE : DEB_TWIG;
            farm->debrisVar[i] = (uint8_t)(rand32() & 1);
        } else if (k == "grass" && f.size() > 4 && f[1] == "Farm") {
            int x = I(f, 2), y = I(f, 3);
            if (farm->map.inBounds(x, y)) farm->grass[(size_t)y * fw + x] = (uint8_t)(std::min(4, I(f, 4, 1)) | (I(f, 5) ? 8 : 0));
        } else if (k == "tree" && f.size() > 6) {
            Location* L = locFor(f[1]);
            if (!L) continue;
            Tree t;
            t.x = I(f, 2); t.y = I(f, 3); t.type = std::max(1, I(f, 4, 1));
            t.stage = (uint8_t)std::max(0, std::min(TREE_GROWN, I(f, 5, 5)));
            t.stump = I(f, 6) != 0;
            L->trees.push_back(t);
            counts[3]++;
        } else if (k == "soil" && f.size() > 9) {
            Location* L = locFor(f[1]);
            int x = I(f, 2), y = I(f, 3);
            if (!L || !L->map.inBounds(x, y)) continue;
            Soil& s = L->soils[(size_t)y * L->map.width() + x];
            s.tilled = true;
            s.watered = I(f, 4) != 0;
            int seed = I(f, 5, -1);
            const CropDef* c = seed >= 0 ? data.cropForSeed(seed) : nullptr;
            if (c) {
                s.seed = seed;
                int np = (int)c->phaseDays.size();
                s.phase = (uint8_t)(I(f, 8) ? np : std::max(0, std::min(np, I(f, 6))));
                s.dayInPhase = (uint8_t)std::max(0, I(f, 7));
                s.dead = I(f, 9) != 0;
                s.regrown = I(f, 8) != 0;
                counts[4]++;
            }
        } else if (k == "place" && f.size() > 5) {
            Location* L = locFor(f[1]);
            if (!L) continue;
            Placed p;
            p.x = I(f, 2); p.y = I(f, 3);
            p.big = f[4] == "B";
            p.id = I(f, 5);
            if (p.big ? !data.big(p.id) : !data.object(p.id)) continue;
            auto& there = placed[L->name];   // whatever the fresh game put on that tile makes way (the cellar's casks)
            for (size_t q = there.size(); q-- > 0;)
                if (there[q].x == p.x && there[q].y == p.y) there.erase(there.begin() + q);
            there.push_back(p);
            counts[5]++;
        } else if (k == "floor" && f.size() > 4) {   // paths and floors
            Location* L = locFor(f[1]);
            int fx = I(f, 2), fy = I(f, 3), kind = I(f, 4);
            if (L && L->map.inBounds(fx, fy) && kind >= 0 && kind < 13) L->floors[(size_t)fy * L->map.width() + fx] = (uint8_t)(kind + 1);
        } else if (k == "fruittree" && f.size() > 5) {
            Location* L = locFor(f[1]);
            if (!L || !data.fruitTreeForSapling(I(f, 4))) continue;
            FruitTree t;
            t.x = I(f, 2); t.y = I(f, 3); t.sapling = I(f, 4); t.daysLeft = std::max(0, I(f, 5)); t.fruit = std::min(3, I(f, 6));
            L->fruitTrees.push_back(t);
            counts[3]++;
        } else if (k == "bundledef" && f.size() > 2) {
            bundleDefs.push_back({f[1], f[2]});
        } else if (k == "bundle" && f.size() > 2) {
            std::vector<uint8_t> v;
            for (char ch : f[2]) v.push_back(ch == '1');
            bundleGiven[I(f, 1)] = v;
        } else if (k == "furniture" && f.size() > 4) {   // your house as you left it
            if (!importedFurniture) {
                for (size_t q = furniture.size(); q-- > 0;) if (furniture[q].map == f[1]) furniture.erase(furniture.begin() + q);
                importedFurniture = true;
            }
            FurniturePlacement fp;
            fp.map = f[1]; fp.id = I(f, 2); fp.x = I(f, 3); fp.y = I(f, 4);
            if (data.furniture.count(fp.id)) furniture.push_back(fp);
        } else if (k == "museum" && f.size() > 1) {
            museumDonated.insert(I(f, 1));
        } else if (k == "drop" && f.size() > 4) {   // an egg or forage lying there
            Location* L = locFor(f[1]);
            if (!L || !data.object(I(f, 4))) continue;
            WorldObject o;
            o.x = I(f, 2); o.y = I(f, 3); o.itemId = I(f, 4);
            L->objects.push_back(o);
        } else if ((k == "chest" || k == "held") && f.size() > 5) {
            Location* L = locFor(f[1]);
            if (!L) continue;
            Placed* p = nullptr;
            for (auto& q : placed[L->name]) if (q.x == I(f, 2) && q.y == I(f, 3)) p = &q;
            if (!p) continue;
            if (k == "chest") {
                Slot s;
                if (slotFrom(f, 4, s)) { p->items.push_back(s); counts[6]++; }
            } else {
                Slot s;
                s.type = SLOT_OBJECT; s.id = I(f, 4); s.count = std::max(1, I(f, 5, 1)); s.quality = (uint8_t)std::max(0, std::min(4, I(f, 6)));
                if (!data.object(s.id)) continue;
                for (size_t r = 0; r < data.machineRules.size(); r++)
                    if (data.machineRules[r].machine == p->id) { p->rule = (int)r; break; }
                if (p->rule < 0) continue;
                p->held = s;
                p->readyAt = nowMinutes() + std::max(0, I(f, 7));
            }
        }
    }
    museumRewardsCheck(false);   // rewards you already got on the PC
    ensureGreenhouse();
    farm->spawnDay = absoluteDay();
    {   // the morning banner was written for the fresh game's first day: show the farm's own date
        char date[64];
        snprintf(date, sizeof date, "%s %d, Year %d%s", seasonName(season), day, year, raining ? " - Rain" : "");
        std::string s = date;
        s[0] = (char)toupper((unsigned char)s[0]);
        say(s);
    }
    if (summary) {
        std::ostringstream ss;
        ss << "Welcome back, " << me.name << "! Brought over " << counts[0] << " items, " << counts[5] << " machines/chests ("
           << counts[6] << " things inside), " << counts[4] << " crops, " << counts[1] << " buildings, " << counts[2] << " animals.";
        *summary = ss.str();
    }
    return true;
}

} // namespace sv
