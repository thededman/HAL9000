#include "hal_chess.h"

#include <algorithm>

namespace hal::chess {

const std::vector<Game>& games() {
  static const std::vector<Game> g = {
      {"Scholar's mate", true, {"e2e4", "e7e5", "f1c4", "b8c6", "d1h5", "g8f6", "h5f7"}},
      {"Fool's mate", false, {"f2f3", "e7e5", "g2g4", "d8h4"}},
      {"Legal's mate", true,
       {"e2e4", "e7e5", "g1f3", "d7d6", "f1c4", "c8g4", "b1c3", "g7g6", "f3e5", "g4d1", "c4f7", "e8e7", "c3d5"}},
      {"Blackburne Shilling trap", false,
       {"e2e4", "e7e5", "g1f3", "b8c6", "f1c4", "c6d4", "f3e5", "d8g5", "e5f7", "g5g2", "h1f1", "g2e4", "c4e2",
        "d4f3"}},
  };
  return g;
}

Board startBoard() {
  Board b;
  b.fill('.');
  const char* back = "RNBQKBNR";
  for (int f = 0; f < 8; ++f) {
    b[f] = back[f];
    b[8 + f] = 'P';
    b[48 + f] = 'p';
    b[56 + f] = static_cast<char>(back[f] - 'A' + 'a');
  }
  return b;
}

int square(const char* sq) {
  if (!sq || sq[0] < 'a' || sq[0] > 'h' || sq[1] < '1' || sq[1] > '8' || sq[2] != '\0') return -1;
  return (sq[1] - '1') * 8 + (sq[0] - 'a');
}

bool applyMove(Board& b, const std::string& move) {
  if (move.size() != 4) return false;
  int from = square(move.substr(0, 2).c_str()), to = square(move.substr(2, 2).c_str());
  if (from < 0 || to < 0 || b[from] == '.') return false;
  b[to] = b[from];
  b[from] = '.';
  return true;
}

Board replay(const Game& g, int movesPlayed) {
  Board b = startBoard();
  int n = std::min<int>(movesPlayed, static_cast<int>(g.moves.size()));
  for (int i = 0; i < n; ++i) applyMove(b, g.moves[i]);
  return b;
}

int movesShown(uint32_t tMs, uint32_t lineMs, int totalMoves) {
  if (totalMoves <= 0) return 0;
  uint32_t per = lineMs == 0 ? 500 : std::clamp<uint32_t>(lineMs / totalMoves, 300, 900);
  return std::min<int>(totalMoves, static_cast<int>(tMs / per));
}

}  // namespace hal::chess
