#include <unity.h>

#include <string>

#include "hal_readings.h"

using hal::Reading;

void setUp() {}
void tearDown() {}

namespace {
void expect(const Reading& r, const char* code, const char* value, bool alarm) {
  TEST_ASSERT_TRUE_MESSAGE(r.valid, code);
  TEST_ASSERT_EQUAL_STRING(code, r.code.c_str());
  TEST_ASSERT_EQUAL_STRING(value, r.value.c_str());
  TEST_ASSERT_EQUAL_MESSAGE(alarm, r.alarm, value);
}
void expectInvalid(const Reading& r, const char* code) {
  TEST_ASSERT_EQUAL_STRING(code, r.code.c_str());
  TEST_ASSERT_FALSE_MESSAGE(r.valid, code);
  TEST_ASSERT_FALSE_MESSAGE(r.alarm, code);
  TEST_ASSERT_FALSE_MESSAGE(r.name.empty(), code);
}
}  // namespace

void test_power() {
  expect(hal::powerReading(87, true), "PWR", "87 % CHG", false);
  expect(hal::powerReading(19, false), "PWR", "19 %", true);
  expect(hal::powerReading(19, true), "PWR", "19 % CHG", false);
  expect(hal::powerReading(20, false), "PWR", "20 %", false);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.87f, hal::powerReading(87, false).level);
  expectInvalid(hal::powerReading(-1, false), "PWR");
}

void test_light_and_proximity() {
  expect(hal::lightReading(38), "LUX", "38 %", false);
  expectInvalid(hal::lightReading(-1), "LUX");
  expect(hal::proximityReading(35), "PRX", "35", false);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.035f, hal::proximityReading(35).level);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, hal::proximityReading(2047).level);
  expectInvalid(hal::proximityReading(-1), "PRX");
}

void test_motion_and_tilt() {
  expect(hal::motionReading(12.4f), "MOT", "12 DEG/S", false);
  expect(hal::motionReading(120), "MOT", "120 DEG/S", false);
  expect(hal::motionReading(121), "MOT", "121 DEG/S", true);
  expect(hal::tiltReading(3), "TLT", "3 DEG", false);
  expect(hal::tiltReading(59), "TLT", "59 DEG", false);
  expect(hal::tiltReading(60), "TLT", "60 DEG", true);
  expectInvalid(hal::tiltReading(-1), "TLT");
}

void test_temperature() {
  expect(hal::tempReading(48), "TMP", "48 C", false);
  expect(hal::tempReading(70), "TMP", "70 C", false);
  expect(hal::tempReading(71), "TMP", "71 C", true);
  expectInvalid(hal::tempReading(-999), "TMP");
}

void test_network_and_link() {
  expect(hal::netReading(-46), "NET", "-46 DBM", false);
  expect(hal::netReading(0), "NET", "OFF", false);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, hal::netReading(0).level);
  expect(hal::linkReading(false, false), "LNK", "OFF", false);
  expect(hal::linkReading(true, false), "LNK", "NO CARRIER", true);
  expect(hal::linkReading(true, true), "LNK", "CONNECTED", false);
}

void test_memory() {
  expect(hal::memoryReading(102, 320), "MEM", "102 KB", false);
  expect(hal::memoryReading(3072, 8192), "MEM", "3 MB", false);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.68f, hal::memoryReading(102, 320).level);  // share used
  expect(hal::memoryReading(31, 320), "MEM", "31 KB", true);                   // under 10 % free
  expect(hal::memoryReading(32, 320), "MEM", "32 KB", false);
  expectInvalid(hal::memoryReading(10, 0), "MEM");
}

void test_camera_and_audio() {
  expect(hal::cameraReading(false, 0), "CAM", "OFFLINE", false);
  expect(hal::cameraReading(true, 12), "CAM", "MOTION 12", false);
  expect(hal::audioReading(128, 40), "AUD", "40 LINES", false);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.5f, hal::audioReading(128, 40).level);
  expect(hal::audioReading(0, 1), "AUD", "1 LINE", false);
}

void test_clock_and_uptime() {
  expect(hal::clockReading(73620), "CLK", "20:27", false);
  expect(hal::clockReading(0), "CLK", "00:00", false);
  expectInvalid(hal::clockReading(-1), "CLK");
  expect(hal::uptimeReading(765), "UPT", "00:12:45", false);
  expect(hal::uptimeReading(94200), "UPT", "1D 02:10", false);
}

void test_cpu() {
  expect(hal::cpuReading(41), "CPU", "41 %", false);
  expect(hal::cpuReading(95), "CPU", "95 %", false);
  expect(hal::cpuReading(96), "CPU", "96 %", true);
  expectInvalid(hal::cpuReading(-1), "CPU");
}

void test_peer() {
  expect(hal::peerReading(hal::PeerState::Ok, "idle"), "DSK", "IDLE", false);
  expect(hal::peerReading(hal::PeerState::NotResponding, "idle"), "DSK", "NOT RESPONDING", true);
  expect(hal::peerReading(hal::PeerState::TooOld, ""), "DSK", "TOO OLD", false);
  expect(hal::peerReading(hal::PeerState::Waiting, ""), "DSK", "WAITING", false);
}

void test_log_copies_fields() {
  Reading slot;
  hal::logReading(slot, hal::powerReading(87, true));
  TEST_ASSERT_EQUAL_STRING("PWR", slot.code.c_str());
  TEST_ASSERT_EQUAL_STRING("87 % CHG", slot.value.c_str());
  TEST_ASSERT_TRUE(slot.valid);
  TEST_ASSERT_EQUAL(1, int(slot.trace.size()));
  hal::logReading(slot, hal::powerReading(-1, false));  // a failed read: no point added
  TEST_ASSERT_FALSE(slot.valid);
  TEST_ASSERT_EQUAL(1, int(slot.trace.size()));
}

void test_log_caps_trace() {
  Reading slot;
  for (int i = 0; i < 50; ++i) hal::logReading(slot, hal::lightReading(i), 40);
  TEST_ASSERT_EQUAL(40, int(slot.trace.size()));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.49f, slot.trace.back());   // newest last
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.10f, slot.trace.front());  // the oldest ten are gone
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_power);
  RUN_TEST(test_light_and_proximity);
  RUN_TEST(test_motion_and_tilt);
  RUN_TEST(test_temperature);
  RUN_TEST(test_network_and_link);
  RUN_TEST(test_memory);
  RUN_TEST(test_camera_and_audio);
  RUN_TEST(test_clock_and_uptime);
  RUN_TEST(test_cpu);
  RUN_TEST(test_peer);
  RUN_TEST(test_log_copies_fields);
  RUN_TEST(test_log_caps_trace);
  return UNITY_END();
}
