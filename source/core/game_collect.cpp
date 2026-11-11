// Collections (what you've shipped, caught, cooked, donated) and achievements.
#include "game.h"

#include <algorithm>
#include <cstdlib>

namespace sv {

// The lists the Collections pages show, in a stable order.
std::vector<int> Game::collectionItems(int page) const {
    std::vector<int> out;
    if (page == 0) {          // things a farm ships: crops, forage, animal and artisan goods, flowers
        for (auto& kv : data.objects) {
            int c = kv.second.category;
            if (kv.second.price > 0 && (c == -75 || c == -79 || c == -80 || c == -81 || c == -26 || c == -27 || c == -5 || c == -6 || c == -18))
                out.push_back(kv.first);
        }
    } else if (page == 1) {
        for (auto& kv : data.fish) if (data.object(kv.first)) out.push_back(kv.first);
    } else if (page == 2) {
        for (auto& kv : data.museumItems) if (data.object(kv.first)) out.push_back(kv.first);
    } else if (page == 3) {
        for (auto& r : data.cookRecipes) if (!r.outBig && data.object(r.outId)) out.push_back(r.outId);
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

int Game::collectionCount(int page, int item) const {
    if (page == 2) return museumDonated.count(item) ? 1 : 0;
    const std::map<int, int>& m = page == 0 ? shippedEver : page == 1 ? fishCaught : cookedEver;
    auto it = m.find(item);
    return it == m.end() ? 0 : it->second;
}

// id, the game's name for it (ours if the converted list is missing), and whether you've earned it.
std::vector<Game::Achievement> Game::achievements() {
    if (!achNamesLoaded) {
        achNamesLoaded = true;
        std::vector<std::string> lines;
        if (data.vfs.readTable("data/achievements.dat", lines))
            for (auto& l : lines) {
                size_t a = l.find('\t'), b = a == std::string::npos ? a : l.find('\t', a + 1);
                if (b != std::string::npos) achNames[atoi(l.c_str())] = {l.substr(a + 1, b - a - 1), l.substr(b + 1)};
            }
    }
    int h5 = 0, h8 = 0, h10 = 0;
    for (auto& n : npcs)
        if (n.def->social) { h5 += n.friendship >= 1250; h8 += n.friendship >= 2000; h10 += n.friendship >= 2500; }
    int fishKinds = (int)fishCaught.size(), fishTotal = 0, cooked = (int)cookedEver.size(), crafted = (int)craftedEver.size();
    for (auto& kv : fishCaught) fishTotal += kv.second;
    int mono = 0, poly = 0;
    for (auto& kv : shippedEver) {
        const ObjectDef* o = data.object(kv.first);
        if (!o || (o->category != -75 && o->category != -79 && o->category != -80)) continue;
        mono = std::max(mono, kv.second);
        poly += kv.second >= 15;
    }
    size_t shipKinds = 0, shipAll = collectionItems(0).size();
    for (int id : collectionItems(0)) shipKinds += shippedEver.count(id) != 0;
    size_t museumAll = data.museumItems.size();
    struct Rule { int id; const char* name; const char* desc; bool ok; };
    Rule rules[] = {
        {0, "Greenhorn (15k)", "Earn 15,000g", totalEarned >= 15000},
        {1, "Cowpoke (50k)", "Earn 50,000g", totalEarned >= 50000},
        {2, "Homesteader (250k)", "Earn 250,000g", totalEarned >= 250000},
        {3, "Millionaire (1mil)", "Earn 1,000,000g", totalEarned >= 1000000},
        {4, "Legend (10mil)", "Earn 10,000,000g", totalEarned >= 10000000},
        {5, "A Complete Collection", "Complete the museum collection", museumAll > 0 && museumDonated.size() >= museumAll},
        {28, "Treasure Trove", "Donate 40 different items to the museum", museumDonated.size() >= 40},
        {6, "A New Friend", "Reach a 5-heart friend level with someone", h5 >= 1},
        {7, "Best Friends", "Reach a 10-heart friend level with someone", h10 >= 1},
        {9, "The Beloved Farmer", "Reach a 10-heart friend level with 8 people", h10 >= 8},
        {11, "Cliques", "Reach a 5-heart friend level with 4 people", h5 >= 4},
        {12, "Networking", "Reach a 5-heart friend level with 10 people", h5 >= 10},
        {13, "Popular", "Reach a 5-heart friend level with 20 people", h5 >= 20},
        {15, "Cook", "Cook 10 different recipes", cooked >= 10},
        {16, "Sous Chef", "Cook 25 different recipes", cooked >= 25},
        {17, "Gourmet Chef", "Cook every recipe", !data.cookRecipes.empty() && cooked >= (int)collectionItems(3).size()},
        {18, "Moving Up", "Upgrade your house", houseLevel >= 1},
        {19, "Living Large", "Upgrade your house to the maximum size", houseLevel >= 2},
        {20, "D.I.Y.", "Craft 15 different items", crafted >= 15},
        {21, "Artisan", "Craft 30 different items", crafted >= 30},
        {22, "Craft Master", "Craft every item", !data.recipes.empty() && crafted >= (int)data.recipes.size()},
        {24, "Fisherman", "Catch 10 different fish", fishKinds >= 10},
        {25, "Ol' Mariner", "Catch 24 different fish", fishKinds >= 24},
        {26, "Master Angler", "Catch every fish", !data.fish.empty() && fishKinds >= (int)collectionItems(1).size()},
        {27, "Mother Catch", "Catch 100 fish", fishTotal >= 100},
        {29, "Gofer", "Complete 10 'Help Wanted' requests", questsFinished.size() >= 10},
        {30, "A Big Help", "Complete 40 'Help Wanted' requests", questsFinished.size() >= 40},
        {31, "Polyculture", "Ship 15 of each crop", poly >= 28},
        {32, "Monoculture", "Ship 300 of one crop", mono >= 300},
        {34, "Full Shipment", "Ship every item", shipAll > 0 && shipKinds >= shipAll},
        {41, "Danger In The Deep", "Reach the bottom of the mines", mineDeepest >= 120},
    };
    std::vector<Achievement> out;
    for (auto& r : rules) {
        Achievement a;
        a.id = r.id;
        auto it = achNames.find(r.id);
        a.name = it != achNames.end() ? it->second.first : r.name;
        a.desc = it != achNames.end() ? it->second.second : r.desc;
        a.earned = mailReceived.count("ach:" + std::to_string(r.id)) != 0;
        if (!a.earned && r.ok) {
            a.earned = true;
            a.fresh = true;
            mailReceived.insert("ach:" + std::to_string(r.id));
        }
        out.push_back(a);
    }
    return out;
}

// Each morning: anything newly earned gets its moment.
void Game::achievementsMorning() {
    for (auto& a : achievements())
        if (a.fresh && morningNote.empty()) morningNote = "Achievement: " + a.name + "!";
}

} // namespace sv
