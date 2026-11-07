#include "debug_shot.h"

#include <M5Unified.h>

int serviceDebugCommands() {
  while (Serial.available()) {
    int ch = Serial.read();
    if (ch != 'S') return ch;
    auto& d = M5.Display;
    const int w = d.width(), h = d.height();
    static uint16_t row[320];
    Serial.printf("SHOT %d %d\n", w, h);
    for (int y = 0; y < h; ++y) {
      d.readRect(0, y, w, 1, row);  // RGB565, byte-swapped (big-endian) like the panel
      Serial.write(reinterpret_cast<const uint8_t*>(row), w * 2);
    }
    Serial.flush();
  }
  return 0;
}
