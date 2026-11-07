#include "proximity.h"

#include <M5Unified.h>

namespace {
constexpr uint8_t kAddr = 0x23;
constexpr uint32_t kFreq = 400000;
constexpr uint8_t kPsContr = 0x81, kPsLed = 0x82, kPsPulses = 0x83, kPsRate = 0x84;
constexpr uint8_t kPartId = 0x86, kPsData0 = 0x8D;
}  // namespace

bool proximityBegin() {
  uint8_t id = M5.In_I2C.readRegister8(kAddr, kPartId, kFreq);
  Serial.printf("  LTR-553 part id 0x%02X\n", id);
  if (id == 0x00 || id == 0xFF) return false;
  bool ok = M5.In_I2C.writeRegister8(kAddr, kPsLed, 0x7F, kFreq)  // 60 kHz, 100% duty, 100 mA
            && M5.In_I2C.writeRegister8(kAddr, kPsPulses, 0x08, kFreq)
            && M5.In_I2C.writeRegister8(kAddr, kPsRate, 0x00, kFreq)    // every 50 ms
            && M5.In_I2C.writeRegister8(kAddr, kPsContr, 0x03, kFreq);  // proximity: active
  return ok;
}

int proximityRead() {
  uint8_t b[2];
  if (!M5.In_I2C.readRegister(kAddr, kPsData0, b, 2, kFreq)) return -1;
  return b[0] | ((b[1] & 0x07) << 8);
}
