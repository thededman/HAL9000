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
#include "hal_dialogue.h"
#include "hal_layout.h"
#include "debug_shot.h"
#include "hal_gauges.h"
#include "hal_screen_voices.h"
#include "hal_version.h"
#include "eye.h"
#include "hal_brain.h"
#include "hal_clock.h"
#include "hal_motion.h"
#include "hal_voice_index.h"
#include "net_time.h"
#include "proximity.h"
#include "hal_loader.h"
#include "link_server.h"
#include "sd_storage.h"

namespace {

constexpr uint32_t kShutdownMagic = 0x48414C21;  // "HAL!"
RTC_NOINIT_ATTR uint32_t shutdownFlag;           // survives ESP.restart()

const hal::Layout& L = hal::kCoreS3;
SdStorage storage;
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
// The HAL Zero link (link=on): who's talking and which dialogue line is due.
LinkServer linkServer;
bool linkConversing = false;
std::string linkDialogue;
int linkSayN = -1;
uint32_t linkLastMsgMs = 0;
std::vector<hal::Message> linkInbox;
hal::LinkLiveness zeroLiveness;  // HAL Zero (v2) pings every 2 s; silence = he lost power
int zeroVersion = 0;             // from HAL Zero's HELLO
bool zeroWasConnected = false;
int camFrames = 0;
int camPeak = 0;  // highest motion score since the last log line
float gyroPeak = 0;  // highest gyro rate (deg/s) since the last log line
hal::Mode lastMode = hal::Mode::Booting;
bool lastQuiet = false;
int lastBacklight = -1;
uint32_t consoleStartMs = 0;
uint32_t daisyStartMs = 0;
uint32_t lineMs = 0;  // length of the line HAL started with the current console screen
int linesSpoken = 0;  // since boot, for HAL Zero's diagnostics
float gyroNow = 0;    // deg/s, for the sensor array
hal::ScreenVoices screenVoices(1);
screens::DeskSensors deskSensors;
uint32_t lastSensorsMs = 0;
size_t debugScreen = 0;  // debug 'N': the next screen in catalogue order
bool debugNext = false;
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

bool startLine(const std::string& path) {
  if (!audio.playFile(path.c_str())) return false;
  ++linesSpoken;
  lineMs = audio.durationMs();
  return true;
}

void play(const hal::Actions& a) {
  std::string path;
  bool found = a.index < 0 ? voices.pick(a.folder, &path) : voices.at(a.folder, a.index, &path);
  if (found) startLine(path);
}

// The desk HAL's sensors, for the sensor array (read only while it's showing).
void readDeskSensors() {
  screens::DeskSensors s;
  s.valid = true;
  if (haveProximity) {
    int raw = ambientRead();
    if (raw >= 0) s.lightPct = hal::lightPercent(raw);
    s.proximity = proxReading;
  }
  s.tiltDeg = motion.tiltFromHomeDeg();
  s.motionDps = gyroNow;
  s.batteryPct = M5.Power.getBatteryLevel();
  s.charging = M5.Power.isCharging() == m5::Power_Class::is_charging;
  s.chipTempC = static_cast<int>(temperatureRead());
  s.rssi = WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0;
  s.heapFree = static_cast<int>(ESP.getFreeHeap());
  s.psramFree = static_cast<int>(ESP.getFreePsram());
  deskSensors = s;
}

screens::LinkStats linkStats() {
  screens::LinkStats k;
  k.valid = true;
  k.enabled = config.link;
  k.connected = linkServer.connected();
  k.peer = linkServer.peerAddress();
  k.msgsIn = linkServer.msgsIn();
  k.msgsOut = linkServer.msgsOut();
  k.last = linkServer.lastMessage();
  k.rssi = WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0;
  return k;
}

// The answer to HAL Zero's STATUS: our real numbers.
hal::PeerStatus deskStatus() {
  hal::PeerStatus s;
  s.upS = static_cast<int>(millis() / 1000);
  s.heap = static_cast<int>(std::min<uint32_t>(ESP.getFreeHeap(), 9999999));
  s.psram = static_cast<int>(std::min<uint32_t>(ESP.getFreePsram(), 99999999));
  s.cam = haveCamera ? std::max(0, std::min(100, cameraScore())) : 0;
  s.lines = std::min(linesSpoken, 9999999);
  s.mode = modeName(brain->mode());
  s.rssi = WiFi.status() == WL_CONNECTED ? std::max(-127, std::min(0, static_cast<int>(WiFi.RSSI()))) : 0;
  s.fw = hal::kHalVersion;
  s.cores = ESP.getChipCores();
  s.mhz = static_cast<int>(getCpuFrequencyMhz());
  return s;
}

void setBacklight(int level) {
  if (level == lastBacklight) return;
  lastBacklight = level;
  M5.Display.setBrightness(level);
}

// What the camera sees right now, for the Visual Analysis screen.
screens::CameraView cameraView() {
  screens::CameraView v;
  if (!haveCamera) return v;
  v.frame = cameraFrame();
  v.hasBox = cameraBox(&v.bx, &v.by, &v.bw, &v.bh);
  v.score = cameraScore();
  return v;
}

void drawFrame() {
  uint32_t now = millis();
  hal::Mode m = brain ? brain->mode() : hal::Mode::Booting;
  if (m == hal::Mode::Console) {
    screens::ScreenData d;
    d.cam = cameraView();
    d.voiceLevel = audio.level();
    d.lineMs = lineMs;
    d.sensors = deskSensors;
    if (consoles.currentId() == "comms") d.link = linkStats();
    if (consoles.draw(screen, M5.Display, L, now - consoleStartMs, d)) screen.pushSprite(0, 0);
    return;
  }
  if (m == hal::Mode::Daisy) {  // the memory banks go dark one by one as he sings
    screens::ScreenData d;
    d.voiceLevel = audio.level();
    d.daisyProgress = brain->daisyProgress();
    screens::memory(screen, L, now - daisyStartMs, d);
    screen.pushSprite(0, 0);
    return;
  }
  float daisy = brain ? brain->daisyProgress() : 0;
  drawEye(lens, L, eyeParamsFor(m, lastQuiet, daisy, audio.level(), now));
  lens.pushSprite(lensX(L), lensY(L), eyeTransparentKey());
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
      gyroNow = sqrtf(g[0] * g[0] + g[1] * g[1] + g[2] * g[2]);
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
  lens.createSprite(L.lensSprite, L.lensSprite);
  screen.setPsram(config.link);  // internal RAM (75 KB) unless Wi-Fi stays on for the link
  screen.setColorDepth(8);  // RGB332 is plenty for the console readouts
  screen.createSprite(L.w, L.h);
  if (config.camera) haveCamera = cameraBegin();
  else Serial.println("  camera off in config.txt");
  consoles.begin(esp_random(), screens::Owner::Desk, haveCamera, haveMemory ? &storage : nullptr);
  screenVoices = hal::ScreenVoices(esp_random());
  if (haveMemory) screenVoices.load(storage, consoles.ids());
  for (const std::string& id : consoles.ids())
    if (screenVoices.count(id) > 0) Serial.printf("  %-28s %d\n", (std::string(hal::kScreenVoiceRoot) + "/" + id).c_str(), screenVoices.count(id));

  brain.reset(new hal::Brain(config, esp_random(), afterShutdown));
  brain->setSequenceLengths(voices.count(hal::Folder::Plea), voices.count(hal::Folder::Daisy));
  setBacklight(160);
  drawEyeBackground(M5.Display, L, haveMemory ? &storage : nullptr, !haveMemory);
}

void loop() {
  M5.update();
  uint32_t now = millis();
  switch (serviceDebugCommands()) {
    case 'T': fakeTouchUntil = now + 100; break;
    case 'H': fakeHold = !fakeHold; break;
    case 'P': fakeProx = true; break;
    case 'K': fakeShake = true; break;
    case 'N':  // tap, showing the next screen in catalogue order (for screenshots)
      fakeTouchUntil = now + 100;
      debugNext = true;
      break;
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

  // HAL Zero, if he's talking to us.
  if (config.link && !linkServer.started() && WiFi.status() == WL_CONNECTED) linkServer.begin(config.linkPort);
  bool linkDropped = false;
  linkServer.poll(&linkInbox, &linkDropped);
  const bool zeroConnected = linkServer.connected();
  if (zeroConnected && !zeroWasConnected) {
    zeroLiveness.connected(now);
    zeroVersion = 0;
  }
  zeroWasConnected = zeroConnected;
  for (const hal::Message& m : linkInbox) {
    linkLastMsgMs = now;
    zeroLiveness.heard(now);
    if (m.type == hal::MsgType::Hello) {
      zeroVersion = m.version;
      continue;
    }
    hal::Message r;
    if (m.type == hal::MsgType::Ping) {
      r.type = hal::MsgType::Pong;
    } else if (m.type == hal::MsgType::Say) {
      r.n = m.n;
      if (!brain->canConverse()) {
        r.type = hal::MsgType::Busy;
      } else if (!haveMemory || !storage.exists(hal::dialogueVoicePath(m.id, m.n, hal::Speaker::Old))) {
        r.type = hal::MsgType::Missing;
      } else {
        linkConversing = true;
        linkDialogue = m.id;
        linkSayN = m.n;
        continue;
      }
    } else if (m.type == hal::MsgType::End) {
      linkConversing = false;
      continue;
    } else if (m.type == hal::MsgType::Status) {  // HAL Zero diagnosing us; mid-dialogue is fine
      r.type = hal::MsgType::State;
      r.status = deskStatus();
    } else {
      continue;
    }
    linkServer.send(r);
  }
  // A v2 HAL Zero pings every 2 s: if he goes quiet (power pulled), hang up so the
  // comms screen stops showing him and he can reconnect. v1 never pings: leave him be.
  if (zeroConnected && zeroVersion >= 2 && zeroLiveness.silent(now)) {
    Serial.println("link: HAL Zero went quiet");
    linkServer.drop();
  }
  if (linkDropped || (linkConversing && now - linkLastMsgMs > 30000)) linkConversing = false;
  in.conversing = linkConversing;
  in.sayLine = linkSayN;
  linkSayN = -1;

  hal::Actions a = brain->tick(in);
  if (a.endConversation && linkConversing) {  // Dave tapped the desk HAL mid-dialogue
    hal::Message end;
    end.type = hal::MsgType::End;
    end.reason = "interrupted";
    linkServer.send(end);
    linkConversing = false;
  }

  audio.setVolume(static_cast<uint8_t>(a.volume));
  lastQuiet = a.quiet;
  if (a.stopAudio) audio.stop();
  if (a.newConsole) {
    std::string id;
    if (debugNext) {
      std::vector<std::string> ids = consoles.ids();
      id = ids[debugScreen++ % ids.size()];
      consoles.show(id);
      debugNext = false;
    } else {
      id = consoles.next();
    }
    consoleStartMs = now;
    lineMs = 0;
    if (id == "sensors") lastSensorsMs = 0;  // fresh numbers straight away
    if (a.play) {  // the screen's own line, a camera line for Visual Analysis, or a tap line
      std::string path;
      if (hal::pickConsoleLine(id, screenVoices, voices, &path)) startLine(path);
      a.play = false;
    }
  }
  if (a.play) play(a);
  if (a.dialogueLine > 0) {
    std::string path = hal::dialogueVoicePath(linkDialogue, a.dialogueLine, hal::Speaker::Old);
    hal::Message r;
    r.n = a.dialogueLine;
    if (startLine(path)) {
      r.type = hal::MsgType::Playing;
      r.ms = int(audio.durationMs());
    } else {
      r.type = hal::MsgType::Missing;
    }
    linkServer.send(r);
  }
  if (a.dialogueDone > 0) {
    hal::Message done;
    done.type = hal::MsgType::Done;
    done.n = a.dialogueDone;
    linkServer.send(done);
  }
  if (a.restart) {
    shutdownFlag = kShutdownMagic;
    Serial.println("restarting");
    Serial.flush();
    ESP.restart();
  }

  hal::Mode m = brain->mode();
  if (m != lastMode) {
    Serial.printf("mode %s -> %s\n", modeName(lastMode), modeName(m));
    // Console screens and the Daisy memory banks draw over the eye: put it back.
    if (lastMode == hal::Mode::Console || lastMode == hal::Mode::Daisy)
      drawEyeBackground(M5.Display, L, haveMemory ? &storage : nullptr, !haveMemory);
    if (m == hal::Mode::Daisy) daisyStartMs = now;
    lastMode = m;
  }
  if (m == hal::Mode::Console && consoles.currentId() == "sensors" && (lastSensorsMs == 0 || now - lastSensorsMs >= 500)) {
    lastSensorsMs = now;
    readDeskSensors();
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
    // Light and battery as the sensor array last read them (-1 = not read yet): the
    // sensors are only read while that screen is up.
    Serial.printf("prox=%d light=%d%% bat=%d%% acc=%.2f,%.2f,%.2f mode=%s quiet=%d\n", proxPeak,
                  deskSensors.valid ? deskSensors.lightPct : -1, deskSensors.valid ? deskSensors.batteryPct : -1, ax, ay, az,
                  modeName(m), a.quiet);
    proxPeak = 0;
  }
  frameIfDue();
  maybeSyncClock(in);
  delay(1);
}
