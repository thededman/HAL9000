// Chess: HAL plays one of a few well-known short games against Dave, a move at a
// time over his line, and wins.
#include <stdio.h>

#include <algorithm>
#include <string>

#include "hal_chess.h"
#include "screens.h"

namespace screens {
namespace {

void piece(M5Canvas& c, int x, int y, int sq, char p, const lgfx::IFont* font) {
  const bool white_ = p >= 'A' && p <= 'Z';
  const int cx = x + sq / 2, cy = y + sq / 2, r = sq * 38 / 100;
  char s[2] = {static_cast<char>(white_ ? p : p - 'a' + 'A'), 0};
  c.setTextDatum(middle_center);
  if (white_) {
    c.fillCircle(cx, cy, r, white());
    c.setTextColor(TFT_BLACK);
  } else {
    c.fillCircle(cx, cy, r, TFT_BLACK);
    c.drawCircle(cx, cy, r, red());
    c.setTextColor(red());
  }
  c.drawString(s, cx, cy + 1, font);
}

}  // namespace

void chess(M5Canvas& c, const hal::Layout& L, uint32_t tMs, const ScreenData& d) {
  using namespace hal::chess;
  const Game& g = games()[d.seed % games().size()];
  const int total = static_cast<int>(g.moves.size());
  const int shown = movesShown(tMs, d.lineMs, total);
  const bool mate = shown >= total;
  frame(c, L, "STRATEGY: CHESS", tMs);

  const Board b = replay(g, shown);
  const int sq = (L.contentH - 8) / 8, bx = 8, by = L.contentTop + 4;
  const lgfx::IFont* font = sq >= 22 ? L.font2() : static_cast<const lgfx::IFont*>(&fonts::Font0);
  int from = -1, to = -1;
  if (shown > 0) {
    const std::string& m = g.moves[shown - 1];
    from = square(m.substr(0, 2).c_str());
    to = square(m.substr(2, 2).c_str());
  }
  for (int r = 0; r < 8; ++r)
    for (int f = 0; f < 8; ++f) {
      const int x = bx + f * sq, y = by + (7 - r) * sq, i = r * 8 + f;
      c.fillRect(x, y, sq, sq, (r + f) % 2 ? lgfx::color565(34, 44, 78) : lgfx::color565(10, 12, 22));
      if (i == from || i == to) c.drawRect(x, y, sq, sq, red());
      if (b[i] != '.') piece(c, x, y, sq, b[i], font);
    }
  c.drawRect(bx - 1, by - 1, sq * 8 + 2, sq * 8 + 2, dim());

  // Right: the game, who plays what, and the moves so far.
  const int px = bx + sq * 8 + 10;
  int y = by;
  c.setTextDatum(top_left);
  c.setTextColor(white());
  const std::string name = g.name;
  const size_t fit = static_cast<size_t>((L.w - 6 - px) / 6);  // Font0 is 6 px a character
  if (name.size() <= fit) {
    c.drawString(name.c_str(), px, y, &fonts::Font0);
  } else {  // two lines, split at the last space that fits
    size_t cut = name.rfind(' ', fit);
    if (cut == std::string::npos) cut = fit;
    c.drawString(name.substr(0, cut).c_str(), px, y, &fonts::Font0);
    y += 10;
    c.drawString(name.substr(cut + (name[cut] == ' ' ? 1 : 0)).c_str(), px, y, &fonts::Font0);
  }
  y += 12;
  c.setTextColor(blue());
  c.drawString(g.halIsWhite ? "HAL: WHITE" : "HAL: BLACK", px, y, &fonts::Font0);
  y += 10;
  c.setTextColor(red());
  c.drawString(g.halIsWhite ? "DAVE: BLACK" : "DAVE: WHITE", px, y, &fonts::Font0);
  y += 16;
  const int bannerH = 20;
  const int listBottom = by + sq * 8 - (mate ? bannerH + 16 : 12);
  const int maxRows = std::max(1, (listBottom - y) / 10);
  const int pairs = (shown + 1) / 2, first = std::max(0, pairs - maxRows);
  char buf[32];
  for (int p = first; p < pairs; ++p) {
    const std::string& w = g.moves[p * 2];
    if (p * 2 + 1 < shown) {
      const std::string& k = g.moves[p * 2 + 1];
      snprintf(buf, sizeof buf, "%2d. %.2s-%.2s %.2s-%.2s", p + 1, w.c_str(), w.c_str() + 2, k.c_str(), k.c_str() + 2);
    } else {
      snprintf(buf, sizeof buf, "%2d. %.2s-%.2s", p + 1, w.c_str(), w.c_str() + 2);
    }
    c.setTextColor(p == pairs - 1 ? white() : blue());
    c.drawString(buf, px, y, &fonts::Font0);
    y += 10;
  }
  c.setTextDatum(bottom_left);
  if (mate) {  // beside the board, so the mating move stays in view
    const int bw = L.w - 6 - px, yy = by + sq * 8 - bannerH;
    c.setTextColor(red());
    c.drawString("HAL WINS. AGAIN.", px, yy - 4, &fonts::Font0);
    c.fillRect(px, yy, bw, bannerH, red());
    c.setTextColor(white());
    c.setTextDatum(middle_center);
    c.drawString("CHECKMATE", px + bw / 2, yy + bannerH / 2, &fonts::Font0);
  } else {
    const bool whiteToMove = shown % 2 == 0;
    c.setTextColor((tMs / 400) % 2 ? dim() : blue());
    c.drawString(whiteToMove == g.halIsWhite ? "HAL THINKING" : "DAVE THINKING", px, by + sq * 8, &fonts::Font0);
  }
}

}  // namespace screens
