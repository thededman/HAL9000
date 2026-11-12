#pragma once
// The Cardputer Zero's own screen and keyboard, the way M5Stack's launcher uses
// them: frames go straight into the LCD framebuffer (/dev/fb_lcd), keys come
// from the keyboard's input device (grabbed while HAL Zero runs).
#include <M5GFX.h>

#include <string>

#include "sdl_io.h"  // KeyInput

class FbIo {
 public:
  static bool available();  // is there a Cardputer LCD here?
  bool open();
  void present(M5Canvas& frame);
  void poll(KeyInput* keys);
  void close();

 private:
  int fbFd_ = -1, keyFd_ = -1;
  uint8_t* fb_ = nullptr;
  size_t fbSize_ = 0;
  int xres_ = 0, yres_ = 0, bpp_ = 0, stride_ = 0;
  bool space_ = false, shift_ = false;
};
