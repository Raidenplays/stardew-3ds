#include "setup/setup.h"
#include "setup/fontconv.h"
#include "setup/s3t_write.h"
#include "setup/xnb.h"

#include <cerrno>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <dirent.h>
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
    {"Buildings/Mailbox", false},
    {"LooseSprites/Cursors", false},
    {"TileSheets/emotes", false},
    {"Minigames/jojacorps", false},
    {"Minigames/Intro", false},
    {"LooseSprites/Fence1", false},
    {"TerrainFeatures/tree1_spring", false},
    {"TerrainFeatures/grass", false},
    {"TileSheets/bushes", false},
    {"TileSheets/rain", false},
    {"Maps/Mines/mine_frost", false},
    {"TileSheets/weapons", false},
    {"Characters/Farmer/hats", false},
    {"Characters/Monsters/Green Slime", false},
    {"Characters/Monsters/Bug", false},
    {"Characters/Monsters/Bat", false},
    {"Animals/horse", false},
    {"Animals/cat", false},
    {"Characters/Monsters/Frost Bat", false},
    {"Characters/Monsters/Lava Bat", false},
    {"Characters/Monsters/Dust Spirit", false},
    {"Characters/Monsters/Rock Crab", false},
    {"Maps/Mines/mine_lava", false},
    {"TileSheets/bobbers", false},
    {"TileSheets/critters", false},
    {"LooseSprites/Lighting/lantern", false},
    {"LooseSprites/Lighting/windowLight", false},
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
    locateGame();
    state_ = installed() ? State::Done : haveContent() ? State::Ready : State::NeedFiles;
}

static bool isContent(const std::string& dir) { return fileExists(dir + "Maps/springobjects.xnb"); }

bool Setup::locateGame() {
    // the game folder: where it's most likely, then any folder you dropped in
    std::vector<std::string> games = {root_, root_ + "Stardew Valley/"};
    if (DIR* d = opendir(root_.c_str())) {
        while (dirent* e = readdir(d)) {
            std::string n = e->d_name;
            if (n == "." || n == ".." || n == "tex" || n == "maps" || n == "data" || n == "audio") continue;
            games.push_back(root_ + n + "/");
        }
        closedir(d);
    }
    for (auto& g : games) {
        for (const char* sub : {"Content/", "Contents/Resources/Content/", "Stardew Valley.app/Contents/Resources/Content/"}) {
            if (!isContent(g + sub)) continue;
            content_ = g + sub;
            // SMAPI keeps mods next to the game: Mods/ (or, on a Mac, in Contents/MacOS/Mods)
            mods_.clear();
            for (const char* m : {"Mods/", "Contents/MacOS/Mods/", "Stardew Valley.app/Contents/MacOS/Mods/"})
                if (fileExists(g + m)) { mods_ = g + m; break; }
            if (mods_.empty() && fileExists(root_ + "mods/")) mods_ = root_ + "mods/";
            return true;
        }
    }
    return false;
}

bool Setup::installed() const { return fileExists(root_ + "VERSION"); }

bool Setup::haveContent() const { return isContent(content_) && fileExists(content_ + "Data"); }

static const char* UI_TEXTURES[] = {"LooseSprites/Cursors", "LooseSprites/stardewPanorama",
                                     "Minigames/Clouds", "Minigames/treestrip"};

bool Setup::prepareUi() {
    if (!haveContent() && !locateGame()) return false;
    makeDirs(root_ + "tex/");
    makeDirs(root_ + "data/");
    bool ok = true;
    std::string err;
    for (const char* t : UI_TEXTURES) {
        if (!fileExists(content_ + t + ".xnb")) continue;
        if (convertTexture(content_, t, root_ + "tex/", err)) uiDone_.push_back(t);
        else ok = false;
    }
    if (convertBoldFont(content_, root_, err)) uiDone_.push_back("LooseSprites/font_bold");
    if (!convertSmallFont(content_, root_, 16, err)) ok = false;
    return ok && fileExists(root_ + "tex/LooseSprites_Cursors.s3t");
}

void Setup::queueTextures() {
    std::string texDir = root_ + "tex/";
    for (auto& t : EXTRA_TEXTURES) {
        std::string path = t.path;
        bool required = t.required;
        if (std::find(uiDone_.begin(), uiDone_.end(), path) != uiDone_.end()) continue;   // done by prepareUi
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
    if (!haveContent() && !locateGame()) { state_ = State::NeedFiles; return; }
    makeDirs(root_ + "tex/");
    makeDirs(root_ + "maps/");
    makeDirs(root_ + "data/");
    queueTextures();
    {
        Job j;
        j.stage = "Fonts";
        j.item = "Fonts/SmallFont";
        j.run = [this](std::string& err) {
            if (fileExists(root_ + "data/font_small.dat")) return true;
            return convertSmallFont(content_, root_, 16, err);
        };
        jobs_.push_back(std::move(j));
    }
    {
        // the game reads your XACT sound banks directly: just move them over
        Job j;
        j.stage = "Sound";
        j.item = "Content/XACT";
        j.run = [this](std::string& err) {
            std::string src = content_ + "XACT/", dst = root_ + "audio/";
            DIR* d = opendir(src.c_str());
            if (!d) return true;   // no sound files: the game runs silently
            makeDirs(dst);
            while (dirent* e = readdir(d)) {
                std::string n = e->d_name;
                std::string low = n;
                for (char& c : low) c = (char)tolower((unsigned char)c);
                if (low.size() < 4 || (low.compare(low.size() - 4, 4, ".xwb") && low.compare(low.size() - 4, 4, ".xsb"))) continue;
                remove((dst + n).c_str());
                if (rename((src + n).c_str(), (dst + n).c_str()) != 0) err = "couldn't move " + n;
            }
            closedir(d);
            return err.empty();
        };
        jobs_.push_back(std::move(j));
    }
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
