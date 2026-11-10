#include <unity.h>

#include <string>
#include <vector>

#include "hal_link.h"

using hal::Message;
using hal::MsgType;

void setUp() {}
void tearDown() {}

void test_parse_examples() {
  Message m = hal::parseMessage("SAY intro 3");
  TEST_ASSERT_TRUE(m.type == MsgType::Say);
  TEST_ASSERT_EQUAL_STRING("intro", m.id.c_str());
  TEST_ASSERT_EQUAL(3, m.n);
  m = hal::parseMessage("PLAYING 3 4521");
  TEST_ASSERT_TRUE(m.type == MsgType::Playing);
  TEST_ASSERT_EQUAL(4521, m.ms);
  TEST_ASSERT_TRUE(hal::parseMessage("DONE 3").type == MsgType::Done);
  TEST_ASSERT_TRUE(hal::parseMessage("MISSING 4").type == MsgType::Missing);
  TEST_ASSERT_TRUE(hal::parseMessage("BUSY").type == MsgType::Busy);
  TEST_ASSERT_TRUE(hal::parseMessage("PING").type == MsgType::Ping);
  TEST_ASSERT_TRUE(hal::parseMessage("PONG").type == MsgType::Pong);
  m = hal::parseMessage("HELLO NEW 1");
  TEST_ASSERT_TRUE(m.type == MsgType::Hello);
  TEST_ASSERT_TRUE(m.role == hal::Role::New);
  TEST_ASSERT_EQUAL(1, m.version);
  m = hal::parseMessage("END interrupted");
  TEST_ASSERT_TRUE(m.type == MsgType::End);
  TEST_ASSERT_EQUAL_STRING("interrupted", m.reason.c_str());
}

void test_round_trip_all_messages() {
  const char* const lines[] = {"HELLO OLD 1", "PING", "PONG", "SAY better-model 12", "PLAYING 12 3000",
                               "MISSING 5", "BUSY", "DONE 12", "END done", "END timeout"};
  for (const char* l : lines) {
    Message m = hal::parseMessage(l);
    TEST_ASSERT_FALSE_MESSAGE(m.type == MsgType::Invalid, l);
    TEST_ASSERT_EQUAL_STRING(l, hal::formatMessage(m).c_str());
  }
}

void test_tolerates_cr_and_spaces() {
  Message m = hal::parseMessage("  SAY intro 3 \r");
  TEST_ASSERT_TRUE(m.type == MsgType::Say);
  TEST_ASSERT_EQUAL(3, m.n);
}

void test_rejects_bad_input() {
  const char* const bad[] = {"SAY ../x 3", "SAY Intro 3", "SAY intro 0", "SAY intro 100", "SAY intro x",
                             "SAY intro", "SAY intro 3 extra", "PLAYING 3 -5", "PLAYING 3 700000", "END maybe",
                             "HELLO BOB 1", "HELLO NEW 0", "DONE", "BUSY now", "DANCE", "", "say intro 3"};
  for (const char* b : bad) TEST_ASSERT_TRUE_MESSAGE(hal::parseMessage(b).type == MsgType::Invalid, b);
}

void test_splitter_partial_chunks() {
  hal::LineSplitter s;
  auto a = s.feed("SAY in", 6);
  TEST_ASSERT_EQUAL(0, a.size());
  auto b = s.feed("tro 3\nPI", 8);
  TEST_ASSERT_EQUAL(1, b.size());
  TEST_ASSERT_EQUAL_STRING("SAY intro 3", b[0].c_str());
  auto c = s.feed("NG\n", 3);
  TEST_ASSERT_EQUAL(1, c.size());
  TEST_ASSERT_EQUAL_STRING("PING", c[0].c_str());
}

void test_splitter_drops_overlong_lines() {
  hal::LineSplitter s;
  std::string junk(200, 'a');
  auto a = s.feed(junk.data(), junk.size());
  auto b = s.feed("\nPING\n", 6);
  TEST_ASSERT_EQUAL(0, a.size());
  TEST_ASSERT_EQUAL(1, b.size());
  TEST_ASSERT_EQUAL_STRING("PING", b[0].c_str());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_parse_examples);
  RUN_TEST(test_round_trip_all_messages);
  RUN_TEST(test_tolerates_cr_and_spaces);
  RUN_TEST(test_rejects_bad_input);
  RUN_TEST(test_splitter_partial_chunks);
  RUN_TEST(test_splitter_drops_overlong_lines);
  return UNITY_END();
}
