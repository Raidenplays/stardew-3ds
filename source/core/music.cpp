#include "core/music.h"

namespace sv {

const char* const KEEP_MUSIC = "=keep";
static const char* SEASON_NAMES[4] = {"spring", "summer", "fall", "winter"};

static bool endsWith(const std::string& s, const std::string& e) {
    return s.size() >= e.size() && s.compare(s.size() - e.size(), e.size(), e) == 0;
}

std::string chooseMusic(const Game& g, MusicState& st, const std::string& current, bool currentPlaying) {
    if (g.cutscene.active) {   // cutscenes choose their own music
        const std::string& m = g.cutscene.music;
        if (m == "continue" || m.empty()) return KEEP_MUSIC;
        if (m == "none") return "";
        return m;
    }
    int hhmm = (g.timeOfDay / 60) * 100 + g.timeOfDay % 60;
    const Location& loc = g.location();
    std::string season = SEASON_NAMES[g.season & 3];
    auto it = g.data.music.find(loc.name);
    if (it != g.data.music.end()) {
        const GameData::LocationMusic& m = it->second;
        for (auto& e : m.timed)
            if (hhmm >= e.from && hhmm < e.to) return e.track;
        if (m.def == "none") return "";
        bool skip = (m.ignoreRain && g.raining) || (m.townTheme && g.season != SPRING);
        if (!m.def.empty() && !skip) return m.def;
    }
    if (!loc.map.outdoors) {
        // indoors without music of its own: the outside music carries on, but
        // weather/ambient loops stay outside
        if (current == "rain" || endsWith(current, "_ambient")) return "";
        return KEEP_MUSIC;
    }
    if (g.raining) return "rain";
    if (hhmm >= 1800) return season + "_night_ambient";
    // the day's song plays once (first time outside), then the day ambience
    int today = g.absoluteDay();
    if (st.seasonalDay != today) {
        st.seasonalDay = today;
        st.seasonalTrack = season + std::to_string(1 + (today * 7) % 3);
        return st.seasonalTrack;
    }
    if (current == st.seasonalTrack && currentPlaying) return KEEP_MUSIC;
    return season + "_day_ambient";
}

}  // namespace sv
