#pragma once
// Picks and draws HAL's console screens: five built-in ones plus any JPGs in
// /hal/screens/, shuffled without repeats.
#include <M5Unified.h>

#include <string>
#include <vector>

#include "hal_shuffle.h"

class Consoles {
 public:
  Consoles() : bag_(1) {}
  void begin(uint32_t seed, bool withCamera);  // lists /hal/screens/*.jpg
  bool next();  // moves to another screen; true if it's Visual Analysis
  // Draws the current screen into the canvas and returns true if it should be pushed.
  // Image screens go straight to the display at full colour, once, and return false.
  bool draw(M5Canvas& c, uint32_t tMs, uint8_t voiceLevel);
  int count() const { return static_cast<int>(bag_.size()); }

 private:
  hal::ShuffleBag bag_;
  std::vector<std::string> jpgs_;
  int current_ = 0;
  bool withCamera_ = false;
  bool jpgDrawn_ = false;
};
