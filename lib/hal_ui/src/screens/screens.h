#pragma once
// HAL's console screens. Each draws a full frame into the canvas, laid out for
// the device by `L` (positions are designed in CoreS3 coordinates, see hal_layout.h).
// tMs = time since the screen started, level = HAL's voice loudness (0..255).
#include <M5GFX.h>

#include <string>
#include <vector>

#include "../hal_layout.h"
#include "hal_link.h"

namespace screens {

// Palette: white, blue and red on black.
inline uint16_t white() { return lgfx::color565(232, 232, 232); }
inline uint16_t blue() { return lgfx::color565(58, 141, 255); }
inline uint16_t red() { return lgfx::color565(224, 32, 27); }
inline uint16_t dim() { return lgfx::color565(36, 36, 85); }  // survives RGB332

// What the camera sees, for the Visual Analysis screen.
struct CameraView {
  const uint16_t* frame = nullptr;  // 160x120, byte-swapped RGB565; nullptr = no signal
  bool hasBox = false;              // something moved: x, y, w, h in frame coordinates
  int bx = 0, by = 0, bw = 0, bh = 0;
  int score = 0;
};

// Which HAL shows a screen.
enum class Owner { Both, Desk, Zero };

// The desk HAL's own sensors, for the sensor array. -1 / -999 = not read.
struct DeskSensors {
  bool valid = false;
  int lightPct = -1, proximity = -1, tiltDeg = -1;
  float motionDps = 0;
  int batteryPct = -1;
  bool charging = false;
  int chipTempC = -999, rssi = 0, heapFree = 0, psramFree = 0;  // rssi 0 = Wi-Fi off
};

// The desk HAL's end of the link to HAL Zero, for the comms screen.
struct LinkStats {
  bool valid = false, enabled = false, connected = false;
  std::string peer, last;  // HAL Zero's address, the last message
  int msgsIn = 0, msgsOut = 0, rssi = 0;
};

// HAL Zero's own machine, for the system monitor and benchmark. Series are oldest
// first, at most 60 samples.
struct SysStats {
  bool valid = false;
  int cores = 0, maxMHz = 0;
  long memTotalKb = 0;
  std::vector<float> cpu[4], memUsedPct, tempC, rssiDbm;
};

// What HAL Zero knows about the desk HAL, for diagnostics and benchmark.
struct PeerView {
  hal::PeerState state = hal::PeerState::NotResponding;
  hal::PeerStatus status;
};

// Everything a screen may draw from. Each HAL fills in what it has.
struct ScreenData {
  CameraView cam;
  uint8_t voiceLevel = 0;
  uint32_t lineMs = 0;            // length of the line HAL is saying (0 = unknown)
  float daisyProgress = -1;       // >= 0 only during Daisy Bell
  uint32_t seed = 0;              // picked each time a screen opens (which chess game, ...)
  int secondsSinceMidnight = -1;  // local time
  DeskSensors sensors;
  LinkStats link;
  SysStats sys;
  PeerView peer;
};

// Title bar across the top and a status line along the bottom.
void frame(M5Canvas& c, const hal::Layout& L, const char* title, uint32_t tMs);

void vitals(M5Canvas& c, const hal::Layout& L, uint32_t tMs, uint8_t level);
void readout(M5Canvas& c, const hal::Layout& L, uint32_t tMs, uint8_t level);
void trajectory(M5Canvas& c, const hal::Layout& L, uint32_t tMs, uint8_t level);
void analyser(M5Canvas& c, const hal::Layout& L, uint32_t tMs, uint8_t level);
void globe(M5Canvas& c, const hal::Layout& L, uint32_t tMs, uint8_t level);
void visual(M5Canvas& c, const hal::Layout& L, uint32_t tMs, uint8_t level, const CameraView& cam);

// The desk HAL's own screens.
void sensors(M5Canvas& c, const hal::Layout& L, uint32_t tMs, const ScreenData& d);
void chess(M5Canvas& c, const hal::Layout& L, uint32_t tMs, const ScreenData& d);
void memory(M5Canvas& c, const hal::Layout& L, uint32_t tMs, const ScreenData& d);  // also Daisy Bell
void comms(M5Canvas& c, const hal::Layout& L, uint32_t tMs, const ScreenData& d);

}  // namespace screens
