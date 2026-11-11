#include "hal_readings.h"

#include <cctype>
#include <cstdio>

namespace hal {
namespace {

float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

Reading make(const char* code, const char* name) {
  Reading r;
  r.code = code;
  r.name = name;
  r.value = "N/A";
  return r;
}

Reading ok(Reading r, const std::string& value, float level, bool alarm = false) {
  r.value = value;
  r.level = level;
  r.alarm = alarm;
  r.valid = true;
  return r;
}

std::string fmt(const char* f, int a) {
  char b[32];
  std::snprintf(b, sizeof b, f, a);
  return b;
}

}  // namespace

void logReading(Reading& slot, const Reading& fresh, size_t cap) {
  slot.code = fresh.code;
  slot.name = fresh.name;
  slot.value = fresh.value;
  slot.level = fresh.level;
  slot.alarm = fresh.alarm;
  slot.valid = fresh.valid;
  if (!fresh.valid || fresh.level < 0) return;
  slot.trace.push_back(fresh.level);
  if (slot.trace.size() > cap) slot.trace.erase(slot.trace.begin(), slot.trace.end() - static_cast<long>(cap));
}

Reading noReading(const char* code, const char* name) { return make(code, name); }

Reading powerReading(int pct, bool charging) {
  Reading r = make("PWR", "POWER");
  if (pct < 0) return r;
  return ok(r, fmt(charging ? "%d %% CHG" : "%d %%", pct), clamp01(pct / 100.0f), pct < 20 && !charging);
}

Reading lightReading(int pct) {
  Reading r = make("LUX", "LIGHT");
  if (pct < 0) return r;
  return ok(r, fmt("%d %%", pct), clamp01(pct / 100.0f));
}

Reading proximityReading(int raw) {
  Reading r = make("PRX", "PROXIMITY");
  if (raw < 0) return r;
  return ok(r, fmt("%d", raw), clamp01(raw / 1000.0f));
}

Reading motionReading(float dps) {
  Reading r = make("MOT", "MOTION");
  if (dps < 0) dps = -dps;
  return ok(r, fmt("%d DEG/S", static_cast<int>(dps + 0.5f)), clamp01(dps / 250.0f), dps > 120.0f);
}

Reading tiltReading(int deg) {
  Reading r = make("TLT", "TILT");
  if (deg < 0) return r;
  return ok(r, fmt("%d DEG", deg), clamp01(deg / 90.0f), deg >= 60);
}

Reading tempReading(int c) {
  Reading r = make("TMP", "TEMPERATURE");
  if (c <= -999) return r;
  return ok(r, fmt("%d C", c), clamp01((c - 20) / 60.0f), c > 70);
}

Reading netReading(int rssiDbm) {
  Reading r = make("NET", "NETWORK");
  if (rssiDbm == 0) return ok(r, "OFF", 0);
  return ok(r, fmt("%d DBM", rssiDbm), clamp01((rssiDbm + 100) / 70.0f));
}

Reading memoryReading(int freeKb, int totalKb) {
  Reading r = make("MEM", "MEMORY");
  if (totalKb <= 0 || freeKb < 0) return r;
  std::string v = freeKb < 1024 ? fmt("%d KB", freeKb) : fmt("%d MB", freeKb / 1024);
  return ok(r, v, clamp01(1.0f - float(freeKb) / float(totalKb)), freeKb * 10 < totalKb);
}

Reading linkReading(bool enabled, bool connected) {
  Reading r = make("LNK", "LINK");
  if (!enabled) return ok(r, "OFF", 0);
  if (!connected) return ok(r, "NO CARRIER", 0, true);
  return ok(r, "CONNECTED", 1);
}

Reading cameraReading(bool present, int score) {
  Reading r = make("CAM", "CAMERA");
  if (!present) return ok(r, "OFFLINE", 0);
  return ok(r, fmt("MOTION %d", score), clamp01(score / 100.0f));
}

Reading audioReading(int level255, int lines) {
  Reading r = make("AUD", "AUDIO");
  return ok(r, fmt(lines == 1 ? "%d LINE" : "%d LINES", lines), clamp01(level255 / 255.0f));
}

Reading clockReading(int s) {
  Reading r = make("CLK", "CLOCK");
  if (s < 0) return r;
  s %= 86400;
  char b[8];
  std::snprintf(b, sizeof b, "%02d:%02d", s / 3600, s / 60 % 60);
  return ok(r, b, s / 86400.0f);
}

Reading uptimeReading(uint32_t s) {
  Reading r = make("UPT", "UPTIME");
  char b[24];
  if (s >= 86400) std::snprintf(b, sizeof b, "%uD %02u:%02u", unsigned(s / 86400), unsigned(s / 3600 % 24), unsigned(s / 60 % 60));
  else std::snprintf(b, sizeof b, "%02u:%02u:%02u", unsigned(s / 3600), unsigned(s / 60 % 60), unsigned(s % 60));
  return ok(r, b, clamp01(s / 86400.0f));
}

Reading cpuReading(int pct) {
  Reading r = make("CPU", "PROCESSOR");
  if (pct < 0) return r;
  return ok(r, fmt("%d %%", pct), clamp01(pct / 100.0f), pct > 95);
}

Reading peerReading(PeerState state, const std::string& mode) {
  Reading r = make("DSK", "DESK UNIT");
  switch (state) {
    case PeerState::NotResponding: return ok(r, "NOT RESPONDING", 0, true);
    case PeerState::TooOld: return ok(r, "TOO OLD", 0);
    case PeerState::Waiting: return ok(r, "WAITING", 0);
    case PeerState::Ok: break;
  }
  std::string m = mode;
  for (char& ch : m) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
  return ok(r, m.empty() ? "OK" : m, 1);
}

}  // namespace hal
