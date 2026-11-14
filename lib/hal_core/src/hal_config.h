#pragma once
// HAL's settings, read from /hal/config.txt on the SD card.
#include <string>
#include <vector>

namespace hal {

struct Config {
  std::string wifiSsid;
  std::string wifiPassword;
  std::string timezone = "EST5EDT,M3.2.0,M11.1.0";
  int quietStartMin = 22 * 60;
  int quietEndMin = 7 * 60 + 30;
  int volume = 180;
  int quietVolume = 90;
  int idleMinMinutes = 20;
  int idleMaxMinutes = 60;
  int greetCooldownMinutes = 10;
  int shakeCooldownSeconds = 20;
  int proximityThreshold = 120;  // LTR-553 behind the glass: a hand within a few cm
  int holdSeconds = 3;
  bool camera = true;
  int cameraMotionThreshold = 20;
  bool link = false;  // talk to the other HAL over Wi-Fi
  int linkPort = 9000;
  std::string peerHost = "hal9000-old.local";  // where HAL Zero finds the desk HAL
  // Where HAL Zero finds the other HALs, if there are any (v0.7): HAL Nano, HAL Paper, HAL Card,
  // in any order (each says who he is). Three at most.
  std::vector<std::string> friends;
  // The AI brain on the computer (HAL Zero only). `tools/brain.sh pair` writes these.
  bool brain = false;
  std::string brainHost;  // its name or address on the network
  int brainPort = 9100;
  std::string brainKey;   // the shared key: 32 lower-case hex characters. Never logged.
  // Chats of their own (HAL Zero only; needs the brain and the link): every so often the two
  // HALs strike up a short conversation that nobody asked for.
  bool chat = true;
  int chatMinMinutes = 20;
  int chatMaxMinutes = 40;
};

// The brain is switched on and has somewhere to call and a key to call with.
inline bool brainReady(const Config& c) { return c.brain && !c.brainHost.empty() && !c.brainKey.empty(); }

// key=value lines, '#' comments. Bad values keep their default and add a
// warning naming the key. `warnings` may be null.
Config parseConfig(const std::string& text, std::vector<std::string>* warnings);

}  // namespace hal
