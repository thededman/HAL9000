#pragma once
// HAL Paper's page (M5Paper: 540x960 e-ink, 16 greys). He has no speaker: he keeps the
// minutes. The page is a header with his eye, the minutes of the conversation (hal_minutes.h
// says where each entry goes), and a band at the foot where his own line is written large
// while the others wait. An original design, drawn by the same code on the device and in the
// Mac's previews (hal9000 --shots DIR --layout paper).
//
// E-ink is slow and each change is pushed as its own rectangle, so every function draws one
// part and returns the rectangle it touched.
#include <M5GFX.h>

#include <string>

#include "hal_minutes.h"

namespace paperui {

constexpr int kW = 540, kH = 960;
constexpr int kCols = 36;      // characters to a row of the minutes
constexpr int kRows = 22;      // rows of minutes to a page
constexpr int kOwnCols = 24;   // ... and of his own line, written large
constexpr int kOwnRows = 5;

struct Rect {
  int x = 0, y = 0, w = 0, h = 0;
};

// The whole page: header, every entry on it, and the band (`own` may be empty).
void drawPage(lgfx::LovyanGFX& f, const hal::Minutes& minutes, const std::string& status, int battery,
              const std::string& own);
Rect drawHeader(lgfx::LovyanGFX& f, const std::string& status, int battery, int page);  // his eye, name, state
Rect drawEntry(lgfx::LovyanGFX& f, const hal::MinutesEntry& entry);                     // one entry of the minutes
Rect drawOwn(lgfx::LovyanGFX& f, const std::string& text);                              // his own line, large ("" clears it)

}  // namespace paperui
