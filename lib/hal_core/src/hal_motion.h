#pragma once
// Sensor logic: turns raw accelerometer and proximity readings into events.
#include <cstdint>
#include <vector>

namespace hal {

enum class MotionEvent { None, Shake, Pickup, TipOver };

// Feed accelerometer readings (in g) every ~20 ms.
//  Shake:   3 jolts (| |a| - 1 | > 0.8 g, >= 60 ms apart) within 600 ms.
//  Pickup:  tilted 20-60 degrees from rest for 700 ms.
//  TipOver: tilted >= 60 degrees from rest for 1000 ms.
// "Rest" is captured after 2 s of stillness; after a Pickup or TipOver no
// further tilt events fire until a new rest is captured.
class MotionDetector {
 public:
  MotionEvent update(float ax, float ay, float az, uint32_t nowMs);

 private:
  float g_[3] = {0, 0, 0};
  bool gInit_ = false;
  float rest_[3] = {0, 0, 0};
  bool haveRest_ = false;
  bool disturbed_ = false;
  bool stableTiming_ = false;
  uint32_t stableSince_ = 0;
  bool pickupTiming_ = false;
  uint32_t pickupSince_ = 0;
  bool tipTiming_ = false;
  uint32_t tipSince_ = 0;
  std::vector<uint32_t> peaks_;
};

// True once when a reading reaches `threshold` after having dropped below
// 60% of it (so someone standing still in front doesn't retrigger).
class ProximityEdge {
 public:
  explicit ProximityEdge(int threshold) : threshold_(threshold) {}
  bool update(int reading);

 private:
  int threshold_;
  bool armed_ = false;
};

}  // namespace hal
