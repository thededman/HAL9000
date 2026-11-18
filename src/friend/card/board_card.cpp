// HAL Card: the original Cardputer (an M5Stack StampS3 behind a 240x135 screen, a small
// speaker and a keyboard). He speaks aloud, with the project's first voice, but has very
// little memory: one buffer of 96 KB holds the line he is about to say, kept from the moment
// he starts so that Wi-Fi can't take it. The brain makes his lines small enough to fit
// (unsigned 8-bit mono at 11 025 Hz). The G0 button is his tap.
#include <M5Unified.h>

#include <stdlib.h>

#include <vector>

#include "../common/board.h"
#include "card_fetch.h"
#include "friend_screens.h"
#include "hal_cast.h"
#include "hal_log.h"
#include "hal_wav.h"

namespace board {

extern const int kWho = hal::kCard;
extern const char* const kHost = "hal-card";
extern const bool kWifiSleep = false;  // quick answers matter more than his battery: he fetches every line

namespace {

constexpr size_t kClipBytes = 96 * 1024;  // measured on the device: with this and his screen buffer, 100 KB stays free
constexpr uint32_t kFrameMs = 66, kLevelMs = 50;

uint8_t* clip = nullptr;  // the one line he can hold
bool clipReady = false;   // ... and it holds one, fetched and checked
CardFetch fetcher;
M5Canvas frame(&M5.Display);
bool buffered = false;
friendui::View view;
std::vector<uint8_t> loudness;  // of the line being spoken, a value every kLevelMs
bool speaking = false;
uint32_t spokeAtMs = 0, lastFrameMs = 0, lastBatteryMs = 0;

}  // namespace

void begin() {
  clip = static_cast<uint8_t*>(malloc(kClipBytes));  // first of all: before the screen buffer, long before Wi-Fi
  M5.Display.setRotation(1);
  M5.Display.setBrightness(140);
  frame.setColorDepth(8);  // 32 KB: full colour would take twice that, and he hasn't got it
  buffered = frame.createSprite(M5.Display.width(), M5.Display.height()) != nullptr;
  M5.Speaker.setVolume(200);
  view.me = kWho;
  halLog("screen: %dx%d, %s; clip memory: %s; %u bytes free", int(M5.Display.width()), int(M5.Display.height()),
         buffered ? "buffered in 256 colours" : "NOT buffered", clip ? "96 KB kept" : "NONE", unsigned(ESP.getFreeHeap()));
}

void update(uint32_t now) {
  if (speaking) {
    const size_t at = (now - spokeAtMs) / kLevelMs;
    view.level = at < loudness.size() ? loudness[at] : 0;
    if (at >= loudness.size() && !M5.Speaker.isPlaying()) speaking = false;
  } else {
    view.level = 0;
  }
  if (now - lastBatteryMs > 10000 || lastBatteryMs == 0) {
    lastBatteryMs = now ? now : 1;
    view.battery = M5.Power.getBatteryLevel();
    view.charging = M5.Power.isCharging() == m5::Power_Class::is_charging;
  }
  if (now - lastFrameMs < kFrameMs) return;
  lastFrameMs = now;
  if (buffered) {
    friendui::draw(frame, view, now);
    frame.pushSprite(0, 0);
  } else {
    friendui::draw(M5.Display, view, now);
  }
}

bool tapped() { return M5.BtnA.wasPressed(); }

void idle(const std::string& status) {
  view.speaker = -1;
  view.text.clear();
  view.status = status;
}

void showLine(int who, int n, const std::string& text) {
  view.speaker = who;
  view.line = n;
  view.text = text;
}

void say(const std::string&, uint32_t) {}  // he speaks his lines (fetch, then play): nothing to chirp or write

bool fetch(const std::string& host, int port, const std::string& token) {
  clipReady = false;  // the buffer is about to be written over
  if (speaking) return false;
  return fetcher.begin(host, port, token, clip, kClipBytes);
}

bool fetched(bool* ok) {
  const CardFetch::State state = fetcher.poll();
  if (state == CardFetch::State::Fetching || state == CardFetch::State::Idle) return false;
  clipReady = state == CardFetch::State::Done;
  if (clipReady) halLog("clip: %u bytes", unsigned(fetcher.length()));
  *ok = clipReady;
  return true;
}

uint32_t play() {
  hal::WavInfo info;
  if (!clipReady || !hal::parseClipWav(clip, fetcher.length(), &info) || info.bitsPerSample != 8) return 0;
  loudness = hal::loudnessEnvelope(clip, info, kLevelMs);
  // Unsigned 8-bit samples straight from his buffer (the speaker reads them there while it plays).
  if (!M5.Speaker.playRaw(clip + info.dataOffset, info.dataBytes, info.sampleRate, false, 1, 0, true)) return 0;
  speaking = true;
  spokeAtMs = millis();
  return info.durationMs;
}

void stop() {
  M5.Speaker.stop();
  speaking = false;
  fetcher.abort();
  clipReady = false;
}

void idleTap() {  // a tap with nobody talking: two short notes, to say he is awake
  M5.Speaker.tone(660, 70);
  M5.Speaker.tone(880, 90, -1, false);
}

void readings(hal::PeerStatus*, hal::PeerSensors*) {}  // he has no sensors of his own beyond his battery

}  // namespace board
