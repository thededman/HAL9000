#include "hal_motion.h"

#include <algorithm>
#include <cmath>

namespace hal {
namespace {

float angleDeg(const float* u, const float* v) {
  float dot = u[0] * v[0] + u[1] * v[1] + u[2] * v[2];
  float nu = std::sqrt(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]);
  float nv = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
  if (nu < 1e-6f || nv < 1e-6f) return 0;
  float c = std::max(-1.0f, std::min(1.0f, dot / (nu * nv)));
  return std::acos(c) * 57.29578f;
}

}  // namespace

MotionEvent MotionDetector::update(float ax, float ay, float az, uint32_t now) {
  const float a[3] = {ax, ay, az};
  if (!gInit_) {
    for (int i = 0; i < 3; ++i) g_[i] = a[i];
    gInit_ = true;
  } else {
    for (int i = 0; i < 3; ++i) g_[i] = 0.9f * g_[i] + 0.1f * a[i];
  }
  float dev = std::fabs(std::sqrt(ax * ax + ay * ay + az * az) - 1.0f);

  // Shake: count distinct jolts in a sliding 600 ms window.
  MotionEvent result = MotionEvent::None;
  if (dev > 0.8f && (peaks_.empty() || now - peaks_.back() >= 60)) peaks_.push_back(now);
  peaks_.erase(std::remove_if(peaks_.begin(), peaks_.end(),
                              [now](uint32_t p) { return now - p > 600; }),
               peaks_.end());
  if (peaks_.size() >= 3) {
    peaks_.clear();
    result = MotionEvent::Shake;
  }

  // Rest: still (1 g, reading matches the smoothed gravity) for 2 s.
  bool still = dev < 0.05f && angleDeg(a, g_) < 3.0f;
  if (!still) {
    stableTiming_ = false;
  } else if (!stableTiming_) {
    stableTiming_ = true;
    stableSince_ = now;
  } else if (now - stableSince_ >= 2000) {
    for (int i = 0; i < 3; ++i) rest_[i] = g_[i];
    haveRest_ = true;
    disturbed_ = false;
  }

  // Tilt away from rest.
  if (haveRest_ && !disturbed_) {
    float tilt = angleDeg(g_, rest_);
    if (tilt >= 60.0f) {
      pickupTiming_ = false;
      if (!tipTiming_) {
        tipTiming_ = true;
        tipSince_ = now;
      } else if (now - tipSince_ >= 1000) {
        tipTiming_ = false;
        disturbed_ = true;
        if (result == MotionEvent::None) result = MotionEvent::TipOver;
      }
    } else if (tilt >= 20.0f) {
      tipTiming_ = false;
      if (!pickupTiming_) {
        pickupTiming_ = true;
        pickupSince_ = now;
      } else if (now - pickupSince_ >= 700) {
        pickupTiming_ = false;
        disturbed_ = true;
        if (result == MotionEvent::None) result = MotionEvent::Pickup;
      }
    } else {
      tipTiming_ = false;
      pickupTiming_ = false;
    }
  }
  return result;
}

bool ProximityEdge::update(int reading) {
  if (reading < threshold_ * 6 / 10) {
    armed_ = true;
    return false;
  }
  if (reading >= threshold_ && armed_) {
    armed_ = false;
    return true;
  }
  return false;
}

bool ArrivalDetector::update(int score, uint32_t nowMs) {
  if (settled_) {
    hits_ = score >= threshold_ ? hits_ + 1 : 0;
    if (hits_ < 2) return false;
    settled_ = false;
    settling_ = false;
    hits_ = 0;
    return true;
  }
  if (score * 2 >= threshold_) {  // still busy: start the settle over
    settling_ = false;
    return false;
  }
  if (!settling_) {
    settling_ = true;
    settleStartMs_ = nowMs;
  } else if (static_cast<int32_t>(nowMs - settleStartMs_) >= 20000) {  // wrap-safe
    settled_ = true;
  }
  return false;
}

}  // namespace hal
