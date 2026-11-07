#pragma once
// HAL's eye. The nameplate and metal ring are drawn once to the screen; the lens
// is redrawn every frame into a small sprite so it can breathe and follow his voice.
#include <M5Unified.h>

#include "hal_brain.h"

struct EyeParams {
  float glow = 0.8f;        // 0..1, how hot the lens is
  float aperture = 1.0f;    // 0..1, lens radius as a fraction of full
  float brightness = 1.0f;  // 0..1, overall (fades to 0 during Daisy Bell)
  bool noMemory = false;    // show "I can't find my memory, Dave."
};

constexpr int kEyeCx = 160;
constexpr int kEyeCy = 130;
constexpr int kLensR = 74;
constexpr int kLensSprite = 152;  // square sprite holding the lens

void drawEyeBackground(M5GFX& d, bool noMemory);
void drawEye(M5Canvas& lens, const EyeParams& p);  // lens is kLensSprite x kLensSprite, 16-bit
uint16_t eyeTransparentKey();                     // pass to pushSprite: corners show the ring
EyeParams eyeParamsFor(hal::Mode m, bool quiet, float daisyProgress, uint8_t voiceLevel, uint32_t nowMs);
