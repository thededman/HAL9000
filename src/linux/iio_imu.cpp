#include "iio_imu.h"

#include <dirent.h>

#include <cstdio>
#include <cstring>

#include "hal_log.h"

namespace {
bool readNumber(const std::string& path, double* out) {
  FILE* f = std::fopen(path.c_str(), "r");
  if (!f) return false;
  bool ok = std::fscanf(f, "%lf", out) == 1;
  std::fclose(f);
  return ok;
}
}  // namespace

bool IioImu::begin() {
  DIR* d = opendir("/sys/bus/iio/devices");
  if (!d) return false;
  while (dirent* e = readdir(d)) {
    std::string dir = std::string("/sys/bus/iio/devices/") + e->d_name;
    char name[32] = {};
    FILE* f = std::fopen((dir + "/name").c_str(), "r");
    if (!f) continue;
    if (std::fgets(name, sizeof name, f) && std::strncmp(name, "bmi270", 6) == 0) dir_ = dir;
    std::fclose(f);
  }
  closedir(d);
  if (dir_.empty() || !readNumber(dir_ + "/in_accel_scale", &accelScale_) ||
      !readNumber(dir_ + "/in_anglvel_scale", &gyroScale_)) {
    dir_.clear();
    return false;
  }
  halLog("motion sensor: bmi270 at %s", dir_.c_str());
  return true;
}

bool IioImu::read(float a[3], float g[3]) {
  if (dir_.empty()) return false;
  static const char* const ax[] = {"x", "y", "z"};
  for (int i = 0; i < 3; ++i) {
    double ra, rg;
    if (!readNumber(dir_ + "/in_accel_" + ax[i] + "_raw", &ra) || !readNumber(dir_ + "/in_anglvel_" + ax[i] + "_raw", &rg))
      return false;
    a[i] = float(ra * accelScale_ / 9.80665);   // m/s^2 -> g
    g[i] = float(rg * gyroScale_ * 57.29578);   // rad/s -> deg/s
  }
  return true;
}
