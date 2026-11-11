#include "camera_feed.h"

#include <M5GFX.h>

namespace {
uint16_t swapped(uint8_t r, uint8_t g, uint8_t b) { return lgfx::swap565(r, g, b); }
}  // namespace

void CameraFeed::analyse() {
  static uint8_t grey[hal::MotionGrid::kW * hal::MotionGrid::kH];
  for (int y = 0; y < hal::MotionGrid::kH; ++y)
    for (int x = 0; x < hal::MotionGrid::kW; ++x)
      grey[y * hal::MotionGrid::kW + x] = hal::greyFromSwapped565(frame_[(y * 2) * kW + x * 2]);
  grid_.update(grey);
  have_ = true;
}

screens::CameraView CameraFeed::view() const {
  screens::CameraView v;
  if (!have_) return v;
  v.frame = frame_.data();
  v.hasBox = grid_.box(&v.bx, &v.by, &v.bw, &v.bh);
  v.score = grid_.score();
  return v;
}

bool FakeCamera::poll(uint32_t nowMs) {
  if (nowMs - lastMs_ < 66) return false;
  lastMs_ = nowMs;
  // A dim room: a lamp-lit wall, a window, a shelf.
  for (int y = 0; y < kH; ++y)
    for (int x = 0; x < kW; ++x) {
      uint8_t v = uint8_t(40 + y / 3 + x / 8);
      frame_[y * kW + x] = swapped(v, uint8_t(v * 0.9f), uint8_t(v * 0.7f));
    }
  for (int y = 18; y < 52; ++y)
    for (int x = 104; x < 146; ++x) frame_[y * kW + x] = swapped(150, 170, 200);  // window
  for (int x = 10; x < 70; ++x) frame_[78 * kW + x] = swapped(90, 60, 40);       // shelf
  // Someone walking past, for 2.5 s after C.
  if (walking_) {
    uint32_t t = nowMs - walkStart_;
    if (t > 2500) {
      walking_ = false;
    } else {
      int cx = int(-20 + t * 200 / 2500);
      for (int y = 30; y < 120; ++y)
        for (int x = cx - 12; x < cx + 12; ++x)
          if (x >= 0 && x < kW) frame_[y * kW + x] = swapped(200, 150, 120);
      for (int y = 14; y < 34; ++y)  // head
        for (int x = cx - 8; x < cx + 8; ++x)
          if (x >= 0 && x < kW) frame_[y * kW + x] = swapped(230, 190, 160);
    }
  }
  analyse();
  return true;
}
