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

// Room light 0..100 on a log scale from the light sensor's raw 0..65535 count.
int lightPercent(int raw);

int rotatingIndex(uint32_t tMs, uint32_t periodMs, int count);  // which item, changing every periodMs

// Which of `count` items a screen shows `tMs` after it opened, a new one every
// periodMs. Each opening starts at `start` (the screen's per-open seed), so over
// several openings every item has its turn: a screen is only up for a few periods.
// `first` (when in range) is shown before the round begins: an item that mustn't
// be missed, like the first reading in alarm.
int showingIndex(uint32_t tMs, uint32_t periodMs, int count, uint32_t start, int first = -1);

enum class Metric { Uptime, FreeMemory, Camera, Lines, Wifi };
const char* verdict(Metric m, int value);
const char* modeVerdict(const std::string& mode);

}  // namespace hal
