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

// A whole number in lo..hi, optionally negative, at most `digits` digits.
bool toSigned(const std::string& s, long lo, long hi, size_t digits, int* out) {
  size_t start = (!s.empty() && s[0] == '-') ? 1 : 0;
  if (s.size() == start || s.size() - start > digits) return false;
  for (size_t i = start; i < s.size(); ++i)
    if (!std::isdigit(static_cast<unsigned char>(s[i]))) return false;
  long v = std::stol(s);
  if (v < lo || v > hi) return false;
  *out = static_cast<int>(v);
  return true;
}

bool validMode(const std::string& m) {
  static const char* const modes[] = {"booting", "idle", "speaking", "console", "pleading", "daisy", "dark"};
  for (const char* x : modes)
    if (m == x) return true;
  return false;
}

// Digits and dots, starting and ending with a digit, no empty parts, 1..8 chars.
bool validFw(const std::string& f) {
  if (f.empty() || f.size() > 8) return false;
  bool lastDigit = false;
  for (char c : f) {
    if (std::isdigit(static_cast<unsigned char>(c))) lastDigit = true;
    else if (c == '.' && lastDigit) lastDigit = false;
    else return false;
  }
  return lastDigit;
}

bool parseState(const std::vector<std::string>& w, PeerStatus* st) {
  if (w.size() != 11) return false;
  unsigned seen = 0;
  for (size_t i = 1; i < w.size(); ++i) {
    size_t eq = w[i].find('=');
    if (eq == std::string::npos) return false;
    std::string k = w[i].substr(0, eq), v = w[i].substr(eq + 1);
    static const char* const keys[] = {"up", "heap", "psram", "cam", "lines", "mode", "rssi", "fw", "cores", "mhz"};
    int idx = -1;
    for (int j = 0; j < 10; ++j)
      if (k == keys[j]) idx = j;
    if (idx < 0 || (seen & (1u << idx))) return false;
    seen |= 1u << idx;
    bool ok = false;
    switch (idx) {
      case 0: ok = toSigned(v, 0, 999999999, 9, &st->upS) && v[0] != '-'; break;
      case 1: ok = toSigned(v, 0, 9999999, 7, &st->heap) && v[0] != '-'; break;
      case 2: ok = toSigned(v, 0, 99999999, 8, &st->psram) && v[0] != '-'; break;
      case 3: ok = toSigned(v, 0, 100, 3, &st->cam) && v[0] != '-'; break;
      case 4: ok = toSigned(v, 0, 9999999, 7, &st->lines) && v[0] != '-'; break;
      case 5: ok = validMode(v); st->mode = v; break;
      case 6: ok = toSigned(v, -127, 0, 3, &st->rssi); break;
      case 7: ok = validFw(v); st->fw = v; break;
      case 8: ok = toSigned(v, 1, 64, 2, &st->cores) && v[0] != '-'; break;
      case 9: ok = toSigned(v, 1, 9999, 4, &st->mhz) && v[0] != '-'; break;
    }
    if (!ok) return false;
  }
  return seen == 0x3FFu;
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
  if (cmd == "STATUS" && w.size() == 1) {
    ok.type = MsgType::Status;
    return ok;
  }
  if (cmd == "STATE" && parseState(w, &ok.status)) {
    ok.type = MsgType::State;
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
    case MsgType::Status: return "STATUS";
    case MsgType::State: {
      const PeerStatus& s = m.status;
      return "STATE up=" + std::to_string(s.upS) + " heap=" + std::to_string(s.heap) + " psram=" +
             std::to_string(s.psram) + " cam=" + std::to_string(s.cam) + " lines=" + std::to_string(s.lines) +
             " mode=" + s.mode + " rssi=" + std::to_string(s.rssi) + " fw=" + s.fw + " cores=" +
             std::to_string(s.cores) + " mhz=" + std::to_string(s.mhz);
    }
    default: return "";
  }
}

PeerState peerState(bool connected, int peerVersion, uint32_t requestMs, uint32_t lastStateMs, uint32_t nowMs) {
  if (!connected) return PeerState::NotResponding;
  if (peerVersion == 1) return PeerState::TooOld;
  if (lastStateMs && nowMs - lastStateMs <= 5000) return PeerState::Ok;
  if (requestMs && nowMs - requestMs < 2000) return PeerState::Waiting;
  return PeerState::NotResponding;
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
