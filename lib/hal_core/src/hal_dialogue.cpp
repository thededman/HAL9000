#include "hal_dialogue.h"

#include <cctype>
#include <cstdio>

namespace hal {
namespace {

std::string trim(const std::string& s) {
  size_t a = 0, b = s.size();
  while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
  while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
  return s.substr(a, b - a);
}

}  // namespace

bool isValidDialogueId(const std::string& id) {
  if (id.empty() || id.size() > 32) return false;
  for (char c : id) {
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return false;
  }
  return true;
}

bool parseDialogue(const std::string& id, const std::string& input, Dialogue* out, std::vector<std::string>* errors) {
  auto fail = [&](int lineNo, const std::string& msg) {
    if (errors) errors->push_back("line " + std::to_string(lineNo) + ": " + msg);
  };
  if (id == "live") {  // on the link that name means the AI brain's lines, which aren't on the card
    fail(0, "a script can't be called 'live'");
    return false;
  }
  Dialogue d;
  d.id = id;
  bool ok = true;
  if (!isValidDialogueId(id)) {
    if (errors) errors->push_back("bad dialogue id '" + id + "' (use a-z, 0-9 and -)");
    return false;
  }
  std::string text = input;
  if (text.compare(0, 3, "\xEF\xBB\xBF") == 0) text.erase(0, 3);

  int pending = 0, lineNo = 0;
  size_t pos = 0;
  while (pos <= text.size()) {
    size_t nl = text.find('\n', pos);
    std::string line = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
    pos = (nl == std::string::npos) ? text.size() + 1 : nl + 1;
    ++lineNo;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    std::string t = trim(line);
    if (t.empty() || t[0] == '#') continue;
    size_t colon = t.find(':');
    std::string tag = colon == std::string::npos ? t : trim(t.substr(0, colon));
    std::string value = colon == std::string::npos ? "" : trim(t.substr(colon + 1));

    if (tag == "TITLE") {
      d.title = value;
    } else if (tag == "PAUSE") {
      bool digits = !value.empty() && value.size() <= 5;
      for (char c : value) digits = digits && std::isdigit(static_cast<unsigned char>(c));
      int ms = digits ? std::stoi(value) : -1;
      if (ms < 0 || ms > 10000) {
        fail(lineNo, "PAUSE needs milliseconds from 0 to 10000");
        ok = false;
      } else {
        pending = ms;
      }
    } else if (tag == "OLD" || tag == "NEW") {
      if (value.empty()) {
        fail(lineNo, tag + " line has no text");
        ok = false;
        continue;
      }
      d.lines.push_back({tag == "OLD" ? Speaker::Old : Speaker::New, value, pending});
      pending = 0;
    } else {
      fail(lineNo, "unknown tag '" + tag + "' (use TITLE, OLD, NEW or PAUSE)");
      ok = false;
    }
  }
  if (d.lines.empty()) {
    if (errors) errors->push_back("the script has no OLD or NEW lines");
    ok = false;
  }
  if (static_cast<int>(d.lines.size()) > kMaxDialogueLines) {
    if (errors) errors->push_back("too many lines (max " + std::to_string(kMaxDialogueLines) + ")");
    ok = false;
  }
  if (ok && out) *out = d;
  return ok;
}

std::string dialogueVoicePath(const std::string& id, int n, Speaker who) {
  char num[8];
  std::snprintf(num, sizeof num, "%03d", n);
  return "/hal/voice/dialogue/" + id + "/" + num + (who == Speaker::Old ? "-old.wav" : "-new.wav");
}

}  // namespace hal
