#include "render/story.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "core/vfs.h"
#include "render/scene.h"

namespace sv {
namespace {

const char* JOJA = "Minigames_jojacorps";   // Grandpa's room, the office, the desk
const char* INTRO = "Minigames_Intro";      // the bus, the road sign
const char* CURSORS = "LooseSprites_Cursors";
const char* CLOUDS = "Minigames_Clouds";
const char* EMOTES = "TileSheets_emotes";
const char* TREE = "TerrainFeatures_tree1_spring";
const char* FENCE = "LooseSprites_Fence1";
const char* LETTER = "LooseSprites_letterBG";

const uint32_t BLACK = RGBA(0, 0, 0), WHITE = RGBA(255, 255, 255);
const uint32_t TXT = RGBA(0x3B, 0x1F, 0x0E), SOFT = RGBA(0x6B, 0x44, 0x23), CREAM = RGBA(0xFF, 0xE8, 0xB0);

enum Part { GRANDPA, YEARS, OFFICE, CUBICLES, DESK, LETTER_READ, AFTER, BUS, END };

// Grandpa's lines (seconds into his scene), from the original
struct Line { float from, to; int idx; };
const Line LINES[] = {{9.0f, 13.6f, 0}, {14.0f, 25.4f, 1}, {26.0f, 29.6f, 2}, {30.0f, 35.4f, 3},
                      {36.0f, 45.6f, 4}, {46.0f, 55.6f, 5}, {56.0f, 65.6f, 6}, {66.0f, 68.0f, 7}};
const float GRANDPA_LEN = 68.6f, YEARS_LEN = 5.0f, OFFICE_LEN = 7.0f, CUBICLE_LEN = 25.0f, DESK_LEN = 6.6f,
            AFTER_LEN = 5.0f, BUS_LEN = 30.0f;
const float CHARS_PER_SEC = 40;

const char* DEFAULT_LINES[8] = {
    "...and for my very special grandson:", "I want you to have this sealed envelope.",
    "No, no, don't open it yet... have patience.", "Now, listen close...",
    "There will come a day when you feel crushed by the burden of modern life...",
    "...and your bright spirit will fade before a growing emptiness.",
    "When that happens, my boy, you'll be ready for this gift.", "Now, let Grandpa rest..."};

float clamp01(float x) { return x < 0 ? 0 : x > 1 ? 1 : x; }
float smooth(float x) { x = clamp01(x); return x * x * (3 - 2 * x); }
void fill(Canvas& c, float w, uint32_t col, float a) {
    if (a > 0) c.rect(0, 0, w, 240, (col & 0xFFFFFF00) | (uint32_t)(255 * clamp01(a)));
}

std::vector<std::string> wrap(Canvas& c, const std::string& s, float scale, float maxW) {
    std::vector<std::string> out;
    std::string line, word;
    for (size_t i = 0; i <= s.size(); i++) {
        char ch = i < s.size() ? s[i] : '^';
        if (ch == ' ' || ch == '^') {
            std::string trial = line.empty() ? word : line + " " + word;
            if (!line.empty() && c.textWidth(trial, scale) > maxW) { out.push_back(line); line = word; }
            else line = trial;
            word.clear();
            if (ch == '^' && i < s.size()) { out.push_back(line); line.clear(); }
        } else {
            word += ch;
        }
    }
    if (!line.empty()) out.push_back(line);
    return out;
}

std::string replaceAll(std::string s, const std::string& a, const std::string& b) {
    for (size_t p = 0; (p = s.find(a, p)) != std::string::npos; p += b.size()) s.replace(p, a.size(), b);
    return s;
}

std::string letterText;   // filled by storyInit

// ---------------------------------------------------------------- scenes
void grandpa(Canvas& c, StoryUi& s) {
    // the firelight flickers between the two paintings (about 8 times a second)
    unsigned k = (unsigned)(s.t * 8) * 2654435761u;
    bool b = (k >> 13) & 1;
    c.image(JOJA, 427, b ? 240 : 0, 427, 240, -13, 0);
    // the envelope he holds up
    if (s.t >= 21.5f && s.t < 24.6f) {
        float pop = s.t < 21.65f ? 0.62f : 0.5f;
        c.imageScaled(JOJA, 729, 524, 130, 62, 127 - 65 * pop, 110 - 31 * pop, pop);
    }
    fill(c, 400, RGBA(0x16, 0x2C, 0x52), 1 - s.t / 1.6f);            // fade in from night blue
    fill(c, 400, BLACK, (s.t - (GRANDPA_LEN - 0.6f)) / 0.6f);        // and out to black
}

void years(Canvas& c, StoryUi& s) {
    c.rect(0, 0, 400, 240, BLACK);
    float a = std::min(clamp01(s.t / 0.6f), clamp01((YEARS_LEN - 0.4f - s.t) / 0.6f));
    c.text(200, 112, 0.5f, (WHITE & 0xFFFFFF00) | (uint32_t)(255 * a), s.yearsLater, ALIGN_CENTER);
}

void office(Canvas& c, StoryUi& s) {
    c.image(JOJA, 0, 0, 427, 240, -13, 0);   // the big hall full of cubicles
    fill(c, 400, BLACK, 1 - s.t / 0.8f);
    fill(c, 400, BLACK, (s.t - (OFFICE_LEN - 0.7f)) / 0.7f);
}

// the long row of cubicles, panning to your desk
const float STRIP_Y = 600, STRIP_H = 200, DESK_X = 897, DESK_TOP = 735;
void cubicles(Canvas& c, const Game& g, StoryUi& s) {
    float cam = std::floor(740 * smooth(s.t / 18.0f));
    c.rect(0, 0, 400, 240, RGBA(0x4C, 0x4C, 0x50));
    float y0 = 240 - STRIP_H - 6;
    c.image(JOJA, (int)cam, (int)STRIP_Y, 400, (int)STRIP_H, 0, y0);
    // you, at the empty desk; the desk front drawn again over your legs
    float fx = DESK_X + 22 - cam, fy = y0 + (DESK_TOP - STRIP_Y) + 14;
    if (fx > -30 && fx < 430) {
        drawFarmerPreview(c, g, g.me.outfit, fx, fy, FACE_RIGHT, 0, 1.0f);
        c.image(JOJA, (int)DESK_X, (int)DESK_TOP, 87, (int)(STRIP_Y + STRIP_H - DESK_TOP), DESK_X - cam,
                y0 + (DESK_TOP - STRIP_Y));
        if (s.t >= 19.5f && s.t < 21.8f && c.texWidth(EMOTES) > 0) {   // "?"
            int f = std::min(3, (int)((s.t - 19.5f) / 0.12f));
            c.image(EMOTES, f * 16, 16, 16, 16, fx - 8, fy - 46);
        }
    }
    c.rect(0, y0 + STRIP_H, 400, 240, RGBA(0x5E, 0x5E, 0x62));
    fill(c, 400, BLACK, 1 - s.t / 0.8f);
    fill(c, 400, BLACK, (s.t - (CUBICLE_LEN - 0.8f)) / 0.8f);
}

// close-up of your desk: the drawer opens on Grandpa's envelope
void desk(Canvas& c, StoryUi& s, int envFrame, float envGrow) {
    c.rect(0, 0, 400, 240, BLACK);
    c.image(JOJA, 0, 240, 427, 92, -13, 0);       // desktop: computer, mouse, pencil
    c.image(JOJA, 0, 333, 258, 88, -13, 92);      // the shadow under it
    bool open = envFrame >= 0;
    if (open) c.image(JOJA, 264, 432, 163, 147, 251, 92);   // drawer pulled out
    else {
        c.image(JOJA, 550, 540, 163, 35, 251, 96);
        c.image(JOJA, 550, 540, 163, 35, 251, 136);
    }
    if (open) {
        static const int ex[3] = {729, 859, 991}, ey[3] = {524, 522, 508}, ew[3] = {130, 132, 131}, eh[3] = {62, 64, 78};
        int f = std::min(2, envFrame);
        float k = 0.55f + 0.35f * smooth(envGrow);
        float cx = 332 + (326 - 332) * smooth(envGrow), cy = 128 + (116 - 128) * smooth(envGrow);
        c.imageScaled(JOJA, ex[f], ey[f], ew[f], eh[f], std::floor(cx - ew[f] * k / 2), std::floor(cy - eh[f] * k / 2), k);
    }
    c.image(JOJA, 16, 428, 241, 172, 3, 68);      // you, from behind
}

void bus(Canvas& c, StoryUi& s) {
    float camY = std::floor(-130 * (1 - smooth(s.t / 6.0f)));   // the camera tilts down from the sky
    // sky
    for (int y = 0; y < 240; y += 4) {
        float k = clamp01((y + camY + 130) / 300.0f);
        c.rect(0, y, 400, 4, RGBA((uint8_t)(0x2C + 0x40 * k), (uint8_t)(0x78 + 0x50 * k), (uint8_t)(0xE6 + 0x10 * k)));
    }
    if (c.texWidth(CLOUDS) > 0) {
        struct Cl { int sx, sy, w, h; float x, y; };
        static const Cl cl[4] = {{0, 471, 149, 66, 60, -110}, {152, 447, 123, 55, 250, -120}, {410, 467, 64, 36, 30, -60},
                                 {0, 471, 149, 66, 300, -55}};
        for (auto& q : cl) {
            float x = std::fmod(q.x - s.t * 4 + 800, 560.0f) - 150;
            c.image(CLOUDS, q.sx, q.sy, q.w, q.h, std::floor(x), q.y - camY);
        }
    }
    // the ocean, then the mountains (title screen range), drifting slowly
    c.rect(0, 22 - camY, 400, 10, RGBA(0x2E, 0x8C, 0xE0));
    float mx = std::fmod(s.t * 8, 640.0f);
    for (float x = -mx; x < 400; x += 640) c.image(CURSORS, 0, 737, 640, 148, std::floor(x), 26 - camY);
    // grass verge, curb and road
    c.rect(0, 160 - camY, 400, 44, RGBA(0x5C, 0xB4, 0x2A));
    c.rect(0, 160 - camY, 400, 3, RGBA(0x78, 0xC8, 0x3A));
    c.rect(0, 204 - camY, 400, 4, RGBA(0xA8, 0xA8, 0xA8));
    c.rect(0, 208 - camY, 400, 240, RGBA(0x80, 0x80, 0x84));
    float road = s.t * 95;
    for (float x = -std::fmod(road, 48.0f); x < 400; x += 48) c.rect(std::floor(x), 232 - camY, 22, 3, RGBA(0xF0, 0xC8, 0x20));
    // things going by: trees, fences, and finally the sign
    float scroll = s.t * 70;
    auto onScreen = [&](float wx, float w) { return wx - scroll > -w && wx - scroll < 400; };
    for (float wx : {520.0f, 1150.0f, 1700.0f}) {
        if (!onScreen(wx, 48) || c.texWidth(TREE) == 0) continue;
        float x = std::floor(wx - scroll);
        c.image(TREE, 32, 128, 16, 32, x + 16, 172 - camY);   // trunk
        c.image(TREE, 0, 0, 48, 96, x, 100 - camY);           // leaves
    }
    for (auto rng : {std::make_pair(900.0f, 1300.0f), std::make_pair(1650.0f, 2050.0f)})
        for (float wx = rng.first; wx < rng.second; wx += 16)
            if (onScreen(wx, 16) && c.texWidth(FENCE) > 0) c.image(FENCE, 16, 64, 16, 32, std::floor(wx - scroll), 172 - camY);
    if (onScreen(2080, 62)) c.image(INTRO, 49, 178, 62, 55, std::floor(2080 - scroll), 150 - camY);
    // the bus
    float bob = ((int)(s.t * 6) % 2) ? 1 : 0;
    c.image(INTRO, 0, 0, 128, 64, 136, 184 - camY + bob);
    fill(c, 400, RGBA(0x3F, 0x7F, 0xE6), 1 - s.t / 1.2f);
    fill(c, 400, BLACK, (s.t - (BUS_LEN - 2)) / 2);
}

void advance(StoryUi& s) {
    s.t = 0;
    s.part++;
    if (s.part >= END) s.done = true;
}

}  // namespace

void storyInit(StoryUi& s, const GameData& data, const Game& g) {
    s = StoryUi();
    for (auto* l : DEFAULT_LINES) s.lines.push_back(l);
    std::vector<std::string> lines;
    std::string letter =
        "Dear {0},^^If you're reading this, you must be in dire need of a change.^^^I've enclosed the deed to that "
        "place... my pride and joy: {1} Farm. It's located in Stardew Valley, on the southern coast.^^Love, Grandpa";
    static const char* KEYS[8] = {"GrandpaStory.cs.12026", "GrandpaStory.cs.12029", "GrandpaStory.cs.12030",
                                  "GrandpaStory.cs.12031", "GrandpaStory.cs.12034", "GrandpaStory.cs.12035",
                                  "GrandpaStory.cs.12036", "GrandpaStory.cs.12040"};
    if (readTextLines(data.vfs.path("data/story.dat"), lines))
        for (auto& l : lines) {
            size_t tab = l.find('\t');
            if (tab == std::string::npos) continue;
            std::string k = l.substr(0, tab), v = l.substr(tab + 1);
            for (int i = 0; i < 8; i++) if (k == KEYS[i]) s.lines[i] = v;
            if (k == "GrandpaStory.cs.12051") letter = v;
            if (k == "GrandpaStory.cs.12059") s.yearsLater = v;
        }
    letterText = replaceAll(replaceAll(letter, "{0}", g.me.name), "{1}", g.me.farm);
    letterText = replaceAll(letterText, "^^P.S.", "^^^P.S.");   // the P.S. gets its own page, like the original
}

void storyTick(StoryUi& s, float dt) {
    if (s.done) return;
    s.t += dt;
    switch (s.part) {
        case GRANDPA: if (s.t >= GRANDPA_LEN) advance(s); break;
        case YEARS: if (s.t >= YEARS_LEN) advance(s); break;
        case OFFICE: if (s.t >= OFFICE_LEN) advance(s); break;
        case CUBICLES: if (s.t >= CUBICLE_LEN) advance(s); break;
        case DESK: if (s.t >= DESK_LEN) advance(s); break;
        case LETTER_READ: break;   // waits for you to read it
        case AFTER: if (s.t >= AFTER_LEN) advance(s); break;
        case BUS: if (s.t >= BUS_LEN) advance(s); break;
        default: s.done = true;
    }
}

void storyA(StoryUi& s) {
    if (s.part == GRANDPA) {
        // jump to the next line (or finish typing this one)
        for (auto& l : LINES)
            if (s.t >= l.from && s.t < l.to) {
                float full = l.from + s.lines[l.idx].size() / CHARS_PER_SEC;
                s.t = s.t < full ? full : l.to;
                return;
            }
        return;
    }
    if (s.part == LETTER_READ) {
        if (s.letterPage + 1 < (int)s.letterPages.size()) s.letterPage++;
        else advance(s);
    }
}

void storySkip(StoryUi& s) {
    s.part = END;
    s.done = true;
}

void drawStoryTop(Canvas& c, const Game& g, StoryUi& s) {
    switch (s.part) {
        case GRANDPA: grandpa(c, s); break;
        case YEARS: years(c, s); break;
        case OFFICE: office(c, s); break;
        case CUBICLES: cubicles(c, g, s); break;
        case DESK: {
            int f = s.t < 2.5f ? -1 : 0;
            desk(c, s, f, (s.t - 4.0f) / 1.6f);
            fill(c, 400, BLACK, 1 - s.t / 0.6f);
            break;
        }
        case LETTER_READ: desk(c, s, 2, 1); break;
        case AFTER:
            desk(c, s, 2, 1);
            fill(c, 400, RGBA(0x3F, 0x7F, 0xE6), (s.t - 1.5f) / 2.0f);   // fades to blue
            break;
        case BUS: bus(c, s); break;
        default: c.rect(0, 0, 400, 240, BLACK);
    }
}

void drawStoryBottom(Canvas& c, const Game& g, StoryUi& s) {
    (void)g;
    c.rect(0, 0, 320, 240, BLACK);
    if (s.part == GRANDPA) {
        for (auto& l : LINES) {
            if (s.t < l.from || s.t >= l.to) continue;
            // Grandpa talks in the dialogue box, typed out like the original
            std::string text = s.lines[l.idx];
            size_t n = (size_t)((s.t - l.from) * CHARS_PER_SEC);
            if (n < text.size()) text = text.substr(0, n);
            uiFrame(c, 4, 4, 312, 132);
            auto ls = wrap(c, text, 0.5f, 294);
            for (size_t i = 0; i < ls.size() && i < 7; i++) c.text(12, 12 + i * 16, 0.5f, TXT, ls[i]);
            c.text(308, 118, 0.42f, SOFT, "A  next", ALIGN_RIGHT);
        }
    } else if (s.part == LETTER_READ) {
        if (s.letterPages.empty()) {
            // split into pages that fit the paper: '^^^' always starts a new page
            std::vector<std::string> lines;
            std::string t = letterText;
            size_t p = 0;
            while (p <= t.size()) {
                size_t q = t.find("^^^", p);
                std::string part = t.substr(p, q == std::string::npos ? std::string::npos : q - p);
                auto ls = wrap(c, part, 0.42f, 286);
                std::string page;
                int count = 0;
                for (auto& ln : ls) {
                    if (count == 12) { s.letterPages.push_back(page); page.clear(); count = 0; }
                    page += ln + "\n";
                    count++;
                }
                if (!page.empty()) s.letterPages.push_back(page);
                if (q == std::string::npos) break;
                p = q + 3;
            }
        }
        if (c.texWidth(LETTER) > 0) c.image(LETTER, 0, 0, 312, 196, 4, 4);
        else uiFrame(c, 4, 4, 312, 196);
        const std::string& page = s.letterPages[std::min((int)s.letterPages.size() - 1, s.letterPage)];
        float y = 16;
        size_t a = 0;
        while (a < page.size()) {
            size_t b = page.find('\n', a);
            c.text(18, y, 0.42f, TXT, page.substr(a, b - a));
            y += 14;
            a = b + 1;
        }
        bool last = s.letterPage + 1 >= (int)s.letterPages.size();
        uiFrame(c, 8, 206, 304, 28, true);
        c.text(160, 212, 0.48f, TXT, last ? "A  Close" : "A  Next page", ALIGN_CENTER);
    } else if (!s.done) {
        c.text(160, 214, 0.4f, RGBA(0x80, 0x80, 0x80), "START: Skip", ALIGN_CENTER);
    }
    (void)CREAM;
}

}  // namespace sv
