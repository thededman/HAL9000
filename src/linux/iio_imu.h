#pragma once
// The Cardputer Zero's BMI270 through Linux IIO (/sys/bus/iio/devices/*/name = bmi270):
// acceleration in g and rotation in deg/s, as HAL's motion code expects.
#include <string>

class IioImu {
 public:
  bool begin();  // false if there's no BMI270
  bool read(float accelG[3], float gyroDps[3]);

 private:
  std::string dir_;
  double accelScale_ = 0, gyroScale_ = 0;
};
