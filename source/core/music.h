// Which music should be playing right now, following the game's rules:
// a location's own track (Data/Locations), rain, the day's seasonal song
// outdoors, then day or night ambience.
#pragma once
#include <string>

#include "core/game.h"

namespace sv {

struct MusicState { int seasonalDay = -1; std::string seasonalTrack; };

// Returns the cue to play ("" = silence), or KEEP_MUSIC to leave it alone.
extern const char* const KEEP_MUSIC;
std::string chooseMusic(const Game& g, MusicState& st, const std::string& current, bool currentPlaying);

}  // namespace sv
