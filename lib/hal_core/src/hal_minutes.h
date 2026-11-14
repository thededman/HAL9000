#pragma once
// HAL Paper's page: the minutes of the conversation. Each entry is a name and what was said,
// wrapped to the page's width; entries stack down the page with a blank row between, and when
// one won't fit, the page is turned. (E-ink is slow: adding an entry redraws only its own rows,
// and only a page turn redraws everything, so where an entry lands is worked out here.)
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace hal {

constexpr uint32_t kReadingFloorMs = 4000;  // HAL Paper's own line stays up at least this long
// How long a written line is held for reading, in place of the time it would take to say.
uint32_t readingMs(const std::string& text);

struct MinutesEntry {
  int who = 0;                    // a cast number (hal_cast.h)
  std::vector<std::string> rows;  // what was said, wrapped
  int row = 0;                    // the page row its name is on
};

class Minutes {
 public:
  Minutes(size_t cols, size_t rowsPerPage) : cols_(cols), rowsPerPage_(rowsPerPage) {}

  struct Placed {
    bool newPage = false;  // the page was full: draw a fresh one (holding just this entry)
    int row = 0;           // the row its name goes on
    int rows = 0;          // rows it takes: the name, then the words
  };
  Placed add(int who, const std::string& text);
  void clear();  // a fresh page 1
  const std::vector<MinutesEntry>& entries() const { return entries_; }  // what is on the page now
  int pageNumber() const { return page_; }

 private:
  size_t cols_, rowsPerPage_;
  std::vector<MinutesEntry> entries_;
  int next_ = 0;  // the next free row
  int page_ = 1;
};

}  // namespace hal
