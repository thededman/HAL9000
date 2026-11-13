#include "hal_subtitle.h"

namespace hal {

std::vector<std::string> subtitleRows(const std::string& text, size_t width, size_t rows) {
  std::vector<std::string> out;
  if (width < 4 || rows == 0) return out;
  // Broken at words.
  std::string flat, line, word;  // `flat`: the text with single spaces
  bool fits = true;
  auto flush = [&] {
    if (word.empty()) return;
    flat += (flat.empty() ? "" : " ") + word;
    if (word.size() > width) fits = false;
    if (line.empty()) {
      line = word;
    } else if (line.size() + 1 + word.size() <= width) {
      line += " " + word;
    } else {
      out.push_back(line);
      line = word;
    }
    word.clear();
  };
  for (char c : text) {
    if (c == ' ') flush();
    else word += c;
  }
  flush();
  if (!line.empty()) out.push_back(line);
  if (fits && out.size() <= rows) return out;

  // Too many rows that way: fill each to the edge instead.
  out.clear();
  size_t at = 0;
  while (at < flat.size() && out.size() < rows) {
    if (flat[at] == ' ') ++at;  // a row doesn't start with the space between two words
    out.push_back(flat.substr(at, width));
    at += out.back().size();
  }
  if (at < flat.size() && !out.empty()) {  // more than the box holds
    std::string& last = out.back();
    last = last.substr(0, width - 3) + "...";
  }
  return out;
}

}  // namespace hal
