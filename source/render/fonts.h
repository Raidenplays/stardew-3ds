// Loads the game-font tables the converter wrote (data/font_small.dat,
// data/font_bold.dat) into a Canvas. Missing files = keep the system font.
#pragma once
#include <functional>
#include <string>

#include "canvas.h"

bool loadUiFonts(Canvas& c, const std::function<std::string(const std::string&)>& resolve);
