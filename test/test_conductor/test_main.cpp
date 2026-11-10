#include <unity.h>

#include <string>

#include "hal_conductor.h"

using hal::ConductorOut;
using hal::Message;
using hal::MsgType;

void setUp() {}
void tearDown() {}

namespace {

hal::Dialogue script(const char* text) {
  hal::Dialogue d;
  std::vector<std::string> err;
  TEST_ASSERT_TRUE(hal::parseDialogue("intro", text, &d, &err));
  return d;
}

Message msg(const char* line) { return hal::parseMessage(line); }

bool sent(const ConductorOut& o, const char* line) { return o.send && hal::formatMessage(o.msg) == line; }

}  // namespace

void test_alternating_speakers() {
  hal::Conductor c;
  c.start(script("OLD: a.\nNEW: b.\nOLD: c.\n"), 0);
  TEST_ASSERT_TRUE(sent(c.tick(0), "SAY intro 1"));
  TEST_ASSERT_FALSE(c.onRemote(msg("PLAYING 1 1000"), 100).send);
  ConductorOut o = c.onRemote(msg("DONE 1"), 1100);
  TEST_ASSERT_FALSE(o.playLocal);
  TEST_ASSERT_FALSE(c.tick(1499).playLocal);  // 400 ms gap
  o = c.tick(1500);
  TEST_ASSERT_TRUE(o.playLocal);
  TEST_ASSERT_EQUAL(2, o.localLine);
  TEST_ASSERT_EQUAL(2, c.line());
  c.onLocalDone(3000);
  TEST_ASSERT_TRUE(sent(c.tick(3400), "SAY intro 3"));
  c.onRemote(msg("PLAYING 3 800"), 3450);
  o = c.onRemote(msg("DONE 3"), 4300);
  TEST_ASSERT_TRUE(o.finished);
  TEST_ASSERT_EQUAL_STRING("done", o.reason.c_str());
  TEST_ASSERT_TRUE(sent(o, "END done"));
  TEST_ASSERT_FALSE(o.gloat);
  TEST_ASSERT_FALSE(c.active());
}

void test_same_speaker_twice() {
  hal::Conductor c;
  c.start(script("NEW: a.\nNEW: b.\n"), 0);
  ConductorOut o = c.tick(0);
  TEST_ASSERT_TRUE(o.playLocal);
  TEST_ASSERT_EQUAL(1, o.localLine);
  c.onLocalDone(500);
  o = c.tick(900);
  TEST_ASSERT_TRUE(o.playLocal);
  TEST_ASSERT_EQUAL(2, o.localLine);
  o = c.onLocalDone(1500);
  TEST_ASSERT_TRUE(o.finished);
}

void test_pause_overrides_gap() {
  hal::Conductor c;
  c.start(script("NEW: a.\nPAUSE: 1200\nNEW: b.\n"), 0);
  c.tick(0);
  c.onLocalDone(500);
  TEST_ASSERT_FALSE(c.tick(900).playLocal);
  TEST_ASSERT_FALSE(c.tick(1699).playLocal);
  TEST_ASSERT_TRUE(c.tick(1700).playLocal);
}

void test_first_line_pause() {
  hal::Conductor c;
  c.start(script("PAUSE: 500\nNEW: a.\n"), 1000);
  TEST_ASSERT_FALSE(c.tick(1499).playLocal);
  TEST_ASSERT_TRUE(c.tick(1500).playLocal);
}

void test_ack_timeout() {
  hal::Conductor c;
  c.start(script("OLD: a.\nNEW: b.\n"), 0);
  c.tick(0);
  TEST_ASSERT_FALSE(c.tick(2000).finished);
  ConductorOut o = c.tick(2001);
  TEST_ASSERT_TRUE(o.finished);
  TEST_ASSERT_EQUAL_STRING("timeout", o.reason.c_str());
  TEST_ASSERT_TRUE(o.gloat);
  TEST_ASSERT_TRUE(sent(o, "END timeout"));
}

void test_done_timeout_uses_length() {
  hal::Conductor c;
  c.start(script("OLD: a.\nNEW: b.\n"), 0);
  c.tick(0);
  c.onRemote(msg("PLAYING 1 5000"), 100);
  TEST_ASSERT_FALSE(c.tick(8100).finished);  // 100 + 5000 + 3000
  TEST_ASSERT_TRUE(c.tick(8101).finished);
}

void test_missing_line_skipped() {
  hal::Conductor c;
  c.start(script("OLD: a.\nNEW: b.\n"), 0);
  c.tick(0);
  c.onRemote(msg("MISSING 1"), 50);
  ConductorOut o = c.tick(450);
  TEST_ASSERT_TRUE(o.playLocal);
  TEST_ASSERT_EQUAL(2, o.localLine);
}

void test_busy_fails() {
  hal::Conductor c;
  c.start(script("OLD: a.\n"), 0);
  c.tick(0);
  ConductorOut o = c.onRemote(msg("BUSY"), 30);
  TEST_ASSERT_TRUE(o.finished);
  TEST_ASSERT_EQUAL_STRING("busy", o.reason.c_str());
  TEST_ASSERT_TRUE(o.gloat);
}

void test_remote_end_interrupts() {
  hal::Conductor c;
  c.start(script("OLD: a.\nNEW: b.\n"), 0);
  c.tick(0);
  ConductorOut o = c.onRemote(msg("END interrupted"), 500);
  TEST_ASSERT_TRUE(o.finished);
  TEST_ASSERT_EQUAL_STRING("interrupted", o.reason.c_str());
  TEST_ASSERT_TRUE(o.gloat);
  TEST_ASSERT_FALSE(o.send);  // they already know
}

void test_disconnect_is_offline() {
  hal::Conductor c;
  c.start(script("OLD: a.\n"), 0);
  c.tick(0);
  ConductorOut o = c.onDisconnected(10);
  TEST_ASSERT_TRUE(o.finished);
  TEST_ASSERT_EQUAL_STRING("offline", o.reason.c_str());
  TEST_ASSERT_TRUE(o.gloat);
  TEST_ASSERT_FALSE(o.send);
}

void test_local_interrupt() {
  hal::Conductor c;
  c.start(script("NEW: a.\nOLD: b.\n"), 0);
  c.tick(0);
  ConductorOut o = c.interrupt(200);
  TEST_ASSERT_TRUE(o.finished);
  TEST_ASSERT_TRUE(sent(o, "END interrupted"));
  TEST_ASSERT_FALSE(o.gloat);
}

void test_stale_messages_ignored() {
  hal::Conductor c;
  c.start(script("OLD: a.\nNEW: b.\n"), 0);
  c.tick(0);
  ConductorOut o = c.onRemote(msg("DONE 5"), 50);
  TEST_ASSERT_FALSE(o.finished);
  TEST_ASSERT_FALSE(c.tick(450).playLocal);  // still waiting for line 1
  TEST_ASSERT_FALSE(c.onRemote(msg("PONG"), 60).finished);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_alternating_speakers);
  RUN_TEST(test_same_speaker_twice);
  RUN_TEST(test_pause_overrides_gap);
  RUN_TEST(test_first_line_pause);
  RUN_TEST(test_ack_timeout);
  RUN_TEST(test_done_timeout_uses_length);
  RUN_TEST(test_missing_line_skipped);
  RUN_TEST(test_busy_fails);
  RUN_TEST(test_remote_end_interrupts);
  RUN_TEST(test_disconnect_is_offline);
  RUN_TEST(test_local_interrupt);
  RUN_TEST(test_stale_messages_ignored);
  return UNITY_END();
}
