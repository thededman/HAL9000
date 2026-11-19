#include "hal_wake.h"

#include <cctype>
#include <cstdlib>
#include <vector>

namespace hal {
namespace {

struct Word {
  std::string lower;
  size_t start = 0;
};

// The words of a sentence (letters, digits and apostrophes), lower-cased, with where each begins.
std::vector<Word> wordsOf(const std::string& text, size_t atMost) {
  std::vector<Word> words;
  size_t i = 0;
  while (i < text.size() && words.size() < atMost) {
    const unsigned char c = static_cast<unsigned char>(text[i]);
    if (!(std::isalnum(c) || c == '\'')) {
      ++i;
      continue;
    }
    Word w;
    w.start = i;
    while (i < text.size()) {
      const unsigned char d = static_cast<unsigned char>(text[i]);
      if (!(std::isalnum(d) || d == '\'')) break;
      w.lower.push_back(static_cast<char>(std::tolower(d)));
      ++i;
    }
    words.push_back(w);
  }
  return words;
}

bool oneOf(const std::string& word, std::initializer_list<const char*> list) {
  for (const char* candidate : list)
    if (word == candidate) return true;
  return false;
}

}  // namespace

WakeHeard wakeMatch(const std::string& heard) {
  WakeHeard out;
  const std::vector<Word> words = wordsOf(heard, 4);
  if (words.size() < 2) return out;
  // "Hey HAL", and how a recogniser tends to write it. Not "hey how": that begins too much
  // ordinary talk ("Hey, how are you?").
  if (!oneOf(words[0].lower, {"hey", "hay", "hi", "ok", "okay"})) return out;
  if (!oneOf(words[1].lower, {"hal", "hall", "hale", "haul", "howl", "al"})) return out;
  out.forHim = true;
  size_t next = 2;
  if (words.size() > next && words[next].lower == "zero") ++next;  // "Hey HAL Zero": his full name
  if (words.size() <= next) return out;                            // only his name: a call, the question follows
  for (size_t i = words[next].start; i < heard.size() && out.question.size() < 120; ++i) {
    const unsigned char c = static_cast<unsigned char>(heard[i]);
    if (c >= 127) continue;                     // not something the ask box can show
    const char shown = c < 32 ? ' ' : static_cast<char>(c);
    if (shown == ' ' && (out.question.empty() || out.question.back() == ' ')) continue;
    out.question.push_back(shown);
  }
  while (!out.question.empty() && out.question.back() == ' ') out.question.pop_back();
  if (!out.question.empty()) out.question[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(out.question[0])));
  return out;
}

uint8_t wakeLevel(const int16_t* samples, size_t n) {
  if (!samples || n == 0) return 0;
  uint64_t sum = 0;
  for (size_t i = 0; i < n; ++i) sum += static_cast<uint64_t>(std::abs(static_cast<int>(samples[i])));
  const uint64_t level = sum / n / 16;
  return static_cast<uint8_t>(level > 255 ? 255 : level);
}

uint8_t WakeListener::threshold() const {
  const float t = floor_ * 3.0f + 6.0f;  // well clear of the room's own noise
  return static_cast<uint8_t>(t < 12.0f ? 12.0f : t > 200.0f ? 200.0f : t);
}

void WakeListener::reset() {
  state_ = State::Idle;
  loudRun_ = heardFrames_ = quietRun_ = 0;
}

WakeListener::Event WakeListener::feed(uint8_t level) {
  const bool loud = level > threshold();
  switch (state_) {
    case State::Idle:
      if (!loud) {
        loudRun_ = 0;
        floor_ += (static_cast<float>(level) - floor_) * 0.02f;  // the room, averaged over a second or so
        return Event::None;
      }
      if (++loudRun_ * kFrameMs < kStartMs) return Event::None;
      state_ = State::Speaking;
      heardFrames_ = loudRun_;
      loudRun_ = quietRun_ = 0;
      return Event::Started;
    case State::Speaking:
      ++heardFrames_;
      quietRun_ = loud ? 0 : quietRun_ + 1;
      if (heardFrames_ * kFrameMs > kMaxMs) {  // it never pauses: not a question. Wait for quiet.
        state_ = State::Deaf;
        quietRun_ = 0;
        return Event::Dropped;
      }
      if (quietRun_ * kFrameMs < kEndMs) return Event::None;
      state_ = State::Idle;
      loudRun_ = 0;
      return (heardFrames_ - quietRun_) * kFrameMs < kMinMs ? Event::Dropped : Event::Ended;
    case State::Deaf:
      floor_ += (static_cast<float>(level) - floor_) * 0.05f;  // if this is how the room is now, get used to it
      quietRun_ = level > threshold() ? 0 : quietRun_ + 1;
      if (quietRun_ * kFrameMs >= kEndMs) reset();
      return Event::None;
  }
  return Event::None;
}

}  // namespace hal
