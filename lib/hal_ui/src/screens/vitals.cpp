// Crew vitals: a heartbeat for DAVE and a compressor cycle for the FRIDGE.
#include <math.h>

#include "screens.h"

namespace screens {
namespace {

// One heartbeat over phase 0..1: P wave, QRS spike, T wave.
float ecg(float ph) {
  if (ph < 0.10f) return 0.12f * sinf(ph / 0.10f * 3.14159f);
  if (ph < 0.16f) return 0;
  if (ph < 0.18f) return -0.15f;
  if (ph < 0.21f) return 1.0f;
  if (ph < 0.24f) return -0.35f;
  if (ph < 0.36f) return 0;
  if (ph < 0.52f) return 0.25f * sinf((ph - 0.36f) / 0.16f * 3.14159f);
  return 0;
}

// Fridge: slow on/off compressor cycle with a bit of hum.
float fridge(float ph, float x) {
  float base = ph < 0.55f ? 0.55f : -0.45f;
  return base + 0.06f * sinf(x * 0.9f);
}

void trace(M5Canvas& c, int w, int top, int h, uint16_t col, uint32_t tMs, bool heart) {
  for (int y = top; y <= top + h; y += h / 4) c.drawFastHLine(0, y, w, dim());
  for (int x = 0; x < w; x += 32) c.drawFastVLine(x, top, h, dim());
  const int mid = top + h / 2;
  int px = 0, py = mid;
  for (int x = 0; x < w; x += 2) {
    float pos = x + tMs * (heart ? 0.09f : 0.03f);
    float ph = fmodf(pos, heart ? 110.0f : 260.0f) / (heart ? 110.0f : 260.0f);
    float v = heart ? ecg(ph) : fridge(ph, pos);
    int y = mid - static_cast<int>(v * h * 0.42f);
    if (x) c.drawLine(px, py, x, y, col);
    px = x;
    py = y;
  }
}

}  // namespace

void vitals(M5Canvas& c, const hal::Layout& L, uint32_t tMs, uint8_t level) {
  frame(c, L, "CREW VITALS", tMs);
  c.setTextDatum(top_left);
  char buf[48];

  c.setTextColor(white());
  c.drawString("DAVE", 6, L.Y(24), L.font2());
  int bpm = 72 + (level > 120 ? 9 : 0) + int(tMs / 1700 % 3);
  snprintf(buf, sizeof buf, "HR %d   SNACK LVL 87%%   MOOD: HUNGRY", bpm);
  c.setTextColor(blue());
  c.drawString(buf, 60, L.Y(28), &fonts::Font0);
  trace(c, L.w, L.Y(42), L.H(72), blue(), tMs, true);

  c.setTextColor(white());
  c.drawString("FRIDGE", 6, L.Y(122), L.font2());
  snprintf(buf, sizeof buf, "TEMP 3.%d C   DOOR OPENS TODAY: 11", int(tMs / 900 % 10));
  c.setTextColor(red());
  c.drawString(buf, 60, L.Y(126), &fonts::Font0);
  trace(c, L.w, L.Y(140), L.H(72), red(), tMs, false);
}

}  // namespace screens
