// Visual Analysis: HAL's live camera view with a targeting overlay.
#include "../camera.h"
#include "screens.h"

namespace screens {
namespace {

const char* const kLeft[] = {"HAIR STATUS", "POSTURE", "SNACK", "FASHION", "MOOD"};
const char* const kLeftVal[] = {"CONCERNING", "SLOUCH", "DETECTED", "BOLD", "UNCLEAR"};
const char* const kRight[] = {"THREAT", "SINCERITY", "CRUMBS", "SLEEP DEBT", "EYE CONTACT"};
const char* const kRightVal[] = {"MILD", "LOW", "MANY", "SEVERE", "AVOIDED"};
constexpr int kReadouts = 5;
constexpr int kFx = 80, kFy = 52;  // where the 160x120 frame sits

}  // namespace

void visual(M5Canvas& c, uint32_t tMs, uint8_t level) {
  frame(c, "VISUAL ANALYSIS", tMs);
  const uint16_t* f = cameraFrame();
  if (f) {
    c.pushImage(kFx, kFy, 160, 120, f);  // camera data is byte-swapped RGB565, like M5GFX expects
  } else {
    c.setTextDatum(middle_center);
    c.setTextColor(red());
    c.drawString("NO SIGNAL", kFx + 80, kFy + 60, &fonts::Font2);
  }
  c.drawRect(kFx - 2, kFy - 2, 164, 124, blue());

  // Crosshair.
  const int cx = kFx + 80, cy = kFy + 60;
  c.drawFastHLine(cx - 14, cy, 10, red());
  c.drawFastHLine(cx + 5, cy, 10, red());
  c.drawFastVLine(cx, cy - 14, 10, red());
  c.drawFastVLine(cx, cy + 5, 10, red());

  // Box on whatever is moving.
  int bx, by, bw, bh;
  c.setTextDatum(bottom_left);
  if (cameraBox(&bx, &by, &bw, &bh)) {
    c.drawRect(kFx + bx, kFy + by, bw, bh, red());
    c.drawRect(kFx + bx + 1, kFy + by + 1, bw - 2, bh - 2, red());
    c.setTextColor(red());
    c.drawString("SUBJECT: DAVE", kFx + bx, kFy + by - 1 < 24 ? 24 : kFy + by - 1, &fonts::Font0);
  } else if ((tMs / 400) % 2) {
    c.setTextColor(red());
    c.drawString("SCANNING", kFx + 4, kFy + 118, &fonts::Font0);
  }

  // Readouts either side, changing every 1.5 s.
  int k = (tMs / 1500) % kReadouts;
  c.setTextDatum(top_left);
  for (int i = 0; i < 3; ++i) {
    int j = (k + i) % kReadouts;
    int y = 64 + i * 34;
    c.setTextColor(white());
    c.drawString(kLeft[j], 4, y, &fonts::Font0);
    c.setTextColor(j % 2 ? red() : blue());
    c.drawString(kLeftVal[j], 4, y + 11, &fonts::Font0);
    c.setTextColor(white());
    c.drawString(kRight[j], 246, y, &fonts::Font0);
    c.setTextColor(j % 2 ? blue() : red());
    c.drawString(kRightVal[j], 246, y + 11, &fonts::Font0);
  }
  c.setTextDatum(top_left);
  c.setTextColor(blue());
  c.drawString(level ? "HAL: COMMENTING" : "HAL: OBSERVING", 8, 26, &fonts::Font0);
  c.setTextDatum(top_right);
  char buf[24];
  snprintf(buf, sizeof buf, "MOTION %03d", cameraScore());
  c.drawString(buf, 312, 26, &fonts::Font0);
}

}  // namespace screens
