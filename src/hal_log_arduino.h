#pragma once
// Call once, straight after Serial.begin(): logging must never hold a HAL up (hal_log_arduino.cpp).
void halLogNeverWaits();
