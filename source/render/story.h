// The new-game story, timed from the original: Grandpa's bedside, "XX Years
// later", the Joja office, the desk drawer and Grandpa's letter, then the bus
// ride to Stardew Valley. Art comes from your files (Minigames/jojacorps,
// Minigames/Intro, Cursors' mountains); words from Strings/StringsFromCSFiles.
// The scene plays on the top screen; Grandpa's words and the letter are on
// the bottom screen, like all dialogue.
#pragma once
#include <string>
#include <vector>

#include "canvas.h"
#include "core/game.h"

namespace sv {

struct StoryUi {
    float t = 0;            // seconds into the current part
    int part = 0;           // see story.cpp
    bool done = false;
    int letterPage = 0;
    std::vector<std::string> letterPages;   // built on first use
    std::vector<std::string> lines;         // Grandpa's lines, in order
    std::string yearsLater = "XX Years later";
};

// Reads data/story.dat (the converter writes it). Missing file = English defaults.
void storyInit(StoryUi& s, const GameData& data, const Game& g);
void storyTick(StoryUi& s, float dt);
void storyA(StoryUi& s);         // A / tap: next line or letter page
void storySkip(StoryUi& s);      // START: skip the whole story
// The music for the current part (Stardew cue names; "" = silence).
std::string storyMusic(const StoryUi& s);
void drawStoryTop(Canvas& c, const Game& g, StoryUi& s);
void drawStoryBottom(Canvas& c, const Game& g, StoryUi& s);

}  // namespace sv
