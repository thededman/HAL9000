// HAL Nano: an M5StickC Plus2 on Dave's wrist. A 135x240 screen, a buzzer, two buttons, a
// motion sensor and a small battery. He has no voice: he chirps his line (one short tone a
// letter, hal_chirp.h) while the words show on his screen. Button A is his tap.
#include <M5Unified.h>

#include <math.h>

#include <vector>

#include "../common/board.h"
#include "friend_screens.h"
#include "hal_cast.h"
#include "hal_chirp.h"

namespace board {

extern const int kWho = hal::kNano;
extern const char* const kHost = "hal-nano";
extern const bool kWifiSleep = true;

namespace {

constexpr uint8_t kBright = 110, kDim = 12;
constexpr uint32_t kAwakeMs = 20000;  // the screen dims this long after anything last happened
constexpr uint32_t kFrameMs = 66;

M5Canvas frame(&M5.Display);
friendui::View view;
std::vector<hal::Chirp> chirps;
size_t chirpAt = 0;
bool chirping = false;
uint32_t nextChirpMs = 0, lastFrameMs = 0, awakeUntil = 0, lastBatteryMs = 0;
bool dimmed = false;

void wake(uint32_t now) {
  awakeUntil = now + kAwakeMs;
  if (dimmed) M5.Display.setBrightness(kBright);
  dimmed = false;
}

void chirp(const std::string& text) {
  chirps = hal::chirpsFor(text);
  chirpAt = 0;
  chirping = !chirps.empty();
  nextChirpMs = millis();
}

}  // namespace

void begin() {
  M5.Display.setRotation(0);
  M5.Display.setBrightness(kBright);
  frame.setPsram(true);
  frame.setColorDepth(16);
  frame.createSprite(M5.Display.width(), M5.Display.height());
  M5.Speaker.setVolume(140);
  view.me = kWho;
  wake(millis());
}

void update(uint32_t now) {
  if (chirping && int32_t(now - nextChirpMs) >= 0) {
    if (chirpAt < chirps.size()) {
      const hal::Chirp& c = chirps[chirpAt++];
      M5.Speaker.tone(c.hz, c.ms);
      nextChirpMs = now + c.ms + c.gapMs;
      view.level = 255;
    } else {
      chirping = false;
    }
  }
  if (now - lastBatteryMs > 10000 || lastBatteryMs == 0) {
    lastBatteryMs = now ? now : 1;
    view.battery = M5.Power.getBatteryLevel();
    view.charging = M5.Power.isCharging() == m5::Power_Class::is_charging;
  }
  if (!dimmed && int32_t(now - awakeUntil) > 0) {  // nothing for a while: save his little battery
    M5.Display.setBrightness(kDim);
    dimmed = true;
  }
  if (now - lastFrameMs < (dimmed ? 500u : kFrameMs)) return;
  lastFrameMs = now;
  friendui::draw(frame, view, now);
  frame.pushSprite(0, 0);
  view.level = view.level > 40 ? view.level - 40 : 0;  // the eye settles between chirps
}

bool tapped() {
  if (!M5.BtnA.wasPressed()) return false;
  wake(millis());
  return true;
}

void idle(const std::string& status) {
  view.speaker = -1;
  view.text.clear();
  view.status = status;
  wake(millis());
}

void showLine(int who, int n, const std::string& text) {
  view.speaker = who;
  view.line = n;
  view.text = text;
  wake(millis());
}

void say(const std::string& text, uint32_t ms) {
  (void)ms;  // the chirps take as long as the words do (hal::chirpMs told HAL Zero how long)
  chirp(text);
  wake(millis() + ms);
}

void stop() {
  chirping = false;
  M5.Speaker.stop();
}

void idleTap() { chirp("HAL Nano."); }

void readings(hal::PeerStatus* status, hal::PeerSensors* sensors) {
  (void)status;
  if (!sensors) return;
  // Tilt: how far his face is from lying flat, by the motion sensor.
  float ax = 0, ay = 0, az = 1;
  if (M5.Imu.isEnabled() && M5.Imu.getAccel(&ax, &ay, &az)) {
    const float g = sqrtf(ax * ax + ay * ay + az * az);
    if (g > 0.2f) {
      const float c = az / g;
      sensors->tilt = int(acosf(c > 1 ? 1 : c < -1 ? -1 : c) * 57.2958f + 0.5f);
    }
  }
}

}  // namespace board
