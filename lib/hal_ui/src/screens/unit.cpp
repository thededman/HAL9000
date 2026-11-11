// Unit diagnostic: a wireframe of the device itself, turned by the rotation from
// its home pose to where gravity points now, with callouts to its parts.
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "hal_wire.h"
#include "screens.h"

namespace screens {
namespace {

using hal::Vec3;

struct Anchor {
  const char* code;
  Vec3 at;
};

// Half-extents of the slab, where its screen and lens sit on the front face, and
// where each part is.
struct Model {
  float hx, hy, hz;
  float sx0, sy0, sx1, sy1;  // screen rectangle on the front face
  float lx, ly, lr;          // lens circle on the front face
  bool keyboard;             // a key grid below the screen
  const Anchor* anchors;
  int anchorCount;
};

const Anchor kCoreAnchors[] = {{"CAM", {0, 0.9f, 0.3f}},  {"SPK", {0, -0.9f, 0.3f}}, {"IMU", {0.5f, -0.3f, 0}},
                               {"PRX", {0.35f, 0.9f, 0.3f}}, {"SD", {-1.0f, -0.6f, 0}}, {"NET", {0.9f, 0.6f, 0}},
                               {"LNK", {-0.9f, 0.6f, 0}}};
const Anchor kCardAnchors[] = {{"CAM", {-1.0f, 0.45f, 0.15f}}, {"SPK", {1.2f, -0.8f, 0}}, {"IMU", {0, 0.1f, 0}},
                               {"KBD", {0, -0.4f, 0.15f}},     {"NET", {1.3f, 0.6f, 0}},  {"LNK", {-1.3f, 0.7f, 0}}};
const Model kCore = {1.0f, 1.0f, 0.3f, -0.8f, -0.55f, 0.8f, 0.75f, 0, 0.1f, 0.42f, false, kCoreAnchors, 7};
const Model kCard = {1.4f, 0.9f, 0.15f, -0.5f, 0.12f, 1.25f, 0.75f, -0.95f, 0.45f, 0.24f, true, kCardAnchors, 6};

}  // namespace

void unit(M5Canvas& c, const hal::Layout& L, uint32_t tMs, const ScreenData& d) {
  frame(c, L, "UNIT DIAGNOSTIC", tMs);
  const Model& m = d.unitModel == UnitModel::Cardputer ? kCard : kCore;

  // 3/4 view with a slow sway; then the device's real tilt away from its home pose.
  hal::Mat3 rot = hal::rotationYawPitch(-28 + 6 * sinf(tMs * 0.0007f), 18);
  int attitude = -1;
  if (d.gravityValid) {
    const Vec3 now = {d.gravity[0], d.gravity[1], d.gravity[2]}, home = {d.gravityHome[0], d.gravityHome[1], d.gravityHome[2]};
    rot = hal::mul(rot, hal::rotationBetween(now, home));
    const float nn = sqrtf(now.x * now.x + now.y * now.y + now.z * now.z), nh = sqrtf(home.x * home.x + home.y * home.y + home.z * home.z);
    if (nn > 1e-3f && nh > 1e-3f) {
      float cs = (now.x * home.x + now.y * home.y + now.z * home.z) / (nn * nh);
      cs = cs > 1 ? 1 : cs < -1 ? -1 : cs;
      attitude = static_cast<int>(acosf(cs) * 57.29578f + 0.5f);
    }
  }
  const float scale = L.H(m.keyboard ? 70 : 68), cx = L.w / 2.0f, cy = L.Y(112);
  c.setClipRect(0, L.contentTop + 1, L.w, L.footerLineY - L.contentTop - 12);  // the model stays in its band
  auto at = [&](float x, float y, float z) { return hal::project(hal::apply(rot, {x, y, z}), scale, cx, cy, 5.0f); };
  auto line = [&](float x0, float y0, float z0, float x1, float y1, float z1, uint16_t col) {
    const hal::Point2 a = at(x0, y0, z0), b = at(x1, y1, z1);
    c.drawLine(int(a.x), int(a.y), int(b.x), int(b.y), col);
  };

  // The slab: back face and sides in blue, front face in white.
  for (int s = -1; s <= 1; s += 2) {
    const float z = s * m.hz;
    const uint16_t col = s > 0 ? white() : blue();
    line(-m.hx, -m.hy, z, m.hx, -m.hy, z, col);
    line(m.hx, -m.hy, z, m.hx, m.hy, z, col);
    line(m.hx, m.hy, z, -m.hx, m.hy, z, col);
    line(-m.hx, m.hy, z, -m.hx, -m.hy, z, col);
  }
  for (int sx = -1; sx <= 1; sx += 2)
    for (int sy = -1; sy <= 1; sy += 2) line(sx * m.hx, sy * m.hy, -m.hz, sx * m.hx, sy * m.hy, m.hz, blue());
  // Screen, lens and (on the Cardputer) the keyboard, on the front face.
  const float f = m.hz;
  line(m.sx0, m.sy0, f, m.sx1, m.sy0, f, white());
  line(m.sx1, m.sy0, f, m.sx1, m.sy1, f, white());
  line(m.sx1, m.sy1, f, m.sx0, m.sy1, f, white());
  line(m.sx0, m.sy1, f, m.sx0, m.sy0, f, white());
  for (int k = 0; k < 16; ++k) {
    const float a0 = k * 0.3926991f, a1 = (k + 1) * 0.3926991f;
    line(m.lx + m.lr * cosf(a0), m.ly + m.lr * sinf(a0), f, m.lx + m.lr * cosf(a1), m.ly + m.lr * sinf(a1), f, red());
  }
  if (m.keyboard) {
    const float kx0 = -1.25f, kx1 = 1.25f, ky0 = -0.78f, ky1 = -0.02f;
    for (int r = 0; r <= 4; ++r) line(kx0, ky0 + (ky1 - ky0) * r / 4, f, kx1, ky0 + (ky1 - ky0) * r / 4, f, blue());
    for (int k = 0; k <= 11; ++k) line(kx0 + (kx1 - kx0) * k / 11, ky0, f, kx0 + (kx1 - kx0) * k / 11, ky1, f, blue());
  }

  // Callouts: labels down both sides, each with a leader line to its part.
  int faults = 0;
  const int count = d.parts ? static_cast<int>(d.parts->size()) : 0;
  const int rowH = L.H(40), firstY = L.Y(34);
  char label[24];
  for (int i = 0; i < count; ++i) {
    const UnitPart& p = (*d.parts)[i];
    if (!p.ok) ++faults;
    Vec3 a = {0, 0, 0};
    for (int k = 0; k < m.anchorCount; ++k)
      if (strcmp(m.anchors[k].code, p.code) == 0) a = m.anchors[k].at;
    const hal::Point2 pt = at(a.x, a.y, a.z);
    const bool left = i % 2 == 0;
    const int ly = firstY + (i / 2) * rowH, lx = left ? 6 : L.w - 6;
    const bool blinkOff = !p.ok && (tMs / 400) % 2;
    const uint16_t col = p.ok ? white() : red();
    snprintf(label, sizeof label, "%s %s", p.code, p.ok ? "OK" : "FAULT");
    c.setTextDatum(left ? middle_left : middle_right);
    c.setTextColor(blinkOff ? dim() : col);
    c.drawString(label, lx, ly, &fonts::Font0);
    const int tw = c.textWidth(label, &fonts::Font0);
    const int ex = left ? lx + tw + 3 : lx - tw - 3;
    c.drawLine(ex, ly, int(pt.x), int(pt.y), dim());
    c.fillRect(int(pt.x) - 2, int(pt.y) - 2, 4, 4, col);
  }

  c.clearClipRect();
  c.setTextDatum(bottom_left);
  c.setTextColor(blue());
  if (attitude >= 0) snprintf(label, sizeof label, "ATTITUDE %d DEG", attitude);
  else snprintf(label, sizeof label, "ATTITUDE --");
  c.drawString(label, 6, L.footerLineY - 2, &fonts::Font0);
  c.setTextDatum(bottom_right);
  if (faults) {  // said first: a motion sensor that failed never settles
    snprintf(label, sizeof label, faults == 1 ? "%d FAULT" : "%d FAULTS", faults);
    c.setTextColor(red());
    c.drawString(label, L.w - 6, L.footerLineY - 2, &fonts::Font0);
  } else if (!d.gravityValid) {
    c.setTextColor(dim());
    c.drawString("SETTLING", L.w - 6, L.footerLineY - 2, &fonts::Font0);
  } else {
    c.setTextColor(white());
    c.drawString("ALL SYSTEMS NOMINAL", L.w - 6, L.footerLineY - 2, &fonts::Font0);
  }
}

}  // namespace screens
