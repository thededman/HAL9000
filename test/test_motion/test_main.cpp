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

// Camera arrivals: one score per frame, ~15 fps.
struct Cam {
  hal::ArrivalDetector d{20};
  uint32_t t = 0;
  int arrivals = 0;
  void one(int score) {
    if (d.update(score, t)) ++arrivals;
    t += 66;
  }
  void hold(int score, uint32_t ms) {
    for (uint32_t i = 0; i < ms / 66; ++i) one(score);
  }
};

void test_arrival_after_settle() {
  Cam c;
  c.hold(3, 3000);
  TEST_ASSERT_EQUAL(0, c.arrivals);
  c.hold(3, 22000);
  c.one(40);
  c.one(40);
  TEST_ASSERT_EQUAL(1, c.arrivals);
}

void test_no_arrival_before_settle() {
  Cam c;
  c.hold(3, 10000);
  c.one(40);
  c.one(40);
  TEST_ASSERT_EQUAL(0, c.arrivals);
}

void test_single_frame_flicker_ignored() {
  Cam c;
  c.hold(3, 25000);
  c.one(40);
  c.one(3);
  c.one(40);
  c.one(3);
  TEST_ASSERT_EQUAL(0, c.arrivals);
}

void test_fires_once_then_resettles() {
  Cam c;
  c.hold(3, 25000);
  c.one(40);
  c.one(40);
  c.hold(40, 5000);
  TEST_ASSERT_EQUAL(1, c.arrivals);
  c.hold(3, 21000);
  c.one(40);
  c.one(40);
  TEST_ASSERT_EQUAL(2, c.arrivals);
}

// Review Focus 1: a light switching on is one big spike
void test_light_switch_spike() {
  Cam c;
  c.hold(3, 25000);
  c.one(120);
  c.one(120);
  c.one(120);
  c.hold(3, 60000);
  TEST_ASSERT_EQUAL(1, c.arrivals);
}

// Review Focus 2: auto-exposure hunting around the threshold
void test_hovering_scores() {
  Cam c;
  c.hold(3, 25000);
  for (int i = 0; i < 60000 / 66; ++i) c.one(i % 2 ? 22 : 18);
  TEST_ASSERT_TRUE(c.arrivals <= 1);
}

// Review Focus 4: millis() wraps during the settle
void test_arrival_across_millis_wrap() {
  Cam c;
  c.t = 0xFFFFFFFFu - 10000u;
  c.hold(3, 25000);
  c.one(40);
  c.one(40);
  TEST_ASSERT_EQUAL(1, c.arrivals);
}

// HAL being moved swings the whole camera view: that must not count as someone arriving.
void test_disturb_requires_resettle() {
  Cam c;
  c.hold(3, 25000);  // settled
  c.d.disturb();     // the IMU says HAL is moving
  c.one(40);
  c.one(40);
  TEST_ASSERT_EQUAL(0, c.arrivals);
  c.hold(3, 21000);
  c.one(40);
  c.one(40);
  TEST_ASSERT_EQUAL(1, c.arrivals);
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
  RUN_TEST(test_arrival_after_settle);
  RUN_TEST(test_no_arrival_before_settle);
  RUN_TEST(test_single_frame_flicker_ignored);
  RUN_TEST(test_fires_once_then_resettles);
  RUN_TEST(test_light_switch_spike);
  RUN_TEST(test_hovering_scores);
  RUN_TEST(test_arrival_across_millis_wrap);
  RUN_TEST(test_disturb_requires_resettle);
  return UNITY_END();
}
