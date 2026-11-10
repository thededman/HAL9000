#pragma once
// HAL Zero's keyboard menu: a list with a selection that wraps around.
#include <string>
#include <vector>

namespace hal {

struct MenuItem {
  std::string id;
  std::string label;
};

class Menu {
 public:
  void open(std::vector<MenuItem> items) {
    items_ = std::move(items);
    sel_ = 0;
    first_ = 0;
    open_ = true;
  }
  void close() { open_ = false; }
  bool isOpen() const { return open_; }
  void up() { move(-1); }
  void down() { move(+1); }
  int selected() const { return sel_; }
  const std::string& selectedId() const {
    static const std::string none;
    return items_.empty() ? none : items_[sel_].id;
  }
  const std::vector<MenuItem>& items() const { return items_; }
  // First row to draw when `rows` fit on screen, keeping the selection visible.
  int firstVisible(int rows);

 private:
  void move(int d) {
    if (items_.empty()) return;
    int n = static_cast<int>(items_.size());
    sel_ = ((sel_ + d) % n + n) % n;
  }
  std::vector<MenuItem> items_;
  int sel_ = 0;
  int first_ = 0;
  bool open_ = false;
};

inline int Menu::firstVisible(int rows) {
  if (rows <= 0) return 0;
  if (sel_ < first_) first_ = sel_;
  if (sel_ >= first_ + rows) first_ = sel_ - rows + 1;
  return first_;
}

}  // namespace hal
