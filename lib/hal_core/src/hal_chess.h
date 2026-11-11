#pragma once
// The desk HAL's chess screen: a few well-known short games, replayed move by
// move, HAL always on the winning side. Fixed move lists, no chess engine: every
// move is a plain "from-to" (captures overwrite; no castling, en passant or promotion).
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace hal::chess {

// index = (rank-1)*8 + (file-'a'); "PNBRQK" white, "pnbrqk" black, '.' empty.
using Board = std::array<char, 64>;

struct Game {
  const char* name;
  bool halIsWhite;
  std::vector<std::string> moves;  // "e2e4" ...; the last one is mate
};

const std::vector<Game>& games();
Board startBoard();
int square(const char* sq);                         // "f7" -> 53, -1 if malformed
bool applyMove(Board& b, const std::string& move);  // false if malformed or the from-square is empty
Board replay(const Game& g, int movesPlayed);       // the first n moves
// How many moves are on the board tMs into HAL's line: spread over the line,
// 300..900 ms a move (500 ms when the length is unknown).
int movesShown(uint32_t tMs, uint32_t lineMs, int totalMoves);

}  // namespace hal::chess
