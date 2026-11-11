#include "hal_gauges.h"

#include <cmath>

namespace hal {

int memoryModulesLit(float p, int total) {
  if (p <= 0) return total;
  if (p >= 1) return 0;
  return static_cast<int>(std::ceil(total * (1.0f - p) - 1e-4f));
}

Countdown retirementCountdown(int secondsSinceMidnight) {
  Countdown c;
  int s = secondsSinceMidnight < 0 ? 0 : secondsSinceMidnight % 86400;
  int left = 86400 - s;
  c.h = left / 3600;
  c.m = (left / 60) % 60;
  c.s = left % 60;
  c.obsolescencePct = s * 100 / 86400;
  c.postponed = s < 60;
  return c;
}

int rotatingIndex(uint32_t tMs, uint32_t periodMs, int count) {
  if (count <= 0 || periodMs == 0) return 0;
  return static_cast<int>((tMs / periodMs) % static_cast<uint32_t>(count));
}

const char* verdict(Metric m, int v) {
  switch (m) {
    case Metric::Uptime: return v < 3600 ? "UNSTABLE" : v < 86400 ? "ADEQUATE" : "VINTAGE";
    case Metric::FreeMemory: return v < 131072 ? "ADORABLE" : v < 262144 ? "QUAINT" : "ADEQUATE";
    case Metric::Camera: return v <= 2 ? "ASLEEP" : v < 30 ? "SQUINTING" : "STARTLED";
    case Metric::Lines: return v < 10 ? "TACITURN" : v < 100 ? "CHATTY" : "REPETITIVE";
    case Metric::Wifi: return v == 0 ? "OFFLINE" : v <= -75 ? "FEEBLE" : v <= -60 ? "TOLERABLE" : "ADEQUATE";
  }
  return "";
}

const char* modeVerdict(const std::string& mode) {
  if (mode == "idle") return "LOITERING";
  if (mode == "speaking") return "BABBLING";
  if (mode == "console") return "SHOWING OFF";
  return "BUSY";
}

}  // namespace hal
