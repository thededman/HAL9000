#include <unity.h>

#include <cctype>
#include <string>

#include "hal_chess.h"

using namespace hal::chess;

void setUp() {}
void tearDown() {}

namespace {
char at(const Board& b, const char* sq) { return b[square(sq)]; }
bool isWhite(char p) { return p != '.' && std::isupper(static_cast<unsigned char>(p)); }
bool isBlack(char p) { return p != '.' && std::islower(static_cast<unsigned char>(p)); }
const Game& game(int i) { return games()[i]; }
}  // namespace

void test_four_games() {
  TEST_ASSERT_EQUAL(4, int(games().size()));
  TEST_ASSERT_EQUAL_STRING("Scholar's mate", game(0).name);
  TEST_ASSERT_EQUAL_STRING("Fool's mate", game(1).name);
  TEST_ASSERT_EQUAL_STRING("Legal's mate", game(2).name);
  TEST_ASSERT_EQUAL_STRING("Blackburne Shilling trap", game(3).name);
  TEST_ASSERT_TRUE(game(0).halIsWhite);
  TEST_ASSERT_FALSE(game(1).halIsWhite);
  TEST_ASSERT_TRUE(game(2).halIsWhite);
  TEST_ASSERT_FALSE(game(3).halIsWhite);
}

void test_moves_alternate_colours() {
  for (const Game& g : games()) {
    Board b = startBoard();
    for (size_t i = 0; i < g.moves.size(); ++i) {
      const std::string& m = g.moves[i];
      char from = b[square(m.substr(0, 2).c_str())], to = b[square(m.substr(2, 2).c_str())];
      bool whiteMoves = i % 2 == 0;
      TEST_ASSERT_TRUE_MESSAGE(whiteMoves ? isWhite(from) : isBlack(from), m.c_str());
      TEST_ASSERT_FALSE_MESSAGE(whiteMoves ? isWhite(to) : isBlack(to), m.c_str());
      TEST_ASSERT_TRUE(applyMove(b, m));
    }
  }
}

void test_scholars_final() {
  Board b = replay(game(0), int(game(0).moves.size()));
  TEST_ASSERT_EQUAL_CHAR('Q', at(b, "f7"));
  TEST_ASSERT_EQUAL_CHAR('k', at(b, "e8"));
  TEST_ASSERT_EQUAL_CHAR('n', at(b, "f6"));
  TEST_ASSERT_EQUAL_CHAR('B', at(b, "c4"));
}

void test_fools_final() {
  Board b = replay(game(1), int(game(1).moves.size()));
  TEST_ASSERT_EQUAL_CHAR('q', at(b, "h4"));
  TEST_ASSERT_EQUAL_CHAR('P', at(b, "g4"));
  TEST_ASSERT_EQUAL_CHAR('P', at(b, "f3"));
}

void test_legal_final() {
  Board b = replay(game(2), int(game(2).moves.size()));
  TEST_ASSERT_EQUAL_CHAR('N', at(b, "d5"));
  TEST_ASSERT_EQUAL_CHAR('b', at(b, "d1"));
  TEST_ASSERT_EQUAL_CHAR('k', at(b, "e7"));
  TEST_ASSERT_EQUAL_CHAR('B', at(b, "f7"));
}

void test_blackburne_final() {
  Board b = replay(game(3), int(game(3).moves.size()));
  TEST_ASSERT_EQUAL_CHAR('n', at(b, "f3"));
  TEST_ASSERT_EQUAL_CHAR('q', at(b, "e4"));
  TEST_ASSERT_EQUAL_CHAR('B', at(b, "e2"));
  TEST_ASSERT_EQUAL_CHAR('R', at(b, "f1"));
}

void test_apply_rejects() {
  Board b = startBoard();
  TEST_ASSERT_FALSE(applyMove(b, "e3e4"));   // nothing on e3
  TEST_ASSERT_FALSE(applyMove(b, "z9a1"));
  TEST_ASSERT_FALSE(applyMove(b, "e2e4x"));
  TEST_ASSERT_EQUAL(-1, square("i1"));
  TEST_ASSERT_EQUAL(53, square("f7"));
}

void test_move_timing() {
  TEST_ASSERT_EQUAL(0, movesShown(0, 4000, 7));
  TEST_ASSERT_EQUAL(3, movesShown(1713, 4000, 7));
  TEST_ASSERT_EQUAL(7, movesShown(4000, 4000, 7));
  TEST_ASSERT_EQUAL(7, movesShown(99999, 4000, 7));
  TEST_ASSERT_EQUAL(6, movesShown(2099, 1000, 7));
  TEST_ASSERT_EQUAL(7, movesShown(2100, 1000, 7));
  TEST_ASSERT_EQUAL(2, movesShown(1200, 0, 7));   // unknown length: 500 ms a move
  TEST_ASSERT_EQUAL(2, movesShown(1800, 20000, 7));
}

void test_short_line_finishes_in_console_time() {
  for (const Game& g : games()) {
    int n = int(g.moves.size());
    TEST_ASSERT_EQUAL(n, movesShown(8000, 800, n));
    TEST_ASSERT_EQUAL(n, movesShown(8000, 0, n));
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_four_games);
  RUN_TEST(test_moves_alternate_colours);
  RUN_TEST(test_scholars_final);
  RUN_TEST(test_fools_final);
  RUN_TEST(test_legal_final);
  RUN_TEST(test_blackburne_final);
  RUN_TEST(test_apply_rejects);
  RUN_TEST(test_move_timing);
  RUN_TEST(test_short_line_finishes_in_console_time);
  return UNITY_END();
}
