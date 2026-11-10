#include <unity.h>

#include <vector>

#include "hal_motiongrid.h"

using hal::MotionGrid;

void setUp() {}
void tearDown() {}

namespace {
std::vector<uint8_t> flat(uint8_t v) { return std::vector<uint8_t>(MotionGrid::kW * MotionGrid::kH, v); }
}  // namespace

void test_first_frame_scores_zero() {
  MotionGrid g;
  auto f = flat(100);
  g.update(f.data());
  TEST_ASSERT_EQUAL(0, g.score());
  int x, y, w, h;
  TEST_ASSERT_FALSE(g.box(&x, &y, &w, &h));
}

void test_identical_frames_score_zero() {
  MotionGrid g;
  auto f = flat(100);
  g.update(f.data());
  g.update(f.data());
  TEST_ASSERT_EQUAL(0, g.score());
}

void test_changed_cells_score_and_box() {
  MotionGrid g;
  auto a = flat(100), b = flat(100);
  // 16 x 12 = 192 cells (4% of 4800) change by 60, at cells x 10..25, y 20..31
  for (int y = 20; y < 32; ++y)
    for (int x = 10; x < 26; ++x) b[y * MotionGrid::kW + x] = 160;
  g.update(a.data());
  g.update(b.data());
  TEST_ASSERT_EQUAL(20, g.score());
  int x, y, w, h;
  TEST_ASSERT_TRUE(g.box(&x, &y, &w, &h));  // in 160x120 frame coordinates
  TEST_ASSERT_EQUAL(20, x);
  TEST_ASSERT_EQUAL(40, y);
  TEST_ASSERT_EQUAL(32, w);
  TEST_ASSERT_EQUAL(24, h);
}

void test_small_changes_ignored() {
  MotionGrid g;
  auto a = flat(100), b = flat(125);  // +25 everywhere: not more than the 25-level noise floor
  g.update(a.data());
  g.update(b.data());
  TEST_ASSERT_EQUAL(0, g.score());
}

void test_box_needs_twelve_cells() {
  MotionGrid g;
  auto a = flat(100), b = flat(100);
  for (int x = 0; x < 11; ++x) b[x] = 200;  // 11 cells
  g.update(a.data());
  g.update(b.data());
  int x, y, w, h;
  TEST_ASSERT_FALSE(g.box(&x, &y, &w, &h));
}

void test_grey_from_swapped_565() {
  TEST_ASSERT_EQUAL(0, hal::greyFromSwapped565(0x0000));
  TEST_ASSERT_EQUAL(250, hal::greyFromSwapped565(0xFFFF));
  TEST_ASSERT_TRUE(hal::greyFromSwapped565(0x00F8) > 0);  // pure red, byte-swapped
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_first_frame_scores_zero);
  RUN_TEST(test_identical_frames_score_zero);
  RUN_TEST(test_changed_cells_score_and_box);
  RUN_TEST(test_small_changes_ignored);
  RUN_TEST(test_box_needs_twelve_cells);
  RUN_TEST(test_grey_from_swapped_565);
  return UNITY_END();
}
