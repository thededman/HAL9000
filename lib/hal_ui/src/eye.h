#pragma once
// HAL's eye. The ring (and on the CoreS3 the nameplate) is drawn once; the lens
// is redrawn every frame into a small square sprite so it can breathe and follow
// his voice. Geometry comes from the device's Layout.
#include <M5GFX.h>

#include "hal_brain.h"
#include "hal_layout.h"
#include "hal_storage.h"

struct EyeParams {
  float glow = 0.8f;        // 0..1, how hot the lens is
  float aperture = 1.0f;    // 0..1, lens radius as a fraction of full
  float brightness = 1.0f;  // 0..1, overall (fades to 0 during Daisy Bell)
};

// Ring, bezel, banner/nameplate (if the layout has one) and the no-memory line.
// `storage` may be null (no banner). Draws on a display or a full-screen sprite.
void drawEyeBackground(lgfx::LovyanGFX& d, const hal::Layout& L, const hal::Storage* storage, bool noMemory);

// The lens, into a 16-bit sprite of L.lensSprite x L.lensSprite pixels.
void drawEye(M5Canvas& lens, const hal::Layout& L, const EyeParams& p);
uint16_t eyeTransparentKey();  // pass to pushSprite: corners show the ring
inline int lensX(const hal::Layout& L) { return L.eyeCx - L.lensSprite / 2; }
inline int lensY(const hal::Layout& L) { return L.eyeCy - L.lensSprite / 2; }

EyeParams eyeParamsFor(hal::Mode m, bool quiet, float daisyProgress, uint8_t voiceLevel, uint32_t nowMs);
