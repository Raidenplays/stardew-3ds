// First-launch setup screens, drawn like the rest of the game: the title
// screen's sky behind, the game's menu frames and fonts on top. Before the
// player's files are found there's no game art yet, so the same layout falls
// back to plain colours.
#include "render/setupscreen.h"
#include "render/scene.h"
#include "render/title.h"
#include <cstdio>

namespace sv {
namespace {
const uint32_t TXT = RGBA(0x3B, 0x1F, 0x0E);
const uint32_t SOFT = RGBA(0x6B, 0x44, 0x23);
const uint32_t RED = RGBA(0x9A, 0x2A, 0x16);
const uint32_t CREAM = RGBA(0xFF, 0xE8, 0xB0);

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

// A titled menu window like the game's: heading in the bold font on top.
float window(Canvas& c, float x, float y, float w, float h, const char* title) {
    uiFrame(c, x, y, w, h);
    c.text(x + w / 2, y + 10, 0.6f, TXT, title, ALIGN_CENTER);
    return y + 34;
}

void button(Canvas& c, float y, const char* label, bool lit) {
    uiSlot(c, 50, y, 220, 40, lit);
    c.text(160, y + 12, 0.5f, TXT, label, ALIGN_CENTER);
}
}  // namespace

void drawSetupTop(Canvas& c, const setup::Setup& su, SetupView v, float t) {
    titleSky(c, 400, 240, t, false);
    const float X = 18, W = 364, TX = X + 16, TW = W - 32;
    switch (v) {
    case SetupView::NeedFiles: {
        float y = window(c, X, 30, W, 170, "Welcome!");
        y = wrap(c, TX, y, TW, 0.45f, TXT, "This game plays with your own copy of Stardew Valley.", 15) + 4;
        y = wrap(c, TX, y, TW, 0.42f, TXT,
                 "1. On your PC, find your Stardew Valley folder (Steam: right-click the game, Manage, "
                 "Browse local files).", 13) + 2;
        y = wrap(c, TX, y, TW, 0.42f, TXT, "2. Copy that whole folder onto your SD card, into:", 13);
        c.text(TX + 10, y, 0.42f, RED, "sdmc:/3ds/stardew3ds/", ALIGN_LEFT);
        y += 15;
        y = wrap(c, TX, y, TW, 0.42f, TXT,
                 "Mods like Stardew Valley Expanded and Ridgeside Village come along if they're in its Mods "
                 "folder.", 13) + 2;
        wrap(c, TX, y, TW, 0.42f, SOFT, "Then put the SD card back in and press A.", 13);
        break;
    }
    case SetupView::Confirm: {
        float y = window(c, X, 30, W, 180, "Found your files!");
        y = wrap(c, TX, y + 4, TW, 0.45f, TXT,
                 "Your farm is almost ready. The game will now convert your files for the 3DS.", 15) + 6;
        y = wrap(c, TX, y, TW, 0.42f, TXT,
                 "This only happens once, but it takes a while. Keep the console plugged in with the lid open.",
                 13) + 6;
        wrap(c, TX, y, TW, 0.42f, SOFT, "Afterwards you can delete the Content folder to free up space.", 13);
        break;
    }
    case SetupView::Running: {
        float y = window(c, X, 40, W, 160, su.stage().empty() ? "Getting ready..." : su.stage().c_str());
        c.text(200, y, 0.42f, SOFT, su.item(), ALIGN_CENTER);
        float p = su.progress();
        uiSlot(c, 50, y + 22, 300, 24, false);
        if (p > 0) c.rect(54, y + 26, 292 * p, 16, RGBA(0x5C, 0xA8, 0x3C));
        if (p > 0) c.rect(54, y + 26, 292 * p, 3, RGBA(0x8C, 0xD0, 0x5C));   // highlight
        char pct[16];
        snprintf(pct, sizeof pct, "%d%%", (int)(p * 100 + 0.5f));
        c.text(200, y + 52, 0.5f, TXT, pct, ALIGN_CENTER);
        c.text(200, y + 80, 0.4f, SOFT, "Please don't turn off the console or take out the SD card.", ALIGN_CENTER);
        break;
    }
    case SetupView::Result: {
        bool done = su.state() == setup::State::Done;
        float y = window(c, X, 20, W, 200, done ? "All done!" : "Setup stopped");
        y = wrap(c, TX, y + 2, TW, 0.45f, TXT,
                 done ? "Your farm is waiting." : (su.error().empty() ? "Something went wrong." : su.error()), 15) + 4;
        if (!su.warnings().empty()) {
            char buf[64];
            snprintf(buf, sizeof buf, "%d file(s) had problems:", (int)su.warnings().size());
            c.text(TX, y, 0.42f, RED, buf, ALIGN_LEFT);
            y += 15;
            for (size_t i = 0; i < su.warnings().size() && y < 204; i++) {
                c.text(TX + 6, y, 0.4f, SOFT, su.warnings()[i], ALIGN_LEFT);
                y += 13;
            }
        }
        break;
    }
    }
}

void drawSetupBottom(Canvas& c, const setup::Setup&, SetupView v, float t) {
    titleSky(c, 320, 240, t, true);
    switch (v) {
    case SetupView::NeedFiles: button(c, 70, "Check again", true); break;
    case SetupView::Confirm: button(c, 70, "Start converting", true); break;
    case SetupView::Result: button(c, 70, "Continue", true); break;
    case SetupView::Running:
        uiFrame(c, 70, 74, 180, 34);
        c.text(160, 84, 0.45f, TXT, "Converting...", ALIGN_CENTER);
        break;
    }
    if (v != SetupView::Running) {
        uiFrame(c, 110, 136, 100, 26, true);
        c.text(160, 142, 0.4f, TXT, "START: Exit", ALIGN_CENTER);
    }
    (void)CREAM;
}

}  // namespace sv
