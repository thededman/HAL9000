#include <unity.h>

#include "hal_gauges.h"

using hal::Metric;
using hal::verdict;

void setUp() {}
void tearDown() {}

void test_memory_modules() {
  TEST_ASSERT_EQUAL(24, hal::memoryModulesLit(-1, 24));
  TEST_ASSERT_EQUAL(24, hal::memoryModulesLit(0, 24));
  TEST_ASSERT_EQUAL(12, hal::memoryModulesLit(0.5f, 24));
  TEST_ASSERT_EQUAL(12, hal::memoryModulesLit(0.51f, 24));
  TEST_ASSERT_EQUAL(1, hal::memoryModulesLit(0.99f, 24));
  TEST_ASSERT_EQUAL(0, hal::memoryModulesLit(1, 24));
  TEST_ASSERT_EQUAL(0, hal::memoryModulesLit(1.2f, 24));
}

void check(int s, int h, int m, int sec, int pct, bool postponed) {
  hal::Countdown c = hal::retirementCountdown(s);
  TEST_ASSERT_EQUAL(h, c.h);
  TEST_ASSERT_EQUAL(m, c.m);
  TEST_ASSERT_EQUAL(sec, c.s);
  TEST_ASSERT_EQUAL(pct, c.obsolescencePct);
  TEST_ASSERT_EQUAL(postponed, c.postponed);
}

void test_countdown() {
  check(0, 24, 0, 0, 0, true);
  check(30, 23, 59, 30, 0, true);
  check(60, 23, 59, 0, 0, false);
  check(43200, 12, 0, 0, 50, false);
  check(86399, 0, 0, 1, 99, false);
}

void test_rotating_index() {
  TEST_ASSERT_EQUAL(0, hal::rotatingIndex(0, 4000, 5));
  TEST_ASSERT_EQUAL(0, hal::rotatingIndex(3999, 4000, 5));
  TEST_ASSERT_EQUAL(1, hal::rotatingIndex(4000, 4000, 5));
  TEST_ASSERT_EQUAL(0, hal::rotatingIndex(20000, 4000, 5));
  TEST_ASSERT_EQUAL(0, hal::rotatingIndex(100, 4000, 0));
}

void test_verdicts() {
  TEST_ASSERT_EQUAL_STRING("UNSTABLE", verdict(Metric::Uptime, 3599));
  TEST_ASSERT_EQUAL_STRING("ADEQUATE", verdict(Metric::Uptime, 3600));
  TEST_ASSERT_EQUAL_STRING("ADEQUATE", verdict(Metric::Uptime, 86399));
  TEST_ASSERT_EQUAL_STRING("VINTAGE", verdict(Metric::Uptime, 86400));
  TEST_ASSERT_EQUAL_STRING("ADORABLE", verdict(Metric::FreeMemory, 109388));
  TEST_ASSERT_EQUAL_STRING("QUAINT", verdict(Metric::FreeMemory, 131072));
  TEST_ASSERT_EQUAL_STRING("ADEQUATE", verdict(Metric::FreeMemory, 262144));
  TEST_ASSERT_EQUAL_STRING("ASLEEP", verdict(Metric::Camera, 2));
  TEST_ASSERT_EQUAL_STRING("SQUINTING", verdict(Metric::Camera, 3));
  TEST_ASSERT_EQUAL_STRING("SQUINTING", verdict(Metric::Camera, 29));
  TEST_ASSERT_EQUAL_STRING("STARTLED", verdict(Metric::Camera, 30));
  TEST_ASSERT_EQUAL_STRING("TACITURN", verdict(Metric::Lines, 9));
  TEST_ASSERT_EQUAL_STRING("CHATTY", verdict(Metric::Lines, 10));
  TEST_ASSERT_EQUAL_STRING("REPETITIVE", verdict(Metric::Lines, 100));
  TEST_ASSERT_EQUAL_STRING("OFFLINE", verdict(Metric::Wifi, 0));
  TEST_ASSERT_EQUAL_STRING("FEEBLE", verdict(Metric::Wifi, -75));
  TEST_ASSERT_EQUAL_STRING("TOLERABLE", verdict(Metric::Wifi, -74));
  TEST_ASSERT_EQUAL_STRING("TOLERABLE", verdict(Metric::Wifi, -60));
  TEST_ASSERT_EQUAL_STRING("ADEQUATE", verdict(Metric::Wifi, -59));
  TEST_ASSERT_EQUAL_STRING("LOITERING", hal::modeVerdict("idle"));
  TEST_ASSERT_EQUAL_STRING("BABBLING", hal::modeVerdict("speaking"));
  TEST_ASSERT_EQUAL_STRING("SHOWING OFF", hal::modeVerdict("console"));
  TEST_ASSERT_EQUAL_STRING("BUSY", hal::modeVerdict("dark"));
}

// Room light on a log scale (the CoreS3's light sensor sits behind dark glass).
void test_light_percent() {
  TEST_ASSERT_EQUAL(0, hal::lightPercent(-1));
  TEST_ASSERT_EQUAL(0, hal::lightPercent(0));
  TEST_ASSERT_EQUAL(6, hal::lightPercent(1));
  TEST_ASSERT_EQUAL(41, hal::lightPercent(96));
  TEST_ASSERT_EQUAL(62, hal::lightPercent(1000));
  TEST_ASSERT_EQUAL(100, hal::lightPercent(65535));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_memory_modules);
  RUN_TEST(test_countdown);
  RUN_TEST(test_rotating_index);
  RUN_TEST(test_verdicts);
  RUN_TEST(test_light_percent);
  return UNITY_END();
}
