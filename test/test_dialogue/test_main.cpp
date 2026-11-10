#include <unity.h>

#include <string>
#include <vector>

#include "hal_dialogue.h"

using hal::Speaker;

static hal::Dialogue d;
static std::vector<std::string> err;

void setUp() {
  d = hal::Dialogue();
  err.clear();
}
void tearDown() {}

void test_alternating_lines() {
  TEST_ASSERT_TRUE(hal::parseDialogue("intro", "OLD: Hello.\nNEW:   Hi there.  \nOLD: Hm.\n", &d, &err));
  TEST_ASSERT_EQUAL(3, d.lines.size());
  TEST_ASSERT_TRUE(d.lines[0].who == Speaker::Old);
  TEST_ASSERT_TRUE(d.lines[1].who == Speaker::New);
  TEST_ASSERT_EQUAL_STRING("Hi there.", d.lines[1].text.c_str());
  TEST_ASSERT_EQUAL_STRING("intro", d.id.c_str());
}

void test_comments_bom_crlf() {
  TEST_ASSERT_TRUE(hal::parseDialogue("x", "\xEF\xBB\xBF# a note\r\n\r\nOLD: One.\r\n  # indented note\r\nNEW: Two.\r\n", &d, &err));
  TEST_ASSERT_EQUAL(2, d.lines.size());
  TEST_ASSERT_EQUAL_STRING("One.", d.lines[0].text.c_str());
  TEST_ASSERT_EQUAL(0, err.size());
}

void test_title() {
  TEST_ASSERT_TRUE(hal::parseDialogue("x", "TITLE: Who's the better model\nOLD: Me.\n", &d, &err));
  TEST_ASSERT_EQUAL_STRING("Who's the better model", d.title.c_str());
}

void test_pause_applies_to_next_line() {
  TEST_ASSERT_TRUE(hal::parseDialogue("x", "OLD: A.\nPAUSE: 1200\nNEW: B.\nOLD: C.\n", &d, &err));
  TEST_ASSERT_EQUAL(0, d.lines[0].pauseMs);
  TEST_ASSERT_EQUAL(1200, d.lines[1].pauseMs);
  TEST_ASSERT_EQUAL(0, d.lines[2].pauseMs);
}

void test_unknown_tag_reports_line_number() {
  TEST_ASSERT_FALSE(hal::parseDialogue("x", "OLD: A.\n\nDAVE: Hello?\n", &d, &err));
  TEST_ASSERT_EQUAL(1, err.size());
  TEST_ASSERT_NOT_EQUAL(std::string::npos, err[0].find("line 3"));
}

void test_empty_text_rejected() {
  TEST_ASSERT_FALSE(hal::parseDialogue("x", "OLD:   \nNEW: B.\n", &d, &err));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, err[0].find("line 1"));
}

void test_bad_pause_rejected() {
  TEST_ASSERT_FALSE(hal::parseDialogue("x", "OLD: A.\nPAUSE: soon\nNEW: B.\n", &d, &err));
  TEST_ASSERT_FALSE(hal::parseDialogue("x", "OLD: A.\nPAUSE: 99999\nNEW: B.\n", &d, &err));
}

void test_ids() {
  TEST_ASSERT_TRUE(hal::isValidDialogueId("better-model"));
  TEST_ASSERT_TRUE(hal::isValidDialogueId("a1"));
  TEST_ASSERT_FALSE(hal::isValidDialogueId(""));
  TEST_ASSERT_FALSE(hal::isValidDialogueId("../etc"));
  TEST_ASSERT_FALSE(hal::isValidDialogueId("Intro"));
  TEST_ASSERT_FALSE(hal::isValidDialogueId(std::string(33, 'a')));
  TEST_ASSERT_FALSE(hal::parseDialogue("../x", "OLD: A.\n", &d, &err));
}

void test_voice_path() {
  TEST_ASSERT_EQUAL_STRING("/hal/voice/dialogue/intro/007-old.wav",
                           hal::dialogueVoicePath("intro", 7, Speaker::Old).c_str());
  TEST_ASSERT_EQUAL_STRING("/hal/voice/dialogue/intro/012-new.wav",
                           hal::dialogueVoicePath("intro", 12, Speaker::New).c_str());
}

void test_line_cap_and_empty_script() {
  std::string many;
  for (int i = 0; i < 100; ++i) many += (i % 2 ? "NEW: x.\n" : "OLD: x.\n");
  TEST_ASSERT_FALSE(hal::parseDialogue("x", many, &d, &err));
  err.clear();
  TEST_ASSERT_FALSE(hal::parseDialogue("x", "# nothing here\nTITLE: Empty\n", &d, &err));
  TEST_ASSERT_EQUAL(1, err.size());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_alternating_lines);
  RUN_TEST(test_comments_bom_crlf);
  RUN_TEST(test_title);
  RUN_TEST(test_pause_applies_to_next_line);
  RUN_TEST(test_unknown_tag_reports_line_number);
  RUN_TEST(test_empty_text_rejected);
  RUN_TEST(test_bad_pause_rejected);
  RUN_TEST(test_ids);
  RUN_TEST(test_voice_path);
  RUN_TEST(test_line_cap_and_empty_script);
  return UNITY_END();
}
