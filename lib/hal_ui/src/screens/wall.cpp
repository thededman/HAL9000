// Monitor wall: a grid of small live tiles, one per subsystem. One tile at a time
// is selected and shows its recent trace instead of its value.
#include <stdio.h>

#include <algorithm>

#include "hal_gauges.h"
#include "palette.h"
#include "screens.h"

namespace screens {

void wall(M5Canvas& c, const hal::Layout& L, uint32_t tMs, const ScreenData& d) {
  frame(c, L, "MONITOR WALL", tMs);
  constexpr int kCols = 4, kGap = 4;
  constexpr int kRows = 3, tiles = kCols * kRows;
  const int top = L.contentTop + 4, bottom = L.footerLineY - 4;
  const int tw = (L.w - 8 - kGap * (kCols - 1)) / kCols, th = (bottom - top - kGap * (kRows - 1)) / kRows;
  const int n = d.readings ? std::min<int>(tiles, static_cast<int>(d.readings->size())) : 0;
  const int selected = n > 0 ? hal::rotatingIndex(tMs, 2000, n) : -1;
  for (int t = 0; t < tiles; ++t) {
    const int x = 4 + (t % kCols) * (tw + kGap), y = top + (t / kCols) * (th + kGap);
    if (t >= n) {  // nothing to show here
      c.drawRect(x, y, tw, th, dim());
      continue;
    }
    const hal::Reading& r = (*d.readings)[t];
    const bool alarm = r.valid && r.alarm;
    CardColour col = !r.valid ? deadColour() : alarm ? alarmColour() : cardColour(t);
    if (alarm && (tMs / 300) % 2) col.bg = lgfx::color565(96, 8, 8);  // blink
    c.fillRect(x, y, tw, th, col.bg);
    c.setClipRect(x + 1, y + 1, tw - 2, th - 2);  // long values stay inside their tile
    c.setTextColor(col.fg);
    c.setTextDatum(top_left);
    c.drawString(r.code.c_str(), x + 5, y + (L.small ? 3 : 4), L.small ? &fonts::FreeSansBold9pt7b : &fonts::FreeSansBold12pt7b);
    if (t == selected && r.trace.size() >= 2) {
      drawTrace(c, x + 4, y + th / 2, tw - 8, th / 2 - 4, r.trace, col.fg);
    } else {
      // A value too long for the tile ("NOT RESPONDING"): the tile's colour already says alarm.
      const char* value = !r.valid ? "N/A" : c.textWidth(r.value.c_str(), &fonts::Font0) <= tw - 8 ? r.value.c_str() : alarm ? "ALARM" : r.value.c_str();
      c.setTextDatum(bottom_left);
      c.drawString(value, x + 5, y + th - (L.small ? 2 : 4), &fonts::Font0);
    }
    c.clearClipRect();
    if (t == selected) {
      c.drawRect(x, y, tw, th, white());
      c.drawRect(x + 1, y + 1, tw - 2, th - 2, white());
    }
  }
  if (n == 0) {
    c.setTextDatum(middle_center);
    c.fillRect(L.w / 2 - 40, (top + bottom) / 2 - 10, 80, 20, TFT_BLACK);
    c.setTextColor(blue());
    c.drawString("NO DATA", L.w / 2, (top + bottom) / 2, L.font2());
  }
}

}  // namespace screens
