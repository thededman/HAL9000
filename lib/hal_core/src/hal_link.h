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
//   IAM <desk|nano|paper|card>    (v4, a friend) which HAL this is, straight after his HELLO.
//                                 A peer who never says is the desk HAL
//   NOW <n> <who> <text>          (v4, HAL Zero, to every v4 peer) line n is beginning: who says
//                                 it (zero|desk|nano|paper|card) and the words, 1..150 printable
//                                 characters taken exactly as written. A HAL who has no voice
//                                 "says" his line from this, at the SAY live <n> that follows
//
// Anything else is Invalid: the receiver ignores it. v3 messages are only sent to a peer
// whose HELLO said 3 or more, v4 messages to one whose HELLO said 4. The desk HAL and HAL
// Zero say 3 (kLinkVersion); the friends (HAL Nano, HAL Paper, HAL Card) say 4. A line is at
// most 128 bytes, or 256 for what a friend reads (a NOW is up to 163), and a HAL hangs up on a
// peer who sends him more than 24 lines or 1024 bytes in a second (LinkGuard, below).
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace hal {

constexpr int kLinkVersion = 3;         // the desk HAL and HAL Zero
constexpr int kLinkVersionFriends = 4;  // HAL Nano, HAL Paper, HAL Card
constexpr int kLinkPort = 9000;
constexpr size_t kLinkMaxLine = 128;         // the longest line a v3 HAL reads
constexpr size_t kLinkMaxLineFriends = 256;  // ... and a friend, who is sent NOW
constexpr size_t kLinkMaxText = 150;         // the longest spoken line (what the subtitles hold)

enum class MsgType {
  Invalid, Hello, Ping, Pong, Say, Playing, Missing, Busy, Done, End, Status, State,
  Fetch, Ready, Sense, Sensors,  // v3
  Iam, Now                       // v4
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
  std::string who;        // IAM: which HAL this is. NOW: who is speaking (a cast id, hal_cast.h)
  std::string text;       // NOW: the words
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
  explicit LineSplitter(size_t maxLine = kLinkMaxLine) : maxLine_(maxLine) {}
  std::vector<std::string> feed(const char* data, size_t len);

 private:
  size_t maxLine_;
  std::string buf_;
  bool overflow_ = false;
};

// The desk HAL listens on the home network, so anything there can connect and send. A real
// HAL Zero sends a few short lines a second (a PING and a STATUS every 2 s, a handful around
// each spoken line), and gives up on a desk HAL he hasn't heard from in 5 s: even all he can
// have sent while the desk HAL was held up is about a dozen lines, some 150 bytes. These limits
// are twice that and more, so a peer who goes over them in one second is not a HAL Zero.
constexpr size_t kLinkFloodLines = 24;      // lines taken from a peer in one second
constexpr size_t kLinkFloodBytes = 1024;    // bytes read from a peer in one second
constexpr uint32_t kLinkCoolOffMs = 3000;   // after a flood, nobody is let in for this long

// The desk HAL's guard on what arrives over the link. Each pass of his loop it reads what the
// peer has sent and hands over the messages, but never more than the limits above in a second,
// so one pass is short and the inbox small whatever the peer does. A peer who sends more is
// flooding: the guard stops reading, hands over nothing from that pass, and the caller hangs
// up. Hanging up is the simplest answer that is also safe: slowing such a peer down instead
// would mean keeping his backlog and going on answering it, and he is not a HAL Zero anyway.
// A real one who was somehow caught by this reconnects by himself within 5 s.
class LinkGuard {
 public:
  // `maxLine`: the longest line taken (a friend takes kLinkMaxLineFriends; see NOW).
  explicit LinkGuard(size_t maxLine = kLinkMaxLine) : maxLine_(maxLine), splitter_(maxLine) {}
  // Reads up to `max` bytes that have already arrived into `buf`, without waiting; returns how
  // many, or 0 (or less) when there are none.
  using Read = std::function<int(char* buf, size_t max)>;

  // One pass. Leaves this pass's messages in *inbox (none when there are none). False when the
  // peer has flooded: *inbox is empty and it is time to hang up.
  bool take(uint32_t nowMs, const Read& read, std::vector<Message>* inbox);
  bool flooded() const { return flooded_; }
  // True for kLinkCoolOffMs after a flood: let nobody in, or the same peer is back at once.
  // Shorter than the 5 s a HAL Zero waits for an answer, so a real one never notices.
  bool coolingOff(uint32_t nowMs);
  void newPeer();  // someone was let in: forget the last peer's half line and his count
  const std::string& lastLine() const { return last_; }  // the last message handed over, as sent

 private:
  size_t maxLine_;
  LineSplitter splitter_;
  std::string last_;
  size_t lines_ = 0, bytes_ = 0;  // taken in the second that began at windowMs_
  uint32_t windowMs_ = 0, floodMs_ = 0;
  bool counting_ = false, flooded_ = false, cooling_ = false;
};

}  // namespace hal
