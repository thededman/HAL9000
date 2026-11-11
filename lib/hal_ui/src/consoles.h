#pragma once
// Picks and draws HAL's console screens: five built-in ones, Visual Analysis when
// there's a camera, plus any JPGs in /hal/screens/, shuffled without repeats.
#include <M5GFX.h>

#include <string>
#include <vector>

#include "hal_layout.h"
#include "hal_shuffle.h"
#include "hal_storage.h"
#include "screens/screens.h"

class Consoles {
 public:
  Consoles() : bag_(1) {}
  // Lists /hal/screens/*.jpg from `storage` (may be null: built-in screens only).
  void begin(uint32_t seed, bool withCamera, const hal::Storage* storage);
  bool next();  // moves to another screen; true if it's Visual Analysis
  // Draws the current screen into the canvas and returns true if it should be pushed.
  // Image screens go straight to `display` at full colour, once, and return false.
  bool draw(M5Canvas& c, lgfx::LovyanGFX& display, const hal::Layout& L, uint32_t tMs, uint8_t voiceLevel,
            const screens::CameraView& cam);
  int count() const { return static_cast<int>(bag_.size()); }

 private:
  hal::ShuffleBag bag_;
  const hal::Storage* storage_ = nullptr;
  std::vector<std::string> jpgs_;
  int current_ = 0;
  bool withCamera_ = false;
  bool jpgDrawn_ = false;
};
