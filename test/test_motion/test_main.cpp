#include <unity.h>

#include <cmath>
#include <vector>

#include "hal_motion.h"

using hal::MotionEvent;

void setUp() {}
void tearDown() {}

namespace {

struct Feed {
  hal::MotionDetector det;
  uint32_t t = 0;
  std::vector<MotionEvent> events;

  // Holds one accelerometer reading for `ms`, sampling every 20 ms.
  void hold(float x, float y, float z, uint32_t ms) {
    for (uint32_t end = t + ms; t < end; t += 20) {
      MotionEvent e = det.update(x, y, z, t);
      if (e != MotionEvent::None) events.push_back(e);
    }
  }
  int count(MotionEvent e) const {
    int n = 0;
    for (auto v : events) n += (v == e);
    return n;
  }
};

const float kSin30 = 0.5f, kCos30 = 0.8660254f;

}  // namespace

void test_still_is_quiet() {
  Feed f;
  f.hold(0, 0, 1, 5000);
  TEST_ASSERT_EQUAL(0, f.events.size());
}

void test_three_peaks_shake() {
  Feed f;
  f.hold(0, 0, 1, 3000);
  for (int i = 0; i < 3; ++i) {
    f.hold(0, 0, 3, 20);   // spike
    f.hold(0, 0, 1, 140);  // ~150 ms apart
  }
  TEST_ASSERT_EQUAL(1, f.count(MotionEvent::Shake));
}

void test_two_peaks_no_shake() {
  Feed f;
  f.hold(0, 0, 1, 3000);
  for (int i = 0; i < 2; ++i) {
    f.hold(0, 0, 3, 20);
    f.hold(0, 0, 1, 140);
  }
  f.hold(0, 0, 1, 1000);
  TEST_ASSERT_EQUAL(0, f.events.size());
}

void test_tip_over_once() {
  Feed f;
  f.hold(0, 0, 1, 3000);
  f.hold(1, 0, 0, 1500);
  TEST_ASSERT_EQUAL(1, f.count(MotionEvent::TipOver));
  TEST_ASSERT_EQUAL(1, f.events.size());
}

void test_pickup() {
  Feed f;
  f.hold(0, 0, 1, 3000);
  f.hold(kSin30, 0, kCos30, 1200);
  TEST_ASSERT_EQUAL(1, f.count(MotionEvent::Pickup));
  TEST_ASSERT_EQUAL(1, f.events.size());
}

void test_brief_tilt_ignored() {
  Feed f;
  f.hold(0, 0, 1, 3000);
  f.hold(kSin30, 0, kCos30, 300);
  f.hold(0, 0, 1, 2000);
  TEST_ASSERT_EQUAL(0, f.events.size());
}

void test_proximity_edge_hysteresis() {
  hal::ProximityEdge p(300);
  TEST_ASSERT_FALSE(p.update(100));
  TEST_ASSERT_TRUE(p.update(320));   // came close
  TEST_ASSERT_FALSE(p.update(320));  // still close
  TEST_ASSERT_FALSE(p.update(250));  // not far enough to re-arm (needs < 180)
  TEST_ASSERT_FALSE(p.update(320));
  TEST_ASSERT_FALSE(p.update(150));  // far: re-armed
  TEST_ASSERT_TRUE(p.update(320));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_still_is_quiet);
  RUN_TEST(test_three_peaks_shake);
  RUN_TEST(test_two_peaks_no_shake);
  RUN_TEST(test_tip_over_once);
  RUN_TEST(test_pickup);
  RUN_TEST(test_brief_tilt_ignored);
  RUN_TEST(test_proximity_edge_hysteresis);
  return UNITY_END();
}
