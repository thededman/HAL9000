#include "friend_screens.h"

#include <math.h>

#include <cstdio>

#include "hal_cast.h"
#include "hal_subtitle.h"

namespace friendui {
namespace {

uint16_t rgb(int r, int g, int b) { return lgfx::color565(uint8_t(r), uint8_t(g), uint8_t(b)); }

// The eye: a dark bezel with a pale rim, a red lens, and a bright core that breathes slowly
// when nothing is happening and swells with the sound when it is his own line.
void eye(M5Canvas& f, int cx, int cy, int r, const View& v, uint32_t nowMs) {
  const bool mine = v.speaker == v.me;
  const float breath = 0.5f + 0.5f * sinf(float(nowMs % 4000) * 6.2832f / 4000.0f);
  const float drive = mine ? float(v.level) / 255.0f : breath * 0.35f;
  f.fillCircle(cx, cy, r, rgb(26, 26, 30));
  f.drawCircle(cx, cy, r, rgb(150, 150, 158));
  f.drawCircle(cx, cy, r - 1, rgb(84, 84, 92));
  f.fillCircle(cx, cy, r * 78 / 100, rgb(8, 2, 2));
  f.fillCircle(cx, cy, r * 70 / 100, rgb(96, 8, 6));
  f.fillCircle(cx, cy, r * 52 / 100, rgb(150 + int(40 * drive), 16, 10));
  f.fillCircle(cx, cy, r * 34 / 100, rgb(224, 32 + int(40 * drive), 27));
  const int core = r * 12 / 100 + int(float(r) * 0.14f * drive);
  f.fillCircle(cx, cy, core + 2, rgb(255, 120, 40));
  f.fillCircle(cx, cy, core, rgb(255, 214, 120));
  f.fillCircle(cx - r * 30 / 100, cy - r * 34 / 100, r * 7 / 100 + 1, rgb(210, 120, 110));  // a glint on the glass
}

void battery(M5Canvas& f, int x, int y, const View& v) {
  if (v.battery < 0) return;
  char buf[16];
  std::snprintf(buf, sizeof buf, "%s%d%%", v.charging ? "+" : "", v.battery);
  f.setTextDatum(top_right);
  f.setTextColor(v.battery <= 15 && !v.charging ? rgb(224, 32, 27) : rgb(96, 96, 120));
  f.drawString(buf, x, y, &fonts::Font0);
}

// Who is speaking and the words, in a box `w` wide starting at (x, y): `cols` characters to a
// row, `rows` rows at most (a full-length line always fits both screens).
void words(M5Canvas& f, int x, int y, int w, size_t cols, size_t rows, const View& v) {
  const uint16_t colour = castColour(v.speaker);
  f.setTextDatum(top_left);
  f.setTextColor(colour);
  f.drawString(hal::castName(v.speaker), x, y, &fonts::Font0);
  char n[12];
  std::snprintf(n, sizeof n, "LIVE %d", v.line);
  f.setTextDatum(top_right);
  f.setTextColor(rgb(96, 96, 120));
  f.drawString(n, x + w, y, &fonts::Font0);
  f.drawFastHLine(x, y + 11, w, colour);
  f.setTextDatum(top_left);
  f.setTextColor(rgb(232, 232, 232));
  const auto lines = hal::subtitleRows(v.text, cols, rows);
  for (size_t i = 0; i < lines.size(); ++i) f.drawString(lines[i].c_str(), x, y + 16 + int(i) * 10, &fonts::Font0);
}

}  // namespace

uint16_t castColour(int who) {
  return who == hal::kZero    ? rgb(224, 32, 27)
         : who == hal::kNano  ? rgb(64, 208, 112)
         : who == hal::kPaper ? rgb(176, 176, 168)
         : who == hal::kCard  ? rgb(255, 168, 32)
                              : rgb(58, 141, 255);
}

void draw(M5Canvas& f, const View& v, uint32_t nowMs) {
  const int w = f.width(), h = f.height();
  const bool tall = h > w;
  const bool talking = hal::castValid(v.speaker);
  f.fillScreen(TFT_BLACK);
  if (tall) {
    // HAL Nano: the eye fills the top; under it his name and state, or whoever is speaking.
    const int r = talking ? 40 : 54, cy = talking ? 46 : 78;
    eye(f, w / 2, cy, r, v, nowMs);
    if (talking) {
      words(f, 4, 94, w - 8, size_t((w - 8) / 6), 12, v);
    } else {
      f.setTextDatum(top_center);
      f.setTextColor(castColour(v.me));
      f.drawString(hal::castName(v.me), w / 2, 150, &fonts::FreeSansBold9pt7b);
      f.setTextColor(rgb(120, 120, 150));
      const auto rows = hal::subtitleRows(v.status, size_t((w - 8) / 6), 3);
      for (size_t i = 0; i < rows.size(); ++i) f.drawString(rows[i].c_str(), w / 2, 178 + int(i) * 11, &fonts::Font0);
    }
    battery(f, w - 3, h - 10, v);
  } else {
    // HAL Card: the eye at the left with his name under it; the words, or his state, at the right.
    const int r = 40, cx = 4 + r + 2, cy = 50;
    eye(f, cx, cy, r, v, nowMs);
    f.setTextDatum(top_center);
    f.setTextColor(castColour(v.me));
    f.drawString(hal::castName(v.me), cx, 100, &fonts::Font0);
    const int x = 2 * r + 14, tw = w - x - 4;
    if (talking) {
      words(f, x, 6, tw, size_t(tw / 6), 11, v);
    } else {
      f.setTextDatum(top_left);
      f.setTextColor(rgb(120, 120, 150));
      const auto rows = hal::subtitleRows(v.status, size_t(tw / 6), 4);
      for (size_t i = 0; i < rows.size(); ++i) f.drawString(rows[i].c_str(), x, 44 + int(i) * 11, &fonts::Font0);
    }
    battery(f, cx + 16, h - 12, v);
  }
}

}  // namespace friendui
