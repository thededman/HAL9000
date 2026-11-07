#include <unity.h>

#include <set>
#include <vector>

#include "hal_shuffle.h"

void setUp() {}
void tearDown() {}

void test_empty_returns_minus_one() {
  hal::ShuffleBag bag(1);
  bag.reset(0);
  TEST_ASSERT_EQUAL(-1, bag.next());
  TEST_ASSERT_EQUAL(-1, bag.next());
}

void test_single_item() {
  hal::ShuffleBag bag(1);
  bag.reset(1);
  for (int i = 0; i < 5; ++i) TEST_ASSERT_EQUAL(0, bag.next());
}

void test_round_is_permutation() {
  hal::ShuffleBag bag(7);
  bag.reset(7);
  std::set<int> seen;
  for (int i = 0; i < 7; ++i) {
    int v = bag.next();
    TEST_ASSERT_TRUE(v >= 0 && v < 7);
    seen.insert(v);
  }
  TEST_ASSERT_EQUAL(7, seen.size());
}

void test_no_repeat_across_rounds() {
  hal::ShuffleBag bag(3);
  bag.reset(3);
  int prev = bag.next();
  for (int i = 0; i < 300; ++i) {
    int v = bag.next();
    TEST_ASSERT_NOT_EQUAL(prev, v);
    prev = v;
  }
}

void test_same_seed_same_sequence() {
  hal::ShuffleBag a(42), b(42);
  a.reset(9);
  b.reset(9);
  for (int i = 0; i < 20; ++i) TEST_ASSERT_EQUAL(a.next(), b.next());
}

void test_reset_changes_size() {
  hal::ShuffleBag bag(5);
  bag.reset(5);
  bag.next();
  bag.reset(2);
  TEST_ASSERT_EQUAL(2, bag.size());
  for (int i = 0; i < 10; ++i) {
    int v = bag.next();
    TEST_ASSERT_TRUE(v == 0 || v == 1);
  }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_empty_returns_minus_one);
  RUN_TEST(test_single_item);
  RUN_TEST(test_round_is_permutation);
  RUN_TEST(test_no_repeat_across_rounds);
  RUN_TEST(test_same_seed_same_sequence);
  RUN_TEST(test_reset_changes_size);
  return UNITY_END();
}
