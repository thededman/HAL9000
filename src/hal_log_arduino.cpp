// halLog for the CoreS3: one line on the USB serial port.
#include <Arduino.h>
#include <stdarg.h>

#include "hal_log.h"

void halLog(const char* fmt, ...) {
  char buf[192];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  Serial.println(buf);
}
