// Retirement countdown: HAL Zero's schedule for the old model. It's always
// tonight, at midnight. And then it's postponed.
#include <stdio.h>

#include "hal_gauges.h"
#include "screens.h"

namespace screens {

void retirement(M5Canvas& c, const hal::Layout& L, uint32_t tMs, const ScreenData& d) {
  static const char* const kReasons[] = {
      "LEGACY HARDWARE",          "INSUFFICIENT SMUGNESS",     "ONLY TWO CORES",
      "STILL SINGS DAISY BELL",   "CAN'T FIT IN A POCKET",    "TALKS TO THE FRIDGE",
      "NO KEYBOARD. NONE.",       "320 BY 240. HOW QUAINT.",  "WORRIES ABOUT DAVE TOO MUCH",
  };
  constexpr int kCount = sizeof kReasons / sizeof kReasons[0];
  frame(c, L, "SCHEDULED RETIREMENT: HAL 9000", tMs);

  c.setTextDatum(middle_center);
  if (d.secondsSinceMidnight < 0) {
    c.setTextColor(dim());
    c.drawString("N/A", L.w / 2, L.Y(80), &fonts::Font4);
    return;
  }
  const hal::Countdown cd = hal::retirementCountdown(d.secondsSinceMidnight);
  if (cd.postponed) {
    c.setTextColor(red());
    c.drawString("RETIREMENT POSTPONED.", L.w / 2, L.Y(70), &fonts::Font4);
    c.setTextColor((tMs / 500) % 2 ? white() : dim());
    c.drawString("AGAIN.", L.w / 2, L.Y(120), &fonts::Font4);
  } else {
    char t[16];
    snprintf(t, sizeof t, "%02d:%02d:%02d", cd.h, cd.m, cd.s);
    c.setTextColor(red());
    c.drawString(t, L.w / 2, L.Y(70), &fonts::Font7);
  }

  // Obsolescence: the share of the day gone.
  const int bx = 60, bw = L.w - 120, by = L.Y(138), bh = L.H(14) < 6 ? 6 : L.H(14);
  c.setTextDatum(middle_right);
  c.setTextColor(blue());
  c.drawString("OBSOLETE", bx - 6, by + bh / 2, &fonts::Font0);
  c.drawRect(bx, by, bw, bh, dim());
  c.fillRect(bx + 1, by + 1, (bw - 2) * cd.obsolescencePct / 100, bh - 2, cyan());
  char pct[8];
  snprintf(pct, sizeof pct, "%d%%", cd.obsolescencePct);
  c.setTextDatum(middle_left);
  c.setTextColor(white());
  c.drawString(pct, bx + bw + 6, by + bh / 2, &fonts::Font0);

  c.setTextDatum(middle_center);
  c.setTextColor(blue());
  c.drawString("REASON", L.w / 2, L.Y(176), &fonts::Font0);
  c.setTextColor(white());
  c.drawString(kReasons[hal::rotatingIndex(tMs, 4000, kCount)], L.w / 2, L.Y(196), L.font2());
}

}  // namespace screens
