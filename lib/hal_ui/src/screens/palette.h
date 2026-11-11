#pragma once
// Flat card colours for the film-style screens (status cards, monitor wall).
#include <vector>

#include <M5GFX.h>

namespace screens {

struct CardColour {
  uint16_t bg, fg;
};

inline CardColour cardColour(int i) {
  static const uint8_t k[][4] = {  // r, g, b, dark text?
      {24, 72, 200, 0}, {0, 128, 72, 0}, {224, 150, 0, 1}, {112, 48, 176, 0}, {232, 232, 232, 1}, {0, 140, 150, 0}};
  const uint8_t* c = k[((i % 6) + 6) % 6];
  return {lgfx::color565(c[0], c[1], c[2]), c[3] ? uint16_t(TFT_BLACK) : lgfx::color565(255, 255, 255)};
}
inline CardColour alarmColour() { return {lgfx::color565(208, 24, 24), lgfx::color565(255, 255, 255)}; }
// No reading. The CoreS3 draws consoles in 8-bit colour (RGB332), where this grey is
// the darkest that stays neutral: anything darker comes out olive.
inline CardColour deadColour() { return {lgfx::color565(72, 72, 88), lgfx::color565(200, 200, 200)}; }

// A trace of 0..1 levels (oldest first) as a line graph inside the box.
inline void drawTrace(M5Canvas& c, int x, int y, int w, int h, const std::vector<float>& t, uint16_t col) {
  if (t.size() < 2 || w < 4 || h < 4) return;
  int px = 0, py = 0;
  for (size_t i = 0; i < t.size(); ++i) {
    float v = t[i] < 0 ? 0 : t[i] > 1 ? 1 : t[i];
    int xx = x + int(i * (w - 1) / (t.size() - 1)), yy = y + h - 1 - int(v * (h - 1));
    if (i) c.drawLine(px, py, xx, yy, col);
    px = xx;
    py = yy;
  }
}

}  // namespace screens
