#include "hal_wire.h"

#include <cmath>

namespace hal {
namespace {

const Mat3 kIdentity = {{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};

float length(Vec3 v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }
Vec3 scaled(Vec3 v, float k) { return {v.x * k, v.y * k, v.z * k}; }
Vec3 cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

}  // namespace

Mat3 rotationBetween(Vec3 from, Vec3 to) {
  const float nf = length(from), nt = length(to);
  if (nf < 1e-6f || nt < 1e-6f) return kIdentity;
  const Vec3 a = scaled(from, 1 / nf), b = scaled(to, 1 / nt);
  const float c = dot(a, b);
  if (c > 1 - 1e-6f) return kIdentity;
  if (c < -1 + 1e-6f) {  // opposite: half a turn about any axis perpendicular to a
    Vec3 axis = cross(a, std::fabs(a.x) < 0.9f ? Vec3{1, 0, 0} : Vec3{0, 1, 0});
    axis = scaled(axis, 1 / length(axis));
    const float v[3] = {axis.x, axis.y, axis.z};
    Mat3 r;
    for (int i = 0; i < 3; ++i)
      for (int j = 0; j < 3; ++j) r.m[i][j] = 2 * v[i] * v[j] - (i == j ? 1.0f : 0.0f);
    return r;
  }
  // Rodrigues: R = I + [v]x + [v]x^2 / (1 + c), with v = a x b.
  const Vec3 v = cross(a, b);
  const float k = 1 / (1 + c);
  const float vx[3][3] = {{0, -v.z, v.y}, {v.z, 0, -v.x}, {-v.y, v.x, 0}};
  Mat3 r = kIdentity;
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j) {
      float sq = 0;
      for (int n = 0; n < 3; ++n) sq += vx[i][n] * vx[n][j];
      r.m[i][j] += vx[i][j] + sq * k;
    }
  return r;
}

Mat3 rotationYawPitch(float yawDeg, float pitchDeg) {
  const float y = yawDeg * 0.017453293f, p = pitchDeg * 0.017453293f;
  const Mat3 yaw = {{{std::cos(y), 0, std::sin(y)}, {0, 1, 0}, {-std::sin(y), 0, std::cos(y)}}};
  const Mat3 pitch = {{{1, 0, 0}, {0, std::cos(p), -std::sin(p)}, {0, std::sin(p), std::cos(p)}}};
  return mul(pitch, yaw);
}

Mat3 mul(const Mat3& a, const Mat3& b) {
  Mat3 r;
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j) {
      r.m[i][j] = 0;
      for (int n = 0; n < 3; ++n) r.m[i][j] += a.m[i][n] * b.m[n][j];
    }
  return r;
}

Vec3 apply(const Mat3& m, Vec3 v) {
  return {m.m[0][0] * v.x + m.m[0][1] * v.y + m.m[0][2] * v.z, m.m[1][0] * v.x + m.m[1][1] * v.y + m.m[1][2] * v.z,
          m.m[2][0] * v.x + m.m[2][1] * v.y + m.m[2][2] * v.z};
}

Point2 project(Vec3 v, float scale, float cx, float cy, float distance) {
  float depth = distance - v.z;
  if (depth < 0.25f) depth = 0.25f;  // at or behind the eye: clamp rather than flip
  const float f = distance / depth;
  return {cx + v.x * scale * f, cy - v.y * scale * f};
}

}  // namespace hal
