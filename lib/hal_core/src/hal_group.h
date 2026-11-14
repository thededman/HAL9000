#pragma once
// HAL Zero among several HALs (v0.7): who sits in which place on the link, who can join a
// talk the AI brain writes, and the words on his screen that depend on who it is. With only
// the desk HAL there, all of it comes out exactly as it did when there were two.
#include <string>

#include "hal_cast.h"
#include "hal_link.h"

namespace hal {

// The places HAL Zero keeps for the others: the desk HAL's (place 0: peer_host), then three
// for friends (friends=). The desk HAL has never said who he is and needn't; a friend says so
// (IAM) each time he connects.
class Roster {
 public:
  static constexpr int kPlaces = 4;

  // The HAL in `place` said IAM `id`. True if that is who he now is: a friend (nano, paper,
  // card) in a friend's place, whom no other place holds, and who hasn't said he was someone
  // else. The desk HAL's place holds only the desk HAL: anyone else there is nobody until he
  // goes (peer_host points at a friend, who belongs under friends=).
  bool iam(int place, const std::string& id);
  void gone(int place);        // his connection went: a friend must say who he is again
  int who(int place) const;    // a cast number (hal_cast.h), or -1: not said, or no such place
  int placeOf(int who) const;  // where that HAL is, or -1

 private:
  int who_[kPlaces] = {kDesk, -1, -1, -1};
};

// Can this HAL take part in a talk the brain writes? He must be connected and have said hello
// with firmware that knows how: the desk HAL fetches his lines (v3); a friend is also told the
// words (v4). Not HAL Zero himself, and not a friend who hasn't said who he is (-1).
bool canThink(int who, bool connected, int version);

// The cast as the brain is told it, in the cast's order: "zero,desk,nano".
std::string castQuery(unsigned present);

// A HAL as HAL Zero's banners name him: "THE DESK UNIT", "HAL NANO". Anyone who can't be named
// is the desk HAL, as before there were others.
std::string castShout(int who);

// What HAL Zero's banner says when the others let him down. `who` was the last to go (anyone
// who can't be named is the desk HAL, as before there were others); `reason` is the
// conductor's: offline, busy, timeout, or anything else (he walked out).
std::string letDownBanner(int who, const std::string& reason);
// ... when a line of his never became playable (the feed's reason: timeout, or missing).
std::string lostLineBanner(int who, const std::string& reason);
// ... and when he can't be asked to think at all: not there, or too old.
std::string cantThinkBanner(int who, bool connected, int version);

// The ask box: its title for a question to that HAL, and who TAB moves on to. HAL Zero, the
// desk HAL (offered whether he is there or not, as he always was), then each friend present.
std::string askTitle(int who);
int nextToAsk(int current, unsigned present);

}  // namespace hal
