#pragma once
// HAL Zero's interface on the Cardputer Zero's 320x170 screen: boot sequence,
// HUD panels either side of the eye, the command strip, the menu and the
// conversation subtitles.
#include <M5GFX.h>

#include <string>
#include <vector>

#include "hal_menu.h"

struct HudInfo {
  const char* mode = "IDLE";
  uint32_t uptimeS = 0;
  int cpuTempC = -1;         // -1: unknown
  bool wifi = false;
  const char* link = "OFF";  // OFF, READY, TALKING, NO ANSWER
  bool linkBusy = false;     // blink the link line
  uint8_t level = 0;         // voice loudness
  int motion = 0;            // camera motion score 0..255
  const char* optics = "OFFLINE";
  const char* tilt = "N/A";
  int hour = -1, minute = -1;
  bool quiet = false;
};

void drawHud(M5Canvas& f, const HudInfo& h, uint32_t nowMs);
void drawCommandStrip(M5Canvas& f, const char* text);
// The boot screen at `t` ms: lines appear one by one. True once all are shown.
bool drawBoot(M5Canvas& f, uint32_t t, const std::vector<std::string>& lines);
void drawMenu(M5Canvas& f, const char* title, hal::Menu& menu);
// Who's speaking in a conversation, their line, and line n of total.
void drawSubtitle(M5Canvas& f, bool halZero, const std::string& text, int n, int total);
// A short status message across the bottom (e.g. "THE DESK UNIT ISN'T ANSWERING").
void drawBanner(M5Canvas& f, const char* text, uint32_t nowMs);
