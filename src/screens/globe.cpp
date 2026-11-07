// Wireframe globe, slowly turning, with a blinking marker over home.
#include <math.h>

#include "screens.h"

namespace screens {
namespace {

constexpr float kPi = 3.14159265f;

struct P3 {
  float x, y, z;
};

// Latitude/longitude on a unit sphere, spun by `spin`, tilted toward the viewer.
P3 project(float lat, float lon, float spin) {
  float x = cosf(lat) * sinf(lon + spin), y = sinf(lat), z = cosf(lat) * cosf(lon + spin);
  const float tilt = 0.35f;
  return {x, y * cosf(tilt) - z * sinf(tilt), y * sinf(tilt) + z * cosf(tilt)};
}

}  // namespace

void globe(M5Canvas& c, uint32_t tMs, uint8_t level) {
  frame(c, "SURVEILLANCE", tMs);
  const int cx = 160, cy = 122, r = 88;
  float spin = tMs * 0.0004f;
  c.drawCircle(cx, cy, r, blue());

  // Parallels and meridians, front side only.
  for (int i = 1; i < 8; ++i) {
    float lat = -kPi / 2 + i * kPi / 8;
    for (int s = 0; s < 64; ++s) {
      P3 a = project(lat, s * 2 * kPi / 64, spin), b = project(lat, (s + 1) * 2 * kPi / 64, spin);
      if (a.z > 0 && b.z > 0) c.drawLine(cx + a.x * r, cy - a.y * r, cx + b.x * r, cy - b.y * r, blue());
    }
  }
  for (int m = 0; m < 12; ++m) {
    float lon = m * 2 * kPi / 12;
    for (int s = 0; s < 32; ++s) {
      P3 a = project(-kPi / 2 + s * kPi / 32, lon, spin), b = project(-kPi / 2 + (s + 1) * kPi / 32, lon, spin);
      if (a.z > 0 && b.z > 0) c.drawLine(cx + a.x * r, cy - a.y * r, cx + b.x * r, cy - b.y * r, dim());
    }
  }

  // Home marker (roughly the US north-east).
  P3 h = project(41.3f * kPi / 180, -72.9f * kPi / 180, spin);
  c.setTextColor(white());
  if (h.z > 0) {
    int x = cx + h.x * r, y = cy - h.y * r;
    bool on = (tMs / 300) % 2;
    c.drawCircle(x, y, on ? 7 : 4, red());
    c.fillCircle(x, y, 2, red());
    c.setTextDatum(middle_left);
    c.drawString("HOME", x + 10, y, &fonts::Font0);
  }

  c.setTextDatum(top_left);
  c.setTextColor(blue());
  c.drawString("SUBJECT: DAVE", 8, 26, &fonts::Font0);
  c.drawString("STATUS: AT HOME", 8, 36, &fonts::Font0);
  c.setTextDatum(top_right);
  c.drawString(level ? "UPLINK: ACTIVE" : "UPLINK: STANDBY", 312, 26, &fonts::Font0);
  c.drawString("ESCAPE ROUTES: 0", 312, 36, &fonts::Font0);
}

}  // namespace screens
