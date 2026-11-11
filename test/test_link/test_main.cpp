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

// ---- v2: STATUS / STATE (HAL Zero diagnosing the desk HAL) ----

void test_status_round_trip() {
  hal::Message m; m.type = hal::MsgType::Status;
  TEST_ASSERT_EQUAL_STRING("STATUS", hal::formatMessage(m).c_str());
  TEST_ASSERT(hal::parseMessage("STATUS").type == hal::MsgType::Status);
  TEST_ASSERT(hal::parseMessage("STATUS now").type == hal::MsgType::Invalid);
}

hal::PeerStatus sample() {
  hal::PeerStatus s; s.upS = 86400; s.heap = 109388; s.psram = 7340032; s.cam = 12; s.lines = 40;
  s.mode = "idle"; s.rssi = -58; s.fw = "0.4.0"; s.cores = 2; s.mhz = 240; return s;
}

void test_state_round_trip() {
  hal::Message m; m.type = hal::MsgType::State; m.status = sample();
  const char* line = "STATE up=86400 heap=109388 psram=7340032 cam=12 lines=40 mode=idle rssi=-58 fw=0.4.0 cores=2 mhz=240";
  TEST_ASSERT_EQUAL_STRING(line, hal::formatMessage(m).c_str());
  hal::Message p = hal::parseMessage(line);
  TEST_ASSERT(p.type == hal::MsgType::State);
  TEST_ASSERT_EQUAL(-58, p.status.rssi);
  TEST_ASSERT_EQUAL_STRING("0.4.0", p.status.fw.c_str());
  TEST_ASSERT_EQUAL(109388, p.status.heap);
  TEST_ASSERT_EQUAL(86400, p.status.upS);
  TEST_ASSERT_EQUAL_STRING("idle", p.status.mode.c_str());
  TEST_ASSERT_EQUAL(240, p.status.mhz);
}

void test_state_any_order() {
  TEST_ASSERT(hal::parseMessage("STATE mhz=240 cores=2 fw=0.4.0 rssi=0 mode=dark lines=0 cam=0 psram=0 heap=0 up=0").type
              == hal::MsgType::State);
}

void test_state_rejects() {
  const char* bad[] = {
      "STATE up=1 heap=1 psram=1 cam=1 lines=1 mode=idle rssi=-1 fw=1 cores=1",              // missing mhz
      "STATE up=1 up=1 heap=1 psram=1 cam=1 lines=1 mode=idle rssi=-1 fw=1 cores=1 mhz=1",  // duplicate
      "STATE up=1 heap=1 psram=1 cam=1 lines=1 mode=idle rssi=-1 fw=1 cores=1 mhz=1 foo=1", // unknown key
      "STATE up=1 heap=1 psram=1 cam=101 lines=1 mode=idle rssi=-1 fw=1 cores=1 mhz=1",
      "STATE up=1 heap=1 psram=1 cam=1 lines=1 mode=idle rssi=5 fw=1 cores=1 mhz=1",
      "STATE up=1 heap=1 psram=1 cam=1 lines=1 mode=idle rssi=-128 fw=1 cores=1 mhz=1",
      "STATE up=1 heap=1 psram=1 cam=1 lines=1 mode=asleep rssi=-1 fw=1 cores=1 mhz=1",
      "STATE up=1 heap=1 psram=1 cam=1 lines=1 mode=idle rssi=-1 fw=0.4.0-b cores=1 mhz=1",
      "STATE up=1 heap=1 psram=1 cam=1 lines=1 mode=idle rssi=-1 fw=123456789 cores=1 mhz=1",
      "STATE up=1 heap=-1 psram=1 cam=1 lines=1 mode=idle rssi=-1 fw=1 cores=1 mhz=1",
      "STATE up=1000000000 heap=1 psram=1 cam=1 lines=1 mode=idle rssi=-1 fw=1 cores=1 mhz=1",
      "STATE up=1 heap=1 psram=1 cam=1 lines=1 mode=idle rssi=-1 fw=1 cores=0 mhz=1",
      "STATE up=1 heap=1 psram=1 cam=1 lines=1 mode=idle rssi=-1 fw=. cores=1 mhz=1"};
  for (const char* b : bad) TEST_ASSERT_MESSAGE(hal::parseMessage(b).type == hal::MsgType::Invalid, b);
}

void test_longest_state_fits() {
  hal::Message m; m.type = hal::MsgType::State;
  m.status = {999999999, 9999999, 99999999, 100, 9999999, "speaking", -127, "10.10.10", 64, 9999};
  std::string line = hal::formatMessage(m);
  TEST_ASSERT_TRUE(line.size() <= 122);
  hal::LineSplitter split(128);
  std::string wire = line + "\n";
  auto got = split.feed(wire.data(), wire.size());
  TEST_ASSERT_EQUAL(1, int(got.size()));
  TEST_ASSERT(hal::parseMessage(got[0]).type == hal::MsgType::State);
}

void test_version_two_still_reads_one() {
  TEST_ASSERT_EQUAL(2, hal::kLinkVersion);
  TEST_ASSERT_EQUAL(1, hal::parseMessage("HELLO OLD 1").version);
}

void test_peer_state() {
  using hal::PeerState;
  TEST_ASSERT(hal::peerState(false, 2, 1000, 900, 1500) == PeerState::NotResponding);  // unplugged
  TEST_ASSERT(hal::peerState(true, 1, 1000, 0, 1500) == PeerState::TooOld);
  TEST_ASSERT(hal::peerState(true, 2, 1000, 0, 2999) == PeerState::Waiting);           // < 2 s since STATUS
  TEST_ASSERT(hal::peerState(true, 2, 1000, 0, 3000) == PeerState::NotResponding);     // 2 s, no STATE
  TEST_ASSERT(hal::peerState(true, 2, 5000, 4000, 6000) == PeerState::Ok);
  TEST_ASSERT(hal::peerState(true, 2, 9500, 4000, 9600) == PeerState::Waiting);        // stale STATE, fresh STATUS
  TEST_ASSERT(hal::peerState(true, 2, 9500, 4000, 11500) == PeerState::NotResponding); // stale and no reply
  TEST_ASSERT(hal::peerState(true, 0, 1000, 1200, 1300) == PeerState::Ok);             // HELLO not seen yet
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_parse_examples);
  RUN_TEST(test_round_trip_all_messages);
  RUN_TEST(test_tolerates_cr_and_spaces);
  RUN_TEST(test_rejects_bad_input);
  RUN_TEST(test_splitter_partial_chunks);
  RUN_TEST(test_splitter_drops_overlong_lines);
  RUN_TEST(test_status_round_trip);
  RUN_TEST(test_state_round_trip);
  RUN_TEST(test_state_any_order);
  RUN_TEST(test_state_rejects);
  RUN_TEST(test_longest_state_fits);
  RUN_TEST(test_version_two_still_reads_one);
  RUN_TEST(test_peer_state);
  return UNITY_END();
}
