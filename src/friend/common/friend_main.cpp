// A friend: HAL Nano, HAL Paper or HAL Card (optional, v0.7). A small HAL that joins the
// others' conversations. HAL Zero conducts and is the only one who talks to the AI brain: a
// friend just listens on the link, shows who is speaking, and says his own lines when cued
// (lib/hal_core/src/hal_friend.h has every rule). Which HAL this is, and what his device can
// do, is in the board's folder (board.h).
//
// He has no SD card. His Wi-Fi settings live in his flash and arrive over USB from
// tools/friend_wifi.py (lib/hal_core/src/hal_friendsetup.h); the password is never printed.
#include <M5Unified.h>
#include <Preferences.h>
#include <WiFi.h>

#include <string>
#include <vector>

#include "../../link_server.h"
#include "board.h"
#include "hal_cast.h"
#include "hal_config.h"
#include "hal_friend.h"
#include "hal_friendsetup.h"
#include "hal_link.h"
#include "hal_log.h"

namespace {

constexpr char kVersion[] = "0.7.0";
constexpr uint32_t kRejoinMs = 20000;  // how often Wi-Fi is tried again while it won't join

hal::Config cfg;
LinkServer zeroLink;  // his end of the link: HAL Zero connects to it
hal::Friend self(board::kWho);
hal::LinkLiveness zeroLiveness;  // HAL Zero pings every 2 s: silence means he lost power
std::vector<hal::Message> inbox;
std::string serialLine;
bool wifiAsked = false, wasJoined = false, wasLinked = false, serialOverflow = false;
uint32_t wifiAskedAt = 0;
bool saying = false;  // his own line is being chirped or written
uint32_t sayUntil = 0;
int linesSaid = 0;
std::string shownStatus;

int clampInt(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

// What the idle screen says under his name.
void status(const std::string& text) {
  if (text == shownStatus && !self.talking()) return;
  shownStatus = text;
  board::idle(text);
}

std::string idleStatus() {
  if (cfg.wifiSsid.empty()) return "NO WI-FI SET UP: tools/friend_wifi.py";
  if (WiFi.status() != WL_CONNECTED) return "JOINING WI-FI";
  return zeroLink.connected() ? "WITH HAL ZERO" : "WAITING FOR HAL ZERO";
}

void loadSettings() {
  Preferences prefs;
  prefs.begin("hal", true);
  const String text = prefs.getString("config", "");
  prefs.end();
  cfg = hal::parseConfig(text.c_str(), nullptr);
}

void joinWifi(uint32_t now) {
  if (cfg.wifiSsid.empty()) return;
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(board::kHost);
  WiFi.setAutoReconnect(true);
  WiFi.begin(cfg.wifiSsid.c_str(), cfg.wifiPassword.c_str());
  WiFi.setSleep(board::kWifiSleep);
  wifiAsked = true;
  wifiAskedAt = now;
  halLog("wifi: joining '%s'", cfg.wifiSsid.c_str());
}

// A line from the USB port: his settings, or a question about him. Anything else is ignored.
void onSerialLine(const std::string& line, uint32_t now) {
  if (hal::isFriendQuery(line)) {
    const bool joined = WiFi.status() == WL_CONNECTED;
    Serial.println(hal::friendAnswer(hal::castId(board::kWho), kVersion, !cfg.wifiSsid.empty(),
                                     joined ? WiFi.localIP().toString().c_str() : "")
                       .c_str());
    return;
  }
  const hal::FriendSetup setup = hal::parseFriendSetup(line);
  if (!setup.mine) return;
  if (!setup.ok) {
    Serial.println("HALCFG BAD");
    return;
  }
  Preferences prefs;
  prefs.begin("hal", false);
  const bool kept = prefs.putString("config", setup.text.c_str()) > 0;
  prefs.end();
  Serial.println(kept ? "HALCFG OK" : "HALCFG BAD");
  if (!kept) return;
  loadSettings();
  WiFi.disconnect();
  joinWifi(now);
}

void pumpSerial(uint32_t now) {
  while (Serial.available() > 0) {
    const char c = char(Serial.read());
    if (c == '\n') {
      if (!serialOverflow) onSerialLine(serialLine, now);
      serialLine.clear();
      serialOverflow = false;
    } else if (serialLine.size() < 2 * hal::kFriendSetupMax + 16) {
      serialLine.push_back(c);
    } else {
      serialOverflow = true;  // far too long to be for him: dropped at its end
    }
  }
}

void sendState() {
  hal::Message m;
  m.type = hal::MsgType::State;
  hal::PeerStatus& s = m.status;
  s.upS = int(millis() / 1000);
  s.heap = clampInt(int(ESP.getFreeHeap()), 0, 9999999);
  s.psram = clampInt(int(ESP.getFreePsram()), 0, 99999999);
  s.cam = 0;
  s.lines = linesSaid;
  s.mode = saying ? "speaking" : "idle";
  s.rssi = clampInt(int(WiFi.RSSI()), -127, 0);
  s.fw = kVersion;
  s.cores = clampInt(int(ESP.getChipCores()), 1, 64);
  s.mhz = clampInt(int(ESP.getCpuFreqMHz()), 1, 9999);
  board::readings(&s, nullptr);
  zeroLink.send(m);
}

void sendSensors() {
  hal::Message m;
  m.type = hal::MsgType::Sensors;
  hal::PeerSensors& s = m.sensors;
  s.light = -1;  // no friend has a light sensor
  s.prox = 0;
  s.tilt = 0;
  s.bat = clampInt(int(M5.Power.getBatteryLevel()), -1, 100);
  s.chg = M5.Power.isCharging() == m5::Power_Class::is_charging;
  s.temp = clampInt(int(temperatureRead()), -40, 125);
  board::readings(nullptr, &s);
  zeroLink.send(m);
}

// What the friend logic decided: answers for HAL Zero, and what his device should do.
void act(const hal::FriendOut& o, uint32_t now) {
  if (o.stop) {
    saying = false;
    board::stop();
    shownStatus.clear();
    status(idleStatus());
  }
  if (o.show) board::showLine(o.showWho, o.showN, o.showText);
  for (const hal::Message& m : o.send) zeroLink.send(m);  // (PLAYING goes out before he starts: the conductor waits 2 s for it)
  if (o.wantState) sendState();
  if (o.wantSensors) sendSensors();
  if (o.say) {
    saying = true;
    sayUntil = now + o.sayMs;
    board::say(o.sayText, o.sayMs);
  }
  if (o.idleTap) board::idleTap();
}

}  // namespace

void setup() {
  auto m5cfg = M5.config();
  M5.begin(m5cfg);
  Serial.begin(115200);
  board::begin();
  loadSettings();
  halLog("%s %s, wifi %s", hal::castName(board::kWho), kVersion, cfg.wifiSsid.empty() ? "not set up" : "set up");
  status(idleStatus());
  joinWifi(millis());
}

void loop() {
  M5.update();
  const uint32_t now = millis();
  pumpSerial(now);

  // Wi-Fi: the radio rejoins by itself; if it hasn't for a while, it is asked again.
  const bool joined = WiFi.status() == WL_CONNECTED;
  if (joined && !wasJoined) halLog("wifi: joined as %s", WiFi.localIP().toString().c_str());
  if (!joined && wifiAsked && now - wifiAskedAt > kRejoinMs) joinWifi(now);
  wasJoined = joined;
  if (joined && !zeroLink.started()) {
    zeroLink.begin(cfg.linkPort, board::kHost, hal::kLinkVersionFriends, hal::castId(board::kWho), hal::kLinkMaxLineFriends);
  }

  // HAL Zero's messages.
  bool dropped = false;
  zeroLink.poll(&inbox, &dropped);
  const bool linked = zeroLink.connected();
  if (linked && !wasLinked) zeroLiveness.connected(now);
  wasLinked = linked;
  for (const hal::Message& m : inbox) {
    zeroLiveness.heard(now);
    act(self.onMessage(m, now), now);
  }
  if (linked && zeroLiveness.silent(now)) zeroLink.drop();  // no hang-up when HAL Zero loses power
  if (dropped) act(self.onDisconnected(), now);
  act(self.tick(now), now);

  if (board::tapped()) act(self.onTap(), now);
  if (saying && int32_t(now - sayUntil) >= 0) {  // his line is finished
    saying = false;
    ++linesSaid;
    act(self.onSaid(), now);
  }
  if (!self.talking()) status(idleStatus());
  board::update(now);
  delay(5);
}
