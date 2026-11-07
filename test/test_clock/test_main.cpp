#include <unity.h>
#include "hal_clock.h"

void setUp() {}
void tearDown() {}

void test_parseHHMM() {
  TEST_ASSERT_EQUAL(0, hal::parseHHMM("0:00"));
  TEST_ASSERT_EQUAL(22 * 60, hal::parseHHMM("22:00"));
  TEST_ASSERT_EQUAL(7 * 60 + 30, hal::parseHHMM("07:30"));
  TEST_ASSERT_EQUAL(-1, hal::parseHHMM("24:00"));
  TEST_ASSERT_EQUAL(-1, hal::parseHHMM("7:60"));
  TEST_ASSERT_EQUAL(-1, hal::parseHHMM("seven"));
  TEST_ASSERT_EQUAL(-1, hal::parseHHMM(""));
}

// Review Focus 5: window across midnight, boundaries
void test_quiet_across_midnight() {
  int s = 22 * 60, e = 7 * 60 + 30;
  TEST_ASSERT_TRUE(hal::isQuietHours(22 * 60, s, e));
  TEST_ASSERT_TRUE(hal::isQuietHours(0, s, e));
  TEST_ASSERT_TRUE(hal::isQuietHours(7 * 60 + 29, s, e));
  TEST_ASSERT_FALSE(hal::isQuietHours(7 * 60 + 30, s, e));
  TEST_ASSERT_FALSE(hal::isQuietHours(21 * 60 + 59, s, e));
}

void test_quiet_same_day_and_empty() {
  TEST_ASSERT_TRUE(hal::isQuietHours(14 * 60, 13 * 60, 15 * 60));
  TEST_ASSERT_FALSE(hal::isQuietHours(15 * 60, 13 * 60, 15 * 60));
  TEST_ASSERT_FALSE(hal::isQuietHours(9 * 60, 9 * 60, 9 * 60));
}

void test_greet_bucket() {
  TEST_ASSERT_TRUE(hal::GreetBucket::Evening == hal::greetBucket(4));
  TEST_ASSERT_TRUE(hal::GreetBucket::Morning == hal::greetBucket(5));
  TEST_ASSERT_TRUE(hal::GreetBucket::Morning == hal::greetBucket(11));
  TEST_ASSERT_TRUE(hal::GreetBucket::Afternoon == hal::greetBucket(12));
  TEST_ASSERT_TRUE(hal::GreetBucket::Afternoon == hal::greetBucket(16));
  TEST_ASSERT_TRUE(hal::GreetBucket::Evening == hal::greetBucket(17));
}

void test_idle_delay_range() {
  TEST_ASSERT_EQUAL_UINT32(20 * 60000u, hal::nextIdleDelayMs(0, 20, 60));
  TEST_ASSERT_EQUAL_UINT32(60 * 60000u, hal::nextIdleDelayMs(40 * 60000u, 20, 60));
  TEST_ASSERT_EQUAL_UINT32(20 * 60000u, hal::nextIdleDelayMs(12345, 20, 20));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_parseHHMM);
  RUN_TEST(test_quiet_across_midnight);
  RUN_TEST(test_quiet_same_day_and_empty);
  RUN_TEST(test_greet_bucket);
  RUN_TEST(test_idle_delay_range);
  return UNITY_END();
}
