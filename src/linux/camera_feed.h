#pragma once
// HAL Zero's camera: 160x120 frames, analysed with the same MotionGrid as the
// CoreS3. FakeCamera stands in on the Mac (press C and someone walks past);
// the Cardputer's real camera (rpicam-vid) plugs in behind the same interface.
#include <atomic>
#include <cstdint>
#include <cstdio>  // M5GFX's Linux build needs stdio before it
#include <mutex>
#include <thread>
#include <vector>

#include "hal_motiongrid.h"
#include "screens/screens.h"

class CameraFeed {
 public:
  static constexpr int kW = 160, kH = 120;
  virtual ~CameraFeed() = default;
  virtual bool begin() = 0;
  virtual bool poll(uint32_t nowMs) = 0;  // true when a new frame arrived
  virtual void close() {}
  virtual const char* name() const = 0;
  int score() const { return grid_.score(); }
  screens::CameraView view() const;

 protected:
  void analyse();                      // frame_ -> grey grid -> MotionGrid
  std::vector<uint16_t> frame_ = std::vector<uint16_t>(kW * kH);  // byte-swapped RGB565
  hal::MotionGrid grid_;
  bool have_ = false;
};

// The Cardputer Zero's IMX219 through rpicam-vid (320x240 YUV420 at 15 fps,
// read by a background thread, sampled down to 160x120).
class RpiCamera : public CameraFeed {
 public:
  ~RpiCamera() override { close(); }
  static bool available();  // rpicam-vid installed?
  bool begin() override;
  bool poll(uint32_t nowMs) override;
  void close() override;
  const char* name() const override;

 private:
  void reader();
  int pid_ = -1, fd_ = -1;
  std::thread thread_;
  std::mutex mu_;
  std::vector<uint8_t> latest_;  // one whole YUV420 frame
  bool fresh_ = false;
  std::atomic<bool> running_{false};
  uint32_t lastFrameMs_ = 0;
};

class FakeCamera : public CameraFeed {
 public:
  bool begin() override { return true; }
  bool poll(uint32_t nowMs) override;
  const char* name() const override { return "FAKE"; }
  void walkIn(uint32_t nowMs) { walkStart_ = nowMs; walking_ = true; }

 private:
  uint32_t lastMs_ = 0;
  uint32_t walkStart_ = 0;
  bool walking_ = false;
};
