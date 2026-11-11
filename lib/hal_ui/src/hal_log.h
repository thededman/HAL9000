#pragma once
// One log line (printf-style, newline added). Each device defines it once:
// Serial on the CoreS3, stderr on Linux.
void halLog(const char* fmt, ...);
