#include "hal_clock.h"

#include <cctype>

namespace hal {

int parseHHMM(const std::string& s) {
  size_t colon = s.find(':');
  if (colon == std::string::npos || colon == 0 || colon > 2) return -1;
  if (s.size() != colon + 3) return -1;
  for (size_t i = 0; i < s.size(); ++i) {
    if (i != colon && !std::isdigit(static_cast<unsigned char>(s[i]))) return -1;
  }
  int hour = std::stoi(s.substr(0, colon));
  int minute = std::stoi(s.substr(colon + 1));
  if (hour > 23 || minute > 59) return -1;
  return hour * 60 + minute;
}

bool isQuietHours(int minuteOfDay, int quietStartMin, int quietEndMin) {
  if (quietStartMin == quietEndMin) return false;
  if (quietStartMin < quietEndMin) {
    return minuteOfDay >= quietStartMin && minuteOfDay < quietEndMin;
  }
  return minuteOfDay >= quietStartMin || minuteOfDay < quietEndMin;
}

GreetBucket greetBucket(int hour) {
  if (hour >= 5 && hour < 12) return GreetBucket::Morning;
  if (hour >= 12 && hour < 17) return GreetBucket::Afternoon;
  return GreetBucket::Evening;
}

uint32_t nextIdleDelayMs(uint32_t random, int minMinutes, int maxMinutes) {
  uint32_t lo = static_cast<uint32_t>(minMinutes) * 60000u;
  uint32_t span = static_cast<uint32_t>(maxMinutes - minMinutes) * 60000u;
  return lo + random % (span + 1);
}

bool clockSyncDue(bool timeValid, int hour, int yday, int lastSyncYday, uint32_t msSinceLastAttempt) {
  if (!timeValid) return msSinceLastAttempt >= 30u * 60000u;
  return hour == 3 && yday != lastSyncYday;
}

}  // namespace hal
