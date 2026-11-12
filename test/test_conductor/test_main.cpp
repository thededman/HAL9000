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

// ---- live conversations (the AI brain): lines arrive while it runs ----

void test_live_one_line() {
  hal::Conductor c;
  c.startLive(1000);
  TEST_ASSERT_TRUE(c.active());
  TEST_ASSERT_TRUE(c.live());
  ConductorOut o = c.tick(1100);
  TEST_ASSERT_FALSE(o.playLocal || o.send || o.finished);
  TEST_ASSERT_EQUAL(1, c.append(hal::Speaker::New, "Hi."));
  c.close();
  o = c.tick(1200);  // the first line doesn't wait for a gap
  TEST_ASSERT_TRUE(o.playLocal);
  TEST_ASSERT_EQUAL(1, o.localLine);
  TEST_ASSERT_EQUAL_STRING("Hi.", c.dialogue().lines[0].text.c_str());
  TEST_ASSERT_EQUAL_STRING("live", c.dialogue().id.c_str());
  o = c.onLocalDone(5000);
  TEST_ASSERT_TRUE(o.finished);
  TEST_ASSERT_EQUAL_STRING("done", o.reason.c_str());
  TEST_ASSERT_TRUE(sent(o, "END done"));
  TEST_ASSERT_FALSE(c.active());
  TEST_ASSERT_FALSE(c.live());
}

void test_live_waits_for_the_next_line() {
  hal::Conductor c;
  c.startLive(0);
  c.append(hal::Speaker::New, "One.");
  TEST_ASSERT_TRUE(c.tick(0).playLocal);
  ConductorOut o = c.onLocalDone(5000);  // nothing more yet, and not closed: he waits
  TEST_ASSERT_FALSE(o.finished);
  TEST_ASSERT_FALSE(c.tick(5050).finished);
  TEST_ASSERT_EQUAL(2, c.append(hal::Speaker::Old, "Two."));
  TEST_ASSERT_FALSE(c.tick(5399).send);  // the 400 ms gap counts from the end of line 1
  TEST_ASSERT_TRUE(sent(c.tick(5400), "SAY live 2"));

  hal::Conductor late;  // a line that arrives long after the gap is said at once
  late.startLive(0);
  late.append(hal::Speaker::New, "One.");
  late.tick(0);
  late.onLocalDone(5000);
  TEST_ASSERT_FALSE(late.tick(8999).send);
  late.append(hal::Speaker::Old, "Two.");
  TEST_ASSERT_TRUE(sent(late.tick(9000), "SAY live 2"));
}

void test_live_old_line_flow() {
  hal::Conductor c;
  c.startLive(0);
  c.append(hal::Speaker::Old, "From the desk.");
  TEST_ASSERT_TRUE(sent(c.tick(0), "SAY live 1"));
  c.onRemote(msg("PLAYING 1 3000"), 100);
  ConductorOut o = c.onRemote(msg("DONE 1"), 3100);
  TEST_ASSERT_FALSE(o.finished);  // still open
  TEST_ASSERT_TRUE(c.active());
  c.close();
  o = c.tick(3200);
  TEST_ASSERT_TRUE(o.finished);
  TEST_ASSERT_EQUAL_STRING("done", o.reason.c_str());
  TEST_ASSERT_TRUE(sent(o, "END done"));
}

void test_live_times_out() {
  hal::Conductor c;
  c.startLive(1000);
  TEST_ASSERT_FALSE(c.tick(31000).finished);  // 30 s exactly: still waiting
  ConductorOut o = c.tick(31001);
  TEST_ASSERT_TRUE(o.finished);
  TEST_ASSERT_EQUAL_STRING("timeout", o.reason.c_str());
  TEST_ASSERT_FALSE(o.gloat);  // the brain let him down, not the desk HAL
  TEST_ASSERT_TRUE(sent(o, "END timeout"));

  hal::Conductor mid;  // the clock restarts when a line ends
  mid.startLive(0);
  mid.append(hal::Speaker::New, "One.");
  mid.tick(0);
  mid.onLocalDone(20000);
  TEST_ASSERT_FALSE(mid.tick(50000).finished);
  TEST_ASSERT_TRUE(mid.tick(50001).finished);
}

void test_append_after_end() {
  hal::Conductor never;
  TEST_ASSERT_EQUAL(0, never.append(hal::Speaker::New, "x"));
  TEST_ASSERT_FALSE(never.active());

  hal::Conductor c;
  c.startLive(0);
  c.append(hal::Speaker::New, "One.");
  c.tick(0);
  c.interrupt(500);
  TEST_ASSERT_EQUAL(0, c.append(hal::Speaker::Old, "late"));  // a reply that arrives after the tap
  TEST_ASSERT_FALSE(c.active());
  TEST_ASSERT_FALSE(c.tick(1000).send);

  c.startLive(2000);
  c.close();
  TEST_ASSERT_EQUAL(0, c.append(hal::Speaker::New, "closed"));
  TEST_ASSERT_TRUE(c.tick(2001).finished);  // closed with nothing in it: done
}

void test_live_is_capped() {
  hal::Conductor c;
  c.startLive(0);
  for (int n = 1; n <= 99; ++n) TEST_ASSERT_EQUAL(n, c.append(hal::Speaker::New, "x"));
  TEST_ASSERT_EQUAL(0, c.append(hal::Speaker::New, "one too many"));
}

void test_live_interrupt_and_offline() {
  hal::Conductor c;
  c.startLive(0);
  c.append(hal::Speaker::New, "One.");
  c.tick(0);
  ConductorOut o = c.interrupt(500);
  TEST_ASSERT_TRUE(o.finished);
  TEST_ASSERT_TRUE(sent(o, "END interrupted"));

  c.startLive(1000);
  TEST_ASSERT_TRUE(c.interrupt(1100).finished);  // a tap while he waits for the first line

  c.startLive(2000);
  c.append(hal::Speaker::Old, "From the desk.");
  TEST_ASSERT_TRUE(sent(c.tick(2000), "SAY live 1"));
  o = c.onDisconnected(2100);
  TEST_ASSERT_TRUE(o.finished);
  TEST_ASSERT_EQUAL_STRING("offline", o.reason.c_str());
  TEST_ASSERT_TRUE(o.gloat);

  c.startLive(3000);
  c.append(hal::Speaker::Old, "From the desk.");
  c.tick(3000);
  o = c.tick(5001);  // no PLAYING within 2 s, as in a scripted conversation
  TEST_ASSERT_EQUAL_STRING("timeout", o.reason.c_str());
  TEST_ASSERT_TRUE(o.gloat);
}

void test_scripted_is_not_live() {
  hal::Conductor c;
  c.start(script("NEW: a.\nOLD: b.\n"), 0);
  TEST_ASSERT_FALSE(c.live());
  TEST_ASSERT_EQUAL(0, c.append(hal::Speaker::New, "x"));
  c.close();  // means nothing to a script
  TEST_ASSERT_TRUE(c.tick(0).playLocal);
  c.onLocalDone(1000);
  TEST_ASSERT_TRUE(sent(c.tick(1400), "SAY intro 2"));

  c.startLive(5000);  // and a script after a live conversation is a script again
  c.interrupt(5100);
  c.start(script("NEW: a.\n"), 6000);
  TEST_ASSERT_FALSE(c.live());
  c.tick(6000);
  TEST_ASSERT_TRUE(c.onLocalDone(7000).finished);
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
  RUN_TEST(test_live_one_line);
  RUN_TEST(test_live_waits_for_the_next_line);
  RUN_TEST(test_live_old_line_flow);
  RUN_TEST(test_live_times_out);
  RUN_TEST(test_append_after_end);
  RUN_TEST(test_live_is_capped);
  RUN_TEST(test_live_interrupt_and_offline);
  RUN_TEST(test_scripted_is_not_live);
  return UNITY_END();
}
