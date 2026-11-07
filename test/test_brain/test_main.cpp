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
    for (uint32_t end = in.nowMs + ms; in.nowMs < end; in.nowMs += step) {
      in.audioPlaying = audioLeft > 0;
      last = brain.tick(in);
      if (last.stopAudio) audioLeft = 0;
      if (last.play) {
        plays.push_back(last);
        audioLeft = clipMs;
      }
      if (last.newConsole) ++newConsoles;
      if (brain.mode() == Mode::Dark && darkAt < 0) darkAt = in.nowMs;
      if (last.restart && restartAt < 0) restartAt = in.nowMs;
      in.proximityEdge = false;  // edges and motion last one tick
      in.motion = MotionEvent::None;
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
  RUN_TEST(test_quiet_volume);
  return UNITY_END();
}
