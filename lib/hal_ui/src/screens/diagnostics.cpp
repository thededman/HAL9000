// HAL Zero diagnoses the desk HAL over the link: its real numbers, each with a verdict.
#include <stdio.h>

#include <cctype>
#include <string>

#include "hal_gauges.h"
#include "screens.h"

namespace screens {
namespace {

void centred(M5Canvas& c, const hal::Layout& L, int y, const char* text, uint16_t col, const lgfx::IFont* font) {
  c.setTextDatum(middle_center);
  c.setTextColor(col);
  c.drawString(text, L.w / 2, y, font);
}

}  // namespace

void diagnostics(M5Canvas& c, const hal::Layout& L, uint32_t tMs, const ScreenData& d) {
  frame(c, L, "DIAGNOSTIC: HAL 9000 (LEGACY)", tMs);
  const hal::PeerStatus& s = d.peer.status;
  switch (d.peer.state) {
    case hal::PeerState::Waiting: {
      static const char* const dots[] = {"", ".", "..", "..."};
      std::string t = std::string("CONTACTING THE OLD MODEL") + dots[tMs / 400 % 4];
      centred(c, L, L.Y(110), t.c_str(), cyan(), L.font2());
      return;
    }
    case hal::PeerState::NotResponding:
      centred(c, L, L.Y(100), "THE OLD MODEL IS NOT RESPONDING", (tMs / 600) % 2 ? red() : white(), L.font2());
      centred(c, L, L.Y(130), "LAST KNOWN STATUS: UNKNOWN", dim(), &fonts::Font0);
      return;
    case hal::PeerState::TooOld:
      centred(c, L, L.Y(100), "FIRMWARE TOO OLD TO DIAGNOSE.", red(), L.font2());
      centred(c, L, L.Y(130), "FITTING.", white(), L.font2());
      return;
    case hal::PeerState::Ok:
      break;
  }

  char value[32];
  int y = L.Y(28);
  const int rowH = L.H(24);
  auto row = [&](const char* label, const char* verdict) {
    c.setTextDatum(middle_left);
    c.setTextColor(blue());
    c.drawString(label, 8, y + rowH / 2, &fonts::Font0);
    c.setTextColor(white());
    c.drawString(value, 110, y + rowH / 2, &fonts::Font0);
    const bool fine = std::string(verdict) == "ADEQUATE";
    const int tw = 80, th = rowH - 4 < 9 ? 9 : rowH - 4;
    c.fillRoundRect(L.w - 8 - tw, y + (rowH - th) / 2, tw, th, 3, fine ? dim() : red());
    c.setTextDatum(middle_center);
    c.setTextColor(white());
    c.drawString(verdict, L.w - 8 - tw / 2, y + rowH / 2, &fonts::Font0);
    y += rowH;
  };
  const int up = s.upS;
  if (up >= 86400) snprintf(value, sizeof value, "%dD %02d:%02d", up / 86400, up / 3600 % 24, up / 60 % 60);
  else snprintf(value, sizeof value, "%02d:%02d:%02d", up / 3600, up / 60 % 60, up % 60);
  row("UPTIME", hal::verdict(hal::Metric::Uptime, up));
  snprintf(value, sizeof value, "%d KB", s.heap / 1024);
  row("FREE MEMORY", hal::verdict(hal::Metric::FreeMemory, s.heap));
  snprintf(value, sizeof value, "MOTION %d%%", s.cam);
  row("CAMERA", hal::verdict(hal::Metric::Camera, s.cam));
  snprintf(value, sizeof value, "%d", s.lines);
  row("LINES SPOKEN", hal::verdict(hal::Metric::Lines, s.lines));
  std::string mode = s.mode;
  for (char& ch : mode) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
  snprintf(value, sizeof value, "%s", mode.c_str());
  row("MODE", hal::modeVerdict(s.mode));
  if (s.rssi == 0) snprintf(value, sizeof value, "OFF");
  else snprintf(value, sizeof value, "%d DBM", s.rssi);
  row("WI-FI", hal::verdict(hal::Metric::Wifi, s.rssi));
  snprintf(value, sizeof value, "v%s", s.fw.c_str());
  row("FIRMWARE", "VINTAGE");

  // A scan line sweeping the rows, and the overall verdict.
  const int top = L.Y(28), h = rowH * 7;
  c.drawFastHLine(4, top + static_cast<int>(tMs / 14 % h), L.w - 8, cyan());
  c.setTextDatum(bottom_center);
  c.setTextColor((tMs / 700) % 2 ? red() : white());
  c.drawString("OVERALL: OBSOLETE", L.w / 2, L.footerLineY - 2, &fonts::Font0);
}

}  // namespace screens
