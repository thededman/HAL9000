#pragma once
// A friend's side of the link (HAL Nano, HAL Paper, HAL Card): what he answers HAL Zero, and
// what his device should do. He is a small server like the desk HAL, and HAL Zero conducts.
//
//   on connect            -> HELLO OLD 4, IAM <id>
//   NOW n who text        -> show who is speaking and the words (every friend, every line)
//   a HAL with no voice (he chirps, or writes):
//     SAY live n          -> PLAYING n <ms> at once, then say the words of NOW n; DONE n after
//                            (MISSING n if NOW n wasn't his line; BUSY if he is still on one)
//   a HAL with a voice:
//     FETCH n ...         -> download the clip -> READY n (or MISSING n). One clip at a time:
//                            a FETCH while he is speaking is MISSING
//     SAY live n          -> play it -> PLAYING n <ms> once it has started -> DONE n
//   END, a hang-up, a minute of silence -> stop, forget everything
//   a tap                 -> END interrupted (it stops them all); when nobody is talking, a
//                            chirp or a fresh page instead
#include <cstdint>
#include <string>
#include <vector>

#include "hal_cast.h"
#include "hal_link.h"

namespace hal {

struct FriendOut {
  std::vector<Message> send;   // for HAL Zero, in this order
  bool wantState = false;      // also answer STATE, with the device's own numbers
  bool wantSensors = false;    // ... and SENSORS
  bool show = false;           // someone is speaking: `showWho` says `showText` (line `showN`)
  int showWho = 0, showN = 0;
  std::string showText;
  bool say = false;            // his own line `sayN`: chirp or write `sayText` for `sayMs`, or
  int sayN = 0;                // (with a voice) play the clip fetched for it, then call onPlaying()
  std::string sayText;
  uint32_t sayMs = 0;
  bool fetch = false;          // download line `fetchN` from http://host:port/clip/token
  int fetchN = 0, port = 0;    // (dropping any download still going), then call onFetched()
  std::string host, token;
  bool stop = false;           // the talk is over: stop saying, drop any clip or download
  bool idleTap = false;        // a tap with nobody talking
};

class Friend {
 public:
  static constexpr uint32_t kQuietMs = 60000;  // no word of the conversation for this long: it is over

  explicit Friend(int who) : who_(who), voiced_(castVoiced(who)) {}

  FriendOut hello() const;                                  // on connecting
  FriendOut onMessage(const Message& m, uint32_t nowMs);    // from HAL Zero
  FriendOut onFetched(int n, bool ok);                      // the download for line n ended
  FriendOut onPlaying(int n, uint32_t ms, bool started);    // the speaker took the clip, or didn't
  FriendOut onSaid();                                       // his line is finished
  FriendOut onTap();
  FriendOut onDisconnected();
  FriendOut tick(uint32_t nowMs);

  bool talking() const { return talking_; }   // a conversation is going on (his or not)
  bool speaking() const { return saying_ != 0; }

 private:
  void heard(uint32_t nowMs);
  void forget();

  int who_;
  bool voiced_;
  bool talking_ = false;
  uint32_t lastWordMs_ = 0;
  int nowN_ = 0, nowWho_ = -1;  // the last NOW
  std::string nowText_;
  int fetching_ = 0;  // the line being downloaded
  int held_ = 0;      // the line whose clip is ready
  int cued_ = 0;      // the line he was told to say and hasn't started (a voiced HAL)
  int saying_ = 0;    // the line he is saying
};

}  // namespace hal
