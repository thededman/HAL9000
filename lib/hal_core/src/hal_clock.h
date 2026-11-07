#pragma once
// Time-of-day rules: quiet hours, greeting buckets, idle remark spacing.
#include <cstdint>
#include <string>

namespace hal {

// "H:MM" or "HH:MM" -> minute of day (0..1439), or -1 if invalid.
int parseHHMM(const std::string& s);

// Start inclusive, end exclusive, wraps past midnight. start == end -> never quiet.
bool isQuietHours(int minuteOfDay, int quietStartMin, int quietEndMin);

enum class GreetBucket { Morning, Afternoon, Evening };

// Morning 05-11, afternoon 12-16, evening 17-04.
GreetBucket greetBucket(int hour);

// Delay until the next unprompted remark, uniformly in [min, max] minutes.
uint32_t nextIdleDelayMs(uint32_t random, int minMinutes, int maxMinutes);

// Whether to sync the clock over Wi-Fi now. Unknown time: retry every 30 minutes.
// Known time: once a night, in the 03:00 hour, on a day not yet synced.
bool clockSyncDue(bool timeValid, int hour, int yday, int lastSyncYday, uint32_t msSinceLastAttempt);

}  // namespace hal
