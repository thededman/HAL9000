#pragma once
// HAL Zero's own machine for the system monitor and benchmark: CPU per core,
// memory, temperature and Wi-Fi from Linux, sampled every 500 ms into 60-point
// histories. On the Mac the files don't exist and stats() stays invalid.
#include <vector>

#include "hal_procfs.h"
#include "screens/screens.h"

class SysMon {
 public:
  void sample();
  const screens::SysStats& stats() const { return stats_; }

 private:
  screens::SysStats stats_;
  std::vector<hal::CpuTimes> lastCpu_;
  bool fixedRead_ = false;  // cores, max MHz, total memory: read once
};
