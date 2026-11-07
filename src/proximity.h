#pragma once
// Minimal driver for the CoreS3's LTR-553 proximity sensor (I2C 0x23).
// M5Unified doesn't cover it.

bool proximityBegin();  // false if the sensor doesn't answer
int proximityRead();    // 0..2047, bigger = closer; -1 on error
