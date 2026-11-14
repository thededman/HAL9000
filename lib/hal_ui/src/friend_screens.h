#pragma once
// The friends' screens (HAL Nano's and HAL Card's; HAL Paper has a page of his own): an eye,
// who is speaking, and the words. Original designs, drawn by the same code on the device and
// in the Mac's previews (hal9000 --shots DIR --layout nano).
#include <M5GFX.h>

#include <cstdint>
#include <string>

namespace friendui {

// Each HAL's colour wherever his name is shown: HAL Zero red and the desk HAL blue, as always;
// HAL Nano green, HAL Paper the grey of his page, HAL Card amber.
uint16_t castColour(int who);

struct View {
  int me = 2;            // whose screen this is (a cast number, hal_cast.h)
  int speaker = -1;      // who is speaking, or -1: nobody
  int line = 0;          // the number of the line being spoken
  std::string text;      // its words
  std::string status;    // with nobody speaking: one short line under his name
  int battery = -1;      // percent, or -1: not known
  bool charging = false;
  uint8_t level = 0;     // 0..255: how hard his own line is driving the eye (chirps, or a voice)
};

// Any size. Taller than wide (HAL Nano, 135x240): the eye above, the words below. Wider than
// tall (HAL Card, 240x135): the eye at the left, the words at the right.
void draw(M5Canvas& f, const View& v, uint32_t nowMs);

}  // namespace friendui
