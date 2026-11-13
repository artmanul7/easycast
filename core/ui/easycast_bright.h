// EasyCast 0.2.8 (Claude): brightness of the screen the game is on. easycast.Brightness = % of the panel maximum
// (0 = leave the system value). Settings > Video slider; in game Select + D-Pad Up/Down (mapping combos btn_bright_up /
// btn_bright_down) in steps of 10 %. The launcher (run.py) applies the saved value at start and puts the system
// brightness of both panels back when the emulator ends (F + volume of BananaOS keeps working as before).
#pragma once
#include "cfg/option.h"

namespace config {
extern Option<int> EasycastScreen;			// 0 = top panel (backlight1), 1 = bottom (backlight0)
extern Option<int, false> EasycastBrightness;
}

namespace easycast_bright {
int readPct();				// current brightness of the game panel, % (0 if unknown)
void set(int pct);			// option + panel, no save (the settings screen saves on close)
void step(int dir);			// +1 / -1: next 10 % step, save settings, short notice
}
