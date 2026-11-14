#pragma once
// Setting up a friend (HAL Nano, HAL Paper, HAL Card). He has no SD card for a config.txt: his
// few settings (the Wi-Fi network and its password) live in his own flash, and are put there
// over USB by tools/friend_wifi.py, one line at a time at 115200 baud:
//
//   HALCFG <hex>     his settings text (config.txt lines), each byte as two hex digits
//                    -> HALCFG OK, or HALCFG BAD (and nothing is kept)
//   HALCFG?          -> HALCFG <id> <version> wifi=<set|none> ip=<address|->
//
// He never repeats what he was sent: the password goes in and does not come out.
#include <cstddef>
#include <string>

namespace hal {

constexpr size_t kFriendSetupMax = 400;  // the longest settings text, in bytes

struct FriendSetup {
  bool mine = false;  // the line was a setup (anything else on the port isn't his business)
  bool ok = false;    // ... and a usable one: `text` is to be kept
  std::string text;
};

// A usable setup is printable text that names a network (wifi_ssid, 1 to 32 characters) with
// no password or one of 8 to 63 characters.
FriendSetup parseFriendSetup(const std::string& line);
std::string formatFriendSetup(const std::string& text);  // the line that carries `text`
bool isFriendQuery(const std::string& line);              // "HALCFG?"
std::string friendAnswer(const std::string& id, const std::string& version, bool wifiSet, const std::string& ip);

}  // namespace hal
