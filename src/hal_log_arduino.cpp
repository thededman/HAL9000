// halLog for the HALs on ESP32 boards: one line on the USB serial port.
#include <Arduino.h>
#include <stdarg.h>

#include "hal_log.h"

// The CoreS3 and the Cardputer log through the chip's own USB port. With a cable in and nobody
// reading (a hub, a charger with data lines, a computer with no monitor open), that port makes
// every line wait once its small buffer is full: a tenth of a second and more, again and again.
// A HAL that logs a few long lines then stops answering HAL Zero, who gives him up for dead
// (seen on HAL Card, 2026-11-18). So the port is told never to wait: with nobody reading, old
// lines make way for new ones, and a HAL never stalls for the sake of his log.
void halLogNeverWaits() {
#if ARDUINO_USB_CDC_ON_BOOT && ARDUINO_USB_MODE
  Serial.setTxTimeoutMs(0);
#endif
}

void halLog(const char* fmt, ...) {
  char buf[192];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  Serial.println(buf);
}
