#include <unity.h>

#include <cstdint>
#include <initializer_list>

#include "hal_fetchrule.h"

void setUp() {}
void tearDown() {}

using hal::FetchVerdict;

// Measured on the home network with the computer on battery (2026-11-18): single round trips of
// up to half a second, and a connection that needed its first packet resent and took 1.1 s.
// A HAL who gives up after half a second loses his line for nothing.
void test_a_slow_connection_is_waited_for() {
  for (uint32_t ms : {0u, 300u, 501u, 1100u, 1600u, 3100u, 4000u})
    TEST_ASSERT_EQUAL_MESSAGE(int(FetchVerdict::Wait), int(hal::fetchVerdict(false, 1000, 1000, 1000 + ms)), "still connecting");
}

void test_but_not_for_ever() {
  TEST_ASSERT_EQUAL(int(FetchVerdict::NoAnswer), int(hal::fetchVerdict(false, 1000, 1000, 1000 + hal::kFetchConnectMs + 1)));
  TEST_ASSERT_EQUAL(int(FetchVerdict::NoAnswer), int(hal::fetchVerdict(false, 1000, 1000, 1000 + 6000)));
  // HAL Zero stops waiting for READY after 8 s: the connection's share leaves time for the clip itself.
  TEST_ASSERT_TRUE(hal::kFetchConnectMs >= 3200);  // room for a first packet resent twice (about 1.5 s, then 3 s)
  TEST_ASSERT_TRUE(hal::kFetchConnectMs + 2000 <= hal::kFetchTotalMs);
  TEST_ASSERT_TRUE(hal::kFetchTotalMs < 8000);
}

void test_a_download_that_stalls_or_drags_is_dropped() {
  // Connected at 1200 and reading: fine while bytes keep coming.
  TEST_ASSERT_EQUAL(int(FetchVerdict::Wait), int(hal::fetchVerdict(true, 1000, 3900, 4000)));
  // Nothing for three seconds.
  TEST_ASSERT_EQUAL(int(FetchVerdict::Wait), int(hal::fetchVerdict(true, 1000, 1200, 1200 + hal::kFetchStallMs)));
  TEST_ASSERT_EQUAL(int(FetchVerdict::Stalled), int(hal::fetchVerdict(true, 1000, 1200, 1200 + hal::kFetchStallMs + 1)));
  // Still trickling in after seven seconds in all: HAL Zero has nearly given up anyway.
  TEST_ASSERT_EQUAL(int(FetchVerdict::Wait), int(hal::fetchVerdict(true, 1000, 7900, 1000 + hal::kFetchTotalMs)));
  TEST_ASSERT_EQUAL(int(FetchVerdict::TooLong), int(hal::fetchVerdict(true, 1000, 8000, 1000 + hal::kFetchTotalMs + 1)));
  TEST_ASSERT_EQUAL(int(FetchVerdict::TooLong), int(hal::fetchVerdict(false, 1000, 1000, 1000 + hal::kFetchTotalMs + 1)));
}

void test_the_clock_may_wrap() {  // millis() starts again from 0 after 49.7 days
  const uint32_t started = 0xFFFFFF00u;  // 256 ms before the wrap
  TEST_ASSERT_EQUAL(int(FetchVerdict::Wait), int(hal::fetchVerdict(false, started, started, 1000)));  // 1.256 s in
  TEST_ASSERT_EQUAL(int(FetchVerdict::NoAnswer), int(hal::fetchVerdict(false, started, started, 4000)));
  TEST_ASSERT_EQUAL(int(FetchVerdict::Wait), int(hal::fetchVerdict(true, started, 900, 1000)));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_a_slow_connection_is_waited_for);
  RUN_TEST(test_but_not_for_ever);
  RUN_TEST(test_a_download_that_stalls_or_drags_is_dropped);
  RUN_TEST(test_the_clock_may_wrap);
  return UNITY_END();
}
