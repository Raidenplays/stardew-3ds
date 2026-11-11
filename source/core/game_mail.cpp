// The mailbox: dated letters, trigger letters, letters cutscenes send,
// and what's inside them (items, money, recipes, quests).
#include <algorithm>
#include <cstdlib>
#include <sstream>

#include "game.h"

namespace sv {

static const char* SEASON_KEYS[4] = {"spring", "summer", "fall", "winter"};

static std::vector<std::string> splitList(const std::string& s, const std::string& sep) {
    std::vector<std::string> out;
    size_t p = 0;
    while (p <= s.size()) {
        size_t e = s.find(sep, p);
        std::string part = s.substr(p, e == std::string::npos ? std::string::npos : e - p);
        while (!part.empty() && part[0] == ' ') part.erase(0, 1);
        while (!part.empty() && part.back() == ' ') part.pop_back();
        if (!part.empty()) out.push_back(part);
        if (e == std::string::npos) break;
        p = e + sep.size();
    }
    return out;
}

// A subset of the game's query language, enough for the letters.
bool Game::checkCondition(const std::string& query) const {
    if (query.empty() || query == "-") return true;
    for (const std::string& raw : splitList(query, ", ")) {
        std::string q = raw;
        bool neg = !q.empty() && q[0] == '!';
        if (neg) q.erase(0, 1);
        std::istringstream ss(q);
        std::string kind;
        ss >> kind;
        std::vector<std::string> a;
        for (std::string w; ss >> w;) a.push_back(w);
        bool ok = false;
        if (kind == "PLAYER_HAS_MAIL" && a.size() >= 2) {
            ok = mailReceived.count(a[1]) > 0 ||
                 std::find(mailbox.begin(), mailbox.end(), a[1]) != mailbox.end();
        } else if (kind == "PLAYER_HAS_SEEN_EVENT" && a.size() >= 2) {
            ok = eventsSeen.count(a[1]) > 0;
        } else if ((kind == "PLAYER_FRIENDSHIP_POINTS" || kind == "PLAYER_HEARTS") && a.size() >= 3) {
            const NpcState* n = npcByName(a[1]);
            int have = n ? (kind == "PLAYER_HEARTS" ? n->hearts() : n->friendship) : 0;
            ok = have >= atoi(a[2].c_str());
        } else if (kind == "SEASON") {
            for (auto& s : a) ok |= s == SEASON_KEYS[season & 3];
        } else if (kind == "YEAR" && !a.empty()) {
            ok = year >= atoi(a[0].c_str());
        } else if (kind == "DAY_OF_MONTH") {
            for (auto& d : a) ok |= atoi(d.c_str()) == day;
        } else if (kind == "WEATHER" && a.size() >= 2) {
            for (size_t i = 1; i < a.size(); i++) ok |= (a[i] == "Rain") == raining && (a[i] == "Rain" || a[i] == "Sun");
        } else if (kind == "PLAYER_MONEY_EARNED" && a.size() >= 2) {
            ok = totalEarned >= atoll(a[1].c_str());
        } else {
            ok = false;   // not understood yet: don't send the letter
        }
        if (ok == neg) return false;
    }
    return true;
}

void Game::runTriggers(const std::string& when) {
    for (auto& t : data.mailTriggers) {
        if (t.when != when || triggersApplied.count(t.id)) continue;
        if (!checkCondition(t.condition)) continue;
        bool any = false;
        for (auto& act : t.actions) {
            std::istringstream ss(act);
            std::string verb, who, key, now;
            ss >> verb >> who >> key >> now;
            if (verb == "AddMail" && !key.empty() && data.mail.count(key) && !mailReceived.count(key)) {
                if (now == "now" || when == "DayStarted") mailbox.push_back(key);
                else mailTomorrow.insert(key);
                any = true;
            } else if (verb == "MarkEventSeen" && !key.empty()) {
                const_cast<std::set<std::string>&>(eventsSeen).insert(key);
                any = true;
            }
        }
        if (any) triggersApplied.insert(t.id);
    }
}

void Game::deliverMorningMail() {
    for (auto& k : mailTomorrow)
        if (!mailReceived.count(k) && std::find(mailbox.begin(), mailbox.end(), k) == mailbox.end())
            mailbox.push_back(k);
    mailTomorrow.clear();
    // dated letters: "spring_12" every year, "spring_2_1" only in year 1
    std::string base = std::string(SEASON_KEYS[season & 3]) + "_" + std::to_string(day);
    for (const std::string& k : {base, base + "_" + std::to_string(year)})
        if (data.mail.count(k) && !mailReceived.count(k) &&
            std::find(mailbox.begin(), mailbox.end(), k) == mailbox.end())
            mailbox.push_back(k);
    runTriggers("DayStarted");
}

Game::Letter Game::parseLetter(const std::string& key, const std::string& raw, const std::string& farmer) {
    Letter L;
    L.key = key;
    std::string text = raw;
    size_t t = text.find("[#]");
    if (t != std::string::npos) {
        L.title = text.substr(t + 3);
        text = text.substr(0, t);
    }
    // pull the attachments out
    for (;;) {
        size_t a = text.find("%item ");
        if (a == std::string::npos) break;
        size_t b = text.find("%%", a);
        if (b == std::string::npos) break;
        std::istringstream ss(text.substr(a + 6, b - a - 6));
        std::string kind;
        ss >> kind;
        if (kind == "object") {
            int id, n;
            while (ss >> id >> n) L.items.push_back({id, n});
        } else if (kind == "money") {
            int lo = 0, hi = 0;
            ss >> lo;
            if (!(ss >> hi)) hi = lo;
            L.money = (lo + hi) / 2;
        } else if (kind == "quest") {
            ss >> L.quest;
        } else if (kind == "craftingRecipe" || kind == "cookingRecipe") {
            std::string rest, w;
            while (ss >> w) rest += (rest.empty() ? "" : " ") + w;
            L.recipe = rest;
            L.cooking = kind == "cookingRecipe";
        }
        text.erase(a, b + 2 - a);
    }
    // the farmer's name, line breaks
    text = fillTokens(text);
    for (size_t p; (p = text.find('@')) != std::string::npos;) text.replace(p, 1, farmer);
    L.text = text;
    return L;
}

bool Game::openMail() {
    if (mailbox.empty()) return false;
    std::string key = mailbox.front();
    auto it = data.mail.find(key);
    letter = parseLetter(key, it != data.mail.end() ? it->second : "", me.name);
    letterOpen = true;
    return true;
}

void Game::takeLetterItems() {
    if (!letterOpen) return;
    std::vector<std::pair<int, int>> left;
    for (auto& it : letter.items)
        if (!addItem(it.first, it.second)) left.push_back(it);
    letter.items = left;
    if (letter.money > 0) {
        money += letter.money;
        letter.money = 0;
    }
}

void Game::closeLetter() {
    if (!letterOpen) return;
    takeLetterItems();
    if (!letter.quest.empty()) addQuest(letter.quest);
    if (!letter.recipe.empty()) {
        // the recipe name in letters can be missing ("learn the recipe in the
        // next event") - then nothing is learned here
        mailReceived.insert((letter.cooking ? "cook:" : "craft:") + letter.recipe);
        say("Learned: " + letter.recipe);
    }
    mailReceived.insert(letter.key);
    if (!mailbox.empty() && mailbox.front() == letter.key) mailbox.erase(mailbox.begin());
    letterOpen = false;
}

} // namespace sv
