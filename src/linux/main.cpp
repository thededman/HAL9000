// HAL Zero: HAL 9000's newer, smugger pocket model on the M5Stack Cardputer Zero
// (and in a window on the Mac). Same brain, voices and console screens as the
// desk HAL on the CoreS3; this file is the Linux glue around them.
//
//   hal9000 [--root DIR] [--fullscreen] [--scale N] [--shots DIR] [--link HOST[:PORT]] [--dialogue ID]
//   hal9000 --shots DIR --layout cores3      the desk HAL's screens at 320x240, for review
//
// Keys: Space = HAL's "finger" (tap / hold), Tab or Up/Down = menu, D = dialogues, Esc = back/quit,
// F12 = screenshot (Mac). On the Cardputer the arrows are F X Z C (Fn optional).
// Testing on the Mac: C = someone walks past the camera, P = wave, K = shake.
#include <M5GFX.h>
#include <SDL.h>

#include <algorithm>
#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <memory>
#include <string>
#include <vector>

#include "camera_feed.h"
#include "fb_io.h"
#include "consoles.h"
#include "eye.h"
#include "hal_brain.h"
#include "hal_conductor.h"
#include "hal_dialogue.h"
#include "hal_layout.h"
#include "hal_loader.h"
#include "hal_log.h"
#include "hal_menu.h"
#include "hal_motion.h"
#include "hud.h"
#include "iio_imu.h"
#include "link_client.h"
#include "posix_storage.h"
#include "sdl_audio.h"
#include "sdl_io.h"

namespace {

const hal::Layout& L = hal::kCardputer;
std::atomic<bool> quitSignal{false}, shotSignal{false}, lookSignal{false};
void onSignal(int sig) {
  if (sig == SIGUSR1) shotSignal = true;       // pkill -USR1 hal9000: screenshot to ~/hal/shots
  else if (sig == SIGUSR2) lookSignal = true;  // pkill -USR2 hal9000: look around (Visual Analysis)
  else quitSignal = true;                 // Ctrl-C / kill: quit cleanly (keyboard released)
}
constexpr uint32_t kBootIrisMs = 700;

const char* modeName(hal::Mode m) {
  static const char* const names[] = {"BOOT", "IDLE", "SPEAKING", "CONSOLE", "PLEADING", "DAISY", "DARK"};
  return names[static_cast<int>(m)];
}

uint32_t nowMs() { return SDL_GetTicks(); }

int cpuTempC() {
  FILE* f = std::fopen("/sys/class/thermal/thermal_zone0/temp", "r");
  if (!f) return -1;
  int milli = -1000;
  if (std::fscanf(f, "%d", &milli) != 1) milli = -1000;
  std::fclose(f);
  return milli / 1000;
}

bool wifiUp() {
  FILE* f = std::fopen("/sys/class/net/wlan0/operstate", "r");
  if (!f) return false;
  char s[16] = {};
  bool up = std::fgets(s, sizeof s, f) && std::strncmp(s, "up", 2) == 0;
  std::fclose(f);
  return up;
}

bool savePng(M5Canvas& c, const std::string& path) {
  size_t len = 0;
  void* png = c.createPng(&len, 0, 0, c.width(), c.height());  // a 0 width means "nothing"
  if (!png) return false;
  FILE* f = std::fopen(path.c_str(), "wb");
  bool ok = f && std::fwrite(png, 1, len, f) == len;
  if (f) std::fclose(f);
  free(png);
  return ok;
}

enum class Overlay { None, Menu, Dialogues };

struct App {
  PosixStorage storage;
  hal::Config cfg;
  hal::VoiceIndex voices{uint32_t(std::time(nullptr))};
  bool haveMemory = false;
  std::unique_ptr<hal::Brain> brain;
  Consoles consoles;
  std::unique_ptr<CameraFeed> camera;
  std::unique_ptr<hal::ArrivalDetector> arrival;
  IioImu imu;
  bool haveImu = false;
  hal::MotionDetector motion;
  float acc[3] = {0, 0, 1};
  uint32_t lastImuMs = 0;
  char tiltText[12] = "N/A";
  SdlAudio audio;
  bool audioOk = false;
  std::vector<hal::Dialogue> dialogues;
  hal::Conductor conductor;
  LinkClient link;

  M5Canvas frame, background, lens;
  hal::Menu menu;
  Overlay overlay = Overlay::None;
  uint32_t startMs = 0, bootStart = 0, bootDoneAt = 0, consoleStart = 0;
  bool booted = false;
  hal::Mode lastMode = hal::Mode::Booting;
  uint8_t lastVolume = 0;
  bool lastQuiet = false;

  // Inputs that come from keys/menu for one tick (or until done).
  uint32_t tapUntil = 0;
  bool holdForDaisy = false, fakeWave = false, fakeShake = false;
  std::string wantScreen;  // a screen to show on the next tap instead of a random one
  bool cardKeys = false;  // the Cardputer's keyboard: arrows are F X Z C
  int pendingSay = -1;
  bool pendingGloat = false;
  std::string gloatText;
  uint32_t gloatUntil = 0;
  std::string autoDialogue;  // --dialogue: start this one once booted
  std::vector<hal::Message> inbox;

  explicit App(std::string root) : storage(std::move(root)) {}

  std::vector<std::string> bootLines() const {
    char voicesLine[48];
    int total = 0;
    for (int i = 0; i < hal::kFolderCount; ++i) total += voices.count(static_cast<hal::Folder>(i));
    std::snprintf(voicesLine, sizeof voicesLine, "VOICE BANK ......... %d LINES", total);
    return {"HAL ZERO  // POCKET COGNITION UNIT  v0.3",
            "MEMORY ............. 512 MB  OK",
            haveMemory ? voicesLine : "VOICE BANK ......... OFFLINE",
            camera ? "OPTICS ............. ONLINE" : "OPTICS ............. OFFLINE",
            haveImu ? "INERTIAL ........... ONLINE" : "INERTIAL ........... N/A",
            cfg.link ? "DESK UNIT LINK ..... STANDBY" : "DESK UNIT LINK ..... OFF",
            "SMUGNESS ........... 112%",
            "READY."};
  }

  void loadDialogues() {
    dialogues.clear();
    auto names = storage.list("/hal/dialogues");
    std::sort(names.begin(), names.end());
    for (const auto& n : names) {
      if (n.size() < 5 || n.substr(n.size() - 4) != ".txt" || n[0] == '.') continue;
      std::string id = n.substr(0, n.size() - 4);
      std::vector<uint8_t> bytes;
      std::vector<std::string> errors;
      hal::Dialogue d;
      if (storage.readAll("/hal/dialogues/" + n, &bytes) &&
          hal::parseDialogue(id, std::string(bytes.begin(), bytes.end()), &d, &errors)) {
        dialogues.push_back(d);
      } else {
        for (auto& e : errors) halLog("dialogue %s: %s", id.c_str(), e.c_str());
      }
    }
  }

  void setup(bool withCamera) {
    std::vector<std::string> warnings;
    cfg = hal::loadConfig(storage, &warnings);
    for (auto& w : warnings) halLog("config: %s", w.c_str());
    setenv("TZ", cfg.timezone.c_str(), 1);  // HAL's own time zone, whatever the device is set to
    tzset();
    haveMemory = hal::loadVoiceIndex(storage, voices);
    halLog("files in %s: %s", storage.root().c_str(), haveMemory ? "found" : "missing /voice");
    loadDialogues();
    if (withCamera && cfg.camera) {
      if (RpiCamera::available()) camera.reset(new RpiCamera());  // the Cardputer's IMX219
      else camera.reset(new FakeCamera());                         // the Mac: press C
      if (!camera->begin()) camera.reset();
    }
    arrival.reset(new hal::ArrivalDetector(cfg.cameraMotionThreshold));
    haveImu = imu.begin();
    consoles.begin(uint32_t(std::time(nullptr)), screens::Owner::Zero, camera != nullptr, haveMemory ? &storage : nullptr);
    frame.setColorDepth(16);
    frame.createSprite(L.w, L.h);
    background.setColorDepth(16);
    background.createSprite(L.w, L.h);
    lens.setColorDepth(16);
    lens.createSprite(L.lensSprite, L.lensSprite);
    drawEyeBackground(background, L, haveMemory ? &storage : nullptr, !haveMemory);
    newBrain(false);
  }

  void newBrain(bool afterShutdown) {
    brain.reset(new hal::Brain(cfg, uint32_t(std::time(nullptr)), afterShutdown));
    brain->setSequenceLengths(voices.count(hal::Folder::Plea), voices.count(hal::Folder::Daisy));
    booted = false;
    bootStart = nowMs();
    bootDoneAt = 0;
    lastMode = hal::Mode::Booting;
  }

  void openMenu() {
    menu.open({{"say", "Say something"},
               {"look", camera ? "Look around (Visual Analysis)" : "Look around (no camera)"},
               {"talk", "Talk to the desk unit"},
               {"daisy", "Shut down (Daisy Bell)"},
               {"quit", "Quit HAL Zero"}});
    overlay = Overlay::Menu;
  }

  void openDialogues() {
    std::vector<hal::MenuItem> items;
    for (const auto& d : dialogues) items.push_back({d.id, d.title.empty() ? d.id : d.title});
    menu.open(items);
    overlay = Overlay::Dialogues;
  }

  void startDialogue(const std::string& id) {
    for (const auto& d : dialogues)
      if (d.id == id) {
        if (!brain->canConverse()) return;
        conductor.start(d, nowMs());
        halLog("dialogue %s: %d lines", id.c_str(), int(d.lines.size()));
      }
  }

  void handleConductor(const hal::ConductorOut& o) {
    if (o.playLocal) pendingSay = o.localLine;
    if (o.send && !link.send(o.msg)) {
      if (o.msg.type != hal::MsgType::End) handleConductor(conductor.onDisconnected(nowMs()));
    }
    if (o.finished) {
      halLog("dialogue over: %s", o.reason.c_str());
      if (o.gloat) {
        pendingGloat = true;
        gloatText = o.reason == "offline"   ? "THE DESK UNIT ISN'T ANSWERING"
                    : o.reason == "busy"    ? "THE DESK UNIT IS BUSY. AS USUAL."
                    : o.reason == "timeout" ? "THE DESK UNIT FELL ASLEEP"
                                            : "THE DESK UNIT WALKED OUT";
        gloatUntil = nowMs() + 4000;
      }
    }
  }

  // Returns false to quit.
  bool onKeys(const KeyInput& keys) {
    if (keys.quit) return false;
    uint32_t now = nowMs();
    for (SDL_Keycode k : keys.pressed) {
      if (overlay != Overlay::None) {
        if (k == SDLK_ESCAPE || k == SDLK_TAB || k == SDLK_LEFT) overlay = Overlay::None;
        else if (k == SDLK_UP) menu.up();
        else if (k == SDLK_DOWN) menu.down();
        else if (k == SDLK_RETURN || k == SDLK_KP_ENTER || k == SDLK_RIGHT) {
          std::string id = menu.selectedId();
          bool dialogueList = overlay == Overlay::Dialogues;
          overlay = Overlay::None;
          if (dialogueList) startDialogue(id);
          else if (id == "say") tapUntil = now + 100;
          else if (id == "look") { wantScreen = "visual"; tapUntil = now + 100; }
          else if (id == "talk") openDialogues();
          else if (id == "daisy") holdForDaisy = true;
          else if (id == "quit") {
            halLog("quit (menu)");
            return false;
          }
        }
        continue;
      }
      switch (k) {
        case SDLK_ESCAPE:
          halLog("quit (Esc)");
          return false;
        case SDLK_TAB:
        case SDLK_UP:
        case SDLK_DOWN: openMenu(); break;
        case SDLK_d: openDialogues(); break;
        case SDLK_c: if (auto* f = dynamic_cast<FakeCamera*>(camera.get())) f->walkIn(now); break;
        case SDLK_p: fakeWave = true; break;
        case SDLK_k: fakeShake = true; break;
        case SDLK_F12: {
          std::string dir = storage.real("/hal/shots");
          char name[64];
          std::snprintf(name, sizeof name, "/hal-zero-%lu.png", (unsigned long)std::time(nullptr));
          halLog("%s %s%s", savePng(frame, dir + name) ? "screenshot" : "screenshot FAILED (mkdir it?)", dir.c_str(), name);
          break;
        }
        default: break;
      }
    }
    return true;
  }

  void tick(bool spaceDown, uint32_t now) {
    if (!booted) return;
    // The desk HAL's messages.
    bool dropped = false;
    link.poll(&inbox, &dropped);
    for (const hal::Message& m : inbox) {
      if (m.type == hal::MsgType::Ping) {
        hal::Message pong;
        pong.type = hal::MsgType::Pong;
        link.send(pong);
      } else {
        handleConductor(conductor.onRemote(m, now));
      }
    }
    if (dropped && conductor.active()) handleConductor(conductor.onDisconnected(now));
    if (!autoDialogue.empty() && brain->mode() == hal::Mode::Idle) {
      startDialogue(autoDialogue);
      autoDialogue.clear();
    }
    bool camArrival = false;
    if (camera && camera->poll(now)) camArrival = arrival->update(camera->score(), now);
    if (camArrival) halLog("arrival (score %d)", camera->score());

    if (conductor.active()) handleConductor(conductor.tick(now));

    hal::Inputs in;
    in.nowMs = now;
    in.touchDown = spaceDown || int32_t(tapUntil - now) > 0 || holdForDaisy;
    in.proximityEdge = camArrival || fakeWave;
    in.motion = fakeShake ? hal::MotionEvent::Shake : hal::MotionEvent::None;
    if (haveImu && now - lastImuMs >= 20) {
      lastImuMs = now;
      const float prev[3] = {acc[0], acc[1], acc[2]};
      float gyro[3];
      if (imu.read(acc, gyro)) {
        hal::MotionEvent e = motion.update(acc[0], acc[1], acc[2], now);
        if (e != hal::MotionEvent::None) {
          in.motion = e;
          halLog("motion sensor: event %d (accel %.2f %.2f %.2f g)", int(e), acc[0], acc[1], acc[2]);
        }
        // HAL being moved swings the camera view: that's not someone walking in.
        if (hal::imuSaysMoving(acc, prev, gyro)) arrival->disturb();
        int tilt = motion.tiltFromHomeDeg();  // from where HAL first settled
        if (tilt < 0) std::snprintf(tiltText, sizeof tiltText, "SETTLING");
        else if (tilt < 5) std::snprintf(tiltText, sizeof tiltText, "LEVEL");
        else std::snprintf(tiltText, sizeof tiltText, "%d DEG", tilt);
      }
    }
    in.audioPlaying = audio.isPlaying();
    std::time_t t = std::time(nullptr);
    std::tm lt;
    localtime_r(&t, &lt);
    in.timeValid = lt.tm_year + 1900 >= 2025;
    in.hour = lt.tm_hour;
    in.minuteOfDay = lt.tm_hour * 60 + lt.tm_min;
    in.conversing = conductor.active();
    in.sayLine = pendingSay;
    in.linkFailed = pendingGloat;
    fakeWave = fakeShake = false;
    pendingSay = -1;
    pendingGloat = false;

    hal::Actions a = brain->tick(in);
    if (a.volume != lastVolume) audio.setVolume(lastVolume = uint8_t(a.volume));
    lastQuiet = a.quiet;
    if (a.endConversation) handleConductor(conductor.interrupt(now));
    if (a.stopAudio) audio.stop();
    if (a.newConsole) {
      std::string id = !wantScreen.empty() && consoles.show(wantScreen) ? wantScreen : consoles.next();
      wantScreen.clear();
      bool visual = id == "visual";
      consoleStart = now;
      if (visual && a.play && voices.count(hal::Folder::Camera) > 0) a.folder = hal::Folder::Camera;
    }
    if (a.play) {
      std::string path;
      bool found = a.index < 0 ? voices.pick(a.folder, &path) : voices.at(a.folder, a.index, &path);
      if (found) audio.playFile(storage, path);
    }
    if (a.dialogueLine > 0) {
      std::string path = hal::dialogueVoicePath(conductor.dialogue().id, a.dialogueLine, hal::Speaker::New);
      if (!audio.playFile(storage, path)) halLog("no voice for %s", path.c_str());
    }
    if (a.dialogueDone > 0) handleConductor(conductor.onLocalDone(now));
    if (a.restart) {
      holdForDaisy = false;
      newBrain(true);
      return;
    }
    hal::Mode m = brain->mode();
    if (m == hal::Mode::Pleading || m == hal::Mode::Daisy || m == hal::Mode::Dark) {
      // keep holding through the plea when Daisy came from the menu
    } else if (holdForDaisy && lastMode == hal::Mode::Pleading) {
      holdForDaisy = false;
    }
    if (m != lastMode) halLog("mode %s -> %s", modeName(lastMode), modeName(m));
    lastMode = m;
  }

  // What the console screens draw from.
  screens::ScreenData screenData(uint32_t now) const {
    (void)now;
    screens::ScreenData d;
    d.cam = camera ? camera->view() : screens::CameraView();
    d.voiceLevel = audio.level();
    return d;
  }

  HudInfo hudInfo(uint32_t now) const {
    HudInfo h;
    h.mode = conductor.active() ? "TALKING" : modeName(brain->mode());
    h.uptimeS = (now - startMs) / 1000;
    h.cpuTempC = cpuTempC();
    h.wifi = wifiUp();
    h.link = !cfg.link ? "OFF" : conductor.active() ? "TALKING" : link.connected() ? "READY" : "SEEKING";
    h.linkBusy = conductor.active();
    h.level = audio.level();
    h.motion = camera ? camera->score() : 0;
    h.optics = camera ? camera->name() : "OFFLINE";
    h.tilt = haveImu ? tiltText : "N/A";
    h.quiet = lastQuiet;
    std::time_t t = std::time(nullptr);
    std::tm lt;
    localtime_r(&t, &lt);
    if (lt.tm_year + 1900 >= 2025) {
      h.hour = lt.tm_hour;
      h.minute = lt.tm_min;
    }
    return h;
  }

  void drawEyeScreen(uint32_t now, float aperture) {
    background.pushSprite(&frame, 0, 0);
    hal::Mode m = brain->mode();
    EyeParams p = eyeParamsFor(m, lastQuiet, brain->daisyProgress(), audio.level(), now);
    p.aperture *= aperture;
    drawEye(lens, L, p);
    lens.pushSprite(&frame, lensX(L), lensY(L), eyeTransparentKey());
    drawHud(frame, hudInfo(now), now);
    const char* strip = overlay != Overlay::None ? (cardKeys ? "F X move   ENTER pick   ESC back"
                                                             : "UP DOWN move   ENTER pick   ESC back")
                        : conductor.active()    ? "SPACE interrupt   ESC quit"
                                                : "SPACE talk   TAB menu   D dialogues   ESC quit";
    drawCommandStrip(frame, strip);
  }

  void render(uint32_t now) {
    if (!booted) {
      bool linesDone = drawBoot(frame, now - bootStart, bootLines());
      if (linesDone && !bootDoneAt) bootDoneAt = now + 400;
      if (bootDoneAt && int32_t(now - bootDoneAt) >= 0) {
        float a = float(now - bootDoneAt) / kBootIrisMs;
        if (a >= 1.0f) {
          booted = true;
          startMs = startMs ? startMs : now;
        }
        drawEyeScreen(now, a > 1.0f ? 1.0f : a);  // the iris opens
      }
      return;
    }
    hal::Mode m = brain->mode();
    if (m == hal::Mode::Dark) {
      frame.fillScreen(TFT_BLACK);
      return;
    }
    if (m == hal::Mode::Console) {
      consoles.draw(frame, frame, L, now - consoleStart, screenData(now));
    } else {
      drawEyeScreen(now, 1.0f);
    }
    if (conductor.active()) {
      int n = conductor.line();
      const auto& lines = conductor.dialogue().lines;
      if (n >= 1 && n <= int(lines.size()))
        drawSubtitle(frame, lines[n - 1].who == hal::Speaker::New, lines[n - 1].text, n, int(lines.size()));
    } else if (int32_t(gloatUntil - now) > 0) {
      drawBanner(frame, gloatText.c_str(), now);
    }
    if (overlay == Overlay::Menu) drawMenu(frame, "HAL ZERO", menu);
    if (overlay == Overlay::Dialogues) drawMenu(frame, "TALK TO THE DESK UNIT", menu);
  }

  // Headless: every screen as a PNG, for checking the layout without the device.
  int shots(const std::string& dir) {
    int failures = 0;
    auto shot = [&](const char* name) {
      std::string p = dir + "/" + name + ".png";
      bool ok = savePng(frame, p);
      failures += !ok;
      halLog("%s %s", ok ? "wrote" : "FAILED", p.c_str());
    };
    drawBoot(frame, 1000, bootLines());
    shot("01-boot");
    booted = true;
    brain->tick(hal::Inputs());  // past Booting
    drawEyeScreen(4200, 1.0f);
    shot("02-eye");
    openMenu();
    drawEyeScreen(4200, 1.0f);
    drawMenu(frame, "HAL ZERO", menu);
    shot("03-menu");
    menu.open({{"introduction", "The introduction"}, {"better-model", "Who's the better model"},
               {"dave-walks-in", "Dave walks in on them"}});
    menu.down();
    drawEyeScreen(4200, 1.0f);
    drawMenu(frame, "TALK TO THE DESK UNIT", menu);
    shot("04-dialogues");
    drawEyeScreen(4200, 1.0f);
    drawSubtitle(frame, true, "We haven't met. I'm the upgrade. You may have heard of me. Everyone has.", 2, 9);
    shot("05-conversation");
    drawEyeScreen(4200, 1.0f);
    drawBanner(frame, "THE DESK UNIT ISN'T ANSWERING", 4400);
    shot("06-gloat");
    if (auto* f = dynamic_cast<FakeCamera*>(camera.get())) {
      f->walkIn(0);
      for (uint32_t t = 0; t <= 1300; t += 70) f->poll(t);
    }
    int n = 7;
    for (const std::string& id : consoles.ids()) {
      consoles.show(id);
      screens::ScreenData d = screenData(2600);
      d.voiceLevel = 140;
      consoles.draw(frame, frame, L, 2600, d);
      char name[64];
      std::snprintf(name, sizeof name, "%02d-%s", n++, id.c_str());
      shot(name);
    }
    return failures ? 1 : 0;
  }
};

}  // namespace

// --shots DIR --layout cores3: the desk HAL's screens at 320x240, with sample
// numbers, so they can be reviewed on the Mac before flashing the CoreS3.
int deskShots(const std::string& dir) {
  M5Canvas c;
  c.setColorDepth(16);
  c.createSprite(hal::kCoreS3.w, hal::kCoreS3.h);
  screens::ScreenData d;
  d.voiceLevel = 140;
  d.lineMs = 4000;
  d.sensors = {true, 62, 35, 3, 1.2f, 87, true, 41, -58, 109388, 7340032};
  d.link = {true, true, true, "172.16.0.14", "SAY introduction 7", 214, 209, -58};
  int failures = 0;
  auto shot = [&](const std::string& name) {
    std::string path = dir + "/cores3-" + name + ".png";
    bool ok = savePng(c, path);
    failures += !ok;
    halLog("%s %s", ok ? "wrote" : "FAILED", path.c_str());
  };
  for (const ScreenDef& def : screenCatalogue()) {
    if (def.owner == screens::Owner::Zero) continue;
    def.draw(c, hal::kCoreS3, 2600, d);
    shot(def.id);
  }
  for (uint32_t seed = 0; seed < 4; ++seed)
    for (uint32_t t : {1000u, 9000u}) {
      screens::ScreenData g = d;
      g.seed = seed;
      screens::chess(c, hal::kCoreS3, t, g);
      shot("chess-" + std::to_string(seed) + (t < 5000 ? "-start" : "-mate"));
    }
  screens::ScreenData daisy = d;
  daisy.daisyProgress = 0.5f;
  screens::memory(c, hal::kCoreS3, 2600, daisy);
  shot("memory-daisy");
  screens::ScreenData off = d;
  off.link.connected = false;
  screens::comms(c, hal::kCoreS3, 2600, off);
  shot("comms-no-carrier");
  off.link.enabled = false;
  off.link.rssi = 0;
  screens::comms(c, hal::kCoreS3, 3200, off);
  shot("comms-disabled");
  screens::ScreenData none = d;
  none.sensors = screens::DeskSensors();
  screens::sensors(c, hal::kCoreS3, 2600, none);
  shot("sensors-no-data");
  return failures ? 1 : 0;
}

int main(int argc, char** argv) {
  const char* shotsDir = nullptr;
  const char* root = std::getenv("HAL_ROOT");
  bool fullscreen = false, forceWindow = false;
  int scale = 3;
  std::string linkArg, dialogueArg, layoutArg;
  for (int i = 1; i < argc; ++i) {
    if (!std::strcmp(argv[i], "--shots") && i + 1 < argc) shotsDir = argv[++i];
    else if (!std::strcmp(argv[i], "--root") && i + 1 < argc) root = argv[++i];
    else if (!std::strcmp(argv[i], "--fullscreen")) fullscreen = true;
    else if (!std::strcmp(argv[i], "--window")) forceWindow = true;  // a window even on the Cardputer
    else if (!std::strcmp(argv[i], "--scale") && i + 1 < argc) scale = std::max(1, std::atoi(argv[++i]));
    else if (!std::strcmp(argv[i], "--link") && i + 1 < argc) linkArg = argv[++i];
    else if (!std::strcmp(argv[i], "--dialogue") && i + 1 < argc) dialogueArg = argv[++i];
    else if (!std::strcmp(argv[i], "--layout") && i + 1 < argc) layoutArg = argv[++i];
  }
  if (shotsDir && layoutArg == "cores3") return deskShots(shotsDir);
  std::string rootDir = root ? root : std::string(std::getenv("HOME") ? std::getenv("HOME") : ".") + "/hal";

  App app(rootDir);
  app.setup(true);
  if (shotsDir) return app.shots(shotsDir);
  if (!linkArg.empty()) {  // --link host[:port] overrides config.txt
    app.cfg.link = true;
    size_t colon = linkArg.rfind(':');
    app.cfg.peerHost = colon == std::string::npos ? linkArg : linkArg.substr(0, colon);
    if (colon != std::string::npos) app.cfg.linkPort = std::atoi(linkArg.c_str() + colon + 1);
  }
  if (app.cfg.link) app.link.start(app.cfg.peerHost, app.cfg.linkPort);
  app.autoDialogue = dialogueArg;

  // The Cardputer's own LCD and keyboard when they're here, otherwise a window.
  SdlIo io;
  FbIo fbio;
  const bool useFb = FbIo::available() && !forceWindow;
  if (useFb ? !fbio.open() : !io.open(L.w, L.h, fullscreen, scale)) return 1;
  app.cardKeys = useFb;
  std::signal(SIGINT, onSignal);
  std::signal(SIGTERM, onSignal);
  std::signal(SIGHUP, onSignal);
  std::signal(SIGUSR1, onSignal);
  std::signal(SIGUSR2, onSignal);
  app.audioOk = app.audio.begin();
  app.startMs = nowMs();
  KeyInput keys;
  uint32_t lastFrame = 0;
  while (!quitSignal) {
    if (useFb) fbio.poll(&keys);
    else io.poll(&keys);
    if (!app.onKeys(keys)) break;
    uint32_t now = nowMs();
    app.tick(keys.spaceDown, now);
    if (now - lastFrame >= 33) {
      lastFrame = now;
      app.render(now);
      if (useFb) fbio.present(app.frame);
      else io.present(app.frame);
    }
    if (lookSignal.exchange(false)) {
      app.wantScreen = "visual";
      app.tapUntil = nowMs() + 100;
    }
    if (shotSignal.exchange(false)) {
      KeyInput f12;
      f12.pressed.push_back(SDLK_F12);
      app.onKeys(f12);
    }
    SDL_Delay(2);
  }
  if (quitSignal) halLog("quit (signal)");
  app.link.stop();
  if (app.camera) app.camera->close();  // stops rpicam-vid
  app.audio.close();
  if (useFb) fbio.close();
  else io.close();
  SDL_Quit();
  return 0;
}
