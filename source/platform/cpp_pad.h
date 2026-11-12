// Circle Pad Pro (CTR-009) on an old 3DS: a second stick and ZL / ZR / R over infrared (ir:USER).
#pragma once

namespace cpp_pad {

void start();                 // begins looking for the accessory in the background
void stop();
int state();                  // 0 off, 1 looking, 2 connected
// Stick position -1..1 (y up is negative, like the left pad's screen direction), and the extra buttons.
void read(float& x, float& y, bool& zl, bool& zr, bool& r);

}  // namespace cpp_pad
