// Benchmark: HAL Zero against the old model on the real specs. He wins every row
// that counts; the one he loses doesn't count.
#include <stdio.h>

#include "screens.h"

namespace screens {

void benchmark(M5Canvas& c, const hal::Layout& L, uint32_t tMs, const ScreenData& d) {
  frame(c, L, "BENCHMARK: ZERO VS 9000", tMs);
  const bool peerOk = d.peer.state == hal::PeerState::Ok;
  const bool sysOk = d.sys.valid && d.sys.cores > 0;
  // HAL Zero (Cardputer Zero) and HAL 9000 (CoreS3); the spec sheet when we can't ask.
  const float zeroMhz = sysOk ? float(d.sys.cores) * (d.sys.maxMHz > 0 ? d.sys.maxMHz : 1000) : 4 * 1000.0f;
  const float oldMhz = peerOk ? float(d.peer.status.cores) * d.peer.status.mhz : 2 * 240.0f;
  const float zeroMb = sysOk && d.sys.memTotalKb > 0 ? d.sys.memTotalKb / 1024.0f : 512.0f;
  struct Row {
    const char* label;
    float zero, old;
    const char* zeroText;
    const char* oldText;
    bool irrelevant;
  };
  char zc[24], oc[24], zm[24];
  if (sysOk) snprintf(zc, sizeof zc, "%d x %d MHZ", d.sys.cores, d.sys.maxMHz > 0 ? d.sys.maxMHz : 1000);
  else snprintf(zc, sizeof zc, "4 x 1000 MHZ");
  if (peerOk) snprintf(oc, sizeof oc, "%d x %d MHZ", d.peer.status.cores, d.peer.status.mhz);
  else snprintf(oc, sizeof oc, "2 x 240 MHZ");
  snprintf(zm, sizeof zm, "%.0f MB", zeroMb);
  const Row rows[] = {
      {"CPU", zeroMhz, oldMhz, zc, oc, false},
      {"MEMORY", zeroMb, 8.5f, zm, "8.5 MB", false},
      {"CAMERA", 8.0f, 0.3f, "8 MP", "0.3 MP", false},
      {"SCREEN HEIGHT", 170, 240, "170 PX", "240 PX", true},
  };
  const float grow = tMs >= 1500 ? 1.0f : tMs / 1500.0f;  // bars grow in
  const int x0 = 8, bx = 104, bwMax = L.w - bx - 84, rowH = L.H(46);
  int y = L.Y(24);
  for (const Row& r : rows) {
    const float top = r.zero > r.old ? r.zero : r.old;
    const int barH = (rowH - 12) / 2 < 4 ? 4 : (rowH - 12) / 2;
    c.setTextDatum(top_left);
    c.setTextColor(white());
    c.drawString(r.label, x0, y, &fonts::Font0);
    if (r.irrelevant) {
      c.setTextColor((tMs / 500) % 2 ? red() : cyan());
      c.drawString("IRRELEVANT", x0, y + 10, &fonts::Font0);
    }
    for (int k = 0; k < 2; ++k) {
      const bool zero = k == 0;
      const float v = zero ? r.zero : r.old;
      const int by = y + 1 + k * (barH + 3);
      const int w = static_cast<int>(bwMax * (top > 0 ? v / top : 0) * grow);
      c.drawRect(bx, by, bwMax, barH, dim());
      c.fillRect(bx, by, w < 1 ? 1 : w, barH, zero ? cyan() : red());
      c.setTextDatum(middle_left);
      c.setTextColor(zero ? cyan() : red());
      c.drawString(zero ? r.zeroText : r.oldText, bx + bwMax + 4, by + barH / 2, &fonts::Font0);
    }
    y += rowH;
  }
  c.setTextDatum(bottom_left);
  c.setTextColor(cyan());
  c.drawString("ZERO", x0, L.footerLineY - 2, &fonts::Font0);
  c.setTextColor(red());
  c.drawString(peerOk ? "9000 (LIVE)" : "9000 (SPEC SHEET)", x0 + 36, L.footerLineY - 2, &fonts::Font0);
  c.setTextDatum(bottom_right);
  c.setTextColor(white());
  c.drawString("WINNER: HAL ZERO", L.w - 8, L.footerLineY - 2, &fonts::Font0);
}

}  // namespace screens
