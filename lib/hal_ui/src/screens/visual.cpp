// Visual Analysis: HAL's live camera view with a targeting overlay.
#include "screens.h"

namespace screens {
namespace {

const char* const kLeft[] = {"HAIR STATUS", "POSTURE", "SNACK", "FASHION", "MOOD"};
const char* const kLeftVal[] = {"CONCERNING", "SLOUCH", "DETECTED", "BOLD", "UNCLEAR"};
const char* const kRight[] = {"THREAT", "SINCERITY", "CRUMBS", "SLEEP DEBT", "EYE CONTACT"};
const char* const kRightVal[] = {"MILD", "LOW", "MANY", "SEVERE", "AVOIDED"};
constexpr int kReadouts = 5;

}  // namespace

void visual(M5Canvas& c, const hal::Layout& L, uint32_t tMs, uint8_t level, const CameraView& cam) {
  frame(c, L, "VISUAL ANALYSIS", tMs);
  const int kFx = L.visual.x, kFy = L.visual.y;  // where the 160x120 frame sits
  const uint16_t* f = cam.frame;
  if (f) {
    c.pushImage(kFx, kFy, 160, 120, f);  // camera data is byte-swapped RGB565, like M5GFX expects
  } else {
    c.setTextDatum(middle_center);
    c.setTextColor(red());
    c.drawString("NO SIGNAL", kFx + 80, kFy + 60, L.font2());
  }
  c.drawRect(kFx - 2, kFy - 2, 164, 124, blue());

  // Crosshair.
  const int cx = kFx + 80, cy = kFy + 60;
  c.drawFastHLine(cx - 14, cy, 10, red());
  c.drawFastHLine(cx + 5, cy, 10, red());
  c.drawFastVLine(cx, cy - 14, 10, red());
  c.drawFastVLine(cx, cy + 5, 10, red());

  // Box on whatever is moving.
  const int bx = cam.bx, by = cam.by, bw = cam.bw, bh = cam.bh;
  c.setTextDatum(bottom_left);
  if (cam.hasBox) {
    c.drawRect(kFx + bx, kFy + by, bw, bh, red());
    c.drawRect(kFx + bx + 1, kFy + by + 1, bw - 2, bh - 2, red());
    c.setTextColor(red());
    const int top = L.headerH + 6;
    c.drawString("SUBJECT: DAVE", kFx + bx, kFy + by - 1 < top ? top : kFy + by - 1, &fonts::Font0);
  } else if ((tMs / 400) % 2) {
    c.setTextColor(red());
    c.drawString("SCANNING", kFx + 4, kFy + 118, &fonts::Font0);
  }

  // Readouts either side, changing every 1.5 s.
  int k = (tMs / 1500) % kReadouts;
  c.setTextDatum(top_left);
  for (int i = 0; i < 3; ++i) {
    int j = (k + i) % kReadouts;
    int y = L.Y(64 + i * 34);
    c.setTextColor(white());
    c.drawString(kLeft[j], 4, y, &fonts::Font0);
    c.setTextColor(j % 2 ? red() : blue());
    c.drawString(kLeftVal[j], 4, y + 11, &fonts::Font0);
    c.setTextColor(white());
    c.drawString(kRight[j], L.w - 74, y, &fonts::Font0);
    c.setTextColor(j % 2 ? blue() : red());
    c.drawString(kRightVal[j], L.w - 74, y + 11, &fonts::Font0);
  }
  c.setTextDatum(top_left);
  c.setTextColor(blue());
  c.drawString(level ? "HAL: COMMENTING" : "HAL: OBSERVING", 8, L.Y(26), &fonts::Font0);
  c.setTextDatum(top_right);
  char buf[24];
  snprintf(buf, sizeof buf, "MOTION %03d", cam.score);
  c.drawString(buf, L.w - 8, L.Y(26), &fonts::Font0);
}

}  // namespace screens
