#include "hal_config.h"

#include <cctype>

#include "hal_clock.h"

namespace hal {
namespace {

std::string trim(const std::string& s) {
  size_t a = 0, b = s.size();
  while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
  while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
  return s.substr(a, b - a);
}

// Cut at the first '#' that follows whitespace ("150  # loud" -> "150  ").
std::string stripInlineComment(const std::string& s) {
  for (size_t i = 1; i < s.size(); ++i) {
    if (s[i] == '#' && std::isspace(static_cast<unsigned char>(s[i - 1]))) return s.substr(0, i);
  }
  return s;
}

bool parseInt(const std::string& s, int* out) {
  if (s.empty() || s.size() > 6) return false;
  for (char ch : s) {
    if (!std::isdigit(static_cast<unsigned char>(ch))) return false;
  }
  *out = std::stoi(s);
  return true;
}

struct IntKey {
  const char* name;
  int Config::*field;
  int lo;
  int hi;
};

const IntKey kIntKeys[] = {
    {"volume", &Config::volume, 0, 255},
    {"quiet_volume", &Config::quietVolume, 0, 255},
    {"idle_min_minutes", &Config::idleMinMinutes, 1, 1440},
    {"idle_max_minutes", &Config::idleMaxMinutes, 1, 1440},
    {"greet_cooldown_minutes", &Config::greetCooldownMinutes, 1, 1440},
    {"shake_cooldown_seconds", &Config::shakeCooldownSeconds, 0, 3600},
    {"proximity_threshold", &Config::proximityThreshold, 1, 2047},
    {"hold_seconds", &Config::holdSeconds, 1, 10},
};

}  // namespace

Config parseConfig(const std::string& input, std::vector<std::string>* warnings) {
  Config c;
  auto warn = [&](const std::string& msg) {
    if (warnings) warnings->push_back(msg);
  };

  std::string text = input;
  if (text.compare(0, 3, "\xEF\xBB\xBF") == 0) text.erase(0, 3);

  size_t pos = 0;
  while (pos <= text.size()) {
    size_t nl = text.find('\n', pos);
    std::string line = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
    pos = (nl == std::string::npos) ? text.size() + 1 : nl + 1;
    if (!line.empty() && line.back() == '\r') line.pop_back();

    std::string t = trim(line);
    if (t.empty() || t[0] == '#') continue;
    size_t eq = t.find('=');
    if (eq == std::string::npos) {
      warn("ignored line without '=': " + t);
      continue;
    }
    std::string key = trim(t.substr(0, eq));
    std::string raw = t.substr(eq + 1);

    if (key == "wifi_ssid") { c.wifiSsid = trim(raw); continue; }
    if (key == "wifi_password") { c.wifiPassword = trim(raw); continue; }

    std::string value = trim(stripInlineComment(raw));
    if (key == "timezone") {
      // POSIX TZ strings always carry an hour offset and never a '/' (city names do).
      bool hasDigit = value.find_first_of("0123456789") != std::string::npos;
      if (value.empty() || !hasDigit || value.find('/') != std::string::npos) {
        warn("timezone: '" + value + "' isn't a POSIX TZ string like EST5EDT,M3.2.0,M11.1.0, using default");
      } else {
        c.timezone = value;
      }
      continue;
    }
    if (key == "quiet_start" || key == "quiet_end") {
      int m = parseHHMM(value);
      if (m < 0) warn(key + ": '" + value + "' is not a time like 22:00, using default");
      else (key == "quiet_start" ? c.quietStartMin : c.quietEndMin) = m;
      continue;
    }
    bool known = false;
    for (const IntKey& k : kIntKeys) {
      if (key != k.name) continue;
      known = true;
      int v;
      if (!parseInt(value, &v) || v < k.lo || v > k.hi) {
        warn(key + ": '" + value + "' must be a number from " + std::to_string(k.lo) + " to " +
             std::to_string(k.hi) + ", using default");
      } else {
        c.*k.field = v;
      }
      break;
    }
    if (!known) warn("unknown setting '" + key + "' ignored");
  }

  if (c.idleMinMinutes > c.idleMaxMinutes) {
    warn("idle_min_minutes is larger than idle_max_minutes, using defaults for both");
    Config d;
    c.idleMinMinutes = d.idleMinMinutes;
    c.idleMaxMinutes = d.idleMaxMinutes;
  }
  return c;
}

}  // namespace hal
