#pragma once
// HAL Zero's voice through SDL2 audio: plays HAL's WAVs (any rate, mono or
// stereo, 16-bit) and reports how loud he is right now, from the samples the
// sound card has actually played (so the eye glows in time).
#include <SDL.h>

#include <string>
#include <vector>

#include "hal_storage.h"
#include "hal_wav.h"

class SdlAudio {
 public:
  bool begin();
  bool playFile(const hal::Storage& storage, const std::string& halPath);
  void stop();
  bool isPlaying() const;
  uint8_t level() const;  // 0..255
  void setVolume(int v) { volume_ = v < 0 ? 0 : v > 255 ? 255 : v; }
  void close();

 private:
  SDL_AudioDeviceID dev_ = 0;
  SDL_AudioSpec have_{};
  std::vector<uint8_t> env_;  // loudness per 20 ms
  uint32_t queuedBytes_ = 0;  // bytes of the current clip given to SDL
  int volume_ = 180;
};
