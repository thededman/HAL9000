#include "hal_group.h"

namespace hal {
namespace {

bool isFriend(int who) { return who == kNano || who == kPaper || who == kCard; }

}  // namespace

std::string castShout(int who) { return isFriend(who) ? castName(who) : "THE DESK UNIT"; }

bool Roster::iam(int place, const std::string& id) {
  if (place < 0 || place >= kPlaces) return false;
  const int said = castFromId(id);
  if (place == 0) {
    who_[0] = said == kDesk ? kDesk : -1;
    return said == kDesk;
  }
  if (!isFriend(said)) return false;
  if (who_[place] != -1) return who_[place] == said;  // he doesn't turn into someone else
  if (placeOf(said) != -1) return false;              // there is one of each
  who_[place] = said;
  return true;
}

void Roster::gone(int place) {
  if (place < 0 || place >= kPlaces) return;
  who_[place] = place == 0 ? kDesk : -1;
}

int Roster::who(int place) const { return place < 0 || place >= kPlaces ? -1 : who_[place]; }

int Roster::placeOf(int who) const {
  if (!castValid(who)) return -1;
  for (int place = 0; place < kPlaces; ++place)
    if (who_[place] == who) return place;
  return -1;
}

bool canThink(int who, bool connected, int version) {
  if (!connected) return false;
  if (who == kDesk) return version >= kLinkVersion;
  return isFriend(who) && version >= kLinkVersionFriends;
}

std::string castQuery(unsigned present) {
  std::string out;
  for (int who = 0; who < kCastSize; ++who) {
    if (!castHas(present, who)) continue;
    if (!out.empty()) out.push_back(',');
    out += castId(who);
  }
  return out;
}

std::string letDownBanner(int who, const std::string& reason) {
  return castShout(who) + (reason == "offline"   ? " ISN'T ANSWERING"
                        : reason == "busy"    ? " IS BUSY. AS USUAL."
                        : reason == "timeout" ? " FELL ASLEEP"
                                              : " WALKED OUT");
}

std::string lostLineBanner(int who, const std::string& reason) {
  return castShout(who) + (reason == "timeout" ? " IS TAKING TOO LONG" : " LOST THE THREAD");
}

std::string cantThinkBanner(int who, bool connected, int version) {
  // (Connected but not yet heard from, in the moment before his HELLO: that isn't "too old".)
  return castShout(who) + (!connected || version == 0 ? " ISN'T ANSWERING" : " IS TOO OLD TO THINK");
}

std::string askTitle(int who) { return "ASK " + (who == kZero ? std::string(castName(kZero)) : castShout(who)); }

int nextToAsk(int current, unsigned present) {
  if (current == kZero) return kDesk;
  if (!castValid(current)) return kDesk;
  for (int who = current + 1; who < kCastSize; ++who)
    if (castHas(present, who)) return who;
  return kZero;
}

}  // namespace hal
