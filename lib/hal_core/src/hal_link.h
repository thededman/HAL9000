#pragma once
// The HAL-to-HAL link: one text line per message over TCP (port 9000).
//
//   HELLO <OLD|NEW> <version>     who is talking
//   PING / PONG                   still there?
//   SAY <id> <n>                  play dialogue line n
//   PLAYING <n> <ms>  |  MISSING <n>  |  BUSY      answers to SAY
//   DONE <n>                      line n finished
//   END <done|interrupted|timeout|busy|offline>     either side ends the dialogue
//
// Anything else is Invalid: the receiver ignores it.
#include <cstddef>
#include <string>
#include <vector>

namespace hal {

constexpr int kLinkVersion = 1;
constexpr int kLinkPort = 9000;

enum class MsgType { Invalid, Hello, Ping, Pong, Say, Playing, Missing, Busy, Done, End };
enum class Role { Old, New };

struct Message {
  MsgType type = MsgType::Invalid;
  Role role = Role::Old;  // HELLO
  int version = 0;        // HELLO
  std::string id;         // SAY
  int n = 0;              // SAY, PLAYING, MISSING, DONE (1..99)
  int ms = 0;             // PLAYING (0..600000)
  std::string reason;     // END
};

Message parseMessage(const std::string& line);
std::string formatMessage(const Message& m);  // without the newline

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
