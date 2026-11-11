#include "screens.h"

namespace screens {

void frame(M5Canvas& c, const hal::Layout& L, const char* title, uint32_t tMs) {
  c.fillScreen(TFT_BLACK);
  c.fillRect(0, 0, L.w, L.headerH, blue());
  c.setTextColor(TFT_BLACK);
  c.setTextDatum(middle_left);
  c.drawString(title, 6, L.headerH / 2, L.font2());
  c.setTextDatum(middle_right);
  c.drawString(L.name, L.w - 6, L.headerH / 2, L.font2());

  c.drawFastHLine(0, L.footerLineY, L.w, dim());
  c.setTextColor(blue());
  c.setTextDatum(bottom_left);
  char buf[40];
  uint32_t s = tMs / 1000;
  snprintf(buf, sizeof buf, "T+ %02lu:%02lu.%01lu", (unsigned long)(s / 60), (unsigned long)(s % 60),
           (unsigned long)(tMs / 100 % 10));
  c.drawString(buf, 6, L.footerTextY, &fonts::Font0);
  c.setTextDatum(bottom_right);
  c.setTextColor((tMs / 500) % 2 ? red() : dim());
  c.drawString("MONITORING", L.w - 6, L.footerTextY, &fonts::Font0);
}

}  // namespace screens
