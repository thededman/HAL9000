#include "hal_procfs.h"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <sstream>

namespace hal {

std::vector<CpuTimes> parseProcStat(const std::string& text) {
  std::vector<CpuTimes> cpus;
  std::istringstream in(text);
  for (std::string line; std::getline(in, line);) {
    if (line.size() < 4 || line.compare(0, 3, "cpu") != 0 || !std::isdigit(static_cast<unsigned char>(line[3])))
      continue;
    std::istringstream f(line);
    std::string name;
    long long v[8] = {};  // user nice system idle iowait irq softirq steal
    f >> name;
    int got = 0;
    while (got < 8 && f >> v[got]) ++got;
    if (got < 5) continue;
    CpuTimes t;
    t.busy = v[0] + v[1] + v[2] + v[5] + v[6] + v[7];
    t.total = t.busy + v[3] + v[4];
    cpus.push_back(t);
  }
  return cpus;
}

int cpuPercent(const CpuTimes& before, const CpuTimes& after) {
  long long dt = after.total - before.total, db = after.busy - before.busy;
  if (dt <= 0 || db < 0) return 0;
  long long p = db * 100 / dt;
  return static_cast<int>(p > 100 ? 100 : p);
}

long parseMeminfoKb(const std::string& text, const char* key) {
  std::istringstream in(text);
  const std::string want = std::string(key) + ":";
  for (std::string line; std::getline(in, line);) {
    std::istringstream f(line);
    std::string name;
    long kb = -1;
    if (f >> name >> kb && name == want) return kb;
  }
  return -1;
}

int parseWirelessDbm(const std::string& text) {
  std::istringstream in(text);
  for (std::string line; std::getline(in, line);) {
    size_t colon = line.find(':');
    if (colon == std::string::npos || line.find('|') != std::string::npos) continue;  // headers
    std::istringstream f(line.substr(colon + 1));
    std::string status, link, level;
    if (!(f >> status >> link >> level)) continue;
    return std::atoi(level.c_str());  // "-58." -> -58
  }
  return 0;
}

}  // namespace hal
