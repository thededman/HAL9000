#pragma once
// Camera motion analysis shared by the CoreS3 and the Cardputer: an 80x60 grey
// grid compared frame to frame -> a motion score and a box around what moved.
#include <cstdint>

namespace hal {

// Byte-swapped RGB565 (as cameras and M5GFX sprites store it) -> grey 0..250.
uint8_t greyFromSwapped565(uint16_t swapped);

class MotionGrid {
 public:
  static constexpr int kW = 80, kH = 60;  // grid cells (a 160x120 frame sampled every 2 px)
  static constexpr int kCellChange = 25;  // grey-level change that counts a cell as moving
  static constexpr int kMinBoxCells = 12;

  // `grey` holds kW * kH values, row by row.
  void update(const uint8_t* grey);
  int score() const { return score_; }  // hal::motionScore of the moving cells
  // What moved, in 160x120 frame coordinates; false when fewer than kMinBoxCells moved.
  bool box(int* x, int* y, int* w, int* h) const;
  bool havePrevious() const { return havePrev_; }

 private:
  uint8_t prev_[kW * kH] = {};
  bool havePrev_ = false;
  int score_ = 0;
  bool haveBox_ = false;
  int boxX_ = 0, boxY_ = 0, boxW_ = 0, boxH_ = 0;
};

}  // namespace hal
