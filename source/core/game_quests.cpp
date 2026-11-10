// Quests from your Data/Quests: the journal, progress hooks, rewards.
#include <algorithm>
#include <cstdlib>

#include "game.h"

namespace sv {

static const int MEET_GOAL = 28;   // villagers to meet for the introductions quest

bool Game::addQuest(const std::string& id) {
    const QuestDef* q = data.quest(id);
    if (!q || questsFinished.count(id)) return false;
    for (auto& s : quests)
        if (s.id == id) return false;
    QuestState st;
    st.id = id;
    quests.insert(quests.begin(), st);
    hasUnseenQuest = true;
    say("New quest: " + q->title);
    if (q->type == "Social") questMet();   // count anyone already met
    if (q->type == "Location" && !q->target.empty() && cur && cur->name == q->target[0]) questEntered(cur->name);
    return true;
}

int Game::questGoal(const QuestDef& q) const {
    if (q.type == "Social") {
        int social = 0;
        for (auto& n : npcs) social += n.def->social;
        return std::min(MEET_GOAL, std::max(1, social));
    }
    if (q.type == "ItemHarvest" && q.target.size() >= 2) return std::max(1, atoi(q.target[1].c_str()));
    return 0;
}

void Game::completeQuest(QuestState& s) {
    if (s.done) return;
    s.done = true;
    const QuestDef* q = data.quest(s.id);
    if (!q) return;
    say("Quest complete: " + q->title + (q->reward > 0 ? "  (collect " + std::to_string(q->reward) + "g)" : ""));
    hasUnseenQuest = true;
    std::vector<std::string> next = q->next;   // q may move while adding
    for (auto& n : next) addQuest(n);
}

void Game::questItem(int itemId, int count) {
    for (size_t i = 0; i < quests.size(); i++) {
        QuestState& s = quests[i];
        const QuestDef* q = data.quest(s.id);
        if (s.done || !q || q->target.empty()) continue;
        if ((q->type == "ItemHarvest" || q->type == "Fishing") && atoi(q->target[0].c_str()) == itemId &&
            q->target[0][0] != 'B') {
            s.progress += count;
            int goal = std::max(1, questGoal(*q));
            if (s.progress >= goal) completeQuest(s);
        }
    }
}

void Game::questCrafted(bool big, int id) {
    for (size_t i = 0; i < quests.size(); i++) {
        QuestState& s = quests[i];
        const QuestDef* q = data.quest(s.id);
        if (s.done || !q || q->type != "Crafting" || q->target.empty()) continue;
        const std::string& t = q->target[0];
        bool match = big ? (t[0] == 'B' && atoi(t.c_str() + 1) == id) : (t[0] != 'B' && atoi(t.c_str()) == id);
        if (match) completeQuest(s);
    }
}

void Game::questEntered(const std::string& location) {
    for (size_t i = 0; i < quests.size(); i++) {
        QuestState& s = quests[i];
        const QuestDef* q = data.quest(s.id);
        if (s.done || !q || q->target.empty()) continue;
        if (q->type == "Location" && q->target[0] == location) completeQuest(s);
        // the "go to the beach" style introductions are plain Basic quests whose
        // objective names a place; the beach one is the one that matters early on
        if (q->type == "Basic" && s.id == "13" && location == "Beach") completeQuest(s);
    }
}

void Game::questMet() {
    int met = 0;
    for (auto& n : npcs) met += n.def->social && n.introduced;
    for (size_t i = 0; i < quests.size(); i++) {
        QuestState& s = quests[i];
        const QuestDef* q = data.quest(s.id);
        if (s.done || !q || q->type != "Social") continue;
        s.progress = met;
        if (met >= questGoal(*q)) completeQuest(s);
    }
}

bool Game::questDelivered(const std::string& npc, int itemId) {
    for (size_t i = 0; i < quests.size(); i++) {
        QuestState& s = quests[i];
        const QuestDef* q = data.quest(s.id);
        if (s.done || !q || q->type != "ItemDelivery" || q->target.size() < 2) continue;
        if (q->target[0] == npc && atoi(q->target[1].c_str()) == itemId) {
            completeQuest(s);
            return true;
        }
    }
    return false;
}

bool Game::collectQuest(size_t i) {
    if (i >= quests.size() || !quests[i].done) return false;
    const QuestDef* q = data.quest(quests[i].id);
    int reward = q ? q->reward : 0;
    money += reward;
    if (reward > 0) say("+" + std::to_string(reward) + "g");
    questsFinished.insert(quests[i].id);
    quests.erase(quests.begin() + i);
    return true;
}

} // namespace sv
