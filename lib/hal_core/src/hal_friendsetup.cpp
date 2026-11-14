#include "hal_friendsetup.h"

#include "hal_config.h"

namespace hal {
namespace {

std::string withoutCr(std::string line) {
  if (!line.empty() && line.back() == '\r') line.pop_back();
  return line;
}

int hexValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

}  // namespace

FriendSetup parseFriendSetup(const std::string& raw) {
  FriendSetup out;
  const std::string line = withoutCr(raw);
  if (line.compare(0, 7, "HALCFG ") != 0) return out;
  out.mine = true;
  const std::string digits = line.substr(7);
  if (digits.empty() || digits.size() % 2 != 0 || digits.size() > 2 * kFriendSetupMax) return out;
  std::string text;
  for (size_t i = 0; i < digits.size(); i += 2) {
    const int hi = hexValue(digits[i]), lo = hexValue(digits[i + 1]);
    if (hi < 0 || lo < 0) return out;
    const char c = static_cast<char>(hi * 16 + lo);
    if (c != '\n' && (c < 32 || c > 126)) return out;  // settings are plain text
    text.push_back(c);
  }
  const Config cfg = parseConfig(text, nullptr);
  if (cfg.wifiSsid.empty() || cfg.wifiSsid.size() > 32) return out;
  if (!cfg.wifiPassword.empty() && (cfg.wifiPassword.size() < 8 || cfg.wifiPassword.size() > 63)) return out;
  out.ok = true;
  out.text = text;
  return out;
}

std::string formatFriendSetup(const std::string& text) {
  static const char digits[] = "0123456789abcdef";
  std::string out = "HALCFG ";
  for (unsigned char c : text) {
    out.push_back(digits[c >> 4]);
    out.push_back(digits[c & 15]);
  }
  return out;
}

bool isFriendQuery(const std::string& line) { return withoutCr(line) == "HALCFG?"; }

std::string friendAnswer(const std::string& id, const std::string& version, bool wifiSet, const std::string& ip) {
  return "HALCFG " + id + " " + version + " wifi=" + (wifiSet ? "set" : "none") + " ip=" + (ip.empty() ? "-" : ip);
}

}  // namespace hal
