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
//   FETCH <n> <ipv4> <port> <token>   (v3, HAL Zero) download line n's audio from http://ipv4:port/clip/token
//   READY <n>                     (v3, desk HAL) line n is downloaded; MISSING <n> if it couldn't be
//   SAY live <n>                  (v3) play fetched line n (the usual PLAYING / DONE follow)
//   SENSE                         (v3, HAL Zero) what do your sensors read?
//   SENSORS light=<%> prox=<n> tilt=<deg> bat=<%> chg=<0|1> temp=<C>
//                                 (v3, desk HAL) the answer: all six keys, any order
//
// Anything else is Invalid: the receiver ignores it. v3 messages are only sent to a peer
// whose HELLO said 3 or more.
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace hal {

constexpr int kLinkVersion = 3;
constexpr int kLinkPort = 9000;

enum class MsgType {
  Invalid, Hello, Ping, Pong, Say, Playing, Missing, Busy, Done, End, Status, State,
  Fetch, Ready, Sense, Sensors  // v3
};
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

struct PeerSensors {           // SENSORS: what the desk HAL's sensors read right now
  int light = -1;              // light   room light, 0..100 %, -1 = not read
  int prox = 0;                // prox    proximity, raw, 0..65535
  int tilt = 0;                // tilt    degrees from his resting pose, 0..180
  int bat = -1;                // bat     battery, 0..100 %, -1 = not read
  bool chg = false;            // chg     charging, 0|1
  int temp = 0;                // temp    chip temperature, -40..125 C
};

// A dotted IPv4 address in 10/8, 172.16/12 or 192.168/16: the only places a HAL fetches from.
bool isPrivateIpv4(const std::string& dotted);
// Are two dotted IPv4 addresses on the same network, given its mask? False for anything malformed
// and for an empty mask (no network yet). The desk HAL only fetches from his own network.
bool sameSubnet(const std::string& a, const std::string& b, const std::string& mask);

struct Message {
  MsgType type = MsgType::Invalid;
  Role role = Role::Old;  // HELLO
  int version = 0;        // HELLO
  std::string id;         // SAY
  int n = 0;              // SAY, PLAYING, MISSING, DONE (1..99)
  int ms = 0;             // PLAYING (0..600000)
  std::string reason;     // END
  PeerStatus status;      // STATE
  std::string host;       // FETCH: a private IPv4 address
  int port = 0;           // FETCH (1024..65535)
  std::string token;      // FETCH: 16..32 lower-case hex characters
  PeerSensors sensors;    // SENSORS
};

Message parseMessage(const std::string& line);
std::string formatMessage(const Message& m);  // without the newline

// What HAL Zero's diagnostics screen shows about the desk HAL. requestMs = when the
// oldest unanswered STATUS was sent (0 = none outstanding); lastStateMs = when the last
// STATE arrived (0 = never). A STATUS unanswered for 2 s means not responding.
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

// Notices a peer that went quiet without closing the connection (the CoreS3 losing
// power sends nothing at all): ping every 2 s, give up after 5 s with nothing heard.
class LinkLiveness {
 public:
  void connected(uint32_t now) { heard_ = lastPing_ = now == 0 ? 1 : now; }
  void heard(uint32_t now) { heard_ = now == 0 ? 1 : now; }
  bool pingDue(uint32_t now) const { return now - lastPing_ >= 2000; }
  void pinged(uint32_t now) { lastPing_ = now; }
  bool silent(uint32_t now) const { return heard_ != 0 && now - heard_ > 5000; }

 private:
  uint32_t heard_ = 0, lastPing_ = 0;  // 0 = not connected
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
