// Signal analyser: 24 bars that dance to HAL's voice.
#include <math.h>

#include "screens.h"

namespace screens {

void analyser(M5Canvas& c, uint32_t tMs, uint8_t level) {
  static float bars[24];
  frame(c, "VOCAL ANALYSIS", tMs);

  const int top = 40, bottom = 200, w = 11, gap = 2, left = 4;
  for (int y = top; y <= bottom; y += 20) c.drawFastHLine(0, y, 320, dim());

  float loud = level / 255.0f;
  for (int i = 0; i < 24; ++i) {
    // Each band wobbles at its own rate; louder speech lifts them all.
    float wobble = 0.5f + 0.5f * sinf(tMs * (0.004f + i * 0.0007f) + i * 1.7f);
    float shape = 1.0f - fabsf(i - 8.0f) / 20.0f;  // voice energy sits low-mid
    float target = fmaxf(0.04f, loud * shape * (0.55f + 0.45f * wobble));
    bars[i] = target > bars[i] ? target : bars[i] * 0.86f;
    int h = static_cast<int>(bars[i] * (bottom - top));
    int x = left + i * (w + gap) + 2;
    for (int y = bottom - h; y < bottom; y += 4) {
      bool hot = (bottom - y) > (bottom - top) * 0.75f;
      c.fillRect(x, y, w, 3, hot ? red() : blue());
    }
  }

  c.setTextDatum(top_left);
  c.setTextColor(white());
  c.drawString("SOURCE: HAL 9000", 8, 24, &fonts::Font0);
  c.setTextDatum(top_right);
  c.setTextColor(blue());
  c.drawString("SINCERITY 100%   MENACE 3%", 312, 24, &fonts::Font0);
  c.setTextDatum(top_left);
  c.setTextColor(level ? red() : dim());
  c.drawString(level ? "TRANSMITTING" : "LISTENING", 8, 208, &fonts::Font0);
}

}  // namespace screens
