// Screens for first-launch setup: "copy your files here", confirm, progress.
#pragma once
#include "render/canvas.h"
#include "setup/setup.h"

namespace sv {

enum class SetupView { NeedFiles, Confirm, Running, Result };

// t = seconds, for the drifting clouds
void drawSetupTop(Canvas& c, const setup::Setup& su, SetupView v, float t);
void drawSetupBottom(Canvas& c, const setup::Setup& su, SetupView v, float t);
// The big button on the bottom screen (touch target).
inline bool setupButtonHit(int x, int y) { return x >= 50 && x < 270 && y >= 70 && y < 110; }

}  // namespace sv
