#pragma once
// The AI brain's lines the desk HAL has downloaded and not yet spoken. There is room for two:
// the one about to be said and the one after it. Line numbers only rise within a conversation,
// and a conversation can end without anyone saying so (a tap, a HAL that was busy), so a clip is
// only ever played for the FETCH that brought it: fetching line n forgets everything held for n
// or later.
#include <cstddef>
#include <cstdint>
#include <cstdlib>

namespace hal {

class LiveClips {
 public:
  struct Clip {
    int n = 0;
    uint8_t* data = nullptr;
    size_t len = 0;
  };
  using Free = void (*)(void*);

  explicit LiveClips(Free release = std::free) : release_(release) {}
  ~LiveClips() { clear(); }
  LiveClips(const LiveClips&) = delete;
  LiveClips& operator=(const LiveClips&) = delete;

  // Takes the buffer over. A clip already held for line n is replaced; with both places taken,
  // the lower-numbered line makes room.
  void keep(int n, uint8_t* data, size_t len) {
    Clip* slot = nullptr;
    for (Clip& c : clips_)
      if (c.data && c.n == n) slot = &c;
    if (!slot) slot = !clips_[0].data ? &clips_[0] : !clips_[1].data ? &clips_[1] : nullptr;
    if (!slot) slot = clips_[0].n < clips_[1].n ? &clips_[0] : &clips_[1];
    drop(*slot);
    *slot = {n, data, len};
  }

  bool has(int n) const {
    for (const Clip& c : clips_)
      if (c.data && c.n == n) return true;
    return false;
  }

  // Hands line n's buffer to the caller, who frees it; data is null when it isn't held.
  Clip take(int n) {
    for (Clip& c : clips_)
      if (c.data && c.n == n) {
        Clip out = c;
        c = Clip();
        return out;
      }
    return Clip();
  }

  void forgetFrom(int n) {
    for (Clip& c : clips_)
      if (c.data && c.n >= n) drop(c);
  }

  void clear() { forgetFrom(0); }

 private:
  void drop(Clip& c) {
    if (c.data) release_(c.data);
    c = Clip();
  }

  Clip clips_[2];
  Free release_;
};

}  // namespace hal
