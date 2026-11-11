#pragma once
// Plays HAL's WAV clips from the SD card through the CoreS3 speaker and
// reports how loud he is right now (for the eye glow).
#include <Arduino.h>

#include <vector>

#include "hal_wav.h"

class Audio {
 public:
  // Reads the whole file into PSRAM, then plays it. False if it can't.
  bool playFile(const char* path);
  void stop();
  bool isPlaying() const;
  uint8_t level() const;  // 0..255, follows the voice
  void setVolume(uint8_t v);
  uint32_t durationMs() const { return info_.durationMs; }  // of the clip last started

 private:
  void release();
  uint8_t* buf_ = nullptr;
  hal::WavInfo info_;
  std::vector<uint8_t> env_;
  uint32_t startMs_ = 0;
  uint8_t volume_ = 0;
};
