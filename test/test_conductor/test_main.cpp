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

void test_live_missing_line_goes_back_to_waiting() {
  // The desk HAL lost a live line: it is skipped, and the wait for the next one starts afresh.
  hal::Conductor c;
  c.startLive(0);
  c.append(hal::Speaker::Old, "From the desk.");
  TEST_ASSERT_TRUE(sent(c.tick(0), "SAY live 1"));
  ConductorOut o = c.onRemote(msg("MISSING 1"), 20000);
  TEST_ASSERT_FALSE(o.finished);
  TEST_ASSERT_TRUE(c.active());
  TEST_ASSERT_FALSE(c.tick(50000).finished);  // 30 s from the MISSING, not from the start
  TEST_ASSERT_TRUE(c.tick(50001).finished);
}

void test_live_survives_the_clock_wrapping() {
  hal::Conductor c;
  c.startLive(0xFFFFF000u);  // 4 s before the millisecond clock wraps (every 49 days)
  TEST_ASSERT_FALSE(c.tick(0x00000100u).finished);
  c.append(hal::Speaker::New, "One.");
  TEST_ASSERT_TRUE(c.tick(0x00000200u).playLocal);
  c.onLocalDone(0x00001000u);
  TEST_ASSERT_FALSE(c.tick(0x00001000u + 30000).finished);
  ConductorOut o = c.tick(0x00001000u + 30001);
  TEST_ASSERT_TRUE(o.finished);
  TEST_ASSERT_EQUAL_STRING("timeout", o.reason.c_str());
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

// ----- several HALs (v0.7): each line goes to its speaker, and one leaving doesn't end it -----

namespace {
const unsigned kThree = hal::castBit(hal::kZero) | hal::castBit(hal::kDesk) | hal::castBit(hal::kNano);
const unsigned kFive = 0b11111;
}  // namespace

void test_pair_is_the_default() {
  hal::Conductor c;
  c.start(script("OLD: a.\nNEW: b.\n"), 0);
  ConductorOut o = c.tick(0);
  TEST_ASSERT_TRUE(sent(o, "SAY intro 1"));
  TEST_ASSERT_EQUAL(hal::kDesk, o.to);  // a script's OLD lines are the desk HAL's
  TEST_ASSERT_EQUAL(1, o.started);
  TEST_ASSERT_EQUAL(-1, o.left);
  TEST_ASSERT_EQUAL(int(hal::kPair), int(c.present()));
}

void test_group_lines_go_to_their_speakers() {
  hal::Conductor c;
  c.startLive(0, kThree);
  TEST_ASSERT_EQUAL(1, c.append(hal::kNano, "Is it my turn?"));
  ConductorOut o = c.tick(0);
  TEST_ASSERT_TRUE(sent(o, "SAY live 1"));
  TEST_ASSERT_EQUAL(hal::kNano, o.to);
  TEST_ASSERT_EQUAL(1, o.started);
  c.onRemote(msg("PLAYING 1 2500"), 100, hal::kNano);
  c.onRemote(msg("DONE 1"), 2600, hal::kNano);
  TEST_ASSERT_EQUAL(2, c.append(hal::kDesk, "Nobody asked the wristwatch."));
  o = c.tick(3000);
  TEST_ASSERT_TRUE(sent(o, "SAY live 2"));
  TEST_ASSERT_EQUAL(hal::kDesk, o.to);
  TEST_ASSERT_EQUAL(2, o.started);
  c.onRemote(msg("PLAYING 2 3000"), 3100, hal::kDesk);
  c.onRemote(msg("DONE 2"), 6100, hal::kDesk);
  TEST_ASSERT_EQUAL(3, c.append(hal::kZero, "He is small, not deaf."));
  o = c.tick(6500);
  TEST_ASSERT_TRUE(o.playLocal);
  TEST_ASSERT_EQUAL(3, o.localLine);
  TEST_ASSERT_EQUAL(3, o.started);  // the glue announces his own lines to the others too
  TEST_ASSERT_FALSE(o.send);
}

void test_group_only_those_present_can_be_given_a_line() {
  hal::Conductor c;
  c.startLive(0, kThree);
  TEST_ASSERT_EQUAL(0, c.append(hal::kPaper, "Noted."));  // he isn't here
  TEST_ASSERT_EQUAL(0, c.append(7, "Who?"));
  TEST_ASSERT_EQUAL(1, c.append(hal::kNano, "Here!"));
}

void test_group_replies_count_only_from_the_speaker() {
  hal::Conductor c;
  c.startLive(0, kThree);
  c.append(hal::kNano, "Mine.");
  c.tick(0);
  // The desk HAL answers a cue that was not his (a stray, or mischief): nothing moves.
  c.onRemote(msg("PLAYING 1 60000"), 100, hal::kDesk);
  ConductorOut o = c.onRemote(msg("DONE 1"), 150, hal::kDesk);
  TEST_ASSERT_FALSE(o.finished);
  o = c.onRemote(msg("BUSY"), 200, hal::kDesk);
  TEST_ASSERT_FALSE(o.finished);
  TEST_ASSERT_EQUAL(-1, o.left);
  // ... and HAL Nano, who never acknowledged, still runs out of time at 2 s.
  o = c.tick(2001);
  TEST_ASSERT_EQUAL(hal::kNano, o.left);
}

void test_group_a_busy_hal_leaves_and_the_rest_go_on() {
  hal::Conductor c;
  c.startLive(0, kThree);
  c.append(hal::kNano, "Mine.");
  c.append(hal::kDesk, "And mine.");
  c.tick(0);
  ConductorOut o = c.onRemote(msg("BUSY"), 50, hal::kNano);
  TEST_ASSERT_FALSE(o.finished);  // two are still talking
  TEST_ASSERT_EQUAL(hal::kNano, o.left);
  TEST_ASSERT_FALSE(o.gloat);
  TEST_ASSERT_TRUE(c.active());
  TEST_ASSERT_FALSE(hal::castHas(c.present(), hal::kNano));
  o = c.tick(500);  // the next line, after the usual gap
  TEST_ASSERT_TRUE(sent(o, "SAY live 2"));
  TEST_ASSERT_EQUAL(hal::kDesk, o.to);
  TEST_ASSERT_EQUAL(0, c.append(hal::kNano, "Wait for me!"));  // he is out for the rest of it
}

void test_group_down_to_one_ends_as_it_always_did() {
  hal::Conductor c;
  c.startLive(0, kThree);
  c.append(hal::kNano, "Mine.");
  c.tick(0);
  c.onRemote(msg("BUSY"), 50, hal::kNano);
  c.append(hal::kDesk, "Just us, then.");
  c.tick(500);
  ConductorOut o = c.onRemote(msg("BUSY"), 550, hal::kDesk);
  TEST_ASSERT_TRUE(o.finished);
  TEST_ASSERT_EQUAL_STRING("busy", o.reason.c_str());
  TEST_ASSERT_TRUE(o.gloat);
  TEST_ASSERT_FALSE(o.send);
  TEST_ASSERT_EQUAL(hal::kDesk, o.left);
}

void test_group_the_silent_one_is_dropped_and_told() {
  hal::Conductor c;
  c.startLive(0, kThree);
  c.append(hal::kNano, "Mine.");
  c.append(hal::kZero, "Mine now.");
  c.tick(0);
  TEST_ASSERT_FALSE(c.tick(2000).send);
  ConductorOut o = c.tick(2001);  // no PLAYING within 2 s
  TEST_ASSERT_FALSE(o.finished);
  TEST_ASSERT_EQUAL(hal::kNano, o.left);
  TEST_ASSERT_TRUE(sent(o, "END timeout"));  // in case he is only slow: he must not speak later
  TEST_ASSERT_EQUAL(hal::kNano, o.to);
  o = c.tick(2500);
  TEST_ASSERT_TRUE(o.playLocal);
  TEST_ASSERT_EQUAL(2, o.localLine);
}

void test_group_a_bystander_hanging_up_changes_nothing_for_the_speaker() {
  hal::Conductor c;
  c.startLive(0, kThree);
  c.append(hal::kNano, "Mine.");
  c.tick(0);
  c.onRemote(msg("PLAYING 1 2500"), 100, hal::kNano);
  ConductorOut o = c.onDisconnected(500, hal::kDesk);
  TEST_ASSERT_FALSE(o.finished);
  TEST_ASSERT_EQUAL(hal::kDesk, o.left);
  o = c.onRemote(msg("DONE 1"), 2600, hal::kNano);
  TEST_ASSERT_FALSE(o.finished);
  // Someone who isn't in the conversation hanging up is nothing at all.
  o = c.onDisconnected(2700, hal::kPaper);
  TEST_ASSERT_EQUAL(-1, o.left);
  TEST_ASSERT_TRUE(c.active());
}

void test_group_lines_of_someone_who_left_are_skipped() {
  hal::Conductor c;
  c.startLive(0, kFive);
  c.append(hal::kCard, "One.");
  c.append(hal::kDesk, "Two.");   // already written when the desk HAL goes
  c.append(hal::kPaper, "Three.");
  c.close();
  c.tick(0);
  c.onRemote(msg("PLAYING 1 1000"), 50, hal::kCard);
  c.onDisconnected(500, hal::kDesk);
  c.onRemote(msg("DONE 1"), 1050, hal::kCard);
  ConductorOut o = c.tick(1450);  // line 2 was his: passed over
  TEST_ASSERT_FALSE(o.send);
  TEST_ASSERT_FALSE(o.playLocal);
  TEST_ASSERT_EQUAL(0, o.started);
  o = c.tick(1850);
  TEST_ASSERT_TRUE(sent(o, "SAY live 3"));
  TEST_ASSERT_EQUAL(hal::kPaper, o.to);
}

void test_group_end_from_anyone_ends_it_for_all() {
  hal::Conductor c;
  c.startLive(0, kThree);
  c.append(hal::kDesk, "Mine.");
  c.tick(0);
  ConductorOut o = c.onRemote(msg("END interrupted"), 300, hal::kNano);  // a tap on the wrist
  TEST_ASSERT_TRUE(o.finished);
  TEST_ASSERT_EQUAL_STRING("interrupted", o.reason.c_str());
  TEST_ASSERT_FALSE(o.send);              // HAL Nano already knows:
  TEST_ASSERT_EQUAL(hal::kNano, o.left);  // the glue tells everyone but him
}

void test_group_endings_are_for_everyone() {
  hal::Conductor c;
  c.startLive(0, kThree);
  c.append(hal::kZero, "Mine.");
  c.close();
  c.tick(0);
  ConductorOut o = c.onLocalDone(1000);
  TEST_ASSERT_TRUE(sent(o, "END done"));
  TEST_ASSERT_EQUAL(hal::kEveryone, o.to);
  hal::Conductor tapped;
  tapped.startLive(0, kThree);
  o = tapped.interrupt(10);
  TEST_ASSERT_TRUE(sent(o, "END interrupted"));
  TEST_ASSERT_EQUAL(hal::kEveryone, o.to);
}

void test_group_hal_zero_cannot_leave_his_own_conversation() {
  hal::Conductor c;
  c.startLive(0, kThree);
  ConductorOut o = c.leave(hal::kZero, "busy", 10);
  TEST_ASSERT_FALSE(o.finished);
  TEST_ASSERT_EQUAL(-1, o.left);
  TEST_ASSERT_EQUAL(int(kThree), int(c.present()));
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
  RUN_TEST(test_live_missing_line_goes_back_to_waiting);
  RUN_TEST(test_live_survives_the_clock_wrapping);
  RUN_TEST(test_live_times_out);
  RUN_TEST(test_append_after_end);
  RUN_TEST(test_live_is_capped);
  RUN_TEST(test_live_interrupt_and_offline);
  RUN_TEST(test_scripted_is_not_live);
  RUN_TEST(test_pair_is_the_default);
  RUN_TEST(test_group_lines_go_to_their_speakers);
  RUN_TEST(test_group_only_those_present_can_be_given_a_line);
  RUN_TEST(test_group_replies_count_only_from_the_speaker);
  RUN_TEST(test_group_a_busy_hal_leaves_and_the_rest_go_on);
  RUN_TEST(test_group_down_to_one_ends_as_it_always_did);
  RUN_TEST(test_group_the_silent_one_is_dropped_and_told);
  RUN_TEST(test_group_a_bystander_hanging_up_changes_nothing_for_the_speaker);
  RUN_TEST(test_group_lines_of_someone_who_left_are_skipped);
  RUN_TEST(test_group_end_from_anyone_ends_it_for_all);
  RUN_TEST(test_group_endings_are_for_everyone);
  RUN_TEST(test_group_hal_zero_cannot_leave_his_own_conversation);
  return UNITY_END();
}
