#include <unity.h>

#include <cmath>

#include "hal_wire.h"

using hal::Mat3;
using hal::Vec3;

void setUp() {}
void tearDown() {}

namespace {
void near(Vec3 a, Vec3 b, float tol = 1e-3f) {
  TEST_ASSERT_FLOAT_WITHIN(tol, b.x, a.x);
  TEST_ASSERT_FLOAT_WITHIN(tol, b.y, a.y);
  TEST_ASSERT_FLOAT_WITHIN(tol, b.z, a.z);
}
Vec3 unit(Vec3 v) {
  float n = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
  return {v.x / n, v.y / n, v.z / n};
}
bool finite(const Mat3& m) {
  for (auto& row : m.m)
    for (float v : row)
      if (!std::isfinite(v)) return false;
  return true;
}
}  // namespace

void test_rotation_identity() {
  Mat3 r = hal::rotationBetween({0, 1, 0}, {0, 2, 0});
  near(hal::apply(r, {1, 2, 3}), {1, 2, 3});
}

void test_rotation_maps_from_to_to() {
  const Vec3 pairs[][2] = {{{0, 1, 0}, {1, 0, 0}}, {{0.2f, 0.9f, -0.1f}, {-0.5f, 0.5f, 0.7f}}, {{-1, 0, 0}, {-0.87f, 0.5f, 0}}};
  for (auto& p : pairs) near(hal::apply(hal::rotationBetween(p[0], p[1]), unit(p[0])), unit(p[1]));
}

void test_rotation_is_orthonormal() {
  Mat3 r = hal::rotationBetween({0.2f, 0.9f, -0.1f}, {-0.5f, 0.5f, 0.7f});
  Vec3 x = hal::apply(r, {1, 0, 0}), y = hal::apply(r, {0, 1, 0});
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 1.0f, x.x * x.x + x.y * x.y + x.z * x.z);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.0f, x.x * y.x + x.y * y.y + x.z * y.z);
}

void test_rotation_degenerate() {
  near(hal::apply(hal::rotationBetween({0, 0, 0}, {0, 1, 0}), {1, 2, 3}), {1, 2, 3});  // no direction: identity
  near(hal::apply(hal::rotationBetween({0, 1, 0}, {0, 0, 0}), {1, 2, 3}), {1, 2, 3});
  const Vec3 opposite[][2] = {{{0, 1, 0}, {0, -1, 0}}, {{1, 0, 0}, {-1, 0, 0}}, {{0.3f, -0.4f, 0.5f}, {-0.3f, 0.4f, -0.5f}}};
  for (auto& p : opposite) {  // upside down: still a proper rotation, no NaN
    Mat3 r = hal::rotationBetween(p[0], p[1]);
    TEST_ASSERT_TRUE(finite(r));
    near(hal::apply(r, unit(p[0])), unit(p[1]));
  }
}

// Nearly upside down (within a degree of exactly opposite): still a rigid rotation
// that takes `from` onto `to`, not a stretched one.
void test_rotation_near_opposite_is_rigid() {
  const Vec3 froms[] = {{0, 1, 0}, {-1, 0, 0}, unit({0.3f, -0.4f, 0.5f})};
  const float degrees[] = {179.0f, 179.5f, 179.7f, 179.8f, 179.9f, 179.95f, 179.99f};
  for (float deg : degrees)
    for (const Vec3& f : froms) {
      // `to` is `from` turned by `deg` about an axis at right angles to it.
      const Vec3 other = std::fabs(f.x) < 0.9f ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
      const Vec3 p = unit({f.y * other.z - f.z * other.y, f.z * other.x - f.x * other.z, f.x * other.y - f.y * other.x});
      const Vec3 side = {p.y * f.z - p.z * f.y, p.z * f.x - p.x * f.z, p.x * f.y - p.y * f.x};
      const double a = deg * 0.017453292519943295;
      const Vec3 to = {float(f.x * std::cos(a) + side.x * std::sin(a)), float(f.y * std::cos(a) + side.y * std::sin(a)),
                       float(f.z * std::cos(a) + side.z * std::sin(a))};
      const Mat3 r = hal::rotationBetween(f, to);
      TEST_ASSERT_TRUE(finite(r));
      near(hal::apply(r, f), unit(to), 2e-3f);
      const Vec3 x = hal::apply(r, {1, 0, 0}), y = hal::apply(r, {0, 1, 0}), z = hal::apply(r, {0, 0, 1});
      TEST_ASSERT_FLOAT_WITHIN(2e-3f, 1.0f, x.x * x.x + x.y * x.y + x.z * x.z);
      TEST_ASSERT_FLOAT_WITHIN(2e-3f, 1.0f, y.x * y.x + y.y * y.y + y.z * y.z);
      TEST_ASSERT_FLOAT_WITHIN(2e-3f, 1.0f, z.x * z.x + z.y * z.y + z.z * z.z);
      TEST_ASSERT_FLOAT_WITHIN(2e-3f, 0.0f, x.x * y.x + x.y * y.y + x.z * y.z);
      TEST_ASSERT_FLOAT_WITHIN(2e-3f, 0.0f, y.x * z.x + y.y * z.y + y.z * z.z);
    }
}

// A sensor read that isn't a number must not reach the drawing code as NaN geometry.
void test_rotation_ignores_nonsense() {
  const float nan = std::nanf(""), inf = INFINITY;
  const Vec3 bad[] = {{nan, 0, 0}, {0, inf, 0}, {nan, nan, nan}, {-inf, inf, 1}};
  for (const Vec3& b : bad) {
    TEST_ASSERT_TRUE(finite(hal::rotationBetween(b, {0, 1, 0})));
    near(hal::apply(hal::rotationBetween(b, {0, 1, 0}), {1, 2, 3}), {1, 2, 3});
    near(hal::apply(hal::rotationBetween({0, 1, 0}, b), {1, 2, 3}), {1, 2, 3});
  }
}

// The Cardputer Zero's motion sensor is mounted with its x axis pointing into the
// board. Measured on the device: lying flat, screen up, it reads (-1, 0, 0); stood
// on its bottom edge it reads +z. The model's axes are x right, y to the top edge,
// z out of the screen.
void test_cardputer_sensor_to_model() {
  near(hal::cardputerImuToModel({-1, 0, 0}), {0, 0, 1});  // flat: up is out of the screen
  near(hal::cardputerImuToModel({0, 0, 1}), {0, 1, 0});   // on its bottom edge: up is the top edge
  near(hal::cardputerImuToModel({0, -1, 0}), {1, 0, 0});  // on its left edge: up is the right edge
  near(hal::cardputerImuToModel({-0.6f, 0.02f, 0.8f}), {-0.02f, 0.8f, 0.6f});  // leaning back, as recorded
  // A turn, not a mirror image: x cross y is z after mapping too.
  const Vec3 x = hal::cardputerImuToModel({1, 0, 0}), y = hal::cardputerImuToModel({0, 1, 0}), z = hal::cardputerImuToModel({0, 0, 1});
  near({x.y * y.z - x.z * y.y, x.z * y.x - x.x * y.z, x.x * y.y - x.y * y.x}, z);
}

void test_yaw_pitch() {
  near(hal::apply(hal::rotationYawPitch(90, 0), {1, 0, 0}), {0, 0, -1});
  near(hal::apply(hal::rotationYawPitch(0, 90), {0, 0, 1}), {0, -1, 0});
  near(hal::apply(hal::mul(hal::rotationYawPitch(90, 0), hal::rotationYawPitch(-90, 0)), {1, 2, 3}), {1, 2, 3});
}

void test_project() {
  hal::Point2 c = hal::project({0, 0, 0}, 50, 160, 120, 4);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 160, c.x);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 120, c.y);
  hal::Point2 farPt = hal::project({1, 1, -1}, 50, 160, 120, 4), nearPt = hal::project({1, 1, 1}, 50, 160, 120, 4);
  TEST_ASSERT_TRUE(nearPt.x > farPt.x);  // nearer the viewer: further from the centre
  TEST_ASSERT_TRUE(nearPt.y < farPt.y);  // +y is up on screen
  hal::Point2 behind = hal::project({1, 1, 10}, 50, 160, 120, 4);  // past the eye: clamped, finite
  TEST_ASSERT_TRUE(std::isfinite(behind.x) && std::isfinite(behind.y));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_rotation_identity);
  RUN_TEST(test_rotation_maps_from_to_to);
  RUN_TEST(test_rotation_is_orthonormal);
  RUN_TEST(test_rotation_degenerate);
  RUN_TEST(test_rotation_near_opposite_is_rigid);
  RUN_TEST(test_rotation_ignores_nonsense);
  RUN_TEST(test_cardputer_sensor_to_model);
  RUN_TEST(test_yaw_pitch);
  RUN_TEST(test_project);
  return UNITY_END();
}
