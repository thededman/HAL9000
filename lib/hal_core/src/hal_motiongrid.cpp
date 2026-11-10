#include "hal_motiongrid.h"

#include <cstdlib>

#include "hal_motion.h"

namespace hal {

uint8_t greyFromSwapped565(uint16_t swapped) {
  uint16_t v = static_cast<uint16_t>((swapped >> 8) | (swapped << 8));
  int r = (v >> 11) & 31, g = (v >> 5) & 63, b = v & 31;
  return static_cast<uint8_t>((r * 8 * 2 + g * 4 * 4 + b * 8) / 7);
}

void MotionGrid::update(const uint8_t* grey) {
  int moving = 0, minX = kW, minY = kH, maxX = -1, maxY = -1;
  for (int y = 0; y < kH; ++y) {
    for (int x = 0; x < kW; ++x) {
      int i = y * kW + x;
      int d = havePrev_ ? std::abs(int(grey[i]) - int(prev_[i])) : 0;
      if (d > kCellChange) {
        ++moving;
        if (x < minX) minX = x;
        if (y < minY) minY = y;
        if (x > maxX) maxX = x;
        if (y > maxY) maxY = y;
      }
      prev_[i] = grey[i];
    }
  }
  score_ = havePrev_ ? motionScore(moving, kW * kH) : 0;
  havePrev_ = true;
  haveBox_ = moving >= kMinBoxCells;
  if (haveBox_) {
    boxX_ = minX * 2;
    boxY_ = minY * 2;
    boxW_ = (maxX - minX + 1) * 2;
    boxH_ = (maxY - minY + 1) * 2;
  }
}

bool MotionGrid::box(int* x, int* y, int* w, int* h) const {
  if (!haveBox_) return false;
  *x = boxX_;
  *y = boxY_;
  *w = boxW_;
  *h = boxH_;
  return true;
}

}  // namespace hal
