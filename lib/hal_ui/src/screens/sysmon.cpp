// System monitor: HAL Zero's own machine, live from Linux (four CPU cores, memory,
// temperature, Wi-Fi).
#include <cmath>
#include <stdio.h>

#include <vector>

#include "screens.h"

namespace screens {
namespace {

void cell(M5Canvas& c, int x, int y, int w, int h, const char* label, const std::vector<float>& s, float lo, float hi,
          const char* fmt, uint16_t col, bool valid, const char* gone = "N/A") {
  c.drawRoundRect(x, y, w, h, 3, dim());
  c.setTextDatum(top_left);
  c.setTextColor(blue());
  c.drawString(label, x + 4, y + 3, &fonts::Font0);
  char v[16];
  const bool have = valid && !s.empty();
  const bool now = have && !std::isnan(s.back());  // the latest reading exists
  if (now) snprintf(v, sizeof v, fmt, s.back());
  else snprintf(v, sizeof v, "%s", have ? gone : "N/A");
  c.setTextDatum(top_right);
  c.setTextColor(now ? col : dim());
  c.drawString(v, x + w - 4, y + 3, &fonts::Font0);
  if (!have) return;
  const int gx = x + 3, gy = y + 13, gw = w - 6, gh = h - 16;
  if (gh < 4) return;
  const size_t n = s.size();
  int px = 0, py = 0;
  bool pen = false;  // a gap (NaN) lifts the pen
  for (size_t i = 0; i < n; ++i) {
    if (std::isnan(s[i])) {
      pen = false;
      continue;
    }
    float f = (s[i] - lo) / (hi - lo);
    f = f < 0 ? 0 : f > 1 ? 1 : f;
    const int xx = gx + static_cast<int>((60 - n + i) * (gw - 1) / 59.0f);
    const int yy = gy + gh - 1 - static_cast<int>(f * (gh - 1));
    if (pen) c.drawLine(px, py, xx, yy, col);
    pen = true;
    px = xx;
    py = yy;
  }
}

}  // namespace

void sysmon(M5Canvas& c, const hal::Layout& L, uint32_t tMs, const ScreenData& d) {
  frame(c, L, "SYSTEM MONITOR", tMs);
  const SysStats& s = d.sys;
  constexpr int kGap = 5;
  const int top = L.Y(22), bottom = L.Y(218);
  const int w = (L.w - 12 - kGap * 3) / 4, h = (bottom - top - kGap) / 2;
  char label[8];
  for (int i = 0; i < 4; ++i) {
    snprintf(label, sizeof label, "CPU%d", i);
    const bool has = s.valid && i < s.cores;
    cell(c, 6 + i * (w + kGap), top, w, h, label, s.cpu[i], 0, 100, "%.0f%%", cyan(), has);
  }
  const int y2 = top + h + kGap;
  cell(c, 6, y2, w, h, "MEM", s.memUsedPct, 0, 100, "%.0f%%", white(), s.valid);
  cell(c, 6 + (w + kGap), y2, w, h, "TEMP", s.tempC, 30, 90, "%.0fC", red(), s.valid);
  cell(c, 6 + 2 * (w + kGap), y2, w, h, "WI-FI", s.rssiDbm, -100, -30, "%.0f", blue(), s.valid, "OFF");
  // The last cell: what this machine is.
  const int x4 = 6 + 3 * (w + kGap);
  c.drawRoundRect(x4, y2, w, h, 3, dim());
  c.setTextDatum(top_left);
  c.setTextColor(cyan());
  char buf[24];
  int ty = y2 + 4;
  c.drawString("HAL ZERO", x4 + 4, ty, &fonts::Font0);
  ty += 12;
  c.setTextColor(white());
  if (s.valid && s.cores > 0) {
    snprintf(buf, sizeof buf, "%d CORES", s.cores);
    c.drawString(buf, x4 + 4, ty, &fonts::Font0);
    ty += 10;
    snprintf(buf, sizeof buf, "%d MHZ", s.maxMHz);
    c.drawString(buf, x4 + 4, ty, &fonts::Font0);
    ty += 10;
    snprintf(buf, sizeof buf, "%ld MB", s.memTotalKb / 1024);
    c.drawString(buf, x4 + 4, ty, &fonts::Font0);
  } else {
    c.setTextColor(dim());
    c.drawString("NO /PROC", x4 + 4, ty, &fonts::Font0);
  }
}

}  // namespace screens
