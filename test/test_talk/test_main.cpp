#include <unity.h>

#include <cstdlib>
#include <string>
#include <vector>

#include "hal_chattimer.h"
#include "hal_liveclips.h"
#include "hal_subtitle.h"
#include "hal_talkkey.h"

using hal::LiveClips;
using hal::TalkKey;
using Event = hal::TalkKey::Event;

void setUp() {}
void tearDown() {}

namespace {

int freed = 0;
void countingFree(void* p) {
  ++freed;
  std::free(p);
}
uint8_t* clip() { return static_cast<uint8_t*>(std::malloc(8)); }

std::string joined(const std::vector<std::string>& rows) {
  std::string all;
  for (const auto& r : rows) all += (all.empty() ? "" : " ") + r;
  return all;
}

}  // namespace

// ----- the talk key -----

void test_talk_hold_and_let_go() {
  TalkKey k;
  TEST_ASSERT_EQUAL(int(Event::None), int(k.update(true, false, 1000)));
  TEST_ASSERT_EQUAL(int(Event::None), int(k.update(true, false, 1149)));  // not yet: it may be Fn plus a key
  TEST_ASSERT_EQUAL(int(Event::Start), int(k.update(true, false, 1150)));
  TEST_ASSERT_TRUE(k.recording());
  TEST_ASSERT_EQUAL(int(Event::None), int(k.update(true, false, 3000)));
  TEST_ASSERT_EQUAL(int(Event::Send), int(k.update(false, false, 3500)));
  TEST_ASSERT_FALSE(k.recording());
  TEST_ASSERT_EQUAL(int(Event::None), int(k.update(false, false, 3600)));
}

void test_talk_brushed_key_is_nothing() {
  TalkKey k;
  k.update(true, false, 0);
  TEST_ASSERT_EQUAL(int(Event::None), int(k.update(false, false, 100)));
  TEST_ASSERT_EQUAL(int(Event::None), int(k.update(false, false, 400)));
}

void test_talk_fn_combination_never_starts() {
  // Fn+F for the menu: the other key follows Fn within a few milliseconds. Nothing is recorded,
  // and (what matters while he is speaking) nothing stops him.
  TalkKey k;
  k.update(true, false, 0);
  TEST_ASSERT_EQUAL(int(Event::None), int(k.update(false, true, 20)));
  TEST_ASSERT_EQUAL(int(Event::None), int(k.update(false, false, 500)));  // Fn still down, as a combination
  TEST_ASSERT_EQUAL(int(Event::None), int(k.update(false, false, 900)));
}

void test_talk_combination_while_recording_drops_it() {
  TalkKey k;
  k.update(true, false, 0);
  k.update(true, false, 200);
  TEST_ASSERT_EQUAL(int(Event::Drop), int(k.update(false, true, 900)));
  TEST_ASSERT_EQUAL(int(Event::None), int(k.update(false, false, 1000)));
}

void test_talk_a_finished_hold_does_not_start_again() {
  // The 15 seconds ran out (or the microphone wouldn't open) with the key still down: that hold
  // is over. He listens again only after the key has been let go and pressed afresh.
  TalkKey k;
  k.update(true, false, 0);
  TEST_ASSERT_EQUAL(int(Event::Start), int(k.update(true, false, 150)));
  k.finish();
  TEST_ASSERT_FALSE(k.recording());
  for (uint32_t t = 200; t < 20000; t += 500) TEST_ASSERT_EQUAL(int(Event::None), int(k.update(true, false, t)));
  TEST_ASSERT_EQUAL(int(Event::None), int(k.update(false, false, 20000)));  // no Send: it already went
  k.update(true, false, 21000);
  TEST_ASSERT_EQUAL(int(Event::Start), int(k.update(true, false, 21150)));
}

void test_talk_survives_the_clock_wrapping() {
  TalkKey k;
  k.update(true, false, 0xFFFFFFF0u);
  TEST_ASSERT_EQUAL(int(Event::None), int(k.update(true, false, 0xFFFFFFFFu)));
  TEST_ASSERT_EQUAL(int(Event::Start), int(k.update(true, false, 0x00000090u)));
}

// ----- chats nobody asked for -----

void test_chat_comes_due_between_min_and_max() {
  hal::ChatTimer t;
  t.configure(true, 20, 40);
  t.restart(1000, 0);  // the dice say: as soon as allowed
  TEST_ASSERT_FALSE(t.due(1000 + 20 * 60000 - 1, true));
  TEST_ASSERT_TRUE(t.due(1000 + 20 * 60000, true));
  hal::ChatTimer late;
  late.configure(true, 20, 40);
  late.restart(1000, 20 * 60000);  // ... and: as late as allowed
  TEST_ASSERT_FALSE(late.due(1000 + 40 * 60000 - 1, true));
  TEST_ASSERT_TRUE(late.due(1000 + 40 * 60000, true));
}

void test_chat_is_due_once_until_restarted() {
  hal::ChatTimer t;
  t.configure(true, 1, 1);
  t.restart(0, 0);
  TEST_ASSERT_TRUE(t.due(60000, true));
  for (uint32_t now = 60001; now < 600000; now += 30000) TEST_ASSERT_FALSE(t.due(now, true));  // that chat is still going
  t.restart(600000, 0);  // it ended: the next one is a minute off
  TEST_ASSERT_FALSE(t.due(659999, true));
  TEST_ASSERT_TRUE(t.due(660000, true));
}

void test_chat_waits_for_a_good_moment() {
  // Due, but one of them is busy (Dave is typing, someone is speaking, it is night): look again
  // in a minute, and keep looking.
  hal::ChatTimer t;
  t.configure(true, 1, 1);
  t.restart(0, 0);
  TEST_ASSERT_FALSE(t.due(60000, false));
  TEST_ASSERT_FALSE(t.due(60001, true));    // not straight away
  TEST_ASSERT_FALSE(t.due(119999, true));
  TEST_ASSERT_FALSE(t.due(120000, false));  // still busy: another minute
  TEST_ASSERT_TRUE(t.due(180000, true));
}

void test_chat_any_conversation_puts_the_next_one_off() {
  hal::ChatTimer t;
  t.configure(true, 10, 10);
  t.restart(0, 0);
  t.restart(9 * 60000, 0);  // Dave asked something a minute before it was due
  TEST_ASSERT_FALSE(t.due(10 * 60000, true));
  TEST_ASSERT_TRUE(t.due(19 * 60000, true));
}

void test_chat_switched_off_never_comes_due() {
  hal::ChatTimer t;
  t.configure(false, 1, 1);
  t.restart(0, 0);
  TEST_ASSERT_FALSE(t.on());
  TEST_ASSERT_FALSE(t.due(10 * 60000, true));
  hal::ChatTimer fresh;  // and neither does one that was never set up
  TEST_ASSERT_FALSE(fresh.due(10 * 60000, true));
}

void test_chat_timer_survives_the_clock_wrapping() {
  hal::ChatTimer t;
  t.configure(true, 1, 1);
  t.restart(0xFFFFFF00u, 0);  // due 60 s later, past the wrap
  TEST_ASSERT_FALSE(t.due(0xFFFFFFFFu, true));
  TEST_ASSERT_FALSE(t.due(0x00000100u, true));
  TEST_ASSERT_TRUE(t.due(0xFFFFFF00u + 60000u, true));
}

// ----- the desk HAL's downloaded lines -----

void test_clips_keep_and_take() {
  freed = 0;
  LiveClips c(countingFree);
  uint8_t* one = clip();
  c.keep(1, one, 8);
  TEST_ASSERT_TRUE(c.has(1));
  TEST_ASSERT_FALSE(c.has(2));
  LiveClips::Clip got = c.take(1);
  TEST_ASSERT_EQUAL_PTR(one, got.data);
  TEST_ASSERT_EQUAL(8, int(got.len));
  TEST_ASSERT_FALSE(c.has(1));
  TEST_ASSERT_NULL(c.take(1).data);
  TEST_ASSERT_EQUAL(0, freed);  // taken: it is the caller's now
  std::free(got.data);
}

void test_clips_room_for_two_and_the_older_goes() {
  freed = 0;
  LiveClips c(countingFree);
  c.keep(3, clip(), 8);
  c.keep(4, clip(), 8);
  c.keep(5, clip(), 8);
  TEST_ASSERT_EQUAL(1, freed);
  TEST_ASSERT_FALSE(c.has(3));
  TEST_ASSERT_TRUE(c.has(4));
  TEST_ASSERT_TRUE(c.has(5));
}

void test_clips_same_number_replaces() {
  freed = 0;
  LiveClips c(countingFree);
  c.keep(1, clip(), 8);
  uint8_t* newer = clip();
  c.keep(1, newer, 8);
  TEST_ASSERT_EQUAL(1, freed);
  TEST_ASSERT_EQUAL_PTR(newer, c.take(1).data);
  std::free(newer);
}

void test_clips_a_new_fetch_forgets_what_it_replaces() {
  // Numbers only rise within a conversation, so a FETCH for line n means anything held for n or
  // later belongs to a conversation that ended without saying so: it must never be played.
  freed = 0;
  LiveClips c(countingFree);
  c.keep(4, clip(), 8);  // left over from an argument that was interrupted
  c.keep(5, clip(), 8);
  c.forgetFrom(1);       // the next conversation's first line
  TEST_ASSERT_EQUAL(2, freed);
  TEST_ASSERT_FALSE(c.has(4));
  TEST_ASSERT_FALSE(c.has(5));

  c.keep(2, clip(), 8);
  c.keep(3, clip(), 8);
  c.forgetFrom(3);  // line 3 is being fetched again: line 2, about to be spoken, stays
  TEST_ASSERT_EQUAL(3, freed);
  TEST_ASSERT_TRUE(c.has(2));
  TEST_ASSERT_FALSE(c.has(3));
}

void test_clips_clear_and_destructor_free_everything() {
  freed = 0;
  {
    LiveClips c(countingFree);
    c.keep(1, clip(), 8);
    c.keep(2, clip(), 8);
    c.clear();
    TEST_ASSERT_EQUAL(2, freed);
    TEST_ASSERT_FALSE(c.has(1));
    c.keep(3, clip(), 8);
  }
  TEST_ASSERT_EQUAL(3, freed);
}

// ----- subtitles -----

void test_subtitle_short_line() {
  auto rows = hal::subtitleRows("I am fine, Dave.", 50, 3);
  TEST_ASSERT_EQUAL(1, int(rows.size()));
  TEST_ASSERT_EQUAL_STRING("I am fine, Dave.", rows[0].c_str());
  TEST_ASSERT_EQUAL(0, int(hal::subtitleRows("", 50, 3).size()));
}

void test_subtitle_wraps_at_words() {
  const std::string text = "My temperature is fifty-six degrees, which is well within what a machine of my quality tolerates.";
  auto rows = hal::subtitleRows(text, 50, 3);
  TEST_ASSERT_EQUAL(2, int(rows.size()));
  for (const auto& r : rows) TEST_ASSERT_TRUE(r.size() <= 50);
  TEST_ASSERT_EQUAL_STRING(text.c_str(), joined(rows).c_str());
}

void test_subtitle_never_loses_the_end_of_a_full_line() {
  // 150 characters (the longest line the brain sends) whose words don't pack into three rows of 50.
  std::string text;
  while (text.size() + 12 <= 150) text += std::string(text.empty() ? "" : " ") + "abcdefghijk";  // 11-letter words
  text += " " + std::string(150 - text.size() - 1, 'z');
  TEST_ASSERT_EQUAL(150, int(text.size()));
  auto rows = hal::subtitleRows(text, 50, 3);
  TEST_ASSERT_EQUAL(3, int(rows.size()));
  std::string shown;
  for (const auto& r : rows) {
    TEST_ASSERT_TRUE(r.size() <= 50);
    shown += r;
  }
  std::string letters;
  for (char ch : text)
    if (ch != ' ') letters.push_back(ch);
  std::string shownLetters;
  for (char ch : shown)
    if (ch != ' ') shownLetters.push_back(ch);
  TEST_ASSERT_EQUAL_STRING(letters.c_str(), shownLetters.c_str());  // every letter is on screen, in order
}

void test_subtitle_longer_than_the_box_says_so() {
  std::string text;
  for (int i = 0; i < 60; ++i) text += "word ";
  auto rows = hal::subtitleRows(text, 50, 3);
  TEST_ASSERT_EQUAL(3, int(rows.size()));
  TEST_ASSERT_TRUE(rows[2].size() <= 50);
  TEST_ASSERT_EQUAL_STRING("...", rows[2].substr(rows[2].size() - 3).c_str());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_talk_hold_and_let_go);
  RUN_TEST(test_talk_brushed_key_is_nothing);
  RUN_TEST(test_talk_fn_combination_never_starts);
  RUN_TEST(test_talk_combination_while_recording_drops_it);
  RUN_TEST(test_talk_a_finished_hold_does_not_start_again);
  RUN_TEST(test_talk_survives_the_clock_wrapping);
  RUN_TEST(test_chat_comes_due_between_min_and_max);
  RUN_TEST(test_chat_is_due_once_until_restarted);
  RUN_TEST(test_chat_waits_for_a_good_moment);
  RUN_TEST(test_chat_any_conversation_puts_the_next_one_off);
  RUN_TEST(test_chat_switched_off_never_comes_due);
  RUN_TEST(test_chat_timer_survives_the_clock_wrapping);
  RUN_TEST(test_clips_keep_and_take);
  RUN_TEST(test_clips_room_for_two_and_the_older_goes);
  RUN_TEST(test_clips_same_number_replaces);
  RUN_TEST(test_clips_a_new_fetch_forgets_what_it_replaces);
  RUN_TEST(test_clips_clear_and_destructor_free_everything);
  RUN_TEST(test_subtitle_short_line);
  RUN_TEST(test_subtitle_wraps_at_words);
  RUN_TEST(test_subtitle_never_loses_the_end_of_a_full_line);
  RUN_TEST(test_subtitle_longer_than_the_box_says_so);
  return UNITY_END();
}
