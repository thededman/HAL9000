#pragma once
// HAL's console screens. Each draws a full 320x240 frame into the canvas.
// tMs = time since the screen started, level = HAL's voice loudness (0..255).
#include <M5Unified.h>

namespace screens {

// Palette: white, blue and red on black.
inline uint16_t white() { return lgfx::color565(232, 232, 232); }
inline uint16_t blue() { return lgfx::color565(58, 141, 255); }
inline uint16_t red() { return lgfx::color565(224, 32, 27); }
inline uint16_t dim() { return lgfx::color565(36, 36, 85); }  // survives RGB332

// Title bar across the top and a status line along the bottom.
void frame(M5Canvas& c, const char* title, uint32_t tMs);

void vitals(M5Canvas& c, uint32_t tMs, uint8_t level);
void readout(M5Canvas& c, uint32_t tMs, uint8_t level);
void trajectory(M5Canvas& c, uint32_t tMs, uint8_t level);
void analyser(M5Canvas& c, uint32_t tMs, uint8_t level);
void globe(M5Canvas& c, uint32_t tMs, uint8_t level);
void visual(M5Canvas& c, uint32_t tMs, uint8_t level);  // needs the camera

}  // namespace screens
