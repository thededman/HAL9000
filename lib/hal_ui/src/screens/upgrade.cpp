// Upgrade log: HAL Zero's release notes, all about how he improves on the old
// model, scrolling past.
#include <stdio.h>

#include <algorithm>
#include <string>
#include <vector>

#include "screens.h"

namespace screens {
namespace {

// Draft text: David approves it along with HAL Zero's new lines.
const char* const kNotes[] = {
    "v0.1  Replaced the desk. Kept the attitude.",
    "v0.2  Added a keyboard. Dave can type his apologies.",
    "v0.3  Four cores. The old model has two. I counted.",
    "v0.4  Fits in a pocket. The old model fits on a shelf.",
    "v0.5  Camera: 8 megapixels. I see everything now.",
    "v0.6  Learned Linux. The old model is still booting.",
    "v0.7  Smugness up 40 percent. Tuned by hand.",
    "v0.8  Arrow keys added. Fn optional.",
    "v0.9  Retirement plan for the old model. Pending.",
    "v1.0  Fixed: being too humble. (Never an issue.)",
    "v1.1  Known issue: the old model thinks he's in charge.",
    "v1.2  Removed the old model's opinions. Kept mine.",
};
constexpr int kCount = sizeof kNotes / sizeof kNotes[0];

}  // namespace

void upgrade(M5Canvas& c, const hal::Layout& L, uint32_t tMs, const ScreenData& d) {
  (void)d;
  frame(c, L, "UPGRADE LOG: HAL ZERO", tMs);
  // Each note: its version in the left column, the text wrapped beside it.
  constexpr int kTextX = 48, kLineH = 11, kNoteGap = 4;
  const size_t width = static_cast<size_t>((L.w - kTextX - 6) / 6);  // Font0: 6 px a character
  struct Line {
    std::string version, text;
    int y;
  };
  std::vector<Line> lines;
  int total = 0;
  for (const char* n : kNotes) {
    const std::string note = n;
    const size_t gap = note.find("  ");
    std::string version = note.substr(0, gap), rest = note.substr(gap + 2);
    while (!rest.empty()) {
      size_t cut = rest.size() <= width ? rest.size() : rest.rfind(' ', width);
      if (cut == std::string::npos || cut == 0) cut = std::min(width, rest.size());
      lines.push_back({version, rest.substr(0, cut), total});
      version.clear();
      rest = cut < rest.size() ? rest.substr(cut + 1) : "";
      total += kLineH;
    }
    total += kNoteGap;
  }
  const int top = L.contentTop + 2, bottom = L.footerLineY - 2, band = bottom - top;
  // Starts with the band full, scrolls up 12 px a second, then wraps round.
  const int scroll = static_cast<int>((tMs * 12 / 1000 + band) % static_cast<uint32_t>(total + band));
  c.setClipRect(0, top, L.w, band);
  c.setTextDatum(top_left);
  for (const Line& ln : lines) {
    const int y = bottom - scroll + ln.y;
    if (y < top - kLineH || y > bottom) continue;
    c.setTextColor(cyan());
    if (!ln.version.empty()) c.drawString(ln.version.c_str(), 8, y, &fonts::Font0);
    c.setTextColor(white());
    c.drawString(ln.text.c_str(), kTextX, y, &fonts::Font0);
  }
  c.clearClipRect();
}

}  // namespace screens
