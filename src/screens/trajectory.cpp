// Trajectory plot: Dave's transit from the couch to the kitchen and back.
#include <math.h>

#include "screens.h"

namespace screens {

void trajectory(M5Canvas& c, uint32_t tMs, uint8_t level) {
  frame(c, "TRAJECTORY ANALYSIS", tMs);
  const int cx = 160, cy = 120, rx = 120, ry = 62;

  // Grid and range rings.
  for (int x = 20; x < 320; x += 40) c.drawFastVLine(x, 24, 196, dim());
  for (int y = 40; y < 222; y += 30) c.drawFastHLine(0, y, 320, dim());
  c.drawEllipse(cx, cy, rx, ry, blue());
  c.drawEllipse(cx, cy, rx - 1, ry - 1, blue());
  for (int i = 0; i < 36; ++i) {
    float a = i * 6.2831853f / 36;
    int x0 = cx + cosf(a) * (rx - 4), y0 = cy + sinf(a) * (ry - 3);
    int x1 = cx + cosf(a) * (rx + 4), y1 = cy + sinf(a) * (ry + 3);
    c.drawLine(x0, y0, x1, y1, i % 9 == 0 ? white() : dim());
  }

  // End points.
  c.fillCircle(cx - rx, cy, 6, white());
  c.fillCircle(cx + rx, cy, 6, red());
  c.setTextDatum(top_center);
  c.setTextColor(white());
  c.drawString("COUCH", cx - rx + 4, cy + 10, &fonts::Font2);
  c.setTextColor(red());
  c.drawString("KITCHEN", cx + rx - 8, cy + 10, &fonts::Font2);

  // Dave, with a fading trail.
  float a0 = 3.14159f + tMs * 0.0011f;
  for (int k = 12; k >= 0; --k) {
    float a = a0 - k * 0.07f;
    int x = cx + cosf(a) * rx, y = cy + sinf(a) * ry;
    uint8_t v = 255 - k * 18;
    c.fillCircle(x, y, k == 0 ? 5 : 2, lgfx::color565(v / 4, v / 2, v));
  }

  c.setTextDatum(top_left);
  c.setTextColor(blue());
  char buf[48];
  snprintf(buf, sizeof buf, "TRIPS TODAY: %lu", (unsigned long)(11 + tMs / 5700));
  c.drawString(buf, 8, 26, &fonts::Font0);
  c.drawString("PURPOSE: SNACK (PROBABLE)", 8, 36, &fonts::Font0);
  c.setTextDatum(top_right);
  c.drawString(level > 100 ? "HAL: COMMENTING" : "HAL: OBSERVING", 312, 26, &fonts::Font0);
  snprintf(buf, sizeof buf, "ETA FRIDGE %02lu.%lu S", (unsigned long)(9 - tMs / 1000 % 10), (unsigned long)(tMs / 100 % 10));
  c.drawString(buf, 312, 36, &fonts::Font0);
}

}  // namespace screens
