#include "setup/setup.h"
#include "setup/s3t_write.h"
#include "setup/xnb.h"

#include <cerrno>
#include <cstdio>
#include <sys/stat.h>

namespace setup {

// Same list as EXTRA_TEXTURES in tools/convert.py: (content path, required?)
static const struct { const char* path; bool required; } EXTRA_TEXTURES[] = {
    {"Maps/springobjects", true},
    {"TileSheets/crops", true},
    {"TileSheets/tools", true},
    {"TerrainFeatures/hoeDirt", true},
    {"Characters/Farmer/farmer_base", true},
    {"Characters/Farmer/hairstyles", true},
    {"Characters/Farmer/pants", false},
    {"TileSheets/furniture", false},
    {"TileSheets/Craftables", false},
    {"Characters/Farmer/shirts", false},
    {"Buildings/houses", false},
    {"Buildings/Shipping Bin", false},
    {"LooseSprites/Cursors", false},
    {"LooseSprites/emotes", false},
    {"LooseSprites/font_bold", false},
    {"LooseSprites/letterBG", false},
    {"Minigames/TitleButtons", false},
    {"Minigames/Clouds", false},
    {"Minigames/treestrip", false},
    {"LooseSprites/stardewPanorama", false},
};

std::string texName(const std::string& p) {
    std::string s = p;
    for (char& c : s) if (c == '/' || c == '\\' || c == ' ') c = '_';
    return s;
}

bool fileExists(const std::string& p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0;
}

bool makeDirs(const std::string& dir) {
    for (size_t i = 1; i <= dir.size(); i++) {
        if (i < dir.size() && dir[i] != '/') continue;
        std::string sub = dir.substr(0, i);
        if (sub.empty() || sub.back() == ':') continue;   // "sdmc:"
        if (mkdir(sub.c_str(), 0777) != 0 && errno != EEXIST) return false;
    }
    return true;
}

bool convertTexture(const std::string& contentDir, const std::string& contentPath,
                    const std::string& texDir, std::string& err) {
    xnb::File f;
    if (!xnb::load(contentDir + contentPath + ".xnb", f)) { err = f.error; return false; }
    int w = 0, h = 0;
    std::vector<uint8_t> px;
    if (!xnb::readTexture(f, w, h, px)) { err = f.error; return false; }
    if (!s3t::write(texDir + texName(contentPath) + ".s3t", w, h, px)) { err = "couldn't write to the SD card"; return false; }
    return true;
}

Setup::Setup(const std::string& root) : root_(root), content_(root + "Content/") {
    state_ = installed() ? State::Done : haveContent() ? State::Ready : State::NeedFiles;
}

bool Setup::installed() const { return fileExists(root_ + "VERSION"); }

bool Setup::haveContent() const {
    return fileExists(content_ + "Maps/springobjects.xnb") && fileExists(content_ + "Data");
}

void Setup::queueTextures() {
    std::string texDir = root_ + "tex/";
    for (auto& t : EXTRA_TEXTURES) {
        std::string path = t.path;
        bool required = t.required;
        if (!fileExists(content_ + path + ".xnb")) {
            if (required) warnings_.push_back("missing " + path + ".xnb");
            continue;
        }
        Job j;
        j.stage = "Textures";
        j.item = path;
        j.weight = 1;
        j.run = [this, path, texDir](std::string& err) { return convertTexture(content_, path, texDir, err); };
        jobs_.push_back(std::move(j));
    }
}

void Setup::begin() {
    jobs_.clear();
    warnings_.clear();
    next_ = 0;
    doneWeight_ = totalWeight_ = 0;
    error_.clear();
    if (!haveContent()) { state_ = State::NeedFiles; return; }
    makeDirs(root_ + "tex/");
    makeDirs(root_ + "maps/");
    makeDirs(root_ + "data/");
    queueTextures();
    // Still being ported from tools/convert.py: maps, data tables, villagers,
    // events, fonts, and the SVE / Ridgeside merge. Until those land, finish()
    // stops short of writing VERSION so the game doesn't start half-converted.
    for (auto& j : jobs_) totalWeight_ += j.weight;
    state_ = State::Running;
}

bool Setup::step(int budgetMs, const std::function<unsigned()>& nowMs) {
    if (state_ != State::Running) return false;
    unsigned start = nowMs();
    do {
        if (next_ >= jobs_.size()) { finish(); return false; }
        Job& j = jobs_[next_++];
        stage_ = j.stage;
        item_ = j.item;
        std::string err;
        if (!j.run(err)) warnings_.push_back(j.item + ": " + err);
        doneWeight_ += j.weight;
    } while ((int)(nowMs() - start) < budgetMs);
    return true;
}

float Setup::progress() const {
    if (state_ == State::Done) return 1;
    return totalWeight_ > 0 ? doneWeight_ / totalWeight_ : 0;
}

void Setup::finish() {
    stage_ = "Done";
    item_.clear();
    // VERSION is written once every stage is ported (see begin()).
    error_ = "Textures converted. Maps and game data can't be converted on the 3DS yet; "
             "use the phone or PC converter for now.";
    state_ = State::Failed;
}

}  // namespace setup
