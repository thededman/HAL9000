#pragma once
// 3D maths for the unit-diagnostic wireframe: turn the device model by the
// rotation between two gravity directions and project it onto the screen.
// Axes: x right, y up, z towards the viewer.
namespace hal {

struct Vec3 {
  float x, y, z;
};
struct Mat3 {
  float m[3][3];
};
struct Point2 {
  float x, y;
};

// The smallest rotation taking direction `from` onto direction `to`. Identity if
// either has no length or they already agree; for exactly opposite directions,
// a half turn about a perpendicular axis.
Mat3 rotationBetween(Vec3 from, Vec3 to);
// Yaw about the y axis (90 takes +x to -z), then pitch about the x axis (90 takes +z to -y).
Mat3 rotationYawPitch(float yawDeg, float pitchDeg);
Mat3 mul(const Mat3& a, const Mat3& b);  // b first, then a
Vec3 apply(const Mat3& m, Vec3 v);
// Simple perspective: the eye is `distance` in front of the origin; +y is up on screen.
Point2 project(Vec3 v, float scale, float cx, float cy, float distance);

}  // namespace hal
