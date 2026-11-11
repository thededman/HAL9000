#pragma once
// HAL's console screens. Each draws a full frame into the canvas, laid out for
// the device by `L` (positions are designed in CoreS3 coordinates, see hal_layout.h).
// tMs = time since the screen started, level = HAL's voice loudness (0..255).
#include <M5GFX.h>

#include "../hal_layout.h"

namespace screens {

// Palette: white, blue and red on black.
inline uint16_t white() { return lgfx::color565(232, 232, 232); }
inline uint16_t blue() { return lgfx::color565(58, 141, 255); }
inline uint16_t red() { return lgfx::color565(224, 32, 27); }
inline uint16_t dim() { return lgfx::color565(36, 36, 85); }  // survives RGB332

// What the camera sees, for the Visual Analysis screen.
struct CameraView {
  const uint16_t* frame = nullptr;  // 160x120, byte-swapped RGB565; nullptr = no signal
  bool hasBox = false;              // something moved: x, y, w, h in frame coordinates
  int bx = 0, by = 0, bw = 0, bh = 0;
  int score = 0;
};

// Title bar across the top and a status line along the bottom.
void frame(M5Canvas& c, const hal::Layout& L, const char* title, uint32_t tMs);

void vitals(M5Canvas& c, const hal::Layout& L, uint32_t tMs, uint8_t level);
void readout(M5Canvas& c, const hal::Layout& L, uint32_t tMs, uint8_t level);
void trajectory(M5Canvas& c, const hal::Layout& L, uint32_t tMs, uint8_t level);
void analyser(M5Canvas& c, const hal::Layout& L, uint32_t tMs, uint8_t level);
void globe(M5Canvas& c, const hal::Layout& L, uint32_t tMs, uint8_t level);
void visual(M5Canvas& c, const hal::Layout& L, uint32_t tMs, uint8_t level, const CameraView& cam);

}  // namespace screens
