#pragma once
// A spoken line laid out for the subtitle box: broken at words when that fits, and filled to the
// edge of every row when it doesn't, so the end of a full-length line is never left off.
#include <cstddef>
#include <string>
#include <vector>

namespace hal {

// At most `rows` rows of at most `width` characters. Text too long even for that ends in "...".
std::vector<std::string> subtitleRows(const std::string& text, size_t width, size_t rows);

}  // namespace hal
