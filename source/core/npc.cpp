#include "npc.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <queue>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace sv {

static const char* SEASONS[4] = {"spring", "summer", "fall", "winter"};
static const char* DOW[7] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};

static std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == sep) {
            out.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    out.push_back(cur);
    return out;
}

static std::vector<std::string> words(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream ss(s);
    std::string w;
    while (ss >> w) out.push_back(w);
    return out;
}

static bool isNumber(const std::string& s) {
    if (s.empty()) return false;
    size_t i = (s[0] == '-') ? 1 : 0;
    if (i >= s.size()) return false;
    for (; i < s.size(); i++)
        if (!isdigit((unsigned char)s[i])) return false;
    return true;
}

static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t"), b = s.find_last_not_of(" \t");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

int parseClock(const std::string& t) {
    int v = atoi(t.c_str());
    return (v / 100) * 60 + v % 100;
}

// ------------------------------------------------------------------ schedules

static const std::string* findKey(const NpcDef& n, const std::string& k) {
    auto it = n.schedules.find(k);
    return it == n.schedules.end() ? nullptr : &it->second;
}

static std::string chooseKey(const NpcDef& n, const ScheduleContext& c) {
    std::string season = SEASONS[c.season & 3], dow = DOW[c.weekday % 7], day = std::to_string(c.day);
    if (findKey(n, season + "_" + day)) return season + "_" + day;
    for (int h = std::min(c.hearts, 14); h >= 1; h--)
        if (findKey(n, day + "_" + std::to_string(h))) return day + "_" + std::to_string(h);
    if (findKey(n, day)) return day;
    if (c.raining) {
        if ((c.day % 2) == 0 && findKey(n, "rain2")) return "rain2";
        if (findKey(n, "rain")) return "rain";
    }
    for (int h = std::min(c.hearts, 14); h >= 1; h--) {
        std::string k = season + "_" + dow + "_" + std::to_string(h);
        if (findKey(n, k)) return k;
    }
    if (findKey(n, season + "_" + dow)) return season + "_" + dow;
    if (findKey(n, dow)) return dow;
    if (findKey(n, season)) return season;
    if (findKey(n, "spring_" + dow)) return "spring_" + dow;
    if (findKey(n, "spring")) return "spring";
    if (findKey(n, "default")) return "default";
    return "";
}

std::vector<SchedEntry> resolveSchedule(const NpcDef& n, const ScheduleContext& c) {
    std::vector<SchedEntry> out;
    std::string key = chooseKey(n, c);
    if (key.empty()) return out;
    std::string script = *findKey(n, key);

    // Leading commands: GOTO, NOT friendship, MAIL (we never have the mail yet).
    for (int guard = 0; guard < 8; guard++) {
        std::vector<std::string> segs = split(script, '/');
        std::vector<std::string> w = words(segs[0]);
        if (w.empty()) return out;
        if (w[0] == "GOTO" && w.size() > 1) {
            std::string k = w[1] == "season" ? SEASONS[c.season & 3] : w[1];
            if (k == "NO_SCHEDULE") return out;
            const std::string* next = findKey(n, k);
            if (!next) next = findKey(n, "spring");
            if (!next) return out;
            script = *next;
            continue;
        }
        if (w[0] == "NOT" || w[0] == "MAIL") {
            // condition not met: carry on with the rest of the script
            size_t slash = script.find('/');
            if (slash == std::string::npos) return out;
            script = script.substr(slash + 1);
            continue;
        }
        break;
    }

    std::string loc = n.homeMap;
    for (const std::string& seg : split(script, '/')) {
        std::vector<std::string> w = words(seg);
        if (w.size() < 3) continue;
        std::string t = w[0];
        if (!t.empty() && (t[0] == 'a' || t[0] == 'A')) t = t.substr(1);
        if (!isNumber(t)) continue;
        size_t i = 1;
        if (!isNumber(w[1])) {
            loc = w[1];
            i = 2;
        }
        if (i + 1 >= w.size() || !isNumber(w[i]) || !isNumber(w[i + 1])) continue;
        SchedEntry e;
        e.time = parseClock(t);
        e.loc = loc;
        e.x = atoi(w[i].c_str());
        e.y = atoi(w[i + 1].c_str());
        e.facing = 0;
        if (i + 2 < w.size() && isNumber(w[i + 2])) {
            static const int conv[4] = {2, 1, 0, 3};   // game: 0 up 1 right 2 down 3 left
            e.facing = conv[atoi(w[i + 2].c_str()) & 3];
        }
        out.push_back(e);
    }
    std::stable_sort(out.begin(), out.end(),
                     [](const SchedEntry& a, const SchedEntry& b) { return a.time < b.time; });
    return out;
}

// ------------------------------------------------------------------ routes

std::vector<GraphEdge> routeBetween(const GameData& data, const std::string& from,
                                    const std::string& to, bool* ok) {
    std::vector<GraphEdge> out;
    if (ok) *ok = from == to;
    if (from == to) return out;
    std::unordered_map<std::string, std::pair<std::string, const GraphEdge*>> prev;
    std::queue<std::string> q;
    q.push(from);
    prev[from] = {"", nullptr};
    while (!q.empty()) {
        std::string m = q.front();
        q.pop();
        if (m == to) break;
        auto it = data.graph.find(m);
        if (it == data.graph.end()) continue;
        for (const GraphEdge& e : it->second) {
            if (prev.count(e.target)) continue;
            prev[e.target] = {m, &e};
            q.push(e.target);
        }
    }
    if (!prev.count(to)) return out;
    for (std::string m = to; m != from;) {
        auto& p = prev[m];
        out.push_back(*p.second);
        m = p.first;
    }
    std::reverse(out.begin(), out.end());
    if (ok) *ok = true;
    return out;
}

std::vector<std::pair<int, int>> findPath(const TileMap& m, int sx, int sy, int gx, int gy,
                                          int maxNodes) {
    std::vector<std::pair<int, int>> path;
    int w = m.width(), h = m.height();
    gx = std::max(0, std::min(gx, w - 1));
    gy = std::max(0, std::min(gy, h - 1));
    sx = std::max(0, std::min(sx, w - 1));
    sy = std::max(0, std::min(sy, h - 1));
    if (sx == gx && sy == gy) return path;
    auto passable = [&](int x, int y) {
        if (x == gx && y == gy) return true;
        uint8_t f = m.flags(x, y);
        return !((f & CF_BLOCKED) && !(f & CF_PASSABLE));
    };
    std::vector<int> came((size_t)w * h, -1), cost((size_t)w * h, 1 << 30);
    typedef std::pair<int, int> PI;   // (f, cell)
    std::priority_queue<PI, std::vector<PI>, std::greater<PI>> open;
    int start = sy * w + sx, goal = gy * w + gx;
    cost[start] = 0;
    open.push({std::abs(gx - sx) + std::abs(gy - sy), start});
    int expanded = 0;
    static const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
    while (!open.empty() && expanded < maxNodes) {
        int cell = open.top().second;
        open.pop();
        if (cell == goal) break;
        expanded++;
        int cx = cell % w, cy = cell / w;
        for (int k = 0; k < 4; k++) {
            int nx = cx + dx[k], ny = cy + dy[k];
            if (nx < 0 || ny < 0 || nx >= w || ny >= h || !passable(nx, ny)) continue;
            int nc = ny * w + nx, c2 = cost[cell] + 1;
            if (c2 < cost[nc]) {
                cost[nc] = c2;
                came[nc] = cell;
                open.push({c2 + std::abs(gx - nx) + std::abs(gy - ny), nc});
            }
        }
    }
    if (came[goal] < 0) return path;
    for (int c = goal; c != start; c = came[c]) path.push_back({c % w, c / w});
    std::reverse(path.begin(), path.end());
    return path;
}

// ------------------------------------------------------------------ dialogue

static int emotionCode(const std::string& code) {
    if (code == "h") return 1;
    if (code == "s") return 2;
    if (code == "u") return 3;
    if (code == "l") return 4;
    if (code == "a") return 5;
    if (isNumber(code)) return atoi(code.c_str());
    return -1;
}

// Cleans one text segment and pulls out a trailing portrait command.
static std::string cleanText(std::string s, const std::string& farmer, int* portrait) {
    // gendered text "male^female": use the first half
    size_t caret = s.find('^');
    if (caret != std::string::npos) s = s.substr(0, caret);
    // item gift brackets like [433 434]
    for (size_t a; (a = s.find('[')) != std::string::npos;) {
        size_t b = s.find(']', a);
        if (b == std::string::npos) break;
        s.erase(a, b - a + 1);
    }
    std::string out;
    for (size_t i = 0; i < s.size(); i++) {
        char c = s[i];
        if (c == '@') {
            out += farmer;
        } else if (c == '$' && i + 1 < s.size()) {
            size_t j = i + 1;
            while (j < s.size() && isalnum((unsigned char)s[j])) j++;
            int e = emotionCode(s.substr(i + 1, j - i - 1));
            if (e >= 0 && portrait) *portrait = e;
            i = j - 1;
        } else if (c == '%' && out.empty()) {
            // narration marker: drop it
        } else {
            out += c;
        }
    }
    return trim(out);
}

std::vector<DialoguePage> parseDialogue(const std::string& raw, const std::string& farmer) {
    std::vector<DialoguePage> pages;
    std::vector<std::string> segs = split(raw, '#');
    for (size_t i = 0; i < segs.size(); i++) {
        std::string seg = trim(segs[i]);
        if (seg.empty()) continue;
        if (seg.rfind("$q", 0) == 0) {
            DialoguePage p;
            if (i + 1 < segs.size()) p.text = cleanText(segs[++i], farmer, &p.portrait);
            while (i + 2 < segs.size() + 1 && i + 1 < segs.size() && trim(segs[i + 1]).rfind("$r", 0) == 0) {
                std::vector<std::string> w = words(segs[i + 1]);
                DialogueChoice ch;
                if (w.size() > 2) ch.points = atoi(w[2].c_str());
                if (w.size() > 3) ch.responseKey = w[3];
                ch.label = i + 2 < segs.size() ? cleanText(segs[i + 2], farmer, nullptr) : "...";
                p.choices.push_back(ch);
                i += 2;
            }
            pages.push_back(p);
            continue;
        }
        if (seg.rfind("$y", 0) == 0) {
            // $y 'Question_Answer1_Reply1_Answer2_Reply2'
            size_t a = seg.find('\''), b = seg.rfind('\'');
            if (a != std::string::npos && b > a) {
                std::vector<std::string> parts = split(seg.substr(a + 1, b - a - 1), '_');
                DialoguePage p;
                p.text = cleanText(parts[0], farmer, &p.portrait);
                for (size_t k = 1; k + 1 < parts.size(); k += 2) {
                    DialogueChoice ch;
                    ch.label = cleanText(parts[k], farmer, nullptr);
                    ch.inlineReply = parts[k + 1];
                    p.choices.push_back(ch);
                }
                pages.push_back(p);
            }
            continue;
        }
        if (seg[0] == '$') continue;   // $b, $e and other commands
        DialoguePage p;
        p.text = cleanText(seg, farmer, &p.portrait);
        if (!p.text.empty()) pages.push_back(p);
    }
    return pages;
}

static bool hasDia(const NpcDef& n, const std::string& k) { return n.dialogue.count(k) != 0; }

std::string pickDialogueKey(const NpcDef& n, const ScheduleContext& c, bool introduced) {
    if (!introduced && hasDia(n, "Introduction")) return "Introduction";
    std::string season = SEASONS[c.season & 3], dow = DOW[c.weekday % 7];
    std::string k = season + "_" + std::to_string(c.day);
    if (hasDia(n, k)) return k;
    if (c.raining && hasDia(n, "__rain")) return "__rain";
    for (int h = 10; h >= 2; h -= 2) {
        if (c.hearts < h) continue;
        if (hasDia(n, season + "_" + dow + std::to_string(h))) return season + "_" + dow + std::to_string(h);
        if (hasDia(n, dow + std::to_string(h))) return dow + std::to_string(h);
    }
    if (hasDia(n, season + "_" + dow)) return season + "_" + dow;
    if (hasDia(n, dow)) return dow;
    return "";
}

static bool contains(const std::vector<int>& v, int x) { return std::find(v.begin(), v.end(), x) != v.end(); }

Taste giftTaste(const GameData& data, const NpcDef& n, int itemId) {
    const ObjectDef* o = data.object(itemId);
    int cat = o ? o->category : 0;
    for (int t = 0; t < TASTE_COUNT; t++)
        if (contains(n.tastes[t], itemId)) return (Taste)t;
    if (cat < 0)
        for (int t = 0; t < TASTE_COUNT; t++)
            if (contains(n.tastes[t], cat)) return (Taste)t;
    for (int t = 0; t < TASTE_COUNT; t++)
        if (contains(data.universalTastes[t], itemId)) return (Taste)t;
    if (cat < 0)
        for (int t = 0; t < TASTE_COUNT; t++)
            if (contains(data.universalTastes[t], cat)) return (Taste)t;
    return TASTE_NEUTRAL;
}

int giftPoints(Taste t) {
    switch (t) {
        case TASTE_LOVE: return 80;
        case TASTE_LIKE: return 45;
        case TASTE_DISLIKE: return -20;
        case TASTE_HATE: return -40;
        default: return 20;
    }
}

} // namespace sv
