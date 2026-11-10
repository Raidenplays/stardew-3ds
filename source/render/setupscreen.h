// Screens for first-launch setup: "copy your files here", confirm, progress.
#pragma once
#include "render/canvas.h"
#include "setup/setup.h"

namespace sv {

enum class SetupView { NeedFiles, Confirm, Running, Result };

void drawSetupTop(Canvas& c, const setup::Setup& su, SetupView v);
void drawSetupBottom(Canvas& c, const setup::Setup& su, SetupView v);

}  // namespace sv
