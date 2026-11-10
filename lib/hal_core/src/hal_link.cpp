#include "hal_link.h"

#include <cctype>
#include <sstream>

#include "hal_dialogue.h"

namespace hal {
namespace {

std::vector<std::string> words(const std::string& line) {
  std::vector<std::string> w;
  std::istringstream in(line);
  for (std::string s; in >> s;) w.push_back(s);
  return w;
}

bool toInt(const std::string& s, int lo, int hi, int* out) {
  if (s.empty() || s.size() > 7) return false;
  for (char c : s)
    if (!std::isdigit(static_cast<unsigned char>(c))) return false;
  int v = std::stoi(s);
  if (v < lo || v > hi) return false;
  *out = v;
  return true;
}

bool validReason(const std::string& r) {
  return r == "done" || r == "interrupted" || r == "timeout" || r == "busy" || r == "offline";
}

}  // namespace

Message parseMessage(const std::string& line) {
  Message m;
  auto w = words(line);
  if (w.empty()) return m;
  const std::string& cmd = w[0];
  Message ok;
  if ((cmd == "PING" || cmd == "PONG" || cmd == "BUSY") && w.size() == 1) {
    ok.type = cmd == "PING" ? MsgType::Ping : cmd == "PONG" ? MsgType::Pong : MsgType::Busy;
    return ok;
  }
  if (cmd == "HELLO" && w.size() == 3 && (w[1] == "OLD" || w[1] == "NEW") && toInt(w[2], 1, 99, &ok.version)) {
    ok.type = MsgType::Hello;
    ok.role = w[1] == "OLD" ? Role::Old : Role::New;
    return ok;
  }
  if (cmd == "SAY" && w.size() == 3 && isValidDialogueId(w[1]) && toInt(w[2], 1, kMaxDialogueLines, &ok.n)) {
    ok.type = MsgType::Say;
    ok.id = w[1];
    return ok;
  }
  if (cmd == "PLAYING" && w.size() == 3 && toInt(w[1], 1, kMaxDialogueLines, &ok.n) && toInt(w[2], 0, 600000, &ok.ms)) {
    ok.type = MsgType::Playing;
    return ok;
  }
  if ((cmd == "MISSING" || cmd == "DONE") && w.size() == 2 && toInt(w[1], 1, kMaxDialogueLines, &ok.n)) {
    ok.type = cmd == "MISSING" ? MsgType::Missing : MsgType::Done;
    return ok;
  }
  if (cmd == "END" && w.size() == 2 && validReason(w[1])) {
    ok.type = MsgType::End;
    ok.reason = w[1];
    return ok;
  }
  return m;
}

std::string formatMessage(const Message& m) {
  switch (m.type) {
    case MsgType::Hello: return std::string("HELLO ") + (m.role == Role::Old ? "OLD " : "NEW ") + std::to_string(m.version);
    case MsgType::Ping: return "PING";
    case MsgType::Pong: return "PONG";
    case MsgType::Say: return "SAY " + m.id + " " + std::to_string(m.n);
    case MsgType::Playing: return "PLAYING " + std::to_string(m.n) + " " + std::to_string(m.ms);
    case MsgType::Missing: return "MISSING " + std::to_string(m.n);
    case MsgType::Busy: return "BUSY";
    case MsgType::Done: return "DONE " + std::to_string(m.n);
    case MsgType::End: return "END " + m.reason;
    default: return "";
  }
}

std::vector<std::string> LineSplitter::feed(const char* data, size_t len) {
  std::vector<std::string> lines;
  for (size_t i = 0; i < len; ++i) {
    char c = data[i];
    if (c == '\n') {
      if (!overflow_) lines.push_back(buf_);
      buf_.clear();
      overflow_ = false;
    } else if (!overflow_) {
      buf_.push_back(c);
      if (buf_.size() > maxLine_) {  // too long: drop it up to the next newline
        overflow_ = true;
        buf_.clear();
      }
    }
  }
  return lines;
}

}  // namespace hal
