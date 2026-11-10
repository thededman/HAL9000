// HAL 9000 for the M5Stack CoreS3.
//
// Every loop: read the inputs (touch, proximity, motion, clock, speaker), hand
// them to hal::Brain, carry out what it decides, and draw the eye or a console
// screen at ~30 fps. All the decision-making lives in lib/hal_core.
#include <M5Unified.h>
#include <SD.h>
#include <SPI.h>
#include <esp_system.h>

#include <memory>

#include "audio.h"
#include "camera.h"
#include "consoles.h"
#include "debug_shot.h"
#include "eye.h"
#include "hal_brain.h"
#include "hal_clock.h"
#include "hal_motion.h"
#include "hal_voice_index.h"
#include "net_time.h"
#include "proximity.h"
#include "hal_loader.h"
#include "sd_storage.h"

namespace {

constexpr uint32_t kShutdownMagic = 0x48414C21;  // "HAL!"
RTC_NOINIT_ATTR uint32_t shutdownFlag;           // survives ESP.restart()

Audio audio;
hal::VoiceIndex voices(esp_random());
hal::Config config;
std::unique_ptr<hal::Brain> brain;
hal::MotionDetector motion;
std::unique_ptr<hal::ProximityEdge> proxEdge;
std::unique_ptr<hal::ArrivalDetector> arrival;  // someone walked into the camera's view
Consoles consoles;
M5Canvas lens(&M5.Display);
M5Canvas screen(&M5.Display);

bool haveMemory = false;
bool haveProximity = false;
bool haveCamera = false;
int camFrames = 0;
int camPeak = 0;  // highest motion score since the last log line
float gyroPeak = 0;  // highest gyro rate (deg/s) since the last log line
hal::Mode lastMode = hal::Mode::Booting;
bool lastQuiet = false;
int lastBacklight = -1;
uint32_t consoleStartMs = 0;
uint32_t lastFrameMs = 0, lastProxMs = 0, lastImuMs = 0, lastLogMs = 0;
int proxReading = 0;
int proxPeak = 0;  // highest reading since the last log line
float ax = 0, ay = 0, az = 1;
bool bootSyncPending = true;
int lastSyncYday = -1;
uint32_t lastSyncAttemptMs = 0;

// Debug injections over serial (see serviceDebugCommands): T tap, H hold, P walk-up, K shake, W bad Wi-Fi.
uint32_t fakeTouchUntil = 0;
bool fakeHold = false, fakeProx = false, fakeShake = false;

const char* modeName(hal::Mode m) {
  static const char* const names[] = {"booting", "idle", "speaking", "console", "pleading", "daisy", "dark"};
  return names[static_cast<int>(m)];
}

bool mountSd() {
  int cs = M5.getPin(m5::pin_name_t::sd_spi_cs);
  if (cs < 0) cs = GPIO_NUM_4;
  SPI.begin(M5.getPin(m5::pin_name_t::sd_spi_sclk), M5.getPin(m5::pin_name_t::sd_spi_miso),
            M5.getPin(m5::pin_name_t::sd_spi_mosi), cs);
  return SD.begin(cs, SPI, 25000000);
}

void play(const hal::Actions& a) {
  std::string path;
  bool found = a.index < 0 ? voices.pick(a.folder, &path) : voices.at(a.folder, a.index, &path);
  if (found) audio.playFile(path.c_str());
}

void setBacklight(int level) {
  if (level == lastBacklight) return;
  lastBacklight = level;
  M5.Display.setBrightness(level);
}

void drawFrame() {
  uint32_t now = millis();
  hal::Mode m = brain ? brain->mode() : hal::Mode::Booting;
  if (m == hal::Mode::Console) {
    if (consoles.draw(screen, now - consoleStartMs, audio.level())) screen.pushSprite(0, 0);
    return;
  }
  float daisy = brain ? brain->daisyProgress() : 0;
  drawEye(lens, eyeParamsFor(m, lastQuiet, daisy, audio.level(), now));
  lens.pushSprite(kEyeCx - kLensSprite / 2, kEyeCy - kLensSprite / 2, eyeTransparentKey());
}

// Keeps the eye moving while something slow (Wi-Fi) blocks the loop.
void frameIfDue() {
  if (millis() - lastFrameMs >= 33) {
    lastFrameMs = millis();
    drawFrame();
  }
}

hal::Inputs readInputs(uint32_t now) {
  hal::Inputs in;
  in.nowMs = now;
  in.touchDown = M5.Touch.getCount() > 0 || now < fakeTouchUntil || fakeHold;
  if (haveProximity && now - lastProxMs >= 50) {
    lastProxMs = now;
    int r = proximityRead();
    if (r >= 0) {
      proxReading = r;
      if (r > proxPeak) proxPeak = r;
      in.proximityEdge = proxEdge->update(r);
    }
  }
  if (now - lastImuMs >= 20) {
    lastImuMs = now;
    const float prev[3] = {ax, ay, az};
    if (M5.Imu.getAccel(&ax, &ay, &az)) {
      in.motion = motion.update(ax, ay, az, now);
      // If HAL himself moves, the camera view swings: that's not someone walking in.
      float g[3] = {0, 0, 0};
      M5.Imu.getGyro(&g[0], &g[1], &g[2]);
      for (float v : g) gyroPeak = fmaxf(gyroPeak, fabsf(v));
      const float acc[3] = {ax, ay, az};
      if (arrival && hal::imuSaysMoving(acc, prev, g)) arrival->disturb();
    }
  }
  if (fakeProx) in.proximityEdge = true;
  if (fakeShake) in.motion = hal::MotionEvent::Shake;
  fakeProx = fakeShake = false;
  in.audioPlaying = audio.isPlaying();
  if (clockIsSet()) {
    time_t t = time(nullptr);
    struct tm lt;
    localtime_r(&t, &lt);
    in.timeValid = true;
    in.hour = lt.tm_hour;
    in.minuteOfDay = lt.tm_hour * 60 + lt.tm_min;
  }
  return in;
}

void maybeSyncClock(const hal::Inputs& in) {
  if (bootSyncPending && in.nowMs > 1500) {  // after the boot line has started
    bootSyncPending = false;
    lastSyncAttemptMs = in.nowMs;
    syncTime(config, 15000, frameIfDue);
    return;
  }
  if (bootSyncPending || audio.isPlaying()) return;
  time_t t = time(nullptr);
  struct tm lt;
  localtime_r(&t, &lt);
  if (!hal::clockSyncDue(in.timeValid, in.hour, lt.tm_yday, lastSyncYday, in.nowMs - lastSyncAttemptMs)) return;
  if (in.timeValid) lastSyncYday = lt.tm_yday;
  lastSyncAttemptMs = in.nowMs;
  syncTime(config, 15000, frameIfDue);
}

}  // namespace

void setup() {
  bool afterShutdown = esp_reset_reason() == ESP_RST_SW && shutdownFlag == kShutdownMagic;
  shutdownFlag = 0;

  auto cfg = M5.config();
  M5.begin(cfg);
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);  // never stall the loop when no monitor is reading the USB port
  for (uint32_t t0 = millis(); !Serial && millis() - t0 < 2000;) delay(10);  // let a USB monitor attach
  Serial.printf("\nHAL 9000 booting%s\n", afterShutdown ? " (after shutdown)" : "");

  if (mountSd()) {
    SdStorage storage;
    std::vector<std::string> warnings;
    config = hal::loadConfig(storage, &warnings);
    for (auto& w : warnings) Serial.printf("config: %s\n", w.c_str());
    haveMemory = hal::loadVoiceIndex(storage, voices);
    for (int i = 0; haveMemory && i < hal::kFolderCount; ++i) {
      auto f = static_cast<hal::Folder>(i);
      Serial.printf("  %-28s %d\n", hal::folderPath(f), voices.count(f));
    }
  } else {
    Serial.println("no SD card");
  }
  restoreTimeFromRtc(config);
  haveProximity = proximityBegin();
  proxEdge.reset(new hal::ProximityEdge(config.proximityThreshold));
  arrival.reset(new hal::ArrivalDetector(config.cameraMotionThreshold));

  lens.setPsram(false);
  lens.setColorDepth(16);
  lens.createSprite(kLensSprite, kLensSprite);
  screen.setPsram(false);   // 75 KB in internal RAM
  screen.setColorDepth(8);  // RGB332 is plenty for the console readouts
  screen.createSprite(320, 240);
  if (config.camera) haveCamera = cameraBegin();
  else Serial.println("  camera off in config.txt");
  consoles.begin(esp_random(), haveCamera);

  brain.reset(new hal::Brain(config, esp_random(), afterShutdown));
  brain->setSequenceLengths(voices.count(hal::Folder::Plea), voices.count(hal::Folder::Daisy));
  setBacklight(160);
  drawEyeBackground(M5.Display, !haveMemory);
}

void loop() {
  M5.update();
  uint32_t now = millis();
  switch (serviceDebugCommands()) {
    case 'T': fakeTouchUntil = now + 100; break;
    case 'H': fakeHold = !fakeHold; break;
    case 'P': fakeProx = true; break;
    case 'K': fakeShake = true; break;
    case 'W': {  // debug: a Wi-Fi network that doesn't exist must time out without freezing HAL
      hal::Config bogus = config;
      bogus.wifiSsid = "hal-no-such-network";
      uint32_t t0 = millis();
      syncTime(bogus, 15000, frameIfDue);
      Serial.printf("bogus wifi gave up after %lu ms\n", (unsigned long)(millis() - t0));
      break;
    }
  }

  bool camArrival = false;
  if (haveCamera && cameraPoll(now)) {
    ++camFrames;
    if (cameraScore() > camPeak) camPeak = cameraScore();
    camArrival = arrival->update(cameraScore(), now);
    if (camArrival) Serial.printf("arrival (score %d)\n", cameraScore());
  }
  hal::Inputs in = readInputs(now);
  if (camArrival) in.proximityEdge = true;  // a walk-in greets like a wave
  hal::Actions a = brain->tick(in);

  audio.setVolume(static_cast<uint8_t>(a.volume));
  lastQuiet = a.quiet;
  if (a.stopAudio) audio.stop();
  if (a.newConsole) {
    bool visual = consoles.next();
    consoleStartMs = now;
    // Visual Analysis gets a camera line, if there are any on the card.
    if (visual && a.play && voices.count(hal::Folder::Camera) > 0) a.folder = hal::Folder::Camera;
  }
  if (a.play) play(a);
  if (a.restart) {
    shutdownFlag = kShutdownMagic;
    Serial.println("restarting");
    Serial.flush();
    ESP.restart();
  }

  hal::Mode m = brain->mode();
  if (m != lastMode) {
    Serial.printf("mode %s -> %s\n", modeName(lastMode), modeName(m));
    if (lastMode == hal::Mode::Console) drawEyeBackground(M5.Display, !haveMemory);
    lastMode = m;
  }
  setBacklight(m == hal::Mode::Dark ? 0 : (a.quiet ? 60 : 160));

  if (now - lastLogMs >= 500) {
    lastLogMs = now;
    if (haveCamera) {
      Serial.printf("cam=%d score=%d settled=%d gyro=%.1f heap=%u ", camFrames * 2, camPeak, arrival->settled(), gyroPeak,
                    unsigned(ESP.getFreeHeap()));
    }
    camFrames = 0;
    camPeak = 0;
    gyroPeak = 0;
    Serial.printf("prox=%d acc=%.2f,%.2f,%.2f mode=%s quiet=%d\n", proxPeak, ax, ay, az, modeName(m), a.quiet);
    proxPeak = 0;
  }
  frameIfDue();
  maybeSyncClock(in);
  delay(1);
}
