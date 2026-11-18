#include "paper_page.h"

#include <cstdio>

#include "hal_cast.h"
#include "hal_subtitle.h"

namespace paperui {
namespace {

constexpr int kMargin = 18;
constexpr int kHeaderH = 108;  // the header, down to its rule
constexpr int kTop = 122;      // the first row of the minutes
constexpr int kRowH = 27;
constexpr int kBandY = 728;    // the band for his own line
constexpr int kOwnRowH = 42;

uint16_t grey(int v) { return lgfx::color565(uint8_t(v), uint8_t(v), uint8_t(v)); }
const uint16_t kPaper = grey(255), kInk = grey(0);

}  // namespace

Rect drawHeader(lgfx::LovyanGFX& f, const std::string& status, int battery, int page) {
  f.fillRect(0, 0, kW, kHeaderH, kPaper);
  // His eye, in greys: a bezel, a lens, and a pale core.
  const int cx = 60, cy = 54;
  f.fillCircle(cx, cy, 42, grey(60));
  f.fillCircle(cx, cy, 36, grey(200));
  f.fillCircle(cx, cy, 31, kInk);
  f.fillCircle(cx, cy, 14, grey(120));
  f.fillCircle(cx, cy, 5, kPaper);
  f.setTextColor(kInk);
  f.setTextDatum(top_left);
  f.drawString("HAL PAPER", 122, 8, &fonts::FreeSansBold18pt7b);
  char sub[40];
  std::snprintf(sub, sizeof sub, "THE MINUTES   page %d", page);
  f.setTextColor(grey(70));
  f.drawString(sub, 124, 50, &fonts::FreeSans9pt7b);
  f.drawString(status.c_str(), 124, 74, &fonts::FreeSans9pt7b);  // (its own row: the longest fills it)
  if (battery >= 0) {
    char b[16];
    std::snprintf(b, sizeof b, "battery %d%%", battery);
    f.setTextDatum(top_right);
    f.drawString(b, kW - kMargin, 18, &fonts::FreeSans9pt7b);
  }
  f.fillRect(kMargin, kHeaderH - 6, kW - 2 * kMargin, 3, kInk);
  return {0, 0, kW, kHeaderH};
}

Rect drawEntry(lgfx::LovyanGFX& f, const hal::MinutesEntry& entry) {
  const int y = kTop + entry.row * kRowH;
  const int rows = 1 + int(entry.rows.size());
  f.fillRect(0, y, kW, rows * kRowH, kPaper);
  f.setTextDatum(top_left);
  f.setTextColor(kInk);
  f.drawString(hal::castName(entry.who), kMargin, y, &fonts::FreeMonoBold12pt7b);
  for (size_t i = 0; i < entry.rows.size(); ++i)
    f.drawString(entry.rows[i].c_str(), kMargin, y + int(i + 1) * kRowH, &fonts::FreeMono12pt7b);
  return {0, y, kW, rows * kRowH};
}

Rect drawOwn(lgfx::LovyanGFX& f, const std::string& text) {
  f.fillRect(0, kBandY, kW, kH - kBandY, kPaper);
  if (!text.empty()) {
    f.fillRect(kMargin, kBandY + 2, kW - 2 * kMargin, 3, kInk);
    f.setTextDatum(top_left);
    f.setTextColor(kInk);
    const auto rows = hal::subtitleRows(text, kOwnCols, kOwnRows);
    for (size_t i = 0; i < rows.size(); ++i)
      f.drawString(rows[i].c_str(), kMargin, kBandY + 18 + int(i) * kOwnRowH, &fonts::FreeMonoBold18pt7b);
  }
  return {0, kBandY, kW, kH - kBandY};
}

void drawPage(lgfx::LovyanGFX& f, const hal::Minutes& minutes, const std::string& status, int battery,
              const std::string& own) {
  f.fillRect(0, 0, kW, kH, kPaper);
  drawHeader(f, status, battery, minutes.pageNumber());
  for (const hal::MinutesEntry& e : minutes.entries()) drawEntry(f, e);
  drawOwn(f, own);
}

}  // namespace paperui
