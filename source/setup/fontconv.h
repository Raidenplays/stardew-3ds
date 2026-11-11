#pragma once
#include <string>

namespace setup {
// root ends in '/', contentDir is root + "Content/"
bool convertSmallFont(const std::string& contentDir, const std::string& root, int px, std::string& err);
bool convertBoldFont(const std::string& contentDir, const std::string& root, std::string& err);
}
