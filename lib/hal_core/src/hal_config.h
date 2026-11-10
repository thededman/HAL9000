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
};

// key=value lines, '#' comments. Bad values keep their default and add a
// warning naming the key. `warnings` may be null.
Config parseConfig(const std::string& text, std::vector<std::string>* warnings);

}  // namespace hal
