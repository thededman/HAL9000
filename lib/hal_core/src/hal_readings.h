#pragma once
// Readings for the film-style screens (status cards, monitor wall): each subsystem
// as a three-letter code, a name, a formatted value, a 0..1 level and an alarm flag.
// The builders decide the wording and the alarm thresholds; each HAL feeds them
// its own real numbers.
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "hal_link.h"

namespace hal {

struct Reading {
  std::string code, name, value;  // "PWR", "POWER", "87 %"
  float level = -1;               // 0..1 for bars and traces; < 0 = none
  bool alarm = false;
  bool valid = false;             // false: shows N/A, never alarms
  std::vector<float> trace;       // recent levels, oldest first (kept by logReading)
};

// Copies a fresh reading into its slot and adds its level to the slot's trace
// (at most `cap` points; a failed read adds none).
void logReading(Reading& slot, const Reading& fresh, size_t cap = 40);

Reading noReading(const char* code, const char* name);  // doesn't apply right now: N/A
Reading powerReading(int pct, bool charging);      // PWR  alarm: under 20 % and not charging
Reading lightReading(int pct);                     // LUX
Reading proximityReading(int raw);                 // PRX  0..2047
Reading motionReading(float dps);                  // MOT  alarm: over 120 deg/s
Reading tiltReading(int deg);                      // TLT  alarm: 60 degrees or more
Reading tempReading(int c);                        // TMP  alarm: over 70 C
Reading netReading(int rssiDbm);                   // NET  0 = Wi-Fi off
Reading memoryReading(int freeKb, int totalKb);    // MEM  alarm: under 10 % free
Reading linkReading(bool enabled, bool connected); // LNK  alarm: on but no carrier
Reading cameraReading(bool present, int score);    // CAM
Reading audioReading(int level255, int lines);     // AUD
Reading clockReading(int secondsSinceMidnight);    // CLK
Reading uptimeReading(uint32_t seconds);           // UPT
Reading cpuReading(int pct);                       // CPU  alarm: over 95 %
Reading peerReading(PeerState state, const std::string& mode);  // DSK  alarm: not responding

}  // namespace hal
