#pragma once
// The microphone, through SDL2 audio capture (PipeWire on the Cardputer Zero): 16 kHz mono
// samples, only while HAL Zero is told to listen. Opened the first time it is needed.
#include <SDL.h>

#include "hal_recording.h"

class Mic {
 public:
  bool start();                     // begin capturing; false if there is no microphone
  void pump(hal::Recording* into);  // move what has been captured into the recording
  void stop();
  void close();

 private:
  SDL_AudioDeviceID dev_ = 0;
  bool tried_ = false;
};
