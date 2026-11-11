#pragma once
// Screen geometry for each HAL. The console screens are designed in CoreS3
// coordinates (320x240, content band y 18..224); Y()/H() map those into this
// layout's content band, exactly (scale 1) on the CoreS3.
#include <M5GFX.h>

namespace hal {

struct Rect {
  int x, y, w, h;
};

struct Layout {
  const char* name;  // shown in the console title bar
  int w, h;
  // Console screens
  int headerH;      // title bar
  int footerLineY;  // rule above the status line
  int footerTextY;  // status line text (bottom datum)
  int contentTop;   // band between title bar and footer...
  int contentH;     // ...that CoreS3 y 18..224 maps onto
  bool small;       // use the 8 px font where the CoreS3 uses the 16 px one
  Rect visual;      // where the 160x120 camera frame sits
  // Eye
  int eyeCx, eyeCy, ringR, bezelR, lensR, lensSprite;
  bool nameplate;  // room for your banner image (/hal/banner.png) above the eye
  int noMemoryY;   // "I can't find my memory" (bottom datum)

  int Y(int coreY) const { return contentTop + (coreY - 18) * contentH / 206; }
  int H(int coreH) const { return coreH * contentH / 206; }
  const lgfx::IFont* font2() const {
    return small ? static_cast<const lgfx::IFont*>(&fonts::Font0) : static_cast<const lgfx::IFont*>(&fonts::Font2);
  }
};

// The CoreS3 desk HAL: exactly the v0.2 pixels.
inline const Layout kCoreS3 = {"HAL 9000", 320, 240, 18, 224, 238, 18, 206, false, {80, 52, 160, 120},
                               160, 130, 92, 84, 74, 152, true, 239};

// HAL Zero on the Cardputer Zero: 320x170, eye between HUD panels.
inline const Layout kCardputer = {"HAL ZERO", 320, 170, 12, 157, 169, 12, 145, true, {80, 27, 160, 120},
                                  160, 84, 70, 64, 56, 116, false, 168};

}  // namespace hal
