// Status cards: one full-screen flat-colour card per subsystem, with a big
// three-letter code and its real reading, cutting to the next every 1.6 s. A
// screen is only up for about five cards, so each opening starts somewhere else
// in the round, after the first card in alarm (if there is one).
#include <stdio.h>

#include "hal_gauges.h"
#include "hal_readings.h"
#include "palette.h"
#include "screens.h"

namespace screens {

void cards(M5Canvas& c, const hal::Layout& L, uint32_t tMs, const ScreenData& d) {
  if (!d.readings || d.readings->empty()) {
    c.fillScreen(TFT_BLACK);
    c.setTextDatum(middle_center);
    c.setTextColor(dim());
    c.drawString("NO DATA", L.w / 2, L.h / 2, L.font2());
    return;
  }
  const std::vector<hal::Reading>& rs = *d.readings;
  const int n = static_cast<int>(rs.size());
  const int i = hal::showingIndex(tMs, 1600, n, d.seed, hal::firstAlarm(rs));
  const hal::Reading& r = rs[i];
  const bool alarm = r.valid && r.alarm;
  const CardColour col = !r.valid ? deadColour() : alarm ? alarmColour() : cardColour(i);
  c.fillScreen(col.bg);
  c.setTextColor(col.fg);

  // Whose card, and which one.
  char buf[24];
  c.setTextDatum(top_right);
  c.drawString(L.name, L.w - 8, 6, &fonts::Font0);
  snprintf(buf, sizeof buf, "%02d/%02d", i + 1, n);
  c.drawString(buf, L.w - 8, 18, &fonts::Font0);

  // The code, as big as the screen allows, and its name.
  const float size = L.small ? 1.5f : 2.2f;
  const int codeY = L.small ? 10 : 14, codeH = int(36 * size);
  c.setTextDatum(top_left);
  c.setTextSize(size);
  c.drawString(r.code.c_str(), 12, codeY, &fonts::FreeSansBold24pt7b);
  c.setTextSize(1);
  c.drawString(r.name.c_str(), 14, codeY + codeH + (L.small ? 4 : 12), L.small ? &fonts::FreeSansBold9pt7b : &fonts::FreeSansBold12pt7b);

  // The reading, and its recent trace in a box.
  const int base = L.h - (L.small ? 16 : 24);
  const lgfx::IFont* valueFont = L.small ? static_cast<const lgfx::IFont*>(&fonts::FreeSansBold12pt7b) : static_cast<const lgfx::IFont*>(&fonts::FreeSansBold18pt7b);
  const char* value = r.valid ? r.value.c_str() : "N/A";
  c.setTextDatum(bottom_left);
  c.drawString(value, 14, base, valueFont);
  const int tw = L.small ? 96 : 112, th = L.small ? 40 : 56, tx = L.w - tw - 10, ty = base - th;
  if (14 + c.textWidth(value, valueFont) + 8 <= tx) {  // a long value ("NOT RESPONDING") takes the trace's room
    c.drawRect(tx, ty, tw, th, col.fg);
    drawTrace(c, tx + 3, ty + 3, tw - 6, th - 6, r.trace, col.fg);
  }

  // Position dots along the bottom.
  const int dotY = L.h - (L.small ? 7 : 10), gap = 10, x0 = (L.w - (n - 1) * gap) / 2;
  for (int k = 0; k < n; ++k) {
    if (k == i) c.fillRect(x0 + k * gap - 3, dotY - 3, 6, 6, col.fg);
    else c.drawRect(x0 + k * gap - 2, dotY - 2, 4, 4, col.fg);
  }
  if (alarm && (tMs / 300) % 2 == 0)  // a blinking border
    for (int k = 0; k < 4; ++k) c.drawRect(k, k, L.w - 2 * k, L.h - 2 * k, col.fg);
}

}  // namespace screens
