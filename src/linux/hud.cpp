#include "hud.h"

#include <math.h>

#include <cstdio>

#include "screens/screens.h"

using screens::blue;
using screens::dim;
using screens::red;
using screens::white;

namespace {

constexpr int kLeftX = 0, kRightX = 234, kPanelW = 86, kPanelH = 156;
uint8_t history[12];  // recent voice levels for the right panel's bars

void panel(M5Canvas& f, int x) {
  f.drawRoundRect(x + 1, 1, kPanelW - 2, kPanelH - 1, 4, dim());
  // Corner ticks, a little brighter.
  f.drawFastHLine(x + 1, 1, 8, blue());
  f.drawFastVLine(x + 1, 1, 8, blue());
  f.drawFastHLine(x + kPanelW - 9, kPanelH - 1, 8, blue());
  f.drawFastVLine(x + kPanelW - 2, kPanelH - 9, 8, blue());
}

void row(M5Canvas& f, int x, int y, const char* label, const char* value, uint16_t colour) {
  f.setTextDatum(top_left);
  f.setTextColor(dim());
  f.drawString(label, x + 6, y, &fonts::Font0);
  f.setTextColor(colour);
  f.setTextDatum(top_right);
  f.drawString(value, x + kPanelW - 6, y, &fonts::Font0);
}

std::vector<std::string> wrap(const std::string& text, size_t width) {
  std::vector<std::string> lines;
  std::string line, word;
  auto flush = [&] {
    if (line.empty()) {
      line = word;
    } else if (line.size() + 1 + word.size() <= width) {
      line += " " + word;
    } else {
      lines.push_back(line);
      line = word;
    }
    word.clear();
  };
  for (char c : text) {
    if (c == ' ') {
      if (!word.empty()) flush();
    } else {
      word += c;
    }
  }
  if (!word.empty()) flush();
  if (!line.empty()) lines.push_back(line);
  return lines;
}

}  // namespace

void drawHud(M5Canvas& f, const HudInfo& h, uint32_t now) {
  char buf[32];
  // Left: who he is and how he's doing.
  panel(f, kLeftX);
  f.setTextDatum(top_left);
  f.setTextColor(white());
  f.drawString("HAL ZERO", kLeftX + 6, 6, &fonts::Font2);
  f.setTextColor(blue());
  f.drawString("POCKET UNIT", kLeftX + 6, 23, &fonts::Font0);
  f.drawFastHLine(kLeftX + 6, 34, kPanelW - 12, dim());
  row(f, kLeftX, 40, "MODE", h.mode, white());
  snprintf(buf, sizeof buf, "%02u:%02u:%02u", unsigned(h.uptimeS / 3600), unsigned(h.uptimeS / 60 % 60),
           unsigned(h.uptimeS % 60));
  row(f, kLeftX, 54, "UP", buf, white());
  if (h.cpuTempC >= 0) snprintf(buf, sizeof buf, "%dC", h.cpuTempC);
  row(f, kLeftX, 68, "CPU", h.cpuTempC >= 0 ? buf : "--", h.cpuTempC > 70 ? red() : white());
  row(f, kLeftX, 82, "WIFI", h.wifi ? "ON" : "--", h.wifi ? blue() : dim());
  bool blinkOff = h.linkBusy && (now / 300) % 2;
  row(f, kLeftX, 96, "LINK", blinkOff ? "" : h.link, h.linkBusy ? red() : white());
  row(f, kLeftX, 110, "OPTICS", h.optics, white());
  row(f, kLeftX, 124, "QUIET", h.quiet ? "ON" : "OFF", h.quiet ? blue() : dim());
  // A smugness gauge that doesn't quite fit its box.
  f.setTextDatum(top_left);
  f.setTextColor(dim());
  f.drawString("SMUGNESS", kLeftX + 6, 139, &fonts::Font0);
  f.drawRect(kLeftX + 58, 139, 22, 7, dim());
  f.fillRect(kLeftX + 59, 140, 24, 5, red());

  // Right: clock, voice, motion, a radar sweep.
  panel(f, kRightX);
  f.setTextColor(white());
  f.setTextDatum(top_center);
  if (h.hour >= 0) snprintf(buf, sizeof buf, "%02d:%02d", h.hour, h.minute);
  f.drawString(h.hour >= 0 ? buf : "--:--", kRightX + kPanelW / 2, 6, &fonts::Font2);
  f.drawFastHLine(kRightX + 6, 26, kPanelW - 12, dim());
  f.setTextDatum(top_left);
  f.setTextColor(dim());
  f.drawString("VOICE", kRightX + 6, 31, &fonts::Font0);
  for (int i = 0; i < 11; ++i) history[i] = history[i + 1];
  history[11] = h.level;
  for (int i = 0; i < 12; ++i) {
    int bh = 2 + history[i] * 16 / 255;
    f.fillRect(kRightX + 7 + i * 6, 58 - bh, 4, bh, history[i] > 190 ? red() : blue());
  }
  f.drawString("MOTION", kRightX + 6, 63, &fonts::Font0);
  f.drawRect(kRightX + 6, 73, kPanelW - 12, 5, dim());
  f.fillRect(kRightX + 7, 74, (kPanelW - 14) * (h.motion > 60 ? 60 : h.motion) / 60, 3, h.motion > 20 ? red() : blue());
  row(f, kRightX, 83, "TILT", h.tilt, white());
  // Radar: one sweep every 3 s; a blip when something moves.
  const int rx = kRightX + kPanelW / 2, ry = 122, rr = 26;
  f.drawCircle(rx, ry, rr, dim());
  f.drawCircle(rx, ry, rr / 2, dim());
  f.drawFastHLine(rx - rr, ry, rr * 2, dim());
  f.drawFastVLine(rx, ry - rr, rr * 2, dim());
  float a = (now % 3000) / 3000.0f * 6.2831853f;
  for (int k = 0; k < 6; ++k) {
    float b = a - k * 0.08f;
    f.drawLine(rx, ry, rx + int(cosf(b) * rr), ry + int(sinf(b) * rr), k == 0 ? blue() : dim());
  }
  if (h.motion > 10) f.fillCircle(rx + 11, ry - 8, 2, red());
}

void drawCommandStrip(M5Canvas& f, const char* text) {
  f.fillRect(0, 158, f.width(), 12, TFT_BLACK);
  f.drawFastHLine(0, 157, f.width(), dim());
  f.setTextDatum(middle_center);
  f.setTextColor(blue());
  f.drawString(text, f.width() / 2, 164, &fonts::Font0);
}

bool drawBoot(M5Canvas& f, uint32_t t, const std::vector<std::string>& lines) {
  f.fillScreen(TFT_BLACK);
  f.setTextDatum(top_left);
  size_t shown = t / 220;
  if (shown > lines.size()) shown = lines.size();
  for (size_t i = 0; i < shown; ++i) {
    f.setTextColor(i == 0 ? white() : (lines[i].find("OFFLINE") != std::string::npos ? red() : blue()));
    f.drawString(lines[i].c_str(), 10, 10 + int(i) * 13, &fonts::Font0);
  }
  if ((t / 250) % 2 && shown < lines.size()) f.fillRect(10, 10 + int(shown) * 13, 6, 8, white());  // cursor
  return shown >= lines.size();
}

void drawMenu(M5Canvas& f, const char* title, hal::Menu& menu) {
  const int x = 56, y = 26, w = 208, rows = 7, rowH = 14;
  const int h = 20 + rows * rowH + 6;
  f.fillRoundRect(x, y, w, h, 5, TFT_BLACK);
  f.drawRoundRect(x, y, w, h, 5, blue());
  f.fillRect(x + 1, y + 1, w - 2, 15, blue());
  f.setTextColor(TFT_BLACK);
  f.setTextDatum(middle_left);
  f.drawString(title, x + 8, y + 8, &fonts::Font0);
  const auto& items = menu.items();
  int first = menu.firstVisible(rows);
  for (int i = 0; i < rows && first + i < int(items.size()); ++i) {
    int idx = first + i, ry = y + 20 + i * rowH;
    bool sel = idx == menu.selected();
    if (sel) f.fillRect(x + 4, ry, w - 8, rowH - 2, white());
    f.setTextColor(sel ? TFT_BLACK : white());
    f.drawString(items[idx].label.c_str(), x + 10, ry + rowH / 2 - 1, &fonts::Font0);
  }
  if (items.empty()) {
    f.setTextColor(dim());
    f.drawString("(nothing here yet)", x + 10, y + 27, &fonts::Font0);
  }
}

void drawSubtitle(M5Canvas& f, bool halZero, const std::string& text, int n, int total) {
  const int x = 4, y = 112, w = f.width() - 8, h = 44;
  f.fillRoundRect(x, y, w, h, 4, TFT_BLACK);
  f.drawRoundRect(x, y, w, h, 4, halZero ? red() : blue());
  f.setTextDatum(top_left);
  f.setTextColor(halZero ? red() : blue());
  f.drawString(halZero ? "HAL ZERO" : "DESK UNIT", x + 6, y + 4, &fonts::Font0);
  char buf[16];
  snprintf(buf, sizeof buf, "LINE %d/%d", n, total);
  f.setTextDatum(top_right);
  f.setTextColor(dim());
  f.drawString(buf, x + w - 6, y + 4, &fonts::Font0);
  f.setTextDatum(top_left);
  f.setTextColor(white());
  auto lines = wrap(text, 50);
  for (size_t i = 0; i < lines.size() && i < 3; ++i) f.drawString(lines[i].c_str(), x + 6, y + 15 + int(i) * 9, &fonts::Font0);
}

void drawAskBox(M5Canvas& f, const std::string& text, size_t max, uint32_t now) {
  const int x = 4, y = 112, w = f.width() - 8, h = 44;
  f.fillRoundRect(x, y, w, h, 4, TFT_BLACK);
  f.drawRoundRect(x, y, w, h, 4, white());
  f.setTextDatum(top_left);
  f.setTextColor(white());
  f.drawString("ASK HAL ZERO", x + 6, y + 4, &fonts::Font0);
  char count[16];
  snprintf(count, sizeof count, "%d/%d", int(text.size()), int(max));
  f.setTextDatum(top_right);
  f.setTextColor(text.size() >= max ? red() : dim());
  f.drawString(count, x + w - 6, y + 4, &fonts::Font0);
  // "> " and the text, cut every 49 characters (three rows hold all 120), with a blinking cursor.
  std::string shown = "> " + text + ((now / 400) % 2 ? "_" : " ");
  f.setTextDatum(top_left);
  f.setTextColor(white());
  for (size_t row = 0; row < 3 && row * 49 < shown.size(); ++row)
    f.drawString(shown.substr(row * 49, 49).c_str(), x + 6, y + 15 + int(row) * 9, &fonts::Font0);
}

void drawBanner(M5Canvas& f, const char* text, uint32_t now) {
  f.fillRect(0, 140, f.width(), 16, (now / 400) % 2 ? red() : TFT_BLACK);
  f.setTextDatum(middle_center);
  f.setTextColor((now / 400) % 2 ? TFT_BLACK : red());
  f.drawString(text, f.width() / 2, 148, &fonts::Font0);
}
