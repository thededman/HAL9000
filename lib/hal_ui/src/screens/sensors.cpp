// Sensor array: the desk HAL's own sensors, live (light, proximity, tilt, motion,
// battery, chip temperature, Wi-Fi, memory). No microphone: it shares the audio
// bus with the speaker, and HAL is talking while this screen is up.
#include <stdio.h>

#include "screens.h"

namespace screens {
namespace {

float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

void gauge(M5Canvas& c, const hal::Layout& L, int y, int h, int i, const char* label, bool na, float frac,
           const char* value, uint16_t col, uint32_t tMs) {
  c.setTextDatum(middle_left);
  c.setTextColor(na ? dim() : white());
  c.drawString(label, 6, y + h / 2, &fonts::Font0);
  const int bx = 86, bw = 150, bh = h - 8 < 4 ? 4 : h - 8, by = y + (h - bh) / 2;
  c.drawRect(bx, by, bw, bh, dim());
  if (!na) c.fillRect(bx + 1, by + 1, int(clamp01(frac) * (bw - 2)), bh - 2, col);
  c.drawFastVLine(bx + 1 + int((tMs / 9 + i * 37) % (bw - 2)), by, bh, dim());  // scan tick
  c.setTextDatum(middle_right);
  c.setTextColor(na ? dim() : col);
  c.drawString(na ? "N/A" : value, L.w - 6, y + h / 2, &fonts::Font0);
}

}  // namespace

void sensors(M5Canvas& c, const hal::Layout& L, uint32_t tMs, const ScreenData& d) {
  frame(c, L, "SENSOR ARRAY", tMs);
  const DeskSensors& s = d.sensors;
  const int top = L.Y(24), rowH = L.H(22);
  char v[24];
  int i = 0;
  auto row = [&](const char* label, bool ok, float frac, uint16_t col) {
    gauge(c, L, top + i * rowH, rowH, i, label, !s.valid || !ok, frac, v, col, tMs);
    ++i;
  };
  snprintf(v, sizeof v, "%d %%", s.lightPct);
  row("LIGHT", s.lightPct >= 0, s.lightPct / 100.0f, white());
  snprintf(v, sizeof v, "%d", s.proximity);
  row("PROXIMITY", s.proximity >= 0, s.proximity / 1000.0f, blue());
  snprintf(v, sizeof v, "%d DEG", s.tiltDeg);
  row("TILT", s.tiltDeg >= 0, s.tiltDeg / 90.0f, blue());
  snprintf(v, sizeof v, "%.0f DEG/S", s.motionDps);
  row("MOTION", true, s.motionDps / 250.0f, s.motionDps > 50 ? red() : blue());
  snprintf(v, sizeof v, s.charging ? "%d %% CHG" : "%d %%", s.batteryPct);
  row("BATTERY", s.batteryPct >= 0, s.batteryPct / 100.0f, s.batteryPct >= 0 && s.batteryPct < 20 ? red() : white());
  snprintf(v, sizeof v, "%d C", s.chipTempC);
  row("CHIP TEMP", s.chipTempC > -999, (s.chipTempC - 20) / 60.0f, s.chipTempC > 65 ? red() : blue());
  if (s.rssi == 0) snprintf(v, sizeof v, "ASLEEP");
  else snprintf(v, sizeof v, "%d DBM", s.rssi);
  row("WI-FI", true, s.rssi == 0 ? 0 : (s.rssi + 100) / 70.0f, blue());
  snprintf(v, sizeof v, "%d KB", s.heapFree / 1024);
  row("MEMORY", s.heapFree > 0, s.heapFree / 327680.0f, s.heapFree > 0 && s.heapFree < 32768 ? red() : white());
  snprintf(v, sizeof v, "%d KB", s.psramFree / 1024);
  row("PSRAM", s.psramFree > 0, s.psramFree / 8388608.0f, white());
}

}  // namespace screens
