// halLog on Linux/macOS: one line on stderr.
#include <cstdarg>
#include <cstdio>

#include "hal_log.h"

void halLog(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  std::vfprintf(stderr, fmt, ap);
  va_end(ap);
  std::fputc('\n', stderr);
}
