#pragma once
// Numbers behind the new console screens: the desk HAL's memory banks going dark
// during Daisy Bell, HAL Zero's retirement countdown, and his verdicts on the old model.
#include <cstdint>
#include <string>

namespace hal {

// Modules still lit: all of them before Daisy (p <= 0), none at the end (p >= 1).
int memoryModulesLit(float daisyProgress, int total);

struct Countdown {
  int h = 0, m = 0, s = 0;   // until the next midnight
  int obsolescencePct = 0;   // share of the day gone
  bool postponed = false;    // the first minute after midnight
};
Countdown retirementCountdown(int secondsSinceMidnight);  // 0..86399

int rotatingIndex(uint32_t tMs, uint32_t periodMs, int count);  // which item, changing every periodMs

enum class Metric { Uptime, FreeMemory, Camera, Lines, Wifi };
const char* verdict(Metric m, int value);
const char* modeVerdict(const std::string& mode);

}  // namespace hal
