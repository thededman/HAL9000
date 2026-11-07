#include "hal_shuffle.h"

#include <utility>

namespace hal {

ShuffleBag::ShuffleBag(uint32_t seed) : state_(seed ? seed : 0x9E3779B9u) {}

uint32_t ShuffleBag::random() {
  uint32_t x = state_;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  state_ = x;
  return x;
}

void ShuffleBag::reset(size_t count) {
  order_.resize(count);
  for (size_t i = 0; i < count; ++i) order_[i] = static_cast<int>(i);
  last_ = -1;
  shuffle();
}

void ShuffleBag::shuffle() {
  for (size_t i = order_.size(); i > 1; --i) {
    size_t j = random() % i;
    std::swap(order_[i - 1], order_[j]);
  }
  if (order_.size() > 1 && order_[0] == last_) std::swap(order_[0], order_[1]);
  pos_ = 0;
}

int ShuffleBag::next() {
  if (order_.empty()) return -1;
  if (pos_ >= order_.size()) shuffle();
  last_ = order_[pos_++];
  return last_;
}

}  // namespace hal
