#include "hal_minutes.h"

#include "hal_subtitle.h"

namespace hal {

uint32_t readingMs(const std::string& text) {
  const uint32_t ms = 1500 + 55 * static_cast<uint32_t>(text.size());
  return ms < kReadingFloorMs ? kReadingFloorMs : ms > 12000 ? 12000 : ms;
}

Minutes::Placed Minutes::add(int who, const std::string& text) {
  MinutesEntry entry;
  entry.who = who;
  entry.rows = subtitleRows(text, cols_, 8);
  const int rows = 1 + static_cast<int>(entry.rows.size());
  Placed placed;
  if (!entries_.empty() && next_ + rows > static_cast<int>(rowsPerPage_)) {  // no room: turn the page
    entries_.clear();
    next_ = 0;
    ++page_;
    placed.newPage = true;
  }
  placed.row = entry.row = next_;
  placed.rows = rows;
  next_ += rows + 1;  // and a blank row before the next
  entries_.push_back(entry);
  return placed;
}

void Minutes::clear() {
  entries_.clear();
  next_ = 0;
  page_ = 1;
}

}  // namespace hal
