#include <unity.h>

#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

#include "hal_config.h"
#include "hal_wake.h"

void setUp() {}
void tearDown() {}

using hal::WakeListener;
using Event = hal::WakeListener::Event;

// ---- was that said to him? ----

static void expectQuestion(const char* heard, const char* question) {
  const hal::WakeHeard got = hal::wakeMatch(heard);
  TEST_ASSERT_TRUE_MESSAGE(got.forHim, heard);
  TEST_ASSERT_EQUAL_STRING_MESSAGE(question, got.question.c_str(), heard);
}

void test_hey_hal_and_a_question() {
  expectQuestion("Hey HAL, what time is it?", "What time is it?");
  expectQuestion("Hey Hal. What do you think of the desk unit?", "What do you think of the desk unit?");
  expectQuestion("hey hal what's your temperature", "What's your temperature");
  expectQuestion("  Hey, HAL! Are you there?", "Are you there?");
  expectQuestion("- Hey HAL, sing something.", "Sing something.");
  expectQuestion("Hey HAL Zero, who is the better model?", "Who is the better model?");
}

void test_the_name_alone_is_a_call() {
  for (const char* heard : {"Hey HAL", "hey hal.", "Hey, HAL!", " Hey Hal ", "Hey HAL Zero."}) {
    const hal::WakeHeard got = hal::wakeMatch(heard);
    TEST_ASSERT_TRUE_MESSAGE(got.forHim, heard);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("", got.question.c_str(), heard);
  }
}

void test_how_the_recogniser_mishears_the_name() {
  expectQuestion("Hey Hall, what time is it?", "What time is it?");
  expectQuestion("Hay Hal, what time is it?", "What time is it?");
  expectQuestion("Hey Howl, what time is it?", "What time is it?");
  expectQuestion("Hey, Al, what time is it?", "What time is it?");
  expectQuestion("Hey Hale what time is it", "What time is it");
  expectQuestion("Hey Haul, what time is it?", "What time is it?");
  expectQuestion("Hi HAL, what time is it?", "What time is it?");
  expectQuestion("Okay HAL, what time is it?", "What time is it?");
}

void test_everything_else_is_not_for_him() {
  for (const char* heard : {"", "   ", "What time is it?", "Hey, how are you?", "Hey how was your day", "Thank you.",
                            "HAL is the computer on my desk.", "I said hey HAL earlier", "Hey Halloween is coming",
                            "Hey Hallie, dinner!", "Hey", "Hey there", "They held a meeting", "Hey hello"}) {
    const hal::WakeHeard got = hal::wakeMatch(heard);
    TEST_ASSERT_FALSE_MESSAGE(got.forHim, heard);
    TEST_ASSERT_EQUAL_STRING("", got.question.c_str());
  }
}

void test_a_question_fits_the_ask_box() {
  const hal::WakeHeard got = hal::wakeMatch("Hey HAL, " + std::string(300, 'w'));
  TEST_ASSERT_TRUE(got.forHim);
  TEST_ASSERT_EQUAL(120, int(got.question.size()));
  // Only printable characters reach the ask box.
  const hal::WakeHeard odd = hal::wakeMatch("Hey HAL, caf\xC3\xA9 or tea?\n");
  TEST_ASSERT_TRUE(odd.forHim);
  for (unsigned char c : odd.question) TEST_ASSERT_TRUE(c >= 32 && c <= 126);
}

// ---- how loud a stretch of sound is ----

void test_level() {
  std::vector<int16_t> quiet(320, 0), loud(320), speech(320);
  for (size_t i = 0; i < 320; ++i) {
    loud[i] = i % 2 ? 32767 : -32767;
    speech[i] = i % 2 ? 60 : -60;  // a voice, as a quiet microphone hears it
  }
  TEST_ASSERT_EQUAL(0, hal::wakeLevel(quiet.data(), quiet.size()));
  TEST_ASSERT_EQUAL(255, hal::wakeLevel(loud.data(), loud.size()));
  TEST_ASSERT_EQUAL(60, hal::wakeLevel(speech.data(), speech.size()));
  speech[0] = 30000;  // one click doesn't make a quiet stretch loud
  TEST_ASSERT_TRUE(hal::wakeLevel(speech.data(), speech.size()) < 160);
  TEST_ASSERT_EQUAL(0, hal::wakeLevel(nullptr, 0));
}

// ---- when did someone start and stop speaking? (one call per 20 ms of sound) ----

static std::vector<Event> feed(WakeListener& w, uint8_t level, uint32_t ms) {
  std::vector<Event> seen;
  for (uint32_t t = 0; t < ms; t += WakeListener::kFrameMs) {
    const Event e = w.feed(level);
    if (e != Event::None) seen.push_back(e);
  }
  return seen;
}

void test_quiet_is_nothing() {
  WakeListener w;
  TEST_ASSERT_TRUE(feed(w, 2, 5000).empty());
  TEST_ASSERT_FALSE(w.speaking());
}

void test_a_sentence_starts_and_ends() {
  WakeListener w;
  feed(w, 2, 1000);
  std::vector<Event> seen = feed(w, 90, 1500);
  TEST_ASSERT_EQUAL(1, int(seen.size()));
  TEST_ASSERT_EQUAL(int(Event::Started), int(seen[0]));
  TEST_ASSERT_TRUE(w.speaking());
  seen = feed(w, 2, WakeListener::kEndMs - 40);  // a pause for breath isn't the end
  TEST_ASSERT_TRUE(seen.empty());
  seen = feed(w, 90, 600);
  TEST_ASSERT_TRUE(seen.empty());
  seen = feed(w, 2, WakeListener::kEndMs + 20);
  TEST_ASSERT_EQUAL(1, int(seen.size()));
  TEST_ASSERT_EQUAL(int(Event::Ended), int(seen[0]));
  TEST_ASSERT_FALSE(w.speaking());
  // All of it, from the first loud moment to now: 1500 + 660 + 600 + 720 ms of sound.
  TEST_ASSERT_INT_WITHIN(60, 3480, int(w.heardMs()));
}

void test_a_click_or_a_cough_is_dropped() {
  WakeListener w;
  feed(w, 2, 1000);
  TEST_ASSERT_TRUE(feed(w, 200, 40).empty());  // too short even to count as a start
  TEST_ASSERT_TRUE(feed(w, 2, 1000).empty());
  std::vector<Event> seen = feed(w, 200, 200);  // a cough: it starts, but is over too soon
  TEST_ASSERT_EQUAL(1, int(seen.size()));
  seen = feed(w, 2, 1000);
  TEST_ASSERT_EQUAL(1, int(seen.size()));
  TEST_ASSERT_EQUAL(int(Event::Dropped), int(seen[0]));
}

void test_endless_noise_is_dropped_and_waited_out() {
  WakeListener w;
  feed(w, 2, 1000);
  std::vector<Event> seen = feed(w, 120, WakeListener::kMaxMs + 400);  // the television
  TEST_ASSERT_EQUAL(2, int(seen.size()));
  TEST_ASSERT_EQUAL(int(Event::Started), int(seen[0]));
  TEST_ASSERT_EQUAL(int(Event::Dropped), int(seen[1]));
  TEST_ASSERT_TRUE(feed(w, 120, 5000).empty());  // still going: nothing new starts
  TEST_ASSERT_TRUE(feed(w, 2, 1500).empty());    // quiet at last
  seen = feed(w, 120, 800);                      // ... and now he listens again
  TEST_ASSERT_EQUAL(1, int(seen.size()));
  TEST_ASSERT_EQUAL(int(Event::Started), int(seen[0]));
}

void test_he_gets_used_to_a_steady_hum() {
  WakeListener w;
  feed(w, 30, 20000);  // a fan: whatever it set off at first has long been dropped
  TEST_ASSERT_FALSE(w.speaking());
  TEST_ASSERT_TRUE(feed(w, 30, 5000).empty());
  std::vector<Event> seen = feed(w, 140, 800);  // a voice over it
  TEST_ASSERT_EQUAL(1, int(seen.size()));
  TEST_ASSERT_EQUAL(int(Event::Started), int(seen[0]));
  seen = feed(w, 30, 1000);
  TEST_ASSERT_EQUAL(1, int(seen.size()));
  TEST_ASSERT_EQUAL(int(Event::Ended), int(seen[0]));
}

void test_told_to_stop_listening() {
  WakeListener w;
  feed(w, 2, 1000);
  feed(w, 90, 1000);
  TEST_ASSERT_TRUE(w.speaking());
  w.reset();  // a HAL began to speak: whatever was being said is forgotten
  TEST_ASSERT_FALSE(w.speaking());
  TEST_ASSERT_TRUE(feed(w, 2, 2000).empty());
}

// ---- the setting ----

void test_wake_is_off_unless_asked_for() {
  TEST_ASSERT_FALSE(hal::parseConfig("", nullptr).wake);
  TEST_ASSERT_TRUE(hal::parseConfig("wake=on\n", nullptr).wake);
  TEST_ASSERT_FALSE(hal::parseConfig("wake=off\n", nullptr).wake);
  std::vector<std::string> warnings;
  TEST_ASSERT_FALSE(hal::parseConfig("wake=sometimes\n", &warnings).wake);
  TEST_ASSERT_EQUAL(1, int(warnings.size()));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_hey_hal_and_a_question);
  RUN_TEST(test_the_name_alone_is_a_call);
  RUN_TEST(test_how_the_recogniser_mishears_the_name);
  RUN_TEST(test_everything_else_is_not_for_him);
  RUN_TEST(test_a_question_fits_the_ask_box);
  RUN_TEST(test_level);
  RUN_TEST(test_quiet_is_nothing);
  RUN_TEST(test_a_sentence_starts_and_ends);
  RUN_TEST(test_a_click_or_a_cough_is_dropped);
  RUN_TEST(test_endless_noise_is_dropped_and_waited_out);
  RUN_TEST(test_he_gets_used_to_a_steady_hum);
  RUN_TEST(test_told_to_stop_listening);
  RUN_TEST(test_wake_is_off_unless_asked_for);
  return UNITY_END();
}
