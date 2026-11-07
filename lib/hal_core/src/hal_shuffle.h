#pragma once
// Random picks without repeats: every item once per round, and a new round
// never starts with the item the previous round ended on.
#include <cstddef>
#include <cstdint>
#include <vector>

namespace hal {

class ShuffleBag {
 public:
  explicit ShuffleBag(uint32_t seed);
  void reset(size_t count);
  int next();  // -1 when empty
  size_t size() const { return order_.size(); }
  uint32_t random();  // xorshift32

 private:
  void shuffle();
  uint32_t state_;
  std::vector<int> order_;
  size_t pos_ = 0;
  int last_ = -1;
};

}  // namespace hal
