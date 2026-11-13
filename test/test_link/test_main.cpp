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
  TEST_ASSERT_EQUAL(3, hal::kLinkVersion);
  TEST_ASSERT_EQUAL(3, hal::parseMessage("HELLO NEW 3").version);
  TEST_ASSERT_EQUAL(1, hal::parseMessage("HELLO OLD 1").version);
}

// Link v3: the AI brain's lines. HAL Zero tells the desk HAL where to download a clip
// (FETCH), hears when it is ready (READY), and asks for his sensor readings (SENSE).
void test_v3_round_trip() {
  const char* const lines[] = {"FETCH 2 192.168.1.20 9100 0123456789abcdef0123456789abcdef",
                               "FETCH 99 10.0.0.1 1024 0123456789abcdef",
                               "READY 2",
                               "SENSE",
                               "SENSORS light=67 prox=19 tilt=3 bat=37 chg=1 temp=42",
                               "SENSORS light=-1 prox=0 tilt=0 bat=-1 chg=0 temp=-5"};
  for (const char* line : lines) {
    hal::Message m = hal::parseMessage(line);
    TEST_ASSERT_TRUE_MESSAGE(m.type != MsgType::Invalid, line);
    TEST_ASSERT_EQUAL_STRING(line, hal::formatMessage(m).c_str());
  }
  hal::Message f = hal::parseMessage("FETCH 2 192.168.1.20 9100 0123456789abcdef0123456789abcdef");
  TEST_ASSERT(f.type == MsgType::Fetch);
  TEST_ASSERT_EQUAL(2, f.n);
  TEST_ASSERT_EQUAL_STRING("192.168.1.20", f.host.c_str());
  TEST_ASSERT_EQUAL(9100, f.port);
  TEST_ASSERT_EQUAL_STRING("0123456789abcdef0123456789abcdef", f.token.c_str());
  TEST_ASSERT(hal::parseMessage("READY 7").type == MsgType::Ready);
  TEST_ASSERT_EQUAL(7, hal::parseMessage("READY 7").n);
  TEST_ASSERT(hal::parseMessage("SENSE").type == MsgType::Sense);
  hal::Message s = hal::parseMessage("SENSORS temp=42 chg=1 bat=37 tilt=3 prox=19 light=67");  // any order
  TEST_ASSERT(s.type == MsgType::Sensors);
  TEST_ASSERT_EQUAL(67, s.sensors.light);
  TEST_ASSERT_EQUAL(19, s.sensors.prox);
  TEST_ASSERT_EQUAL(3, s.sensors.tilt);
  TEST_ASSERT_EQUAL(37, s.sensors.bat);
  TEST_ASSERT_TRUE(s.sensors.chg);
  TEST_ASSERT_EQUAL(42, s.sensors.temp);
}

void test_same_subnet() {
  TEST_ASSERT_TRUE(hal::sameSubnet("192.168.1.20", "192.168.1.7", "255.255.255.0"));
  TEST_ASSERT_TRUE(hal::sameSubnet("172.16.1.3", "172.16.0.14", "255.255.254.0"));
  TEST_ASSERT_TRUE(hal::sameSubnet("10.9.8.7", "10.0.0.1", "255.0.0.0"));
  // Private, but somewhere else: the modem's own network, a VPN.
  TEST_ASSERT_FALSE(hal::sameSubnet("10.20.30.40", "192.168.1.7", "255.255.255.0"));
  TEST_ASSERT_FALSE(hal::sameSubnet("192.168.2.20", "192.168.1.7", "255.255.255.0"));
  TEST_ASSERT_FALSE(hal::sameSubnet("172.16.2.3", "172.16.0.14", "255.255.254.0"));
  // No network yet (0.0.0.0), or nonsense: never.
  TEST_ASSERT_FALSE(hal::sameSubnet("192.168.1.20", "0.0.0.0", "0.0.0.0"));
  TEST_ASSERT_FALSE(hal::sameSubnet("192.168.1.20", "192.168.1.7", "0.0.0.0"));
  TEST_ASSERT_FALSE(hal::sameSubnet("192.168.1.20", "192.168.1.7", ""));
  TEST_ASSERT_FALSE(hal::sameSubnet("192.168.1", "192.168.1.7", "255.255.255.0"));
  TEST_ASSERT_FALSE(hal::sameSubnet("192.168.1.20", "banana", "255.255.255.0"));
}

void test_private_ipv4() {
  for (const char* yes : {"10.0.0.1", "10.255.255.255", "172.16.0.14", "172.31.255.254", "192.168.1.20", "192.168.0.0"})
    TEST_ASSERT_TRUE_MESSAGE(hal::isPrivateIpv4(yes), yes);
  for (const char* no : {"8.8.8.8", "172.15.0.1", "172.32.0.1", "192.169.1.1", "127.0.0.1", "169.254.1.1", "0.0.0.0",
                         "10.0.0", "10.0.0.256", "010.0.0.1", "10.0.0.1.5", "10.0.0.", ".10.0.0.1", "10..0.1", "a.b.c.d",
                         "10.0.0.1 ", "10.0.0.-1", "1000.0.0.1", ""})
    TEST_ASSERT_FALSE_MESSAGE(hal::isPrivateIpv4(no), no);
}

void test_fetch_rejects() {
  const char* const bad[] = {"FETCH 2 8.8.8.8 9100 0123456789abcdef",                      // not a private address
                             "FETCH 2 example.com 9100 0123456789abcdef",                   // a name
                             "FETCH 2 192.168.1.20 1023 0123456789abcdef",                  // a system port
                             "FETCH 2 192.168.1.20 65536 0123456789abcdef",
                             "FETCH 2 192.168.1.20 9100 0123456789abcde",                   // 15 characters
                             "FETCH 2 192.168.1.20 9100 0123456789abcdef0123456789abcdef0",  // 33
                             "FETCH 2 192.168.1.20 9100 0123456789ABCDEF",                  // upper case
                             "FETCH 2 192.168.1.20 9100 0123456789abcdeg",
                             "FETCH 2 192.168.1.20 9100 ../../../etc/passwd",
                             "FETCH 0 192.168.1.20 9100 0123456789abcdef",
                             "FETCH 100 192.168.1.20 9100 0123456789abcdef",
                             "FETCH 2 192.168.1.20 9100",
                             "FETCH 2 192.168.1.20 9100 0123456789abcdef extra",
                             "FETCH", "READY", "READY 0", "READY 100", "READY 2 3", "SENSE now", "fetch 2 10.0.0.1 9100 0123456789abcdef"};
  for (const char* line : bad) TEST_ASSERT_TRUE_MESSAGE(hal::parseMessage(line).type == MsgType::Invalid, line);
}

void test_sensors_rejects() {
  const char* const bad[] = {"SENSORS light=67 prox=19 tilt=3 bat=37 chg=1",                    // temp missing
                             "SENSORS light=67 prox=19 tilt=3 bat=37 chg=1 temp=42 temp=42",    // twice
                             "SENSORS light=67 light=67 tilt=3 bat=37 chg=1 temp=42",
                             "SENSORS light=67 prox=19 tilt=3 bat=37 chg=1 temp=42 volts=5",    // unknown
                             "SENSORS light=101 prox=19 tilt=3 bat=37 chg=1 temp=42",
                             "SENSORS light=-2 prox=19 tilt=3 bat=37 chg=1 temp=42",
                             "SENSORS light=67 prox=65536 tilt=3 bat=37 chg=1 temp=42",
                             "SENSORS light=67 prox=-1 tilt=3 bat=37 chg=1 temp=42",
                             "SENSORS light=67 prox=19 tilt=181 bat=37 chg=1 temp=42",
                             "SENSORS light=67 prox=19 tilt=3 bat=101 chg=1 temp=42",
                             "SENSORS light=67 prox=19 tilt=3 bat=37 chg=2 temp=42",
                             "SENSORS light=67 prox=19 tilt=3 bat=37 chg=yes temp=42",
                             "SENSORS light=67 prox=19 tilt=3 bat=37 chg=1 temp=126",
                             "SENSORS light=67 prox=19 tilt=3 bat=37 chg=1 temp=-41",
                             "SENSORS light= prox=19 tilt=3 bat=37 chg=1 temp=42",
                             "SENSORS light prox=19 tilt=3 bat=37 chg=1 temp=42",
                             "SENSORS"};
  for (const char* line : bad) TEST_ASSERT_TRUE_MESSAGE(hal::parseMessage(line).type == MsgType::Invalid, line);
}

void test_longest_v3_lines_fit() {
  hal::Message f;
  f.type = MsgType::Fetch;
  f.n = 99;
  f.host = "192.168.255.255";
  f.port = 65535;
  f.token = std::string(32, 'f');
  TEST_ASSERT_TRUE(hal::formatMessage(f).size() <= 128);
  TEST_ASSERT(hal::parseMessage(hal::formatMessage(f)).type == MsgType::Fetch);
  hal::Message s;
  s.type = MsgType::Sensors;
  s.sensors = {100, 65535, 180, 100, true, -40};
  TEST_ASSERT_TRUE(hal::formatMessage(s).size() <= 128);
  TEST_ASSERT(hal::parseMessage(hal::formatMessage(s)).type == MsgType::Sensors);
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
  // Connected, but nobody has asked lately: not known, which isn't the same as not responding.
  TEST_ASSERT(hal::peerState(true, 2, 0, 4000, 20000) == PeerState::Waiting);
  TEST_ASSERT(hal::peerState(true, 2, 0, 0, 20000) == PeerState::Waiting);
}

// HAL Zero re-asks every 2 s; a desk HAL that never answers must still read as
// not responding 2 s after the first unanswered STATUS.
void test_peer_watch_silent() {
  hal::PeerWatch w;
  TEST_ASSERT_TRUE(w.due(500));
  w.sent(1000);
  TEST_ASSERT_FALSE(w.due(2999));
  TEST_ASSERT(w.state(true, 2, 2999) == hal::PeerState::Waiting);
  TEST_ASSERT_TRUE(w.due(3000));
  w.sent(3000);
  TEST_ASSERT(w.state(true, 2, 3300) == hal::PeerState::NotResponding);
  w.sent(5000);
  TEST_ASSERT(w.state(true, 2, 5100) == hal::PeerState::NotResponding);
}

void test_peer_watch_answers_then_goes_quiet() {
  hal::PeerWatch w;
  w.sent(1000);
  w.received(1050);
  TEST_ASSERT(w.state(true, 2, 1100) == hal::PeerState::Ok);
  w.sent(3000);
  w.received(3050);
  w.sent(5000);  // no more answers
  TEST_ASSERT(w.state(true, 2, 6999) == hal::PeerState::Ok);             // asked 2.0 s ago: still in time
  TEST_ASSERT(w.state(true, 2, 7000) == hal::PeerState::NotResponding);  // spec: no STATE within 2 s
  w.sent(7000);
  TEST_ASSERT(w.state(true, 2, 7500) == hal::PeerState::NotResponding);  // the oldest question still counts
  TEST_ASSERT(w.state(false, 2, 1100) == hal::PeerState::NotResponding);
}

void test_peer_watch_forget() {
  hal::PeerWatch w;
  w.sent(1000);
  w.received(1050);
  w.forget();  // link dropped
  TEST_ASSERT_TRUE(w.due(1100));
  TEST_ASSERT(w.state(false, 2, 1100) == hal::PeerState::NotResponding);
  TEST_ASSERT(w.state(true, 2, 1100) == hal::PeerState::Waiting);  // back, not asked yet: the old answer is gone
  w.sent(1100);
  TEST_ASSERT(w.state(true, 2, 1200) == hal::PeerState::Waiting);
}

// The film-style screens read the watch all the time but only ask while they're showing:
// an idle watch on a healthy link must not look like an alarm.
void test_peer_watch_idle_is_not_an_alarm() {
  hal::PeerWatch w;
  TEST_ASSERT(w.state(true, 2, 500) == hal::PeerState::Waiting);  // never asked
  w.sent(1000);
  w.received(1050);
  TEST_ASSERT(w.state(true, 2, 6000) == hal::PeerState::Ok);
  TEST_ASSERT(w.state(true, 2, 60000) == hal::PeerState::Waiting);  // the answer is old and nobody asked again
  w.sent(60000);
  TEST_ASSERT(w.state(true, 2, 60100) == hal::PeerState::Waiting);
  w.received(60150);
  TEST_ASSERT(w.state(true, 2, 60200) == hal::PeerState::Ok);
  TEST_ASSERT(w.state(false, 2, 60200) == hal::PeerState::NotResponding);  // the link itself is down
}

// A desk HAL that loses power never closes the connection: HAL Zero pings every
// 2 s and gives up after 5 s with nothing heard.
void test_liveness_pings_every_two_seconds() {
  hal::LinkLiveness l;
  l.connected(1000);
  TEST_ASSERT_FALSE(l.pingDue(2999));
  TEST_ASSERT_TRUE(l.pingDue(3000));
  l.pinged(3000);
  TEST_ASSERT_FALSE(l.pingDue(4999));
  TEST_ASSERT_TRUE(l.pingDue(5000));
}

void test_liveness_silent_after_five_seconds() {
  hal::LinkLiveness l;
  l.connected(1000);
  TEST_ASSERT_FALSE(l.silent(6000));
  TEST_ASSERT_TRUE(l.silent(6001));
  l.heard(5500);  // a PONG, a STATE, anything
  TEST_ASSERT_FALSE(l.silent(10500));
  TEST_ASSERT_TRUE(l.silent(10501));
}

void test_liveness_idle_before_connecting() {
  hal::LinkLiveness l;
  TEST_ASSERT_FALSE(l.silent(100000));  // nothing to time out yet
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
  RUN_TEST(test_v3_round_trip);
  RUN_TEST(test_private_ipv4);
  RUN_TEST(test_same_subnet);
  RUN_TEST(test_fetch_rejects);
  RUN_TEST(test_sensors_rejects);
  RUN_TEST(test_longest_v3_lines_fit);
  RUN_TEST(test_peer_state);
  RUN_TEST(test_peer_watch_silent);
  RUN_TEST(test_peer_watch_answers_then_goes_quiet);
  RUN_TEST(test_peer_watch_forget);
  RUN_TEST(test_peer_watch_idle_is_not_an_alarm);
  RUN_TEST(test_liveness_pings_every_two_seconds);
  RUN_TEST(test_liveness_silent_after_five_seconds);
  RUN_TEST(test_liveness_idle_before_connecting);
  return UNITY_END();
}
