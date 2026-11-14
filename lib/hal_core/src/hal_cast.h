#pragma once
// The cast: the five HALs, in a fixed order. HAL Zero conducts; the desk HAL has been there
// since v0.1; the other three are optional friends (v0.7). A number here means the same HAL
// everywhere: in a conversation's lines, in HAL Zero's table of links, in the brain's replies.
#include <cstring>
#include <string>

namespace hal {

constexpr int kCastSize = 5;
constexpr int kZero = 0, kDesk = 1, kNano = 2, kPaper = 3, kCard = 4;
constexpr int kEveryone = -1;        // "to all of them" where a single HAL could be named
constexpr unsigned kPair = 0b00011;  // HAL Zero and the desk HAL: every conversation before v0.7

namespace cast_detail {
struct Member {
  const char* id;    // as the link and the brain write it
  const char* name;  // as the subtitles show it
  bool voiced;       // speaks aloud (the others chirp or write)
};
constexpr Member kMembers[kCastSize] = {{"zero", "HAL ZERO", true},
                                        {"desk", "DESK UNIT", true},
                                        {"nano", "HAL NANO", false},
                                        {"paper", "HAL PAPER", false},
                                        {"card", "HAL CARD", true}};
}  // namespace cast_detail

inline bool castValid(int who) { return who >= 0 && who < kCastSize; }
inline const char* castId(int who) { return castValid(who) ? cast_detail::kMembers[who].id : ""; }
inline const char* castName(int who) { return castValid(who) ? cast_detail::kMembers[who].name : ""; }
inline bool castVoiced(int who) { return castValid(who) && cast_detail::kMembers[who].voiced; }

// Which member an id names, or -1.
inline int castFromId(const std::string& id) {
  for (int who = 0; who < kCastSize; ++who)
    if (id == cast_detail::kMembers[who].id) return who;
  return -1;
}

// Who is present, as one bit per member.
inline unsigned castBit(int who) { return castValid(who) ? 1u << who : 0u; }
inline bool castHas(unsigned present, int who) { return (present & castBit(who)) != 0; }
inline int castCount(unsigned present) {
  int n = 0;
  for (int who = 0; who < kCastSize; ++who) n += castHas(present, who) ? 1 : 0;
  return n;
}

}  // namespace hal
