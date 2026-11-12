#pragma once
// The line Dave is typing to HAL Zero: plain characters, a fixed length, Backspace.
#include <cstddef>
#include <string>

namespace hal {

class TextBox {
 public:
  explicit TextBox(size_t max = 120) : max_(max) {}

  // Adds a printable ASCII character. False (and no change) when it isn't one, the
  // box is full, or it is a space with nothing typed yet.
  bool insert(char c) {
    if (c < 32 || c > 126 || text_.size() >= max_ || (c == ' ' && text_.empty())) return false;
    text_.push_back(c);
    return true;
  }
  bool backspace() {
    if (text_.empty()) return false;
    text_.pop_back();
    return true;
  }
  void clear() { text_.clear(); }
  const std::string& text() const { return text_; }
  bool empty() const { return text_.empty(); }
  bool full() const { return text_.size() >= max_; }

 private:
  size_t max_;
  std::string text_;
};

}  // namespace hal
