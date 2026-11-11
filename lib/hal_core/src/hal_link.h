#pragma once
// The HAL-to-HAL link: one text line per message over TCP (port 9000).
//
//   HELLO <OLD|NEW> <version>     who is talking
//   PING / PONG                   still there?
//   SAY <id> <n>                  play dialogue line n
//   PLAYING <n> <ms>  |  MISSING <n>  |  BUSY      answers to SAY
//   DONE <n>                      line n finished
//   END <done|interrupted|timeout|busy|offline>     either side ends the dialogue
//   STATUS                        (v2, HAL Zero) how are you?
//   STATE up=<s> heap=<b> psram=<b> cam=<0-100> lines=<n> mode=<name> rssi=<dBm> fw=<x.y.z> cores=<n> mhz=<n>
//                                 (v2, desk HAL) the answer: all ten keys, any order
//
// Anything else is Invalid: the receiver ignores it.
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace hal {

constexpr int kLinkVersion = 2;
constexpr int kLinkPort = 9000;

enum class MsgType { Invalid, Hello, Ping, Pong, Say, Playing, Missing, Busy, Done, End, Status, State };
enum class Role { Old, New };

struct PeerStatus {            // STATE: the desk HAL's real numbers
  int upS = 0;                 // up      0..999999999
  int heap = 0;                // heap    free internal heap, bytes, 0..9999999
  int psram = 0;               // psram   free PSRAM, bytes, 0..99999999
  int cam = 0;                 // cam     camera motion, 0..100
  int lines = 0;               // lines   lines spoken since boot, 0..9999999
  std::string mode;            // mode    booting|idle|speaking|console|pleading|daisy|dark
  int rssi = 0;                // rssi    dBm, -127..0 (0 = Wi-Fi off)
  std::string fw;              // fw      ^[0-9]+(\.[0-9]+)*$, 1..8 chars ("0.4.0")
  int cores = 0;               // cores   1..64
  int mhz = 0;                 // mhz     1..9999
};

struct Message {
  MsgType type = MsgType::Invalid;
  Role role = Role::Old;  // HELLO
  int version = 0;        // HELLO
  std::string id;         // SAY
  int n = 0;              // SAY, PLAYING, MISSING, DONE (1..99)
  int ms = 0;             // PLAYING (0..600000)
  std::string reason;     // END
  PeerStatus status;      // STATE
};

Message parseMessage(const std::string& line);
std::string formatMessage(const Message& m);  // without the newline

// What HAL Zero's diagnostics screen shows about the desk HAL. requestMs = when the
// latest STATUS was sent (0 = never); lastStateMs = when the last STATE arrived (0 = never).
enum class PeerState { Waiting, Ok, NotResponding, TooOld };
PeerState peerState(bool connected, int peerVersion, uint32_t requestMs, uint32_t lastStateMs, uint32_t nowMs);

// HAL Zero's bookkeeping for STATUS requests while diagnostics or benchmark shows:
// ask on opening, then every 2 s; "waiting" runs from the oldest unanswered request,
// so a desk HAL that never answers reads as not responding after 2 s.
class PeerWatch {
 public:
  bool due(uint32_t now) const { return lastSent_ == 0 || now - lastSent_ >= 2000; }
  void sent(uint32_t now);
  void received(uint32_t now);
  void forget() { lastSent_ = waitingSince_ = lastState_ = 0; }  // the link dropped
  PeerState state(bool connected, int peerVersion, uint32_t now) const {
    return peerState(connected, peerVersion, waitingSince_, lastState_, now);
  }

 private:
  uint32_t lastSent_ = 0, waitingSince_ = 0, lastState_ = 0;  // 0 = never
};

// Turns TCP chunks into whole lines; lines longer than maxLine are dropped.
class LineSplitter {
 public:
  explicit LineSplitter(size_t maxLine = 128) : maxLine_(maxLine) {}
  std::vector<std::string> feed(const char* data, size_t len);

 private:
  size_t maxLine_;
  std::string buf_;
  bool overflow_ = false;
};

}  // namespace hal
