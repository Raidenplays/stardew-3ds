#include "render/setupscreen.h"
#include <cstdio>

namespace sv {
namespace {
const uint32_t BG = RGBA(0x2A, 0x1A, 0x10);
const uint32_t PANEL = RGBA(0xF6, 0xD9, 0x9C);
const uint32_t EDGE = RGBA(0x85, 0x45, 0x1C);
const uint32_t INK = RGBA(0x3B, 0x22, 0x10);
const uint32_t GOLD = RGBA(0xFF, 0xD0, 0x80);
const uint32_t WHITE = RGBA(0xFF, 0xFF, 0xFF);
const uint32_t GREY = RGBA(0xC0, 0xC0, 0xC0);

void panel(Canvas& c, float x, float y, float w, float h) {
    c.rect(x - 3, y - 3, w + 6, h + 6, EDGE);
    c.rect(x, y, w, h, PANEL);
}

// word-wrapped text; returns the y after the last line
float wrap(Canvas& c, float x, float y, float maxW, float scale, uint32_t col, const std::string& s, float lh) {
    std::string line, word;
    for (size_t i = 0; i <= s.size(); i++) {
        char ch = i < s.size() ? s[i] : '\n';
        if (ch == ' ' || ch == '\n') {
            std::string trial = line.empty() ? word : line + " " + word;
            if (!line.empty() && c.textWidth(trial, scale) > maxW) {
                c.text(x, y, scale, col, line, ALIGN_LEFT);
                y += lh;
                line = word;
            } else {
                line = trial;
            }
            word.clear();
            if (ch == '\n') { c.text(x, y, scale, col, line, ALIGN_LEFT); y += lh; line.clear(); }
        } else {
            word += ch;
        }
    }
    return y;
}
}  // namespace

void drawSetupTop(Canvas& c, const setup::Setup& su, SetupView v) {
    c.rect(0, 0, 400, 240, BG);
    c.text(200, 14, 0.75f, GOLD, "stardew3ds setup", ALIGN_CENTER);
    panel(c, 16, 44, 368, 180);
    float y = 54;
    switch (v) {
    case SetupView::NeedFiles:
        y = wrap(c, 26, y, 348, 0.5f, INK, "This game needs your own copy of Stardew Valley.", 15);
        y += 6;
        y = wrap(c, 26, y, 348, 0.45f, INK,
                 "1. On your PC, open your Stardew Valley folder (Steam: right-click it, Manage, Browse local files).", 14);
        y = wrap(c, 26, y, 348, 0.45f, INK, "2. Copy the whole Content folder to your SD card here:", 14);
        c.text(36, y, 0.45f, EDGE, "sdmc:/3ds/stardew3ds/Content/", ALIGN_LEFT);
        y += 18;
        y = wrap(c, 26, y, 348, 0.45f, INK,
                 "3. Optional: put Stardew Valley Expanded and Ridgeside Village (with their Content Patcher "
                 "folders) in sdmc:/3ds/stardew3ds/mods/", 14);
        y += 4;
        wrap(c, 26, y, 348, 0.45f, INK, "Then put the SD card back and press A.", 14);
        break;
    case SetupView::Confirm:
        y = wrap(c, 26, y, 348, 0.5f, INK, "Found your Stardew Valley files!", 16);
        y += 6;
        y = wrap(c, 26, y, 348, 0.45f, INK,
                 "The game will now convert them for the 3DS. This only happens once, but it takes a while. "
                 "Keep the console plugged in and the lid open.", 14);
        y += 6;
        wrap(c, 26, y, 348, 0.45f, INK, "When it's done, the Content folder can be deleted to free up space.", 14);
        break;
    case SetupView::Running: {
        c.text(200, 64, 0.6f, INK, su.stage().empty() ? "Starting..." : su.stage(), ALIGN_CENTER);
        c.text(200, 88, 0.45f, EDGE, su.item(), ALIGN_CENTER);
        float p = su.progress();
        c.rect(40, 120, 320, 22, EDGE);
        c.rect(43, 123, 314, 16, RGBA(0xE0, 0xB8, 0x78));
        c.rect(43, 123, 314 * p, 16, RGBA(0x5C, 0xA8, 0x3C));
        char pct[16];
        snprintf(pct, sizeof pct, "%d%%", (int)(p * 100 + 0.5f));
        c.text(200, 148, 0.5f, INK, pct, ALIGN_CENTER);
        wrap(c, 26, 180, 348, 0.42f, INK, "Please don't turn off the console or take out the SD card.", 13);
        break;
    }
    case SetupView::Result:
        if (su.state() == setup::State::Done) {
            y = wrap(c, 26, y, 348, 0.5f, INK, "All done! Your farm is waiting.", 16);
        } else {
            y = wrap(c, 26, y, 348, 0.48f, INK, su.error().empty() ? "Setup stopped." : su.error(), 15);
        }
        y += 4;
        if (!su.warnings().empty()) {
            char buf[64];
            snprintf(buf, sizeof buf, "%d file(s) had problems:", (int)su.warnings().size());
            c.text(26, y, 0.42f, EDGE, buf, ALIGN_LEFT);
            y += 14;
            for (size_t i = 0; i < su.warnings().size() && y < 210; i++) {
                c.text(30, y, 0.38f, INK, su.warnings()[i], ALIGN_LEFT);
                y += 12;
            }
        }
        break;
    }
}

void drawSetupBottom(Canvas& c, const setup::Setup&, SetupView v) {
    c.rect(0, 0, 320, 240, BG);
    const char* a = nullptr;
    switch (v) {
    case SetupView::NeedFiles: a = "A: Check again"; break;
    case SetupView::Confirm: a = "A: Start converting"; break;
    case SetupView::Running: break;
    case SetupView::Result: a = "A: Continue"; break;
    }
    if (a) {
        panel(c, 60, 90, 200, 40);
        c.text(160, 103, 0.55f, INK, a, ALIGN_CENTER);
    }
    if (v != SetupView::Running) c.text(160, 210, 0.45f, GREY, "START: Exit", ALIGN_CENTER);
    else c.text(160, 110, 0.5f, WHITE, "Converting...", ALIGN_CENTER);
}

}  // namespace sv
