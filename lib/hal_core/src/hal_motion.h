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
  // Degrees between gravity now and the first rest ("home"), or -1 before
  // HAL has first settled. Works whichever way the sensor is mounted.
  int tiltFromHomeDeg() const;

 private:
  float g_[3] = {0, 0, 0};
  bool gInit_ = false;
  float rest_[3] = {0, 0, 0};
  bool haveRest_ = false;
  float home_[3] = {0, 0, 0};
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

// How much of the camera picture changed: 1% of the cells = 5 points, capped at 255.
int motionScore(int movingCells, int totalCells);

// HAL himself is moving: a jolt or tilt (accelerometer change > 0.08 g) or a turn
// (any gyro axis > 5 deg/s). Then the camera view swings and nothing is an arrival.
bool imuSaysMoving(const float accel[3], const float prevAccel[3], const float gyroDps[3]);

// Camera motion scores (0..255, one per frame) -> "someone just walked in".
// The room must be settled (score < threshold/2 for 20 s); then 2 frames in a row
// at or above the threshold fire once, and the room must settle again. Middling
// movement for over a second (someone already there, fidgeting) unsettles it.
class ArrivalDetector {
 public:
  explicit ArrivalDetector(int threshold) : threshold_(threshold) {}
  bool update(int score, uint32_t nowMs);
  // HAL himself is moving (the whole view swings): forget the settle.
  void disturb() {
    settled_ = false;
    settling_ = false;
    busy_ = false;
    hits_ = 0;
  }
  bool settled() const { return settled_; }

 private:
  int threshold_;
  bool settled_ = false;
  bool settling_ = false;
  uint32_t settleStartMs_ = 0;
  bool busy_ = false;
  uint32_t busySinceMs_ = 0;
  int hits_ = 0;
};

}  // namespace hal
