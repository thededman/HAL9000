#include <unity.h>

#include <vector>

#include "hal_brain.h"

using hal::Folder;
using hal::Mode;
using hal::MotionEvent;

void setUp() {}
void tearDown() {}

namespace {

// Drives the brain like the firmware does, with a fake speaker:
// every clip lasts `clipMs` unless stopped.
struct Sim {
  hal::Brain brain;
  hal::Inputs in;
  hal::Actions last;
  std::vector<hal::Actions> plays;
  int newConsoles = 0;
  int clipMs = 1000;
  int audioLeft = 0;
  long darkAt = -1;
  long restartAt = -1;
  std::vector<int> dialogueLines, dialogueDones;
  int ends = 0;

  explicit Sim(const hal::Config& c = hal::Config(), bool afterShutdown = false, int plea = 4, int daisy = 4)
      : brain(c, 1, afterShutdown) {
    brain.setSequenceLengths(plea, daisy);
    at(12, 0);
  }
  void at(int hour, int minute) {
    in.timeValid = true;
    in.hour = hour;
    in.minuteOfDay = hour * 60 + minute;
  }
  void run(uint32_t ms, uint32_t step = 20) {
    for (uint32_t i = 0; i < ms / step; ++i, in.nowMs += step) {  // counts steps: survives millis() wrap
      in.audioPlaying = audioLeft > 0;
      last = brain.tick(in);
      if (last.stopAudio) audioLeft = 0;
      if (last.play) {
        plays.push_back(last);
        audioLeft = clipMs;
      }
      if (last.newConsole) ++newConsoles;
      if (last.dialogueLine > 0) {
        dialogueLines.push_back(last.dialogueLine);
        audioLeft = clipMs;
      }
      if (last.dialogueDone > 0) dialogueDones.push_back(last.dialogueDone);
      if (last.endConversation) ++ends;
      if (brain.mode() == Mode::Dark && darkAt < 0) darkAt = in.nowMs;
      if (last.restart && restartAt < 0) restartAt = in.nowMs;
      in.proximityEdge = false;  // edges, motion and requests last one tick
      in.motion = MotionEvent::None;
      in.sayLine = -1;
      in.linkFailed = false;
      audioLeft -= step;
    }
  }
  void tap() {
    in.touchDown = true;
    run(100);
    in.touchDown = false;
    run(20);
  }
  int playsOf(Folder f) const {
    int n = 0;
    for (auto& p : plays) n += (p.folder == f);
    return n;
  }
  const hal::Actions& lastPlay() const { return plays.back(); }
};

hal::Config noIdle() {
  hal::Config c;
  c.idleMinMinutes = c.idleMaxMinutes = 1440;
  return c;
}

}  // namespace

void test_boot_plays_boot_then_idle() {
  Sim s;
  s.run(20);
  TEST_ASSERT_EQUAL(1, s.plays.size());
  TEST_ASSERT_TRUE(s.lastPlay().folder == Folder::Boot);
  TEST_ASSERT_TRUE(s.brain.mode() == Mode::Speaking);
  s.run(1500);
  TEST_ASSERT_TRUE(s.brain.mode() == Mode::Idle);
}

void test_after_shutdown_plays_reboot() {
  Sim s(hal::Config(), true);
  s.run(20);
  TEST_ASSERT_TRUE(s.lastPlay().folder == Folder::Reboot);
}

void test_tap_starts_console_and_tap_line() {
  Sim s;
  s.run(1500);
  s.tap();
  TEST_ASSERT_TRUE(s.lastPlay().folder == Folder::Tap);
  TEST_ASSERT_EQUAL(-1, s.lastPlay().index);
  TEST_ASSERT_EQUAL(1, s.newConsoles);
  TEST_ASSERT_TRUE(s.brain.mode() == Mode::Console);
}

void test_tap_in_console_skips() {
  Sim s;
  s.run(1500);
  s.tap();
  s.run(1000);
  s.tap();
  TEST_ASSERT_EQUAL(2, s.newConsoles);
  TEST_ASSERT_EQUAL(2, s.playsOf(Folder::Tap));
  TEST_ASSERT_TRUE(s.brain.mode() == Mode::Console);
}

void test_console_ends_after_8s_and_audio() {
  Sim s;
  s.run(1500);
  s.tap();
  s.run(7000);
  TEST_ASSERT_TRUE(s.brain.mode() == Mode::Console);
  s.run(1200);
  TEST_ASSERT_TRUE(s.brain.mode() == Mode::Idle);

  s.clipMs = 10000;  // a long line keeps the console up until it ends
  s.tap();
  s.run(9000);
  TEST_ASSERT_TRUE(s.brain.mode() == Mode::Console);
  s.run(1500);
  TEST_ASSERT_TRUE(s.brain.mode() == Mode::Idle);
}

void test_hold_starts_plea_index0() {
  Sim s(hal::Config(), false, 2, 4);
  s.run(1500);
  s.in.touchDown = true;
  s.run(2980);
  TEST_ASSERT_TRUE(s.brain.mode() == Mode::Idle);
  s.run(60);
  TEST_ASSERT_TRUE(s.brain.mode() == Mode::Pleading);
  TEST_ASSERT_TRUE(s.lastPlay().folder == Folder::Plea);
  TEST_ASSERT_EQUAL(0, s.lastPlay().index);
  TEST_ASSERT_TRUE(s.last.stopAudio || s.plays.size() == 2);
}

void test_release_during_plea_relief() {
  Sim s(hal::Config(), false, 2, 4);
  s.run(1500);
  s.in.touchDown = true;
  s.run(3500);
  s.in.touchDown = false;
  s.run(20);
  TEST_ASSERT_TRUE(s.lastPlay().folder == Folder::Relief);
  TEST_ASSERT_TRUE(s.brain.mode() == Mode::Speaking);
  s.run(1500);
  TEST_ASSERT_TRUE(s.brain.mode() == Mode::Idle);
}

void test_full_shutdown_sequence() {
  Sim s(hal::Config(), false, 2, 4);
  s.run(1500);
  s.in.touchDown = true;
  s.run(3000 + 2 * 1300 + 4 * 1300);
  TEST_ASSERT_TRUE(s.brain.mode() == Mode::Dark);
  // after the boot line: Plea 0, Plea 1, Daisy 0..3 in order
  TEST_ASSERT_EQUAL(7, s.plays.size());
  const Folder want[] = {Folder::Plea, Folder::Plea, Folder::Daisy, Folder::Daisy, Folder::Daisy, Folder::Daisy};
  const int idx[] = {0, 1, 0, 1, 2, 3};
  for (int i = 0; i < 6; ++i) {
    TEST_ASSERT_TRUE(s.plays[i + 1].folder == want[i]);
    TEST_ASSERT_EQUAL(idx[i], s.plays[i + 1].index);
  }
  TEST_ASSERT_EQUAL_FLOAT(1.0f, s.brain.daisyProgress());
  s.run(6000);
  TEST_ASSERT_TRUE(s.restartAt >= s.darkAt + 5000);
  TEST_ASSERT_TRUE(s.restartAt < s.darkAt + 5100);
}

void test_touch_ignored_in_daisy() {
  Sim s(hal::Config(), false, 0, 4);
  s.clipMs = 3000;
  s.run(3500);
  s.in.touchDown = true;
  s.run(3100);
  TEST_ASSERT_TRUE(s.brain.mode() == Mode::Daisy);
  s.in.touchDown = false;
  s.run(100);
  s.tap();
  TEST_ASSERT_TRUE(s.brain.mode() == Mode::Daisy);
  TEST_ASSERT_EQUAL(0, s.playsOf(Folder::Relief));
  TEST_ASSERT_EQUAL(0, s.newConsoles);
}

void test_missing_plea_goes_to_daisy() {
  Sim s(hal::Config(), false, 0, 4);
  s.run(1500);
  s.in.touchDown = true;
  s.run(3100);
  TEST_ASSERT_TRUE(s.brain.mode() == Mode::Daisy);
  TEST_ASSERT_TRUE(s.lastPlay().folder == Folder::Daisy);
  TEST_ASSERT_EQUAL(0, s.lastPlay().index);
}

void test_greet_bucket_and_cooldown() {
  Sim s(noIdle());
  s.at(9, 0);
  s.run(1500);
  s.in.proximityEdge = true;
  s.run(20);
  TEST_ASSERT_TRUE(s.lastPlay().folder == Folder::GreetMorning);
  s.run(5 * 60000, 1000);
  s.in.proximityEdge = true;
  s.run(20);
  TEST_ASSERT_EQUAL(1, s.playsOf(Folder::GreetMorning));
  s.run(6 * 60000, 1000);
  s.in.proximityEdge = true;
  s.run(20);
  TEST_ASSERT_EQUAL(2, s.playsOf(Folder::GreetMorning));

  Sim a(noIdle());
  a.at(13, 0);
  a.run(1500);
  a.in.proximityEdge = true;
  a.run(20);
  TEST_ASSERT_TRUE(a.lastPlay().folder == Folder::GreetAfternoon);

  Sim e(noIdle());
  e.at(19, 0);
  e.run(1500);
  e.in.proximityEdge = true;
  e.run(20);
  TEST_ASSERT_TRUE(e.lastPlay().folder == Folder::GreetEvening);
}

void test_no_greet_in_quiet_hours() {
  Sim s(noIdle());
  s.at(23, 0);
  s.run(1500);
  s.in.proximityEdge = true;
  s.run(20);
  TEST_ASSERT_EQUAL(1, s.plays.size());  // just the boot line
}

void test_unknown_time_greets_afternoon_and_never_quiet() {
  Sim s(noIdle());
  s.at(23, 0);
  s.in.timeValid = false;
  s.run(1500);
  TEST_ASSERT_FALSE(s.last.quiet);
  TEST_ASSERT_EQUAL(180, s.last.volume);
  s.in.proximityEdge = true;
  s.run(20);
  TEST_ASSERT_TRUE(s.lastPlay().folder == Folder::GreetAfternoon);
}

void test_shake_interrupts_speaking_with_cooldown() {
  Sim s(noIdle());
  s.clipMs = 5000;
  s.run(100);  // boot line still playing
  s.in.motion = MotionEvent::Shake;
  s.run(20);
  TEST_ASSERT_TRUE(s.last.stopAudio);
  TEST_ASSERT_TRUE(s.lastPlay().folder == Folder::Shake);
  s.run(5000);
  s.in.motion = MotionEvent::TipOver;
  s.run(20);
  TEST_ASSERT_EQUAL(1, s.playsOf(Folder::Shake));
  s.run(16000);
  s.in.motion = MotionEvent::Pickup;
  s.run(20);
  TEST_ASSERT_EQUAL(2, s.playsOf(Folder::Shake));
}

void test_idle_remark_fires_and_reschedules() {
  hal::Config c;
  c.idleMinMinutes = c.idleMaxMinutes = 20;
  Sim s(c);
  s.run(20 * 60000 - 2000, 1000);
  TEST_ASSERT_EQUAL(0, s.playsOf(Folder::Idle));
  s.run(3000, 1000);
  TEST_ASSERT_EQUAL(1, s.playsOf(Folder::Idle));
  s.run(20 * 60000 - 3000, 1000);
  TEST_ASSERT_EQUAL(1, s.playsOf(Folder::Idle));
  s.run(3000, 1000);
  TEST_ASSERT_EQUAL(2, s.playsOf(Folder::Idle));
}

// millis() wraps after ~49.7 days; a remark scheduled across the wrap must still wait its full delay.
void test_idle_remark_survives_millis_rollover() {
  hal::Config c;
  c.idleMinMinutes = c.idleMaxMinutes = 20;
  Sim s(c);
  s.in.nowMs = 0xFFFFFFFFu - 10 * 60000u;  // 10 minutes before the wrap
  s.run(19 * 60000, 1000);
  TEST_ASSERT_EQUAL(0, s.playsOf(Folder::Idle));
  s.run(2 * 60000, 1000);
  TEST_ASSERT_EQUAL(1, s.playsOf(Folder::Idle));
}

// --- Conversations between the two HALs ---

void test_conversing_suppresses_idle_greet_shake() {
  hal::Config c;
  c.idleMinMinutes = c.idleMaxMinutes = 6;
  Sim s(c);
  s.run(1500);
  s.in.conversing = true;
  s.in.proximityEdge = true;
  s.run(20);
  s.in.motion = MotionEvent::Shake;
  s.run(20);
  s.run(8 * 60000, 1000);  // an idle remark would be due by now
  TEST_ASSERT_EQUAL(1, s.plays.size());  // only the boot line
  s.in.conversing = false;
  s.run(2000, 1000);
  TEST_ASSERT_EQUAL(2, s.plays.size());  // once the conversation is over, he chats again
}

void test_say_line_plays_dialogue_line() {
  Sim s;
  s.run(1500);
  s.in.conversing = true;
  s.in.sayLine = 3;
  s.run(20);
  TEST_ASSERT_EQUAL(3, s.last.dialogueLine);
  TEST_ASSERT_FALSE(s.last.play);
  TEST_ASSERT_TRUE(s.brain.mode() == Mode::Speaking);
}

void test_dialogue_done_reported_once() {
  Sim s;
  s.run(1500);
  s.in.conversing = true;
  s.in.sayLine = 3;
  s.run(20);
  s.run(3000);
  TEST_ASSERT_EQUAL(1, s.dialogueDones.size());
  TEST_ASSERT_EQUAL(3, s.dialogueDones[0]);
  TEST_ASSERT_TRUE(s.brain.mode() == Mode::Idle);
}

void test_say_interrupts_what_he_is_saying() {
  Sim s;
  s.clipMs = 5000;
  s.run(100);  // boot line still playing
  s.in.conversing = true;
  s.in.sayLine = 2;
  s.run(20);
  TEST_ASSERT_TRUE(s.last.stopAudio);
  TEST_ASSERT_EQUAL(2, s.last.dialogueLine);
}

void test_tap_while_conversing_ends_conversation() {
  Sim s;
  s.run(1500);
  s.in.conversing = true;
  s.in.sayLine = 1;
  s.run(20);
  s.tap();
  TEST_ASSERT_EQUAL(1, s.ends);
  TEST_ASSERT_EQUAL(1, s.newConsoles);
  TEST_ASSERT_TRUE(s.lastPlay().folder == Folder::Tap);
  TEST_ASSERT_EQUAL(0, s.dialogueDones.size());  // cut short, not finished
}

void test_hold_while_conversing_pleads_and_ends() {
  Sim s(hal::Config(), false, 2, 4);
  s.run(1500);
  s.in.conversing = true;
  s.in.touchDown = true;
  s.run(3100);
  TEST_ASSERT_TRUE(s.brain.mode() == Mode::Pleading);
  TEST_ASSERT_EQUAL(1, s.ends);
}

void test_cannot_converse_while_pleading_or_dark() {
  Sim s(hal::Config(), false, 2, 0);
  s.run(1500);
  TEST_ASSERT_TRUE(s.brain.canConverse());
  s.in.touchDown = true;
  s.run(3100);
  TEST_ASSERT_FALSE(s.brain.canConverse());
  s.in.sayLine = 1;
  s.run(20);
  TEST_ASSERT_EQUAL(0, s.dialogueLines.size());
}

void test_link_failed_plays_rival_line() {
  Sim s;
  s.run(1500);
  s.in.linkFailed = true;
  s.run(20);
  TEST_ASSERT_TRUE(s.lastPlay().folder == Folder::Rival);
  TEST_ASSERT_TRUE(s.brain.mode() == Mode::Speaking);
}

void test_quiet_volume() {
  Sim s;
  s.at(23, 0);
  s.run(20);
  TEST_ASSERT_TRUE(s.last.quiet);
  TEST_ASSERT_EQUAL(90, s.last.volume);
  s.at(12, 0);
  s.run(20);
  TEST_ASSERT_FALSE(s.last.quiet);
  TEST_ASSERT_EQUAL(180, s.last.volume);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_boot_plays_boot_then_idle);
  RUN_TEST(test_after_shutdown_plays_reboot);
  RUN_TEST(test_tap_starts_console_and_tap_line);
  RUN_TEST(test_tap_in_console_skips);
  RUN_TEST(test_console_ends_after_8s_and_audio);
  RUN_TEST(test_hold_starts_plea_index0);
  RUN_TEST(test_release_during_plea_relief);
  RUN_TEST(test_full_shutdown_sequence);
  RUN_TEST(test_touch_ignored_in_daisy);
  RUN_TEST(test_missing_plea_goes_to_daisy);
  RUN_TEST(test_greet_bucket_and_cooldown);
  RUN_TEST(test_no_greet_in_quiet_hours);
  RUN_TEST(test_unknown_time_greets_afternoon_and_never_quiet);
  RUN_TEST(test_shake_interrupts_speaking_with_cooldown);
  RUN_TEST(test_idle_remark_fires_and_reschedules);
  RUN_TEST(test_idle_remark_survives_millis_rollover);
  RUN_TEST(test_conversing_suppresses_idle_greet_shake);
  RUN_TEST(test_say_line_plays_dialogue_line);
  RUN_TEST(test_dialogue_done_reported_once);
  RUN_TEST(test_say_interrupts_what_he_is_saying);
  RUN_TEST(test_tap_while_conversing_ends_conversation);
  RUN_TEST(test_hold_while_conversing_pleads_and_ends);
  RUN_TEST(test_cannot_converse_while_pleading_or_dark);
  RUN_TEST(test_link_failed_plays_rival_line);
  RUN_TEST(test_quiet_volume);
  return UNITY_END();
}
