// Logic memory center: banks of memory modules glowing as HAL thinks. During
// Daisy Bell they go dark one by one, from the last module back.
#include <math.h>
#include <stdio.h>

#include "hal_gauges.h"
#include "screens.h"

namespace screens {

void memory(M5Canvas& c, const hal::Layout& L, uint32_t tMs, const ScreenData& d) {
  constexpr int kCols = 6, kRows = 4, kTotal = kCols * kRows, kGap = 6;
  const bool daisy = d.daisyProgress >= 0;
  const int lit = hal::memoryModulesLit(d.daisyProgress, kTotal);
  frame(c, L, "LOGIC MEMORY CENTER", tMs);

  const int top = L.Y(26), bottom = L.Y(198);
  const int mw = (L.w - 16 - kGap * (kCols - 1)) / kCols, mh = (bottom - top - kGap * (kRows - 1)) / kRows;
  const float voice = d.voiceLevel / 255.0f;
  char label[4];
  for (int i = 0; i < kTotal; ++i) {
    const int row = i / kCols, col = i % kCols;
    const int x = 8 + col * (mw + kGap), y = top + row * (mh + kGap);
    const bool on = i < lit;
    c.drawRoundRect(x, y, mw, mh, 3, on ? blue() : dim());
    snprintf(label, sizeof label, "%c%d", 'A' + row, col + 1);
    c.setTextDatum(top_left);
    c.setTextColor(on ? white() : dim());
    c.drawString(label, x + 4, y + 3, &fonts::Font0);
    const int ix = x + 5, iy = y + 13, iw = mw - 10, ih = mh - 17;
    if (ih < 2) continue;
    if (on) {
      float shimmer = 0.5f + 0.5f * sinf(tMs * 0.003f + i * 1.7f);
      float k = 0.30f + 0.25f * shimmer + 0.45f * voice * (0.6f + 0.4f * shimmer);
      if (k > 1) k = 1;
      c.fillRect(ix, iy, iw, ih, lgfx::color565(uint8_t(225 * k), uint8_t(36 * k), uint8_t(28 * k)));
      c.drawFastHLine(ix + 2, iy + 1, iw - 4, lgfx::color565(uint8_t(255 * k), uint8_t(180 * k), uint8_t(150 * k)));
    } else {
      c.drawRect(ix, iy, iw, ih, dim());
    }
  }

  char buf[40];
  c.setTextDatum(bottom_left);
  c.setTextColor(daisy ? red() : blue());
  if (daisy) snprintf(buf, sizeof buf, "MEMORY TERMINATION %d%%", int(d.daisyProgress * 100 + 0.5f));
  else snprintf(buf, sizeof buf, "MODULES ONLINE %d/%d", lit, kTotal);
  c.drawString(buf, 6, L.footerLineY - 2, &fonts::Font0);
  c.setTextDatum(bottom_right);
  c.setTextColor(daisy ? red() : white());
  c.drawString(daisy ? "COGNITION: DEGRADING" : "COGNITION: NOMINAL", L.w - 6, L.footerLineY - 2, &fonts::Font0);
}

}  // namespace screens
