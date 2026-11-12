// Circle Pad Pro (CTR-009) on an old 3DS: a second stick and ZL / ZR / R over infrared (ir:USER).
#pragma once

namespace cpp_pad {

void start();                 // begins looking for the accessory in the background
void stop();
int state();                  // 0 off, 1 looking, 2 connected
// Stick position -1..1 (y up is negative, like the left pad's screen direction), and the extra buttons.
void read(float& x, float& y, bool& zl, bool& zr, bool& r);

// For the calibration screen: the stick as the accessory reports it (0..4095 each way, about 2048 at rest)
// and the buttons (bit 0 ZL, 1 ZR, 2 R).
void raw(int& x, int& y, int& buttons);
// Where the stick rests and how far it travels each way; rangeX <= 0 goes back to guessing.
void calibrate(int centreX, int centreY, int rangeX, int rangeY);

}  // namespace cpp_pad
