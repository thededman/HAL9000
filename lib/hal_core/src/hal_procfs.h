#pragma once
// HAL Zero's system monitor: pulls numbers out of Linux's /proc text files.
// Pure parsing (the Cardputer glue reads the files), so it's tested on the Mac.
#include <string>
#include <vector>

namespace hal {

struct CpuTimes {
  long long busy = 0, total = 0;  // jiffies
};
// The "cpuN" lines of /proc/stat, in N order (not the "cpu " total line).
std::vector<CpuTimes> parseProcStat(const std::string& text);
// Busy share between two samples, 0..100; 0 if no time passed or the counters went back.
int cpuPercent(const CpuTimes& before, const CpuTimes& after);
// A /proc/meminfo value in kB ("MemTotal", "MemAvailable"); -1 if absent.
long parseMeminfoKb(const std::string& text, const char* key);
// Signal level of the first interface in /proc/net/wireless, in dBm; 0 if none.
int parseWirelessDbm(const std::string& text);

}  // namespace hal
