// First-launch setup: converts the player's own Stardew Valley files, copied
// to sdmc:/3ds/stardew3ds/Content/, into the engine's formats on the console.
// Works in small steps so the caller can draw a progress bar between them.
#pragma once
#include <functional>
#include <string>
#include <vector>

namespace setup {

enum class State { NeedFiles, Ready, Running, Done, Failed };

struct Job {
    std::string stage;     // shown as the big label ("Textures")
    std::string item;      // shown under it ("Maps/springobjects")
    float weight = 1;      // relative cost, for the progress bar
    std::function<bool(std::string& err)> run;
};

class Setup {
public:
    explicit Setup(const std::string& root);   // e.g. "sdmc:/3ds/stardew3ds/"

    // Converted files present and complete?
    bool installed() const;
    // The player's Content folder copied over?
    bool haveContent() const;
    // Where the player should put things (for the instructions screen).
    std::string contentDir() const { return content_; }

    State state() const { return state_; }
    void begin();
    // Runs jobs for about budgetMs (at least one). Returns false when finished.
    bool step(int budgetMs, const std::function<unsigned()>& nowMs);
    float progress() const;
    const std::string& stage() const { return stage_; }
    const std::string& item() const { return item_; }
    const std::string& error() const { return error_; }
    const std::vector<std::string>& warnings() const { return warnings_; }
    size_t jobCount() const { return jobs_.size(); }

private:
    void queueTextures();
    void finish();

    std::string root_, content_;
    std::vector<Job> jobs_;
    size_t next_ = 0;
    float doneWeight_ = 0, totalWeight_ = 0;
    State state_ = State::NeedFiles;
    std::string stage_, item_, error_;
    std::vector<std::string> warnings_;
};

// "Maps/spring_outdoorsTileSheet" -> "Maps_spring_outdoorsTileSheet"
std::string texName(const std::string& contentPath);
bool fileExists(const std::string& p);
bool makeDirs(const std::string& dir);
// Converts Content/<contentPath>.xnb to tex/<texName>.s3t.
bool convertTexture(const std::string& contentDir, const std::string& contentPath,
                    const std::string& texDir, std::string& err);

}  // namespace setup
