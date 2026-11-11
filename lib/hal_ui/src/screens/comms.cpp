// Comms antenna: the desk HAL's link to HAL Zero, with the real Wi-Fi signal.
#include <math.h>
#include <stdio.h>

#include <algorithm>
#include <string>

#include "screens.h"

namespace screens {
namespace {

void line2(M5Canvas& c, float x0, float y0, float x1, float y1, uint16_t col) {
  c.drawLine(int(x0), int(y0), int(x1), int(y1), col);
  c.drawLine(int(x0), int(y0) + 1, int(x1), int(y1) + 1, col);
}

}  // namespace

void comms(M5Canvas& c, const hal::Layout& L, uint32_t tMs, const ScreenData& d) {
  frame(c, L, "COMMS: HAL ZERO LINK", tMs);
  const LinkStats& k = d.link;
  const bool enabled = k.valid && k.enabled, up = enabled && k.connected;

  // The dish, aimed up and to the right, sweeping slowly.
  const float cx = 68, cy = L.Y(118), r = L.H(44);
  const float a = -0.75f + 0.25f * sinf(tMs * 0.0007f);
  const float ax = cosf(a), ay = sinf(a), px = -ay, py = ax;  // aim and its perpendicular
  c.drawLine(int(cx), int(cy), int(cx), L.Y(196), dim());       // mast
  c.fillRect(int(cx) - 16, L.Y(196), 32, L.H(6), dim());
  float lx = 0, ly = 0;
  for (int i = 0; i <= 16; ++i) {  // parabola, concave towards the aim
    float u = -1 + i / 8.0f;
    float x = cx + ax * (0.35f * r * u * u) + px * (0.8f * r * u);
    float y = cy + ay * (0.35f * r * u * u) + py * (0.8f * r * u);
    if (i) line2(c, lx, ly, x, y, up ? white() : blue());
    lx = x;
    ly = y;
  }
  const float fx = cx + ax * r * 0.6f, fy = cy + ay * r * 0.6f;  // the feed
  c.drawLine(int(cx), int(cy), int(fx), int(fy), blue());
  c.fillCircle(int(fx), int(fy), 2, up ? red() : dim());
  const int x0 = 150;  // the text column
  if (up) {  // wavefronts going out; any that would reach the text aren't drawn
    for (int w = 0; w < 4; ++w) {
      float dist = r * 0.7f + fmodf(tMs * 0.04f + w * 15.0f, 60.0f);
      float half = 4 + dist * 0.12f;
      float wx = cx + ax * dist, wy = cy + ay * dist;
      int x1 = int(wx - px * half), x2 = int(wx + px * half);
      if (std::max(x1, x2) >= x0 - 4) continue;
      c.drawLine(x1, int(wy - py * half), x2, int(wy + py * half), blue());
    }
  }

  // Right: the link in numbers.
  const int rowH = L.H(24);
  int y = L.Y(30);
  char buf[48];
  auto row = [&](const char* label, const char* value, uint16_t col) {
    c.setTextDatum(top_left);
    c.setTextColor(blue());
    c.drawString(label, x0, y, &fonts::Font0);
    c.setTextColor(col);
    c.drawString(value, x0 + 54, y, &fonts::Font0);
    y += rowH;
  };
  row("LINK", up ? "CONNECTED" : "NO CARRIER", up ? blue() : red());
  row("PEER", up && !k.peer.empty() ? k.peer.c_str() : "--", white());
  snprintf(buf, sizeof buf, "%d IN / %d OUT", k.msgsIn, k.msgsOut);
  row("TRAFFIC", buf, white());
  std::string last = k.last.empty() ? "--" : k.last.substr(0, 20);
  row("LAST", last.c_str(), blue());
  if (k.rssi == 0) snprintf(buf, sizeof buf, "WI-FI ASLEEP");
  else snprintf(buf, sizeof buf, "%d DBM", k.rssi);
  row("SIGNAL", buf, k.rssi == 0 ? red() : white());
  const int bars = k.rssi == 0 ? 0 : std::max(0, std::min(5, (k.rssi + 100) / 12));
  for (int b = 0; b < 5; ++b) {
    const int bh = 4 + b * 3;
    c.fillRect(x0 + 54 + b * 8, y + 16 - bh, 6, bh, b < bars ? blue() : dim());
  }

  if (!up) {  // big status, blinking; with link=off in config.txt, says why
    const int cx2 = L.w * 3 / 4 - 10;
    c.setTextDatum(bottom_center);
    c.setTextColor((tMs / 600) % 2 ? red() : dim());
    c.drawString("NO CARRIER", cx2, L.footerLineY - 4, L.font2());
    if (!enabled) {
      c.setTextColor(red());
      c.drawString("LINK DISABLED", cx2, L.footerLineY - 24, &fonts::Font0);
    }
  }
}

}  // namespace screens
