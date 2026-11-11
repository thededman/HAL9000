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
  RUN_TEST(test_yaw_pitch);
  RUN_TEST(test_project);
  return UNITY_END();
}
