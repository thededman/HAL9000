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

bool validToken(const std::string& t) {
  if (t.size() < 16 || t.size() > 32) return false;
  for (char c : t)
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  return true;
}

bool parseSensors(const std::vector<std::string>& w, PeerSensors* s) {
  if (w.size() != 7) return false;
  unsigned seen = 0;
  for (size_t i = 1; i < w.size(); ++i) {
    size_t eq = w[i].find('=');
    if (eq == std::string::npos) return false;
    std::string k = w[i].substr(0, eq), v = w[i].substr(eq + 1);
    static const char* const keys[] = {"light", "prox", "tilt", "bat", "chg", "temp"};
    int idx = -1;
    for (int j = 0; j < 6; ++j)
      if (k == keys[j]) idx = j;
    if (idx < 0 || (seen & (1u << idx))) return false;
    seen |= 1u << idx;
    bool ok = false;
    int chg = 0;
    switch (idx) {
      case 0: ok = toSigned(v, -1, 100, 3, &s->light); break;
      case 1: ok = toSigned(v, 0, 65535, 5, &s->prox) && v[0] != '-'; break;
      case 2: ok = toSigned(v, 0, 180, 3, &s->tilt) && v[0] != '-'; break;
      case 3: ok = toSigned(v, -1, 100, 3, &s->bat); break;
      case 4: ok = (v == "0" || v == "1") && toInt(v, 0, 1, &chg); s->chg = chg == 1; break;
      case 5: ok = toSigned(v, -40, 125, 3, &s->temp); break;
    }
    if (!ok) return false;
  }
  return seen == 0x3Fu;
}

bool validReason(const std::string& r) {
  return r == "done" || r == "interrupted" || r == "timeout" || r == "busy" || r == "offline";
}

}  // namespace

namespace {

// "a.b.c.d" with each part 0..255 and no leading zeros.
bool parseIpv4(const std::string& dotted, int part[4]) {
  size_t start = 0;
  for (int n = 0; n < 4; ++n) {
    size_t dot = dotted.find('.', start);
    if ((dot == std::string::npos) != (n == 3)) return false;  // exactly four parts
    std::string piece = dotted.substr(start, n == 3 ? std::string::npos : dot - start);
    if (piece.empty() || piece.size() > 3 || (piece.size() > 1 && piece[0] == '0')) return false;
    for (char c : piece)
      if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    part[n] = std::stoi(piece);
    if (part[n] > 255) return false;
    start = dot + 1;
  }
  return true;
}

}  // namespace

bool isPrivateIpv4(const std::string& dotted) {
  int part[4] = {0, 0, 0, 0};
  if (!parseIpv4(dotted, part)) return false;
  return part[0] == 10 || (part[0] == 172 && part[1] >= 16 && part[1] <= 31) || (part[0] == 192 && part[1] == 168);
}

bool sameSubnet(const std::string& a, const std::string& b, const std::string& mask) {
  int pa[4], pb[4], pm[4];
  if (!parseIpv4(a, pa) || !parseIpv4(b, pb) || !parseIpv4(mask, pm) || pm[0] == 0) return false;
  for (int i = 0; i < 4; ++i)
    if ((pa[i] & pm[i]) != (pb[i] & pm[i])) return false;
  return true;
}

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
  if (cmd == "FETCH" && w.size() == 5 && toInt(w[1], 1, kMaxDialogueLines, &ok.n) && isPrivateIpv4(w[2]) &&
      toInt(w[3], 1024, 65535, &ok.port) && validToken(w[4])) {
    ok.type = MsgType::Fetch;
    ok.host = w[2];
    ok.token = w[4];
    return ok;
  }
  if (cmd == "READY" && w.size() == 2 && toInt(w[1], 1, kMaxDialogueLines, &ok.n)) {
    ok.type = MsgType::Ready;
    return ok;
  }
  if (cmd == "SENSE" && w.size() == 1) {
    ok.type = MsgType::Sense;
    return ok;
  }
  if (cmd == "SENSORS" && parseSensors(w, &ok.sensors)) {
    ok.type = MsgType::Sensors;
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
    case MsgType::Fetch: return "FETCH " + std::to_string(m.n) + " " + m.host + " " + std::to_string(m.port) + " " + m.token;
    case MsgType::Ready: return "READY " + std::to_string(m.n);
    case MsgType::Sense: return "SENSE";
    case MsgType::Sensors: {
      const PeerSensors& s = m.sensors;
      return "SENSORS light=" + std::to_string(s.light) + " prox=" + std::to_string(s.prox) + " tilt=" +
             std::to_string(s.tilt) + " bat=" + std::to_string(s.bat) + " chg=" + (s.chg ? "1" : "0") + " temp=" +
             std::to_string(s.temp);
    }
    default: return "";
  }
}

PeerState peerState(bool connected, int peerVersion, uint32_t requestMs, uint32_t lastStateMs, uint32_t nowMs) {
  if (!connected) return PeerState::NotResponding;
  if (peerVersion == 1) return PeerState::TooOld;
  if (requestMs && nowMs - requestMs >= 2000) return PeerState::NotResponding;  // asked, no STATE within 2 s
  if (lastStateMs && nowMs - lastStateMs <= 5000) return PeerState::Ok;
  // Asked and still in time, or nobody has asked lately: not known either way.
  return PeerState::Waiting;
}

void PeerWatch::sent(uint32_t now) {
  if (now == 0) now = 1;  // 0 means "never"
  if (waitingSince_ == 0) waitingSince_ = now;
  lastSent_ = now;
}

void PeerWatch::received(uint32_t now) {
  lastState_ = now == 0 ? 1 : now;
  waitingSince_ = 0;
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

bool LinkGuard::take(uint32_t nowMs, const Read& read, std::vector<Message>* inbox) {
  inbox->clear();
  if (flooded_) return false;
  if (!counting_ || nowMs - windowMs_ >= 1000) {  // a new second: count afresh
    counting_ = true;
    windowMs_ = nowMs;
    lines_ = bytes_ = 0;
  }
  std::string last;
  char buf[128];
  while (!flooded_) {
    // One byte past the budget is asked for, because reading it is what proves the flood.
    size_t want = kLinkFloodBytes + 1 - bytes_;
    if (want > sizeof buf) want = sizeof buf;
    int n = read(buf, want);
    if (n <= 0) break;
    bytes_ += size_t(n);
    for (const std::string& line : splitter_.feed(buf, size_t(n))) {
      if (++lines_ > kLinkFloodLines) break;
      Message m = parseMessage(line);
      if (m.type == MsgType::Invalid) continue;  // still counted: it took reading and parsing
      inbox->push_back(m);
      last = line;
    }
    flooded_ = lines_ > kLinkFloodLines || bytes_ > kLinkFloodBytes;
  }
  if (flooded_) {
    inbox->clear();
    cooling_ = true;
    floodMs_ = nowMs;
    return false;
  }
  if (!inbox->empty()) last_ = last;
  return true;
}

bool LinkGuard::coolingOff(uint32_t nowMs) {
  if (cooling_ && nowMs - floodMs_ >= kLinkCoolOffMs) cooling_ = false;
  return cooling_;
}

void LinkGuard::newPeer() {
  splitter_ = LineSplitter();
  lines_ = bytes_ = 0;
  counting_ = flooded_ = cooling_ = false;
}

}  // namespace hal
