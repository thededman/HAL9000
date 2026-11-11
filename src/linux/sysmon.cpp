#include "sysmon.h"

#include <unistd.h>

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

namespace {

constexpr size_t kHistory = 60;

bool slurp(const char* path, std::string* out) {
  std::ifstream f(path);
  if (!f) return false;
  std::stringstream ss;
  ss << f.rdbuf();
  *out = ss.str();
  return true;
}

void push(std::vector<float>& series, float v) {
  series.push_back(v);
  if (series.size() > kHistory) series.erase(series.begin());
}

}  // namespace

void SysMon::sample() {
  std::string text;
  if (!fixedRead_) {
    fixedRead_ = true;
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    stats_.cores = n > 0 ? static_cast<int>(n > 4 ? 4 : n) : 0;
    if (slurp("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq", &text))
      stats_.maxMHz = std::atoi(text.c_str()) / 1000;
    if (slurp("/proc/meminfo", &text)) stats_.memTotalKb = hal::parseMeminfoKb(text, "MemTotal");
  }
  if (slurp("/proc/stat", &text)) {
    std::vector<hal::CpuTimes> now = hal::parseProcStat(text);
    if (!lastCpu_.empty())  // the first sample only sets the baseline
      for (size_t i = 0; i < now.size() && i < lastCpu_.size() && i < 4; ++i)
        push(stats_.cpu[i], float(hal::cpuPercent(lastCpu_[i], now[i])));
    if (!now.empty()) {
      lastCpu_ = now;
      stats_.valid = true;
    }
  }
  if (slurp("/proc/meminfo", &text)) {
    long total = hal::parseMeminfoKb(text, "MemTotal"), avail = hal::parseMeminfoKb(text, "MemAvailable");
    if (total > 0 && avail >= 0) push(stats_.memUsedPct, 100.0f * float(total - avail) / float(total));
  }
  if (slurp("/sys/class/thermal/thermal_zone0/temp", &text) && !text.empty())
    push(stats_.tempC, std::atoi(text.c_str()) / 1000.0f);
  if (slurp("/proc/net/wireless", &text)) {
    int dbm = hal::parseWirelessDbm(text);
    if (dbm != 0) push(stats_.rssiDbm, float(dbm));
  }
}
