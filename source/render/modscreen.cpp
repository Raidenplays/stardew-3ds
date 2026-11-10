#include "modscreen.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <set>

namespace sv {

static const uint32_t BG = RGBA(0x2A, 0x1A, 0x10);
static const uint32_t PANEL = RGBA(0xF9, 0xD8, 0x96);
static const uint32_t PANEL_DARK = RGBA(0xDC, 0xA4, 0x56);
static const uint32_t BORDER = RGBA(0x85, 0x3D, 0x1A);
static const uint32_t TEXT = RGBA(0x3B, 0x1F, 0x0E);
static const uint32_t SOFT = RGBA(0x6B, 0x44, 0x23);
static const uint32_t ROW = RGBA(0xFC, 0xE6, 0xB4);
static const uint32_t ROW_ON = RGBA(0xFF, 0xF1, 0xC9);
static const uint32_t SEL = RGBA(0xC8, 0x32, 0x1E);
static const uint32_t GREEN = RGBA(0x3E, 0x7A, 0x2C);
static const uint32_t GREY = RGBA(0x9A, 0x86, 0x6E);
static const int VISIBLE = 7;

bool modScreenInit(ModScreen& s, const Vfs& vfs) {
    s = ModScreen();
    std::vector<std::string> lines;
    readTextLines(vfs.root() + "mods.txt", lines);
    std::vector<std::string> on;
    std::set<std::string> off;
    for (auto& l : lines) {
        if (l.rfind("#off ", 0) == 0) off.insert(l.substr(5));
        else if (!l.empty() && l[0] != '#') on.push_back(l);
    }
    std::vector<ModInfo> found = vfs.scanMods();
    std::map<std::string, ModInfo> byId;
    for (auto& m : found) byId[m.id] = m;

    for (auto& id : on) {
        auto it = byId.find(id);
        if (it == byId.end()) {
            ModInfo m;
            m.id = m.name = id;
            m.present = false;
            m.description = "Folder is missing from mods/ - it won't load.";
            s.mods.push_back(m);
            s.changed = true;
            continue;
        }
        it->second.enabled = true;
        s.mods.push_back(it->second);
        byId.erase(it);
    }
    for (auto& m : found) {   // remaining: off or brand new (alphabetical)
        if (!byId.count(m.id)) continue;
        ModInfo x = byId[m.id];
        bool isNew = !off.count(x.id);
        x.enabled = isNew;
        if (isNew) s.changed = true;
        s.mods.push_back(x);
    }
    return s.changed;
}

static void clampScroll(ModScreen& s) {
    int n = (int)s.mods.size();
    if (s.cursor < 0) s.cursor = 0;
    if (s.cursor >= n) s.cursor = std::max(0, n - 1);
    if (s.cursor < s.scroll) s.scroll = s.cursor;
    if (s.cursor >= s.scroll + VISIBLE) s.scroll = s.cursor - VISIBLE + 1;
}

void modScreenKey(ModScreen& s, ModKey k) {
    int n = (int)s.mods.size();
    switch (k) {
    case MK_UP: s.cursor--; break;
    case MK_DOWN: s.cursor++; break;
    case MK_TOGGLE:
        if (n && s.mods[s.cursor].present) s.mods[s.cursor].enabled = !s.mods[s.cursor].enabled;
        break;
    case MK_MOVE_UP:
        if (s.cursor > 0) { std::swap(s.mods[s.cursor], s.mods[s.cursor - 1]); s.cursor--; }
        break;
    case MK_MOVE_DOWN:
        if (s.cursor + 1 < n) { std::swap(s.mods[s.cursor], s.mods[s.cursor + 1]); s.cursor++; }
        break;
    case MK_ALL_ON: for (auto& m : s.mods) if (m.present) m.enabled = true; break;
    case MK_ALL_OFF: for (auto& m : s.mods) m.enabled = false; break;
    case MK_PLAY: s.done = true; break;
    }
    clampScroll(s);
}

// bottom-screen buttons: 4 across the bottom row
struct Btn { int x, y, w, h; ModKey k; const char* label; };
static const Btn BUTTONS[] = {
    {8, 150, 72, 30, MK_MOVE_UP, "Move up"},
    {86, 150, 72, 30, MK_MOVE_DOWN, "Move down"},
    {164, 150, 72, 30, MK_TOGGLE, "On / Off"},
    {242, 150, 70, 30, MK_ALL_OFF, "All off"},
    {8, 190, 304, 40, MK_PLAY, "Play  (START)"},
};

void modScreenTouch(ModScreen& s, int px, int py) {
    for (auto& b : BUTTONS)
        if (px >= b.x && px < b.x + b.w && py >= b.y && py < b.y + b.h) { modScreenKey(s, b.k); return; }
}

std::vector<std::string> modScreenEnabled(const ModScreen& s) {
    std::vector<std::string> ids;
    for (auto& m : s.mods)
        if (m.enabled && m.present) ids.push_back(m.id);
    return ids;
}

bool modScreenSave(const ModScreen& s, const std::string& root) {
    FILE* f = fopen((root + "mods.txt").c_str(), "wb");
    if (!f) return false;
    fputs("# enabled mods, in load order (later ones win)\n", f);
    for (auto& m : s.mods)
        if (m.enabled && m.present) fprintf(f, "%s\n", m.id.c_str());
    for (auto& m : s.mods)
        if (!m.enabled && m.present) fprintf(f, "#off %s\n", m.id.c_str());
    fclose(f);
    return true;
}

static void frame(Canvas& c, float x, float y, float w, float h, uint32_t fill) {
    c.rect(x - 2, y - 2, w + 4, h + 4, BORDER);
    c.rect(x, y, w, h, fill);
}

static std::string fit(Canvas& c, std::string s, float scale, float w) {
    if (c.textWidth(s, scale) <= w) return s;
    while (!s.empty() && c.textWidth(s + "...", scale) > w) s.pop_back();
    return s + "...";
}

void drawModTop(Canvas& c, const ModScreen& s) {
    c.rect(0, 0, 400, 240, BG);
    frame(c, 8, 8, 384, 26, PANEL_DARK);
    c.text(200, 12, 0.6f, TEXT, "Mods", ALIGN_CENTER);
    int on = (int)modScreenEnabled(s).size();
    char buf[48];
    snprintf(buf, sizeof buf, "%d of %d on", on, (int)s.mods.size());
    c.text(384, 15, 0.42f, SOFT, buf, ALIGN_RIGHT);
    if (s.changed) c.text(16, 15, 0.42f, SEL, "New mods found", ALIGN_LEFT);

    frame(c, 8, 42, 384, 172, PANEL);
    if (s.mods.empty()) {
        c.text(200, 110, 0.5f, SOFT, "No mods installed. Use the desktop mod manager.", ALIGN_CENTER);
    }
    for (int i = 0; i < VISIBLE && s.scroll + i < (int)s.mods.size(); i++) {
        int idx = s.scroll + i;
        const ModInfo& m = s.mods[idx];
        float y = 46 + i * 24;
        bool cur = idx == s.cursor;
        c.rect(12, y, 376, 22, cur ? ROW_ON : ROW);
        if (cur) {
            c.rect(12, y, 376, 2, SEL);
            c.rect(12, y + 20, 376, 2, SEL);
        }
        // load-order number
        snprintf(buf, sizeof buf, "%d", idx + 1);
        c.text(26, y + 4, 0.42f, SOFT, buf, ALIGN_CENTER);
        // checkbox
        c.rect(38, y + 4, 14, 14, BORDER);
        c.rect(40, y + 6, 10, 10, m.present ? RGBA(0xFF, 0xF8, 0xE6) : GREY);
        if (m.enabled && m.present) c.rect(42, y + 8, 6, 6, GREEN);
        uint32_t col = m.present ? (m.enabled ? TEXT : SOFT) : SEL;
        c.text(60, y + 3, 0.5f, col, fit(c, m.name, 0.5f, 250), ALIGN_LEFT);
        std::string right = m.present ? m.version : "missing";
        c.text(382, y + 4, 0.42f, m.present ? SOFT : SEL, right, ALIGN_RIGHT);
    }
    if (s.scroll > 0) c.text(200, 40, 0.4f, SOFT, "^", ALIGN_CENTER);
    if (s.scroll + VISIBLE < (int)s.mods.size()) c.text(200, 210, 0.4f, SOFT, "v", ALIGN_CENTER);
    c.text(200, 222, 0.4f, RGBA(0xE0, 0xC8, 0xA0),
           "A on/off   L/R move   X all on   Y all off   START play", ALIGN_CENTER);
}

void drawModBottom(Canvas& c, const ModScreen& s) {
    c.rect(0, 0, 320, 240, BG);
    frame(c, 8, 8, 304, 134, PANEL);
    if (!s.mods.empty()) {
        const ModInfo& m = s.mods[s.cursor];
        c.text(16, 12, 0.55f, TEXT, fit(c, m.name, 0.55f, 288), ALIGN_LEFT);
        std::string by = m.version.empty() ? "" : "v" + m.version;
        if (!m.author.empty()) by += (by.empty() ? "" : "  by ") + m.author;
        c.text(16, 30, 0.42f, SOFT, fit(c, by, 0.42f, 288), ALIGN_LEFT);
        // word-wrapped description
        std::string line, word, d = m.description + " ";
        float y = 50;
        for (char ch : d) {
            if (ch == ' ') {
                if (c.textWidth(line + word, 0.45f) > 284 && !line.empty()) {
                    if (y < 118) c.text(16, y, 0.45f, TEXT, line, ALIGN_LEFT);
                    y += 15;
                    line.clear();
                }
                line += word + " ";
                word.clear();
            } else {
                word += ch;
            }
        }
        if (y < 118) c.text(16, y, 0.45f, TEXT, line, ALIGN_LEFT);
    }
    if (!s.backupNote.empty()) c.text(160, 124, 0.4f, GREEN, s.backupNote, ALIGN_CENTER);
    else c.text(160, 124, 0.4f, SOFT, "Your save is backed up when the mod list changes", ALIGN_CENTER);

    for (auto& b : BUTTONS) {
        bool play = b.k == MK_PLAY;
        frame(c, b.x, b.y, b.w, b.h, play ? RGBA(0x7C, 0xB3, 0x42) : PANEL_DARK);
        c.text(b.x + b.w / 2.0f, b.y + (play ? 11 : 8), play ? 0.6f : 0.42f, TEXT, b.label, ALIGN_CENTER);
    }
}

} // namespace sv
