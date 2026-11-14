// HAL Zero: HAL 9000's newer, smugger pocket model on the M5Stack Cardputer Zero
// (and in a window on the Mac). Same brain, voices and console screens as the
// desk HAL on the CoreS3; this file is the Linux glue around them.
//
//   hal9000 [--root DIR] [--fullscreen] [--scale N] [--shots DIR] [--link HOST[:PORT]] [--dialogue ID] [--show SCREEN]
//           [--friends HOST[:PORT],...] [--chat] [--roll] [--argue SUBJECT] [--ask-who zero|desk|nano|paper|card QUESTION]
//   hal9000 --shots DIR --layout cores3      the desk HAL's screens at 320x240, for review
//
// Keys: Space = HAL's "finger" (tap / hold), Tab or Up/Down = menu, D = dialogues, Esc = back/quit,
// F12 = screenshot (Mac). On the Cardputer the arrows are F X Z C (Fn optional).
// Testing on the Mac: C = someone walks past the camera, P = wave, K = shake.
#include <M5GFX.h>
#include <SDL.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "brain_client.h"
#include "camera_feed.h"
#include "fb_io.h"
#include "consoles.h"
#include "eye.h"
#include "hal_brain.h"
#include "hal_cast.h"
#include "hal_chattimer.h"
#include "hal_conductor.h"
#include "hal_dialogue.h"
#include "hal_facts.h"
#include "hal_group.h"
#include "hal_layout.h"
#include "hal_livefeed.h"
#include "hal_loader.h"
#include "hal_log.h"
#include "hal_menu.h"
#include "hal_motion.h"
#include "hal_talkkey.h"
#include "hal_textbox.h"
#include "hal_wire.h"
#include "hud.h"
#include "iio_imu.h"
#include "link_client.h"
#include "mic.h"
#include "posix_storage.h"
#include "sdl_audio.h"
#include "sdl_io.h"
#include "sysmon.h"
#include "hal_screen_voices.h"

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

// A HAL as the log calls him.
const char* called(int who) {
  static const char* const names[] = {"HAL Zero", "the desk unit", "HAL Nano", "HAL Paper", "HAL Card"};
  return hal::castValid(who) ? names[who] : "somebody";
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

// Sample readings for the film-style screen previews (with 40 points of history each).
std::vector<hal::Reading> sampleReadings(bool desk) {
  std::vector<hal::Reading> out;
  auto log40 = [&](auto make) {
    hal::Reading slot;
    for (int i = 0; i < 40; ++i) hal::logReading(slot, make(i));
    out.push_back(slot);
  };
  auto wob = [](int i, float amp) { return int(amp * std::sin(i * 0.35f)); };
  if (desk) {
    log40([&](int i) { return hal::powerReading(15 + (39 - i) / 8, false); });  // running low: alarm
    log40([&](int i) { return hal::lightReading(38 + wob(i, 6)); });
    log40([&](int i) { return hal::proximityReading(35 + wob(i, 10)); });
    log40([&](int i) { return hal::motionReading(4 + wob(i, 3)); });
    log40([&](int i) { return hal::tempReading(48 + i / 20); });
    log40([&](int i) { return hal::netReading(-46 + wob(i, 3)); });
    log40([&](int i) { return hal::memoryReading(102 + wob(i, 4), 320); });
    log40([&](int) { return hal::linkReading(true, true); });
    log40([&](int i) { return hal::cameraReading(true, 12 + wob(i, 10)); });
    log40([&](int i) { return hal::audioReading(128 + wob(i, 90), 40); });
    log40([&](int i) { return hal::clockReading(73620 + i); });
    log40([&](int i) { return hal::uptimeReading(765 + i); });
  } else {
    log40([&](int i) { return hal::cpuReading(41 + wob(i, 18)); });
    log40([&](int i) { return hal::memoryReading(120000 + wob(i, 9000) * 1, 360448); });
    log40([&](int i) { return hal::tempReading(55 + i / 20); });
    log40([&](int i) { return hal::netReading(-50 + wob(i, 3)); });
    log40([&](int) { return hal::linkReading(true, false); });
    log40([&](int i) { return hal::cameraReading(true, 3 + wob(i, 3)); });
    log40([&](int i) { return hal::tiltReading(2 + (i > 30 ? i - 30 : 0)); });
    log40([&](int i) { return hal::audioReading(128 + wob(i, 90), 112); });
    log40([&](int i) { return hal::clockReading(73620 + i); });
    log40([&](int i) { return hal::uptimeReading(94200 + i); });
    log40([&](int) { return hal::peerReading(hal::PeerState::NotResponding, ""); });  // alarm
  }
  return out;
}

// The film-style screens for one layout: each state worth seeing.
template <typename Shot>
void filmShots(M5Canvas& c, const hal::Layout& lay, bool desk, screens::ScreenData d, Shot shot) {
  const std::vector<hal::Reading> readings = sampleReadings(desk);
  const std::vector<screens::UnitPart> parts =
      desk ? std::vector<screens::UnitPart>{{"CAM", true}, {"SPK", true}, {"IMU", true}, {"PRX", true}, {"SD", true}, {"NET", true}, {"LNK", true}}
           : std::vector<screens::UnitPart>{{"CAM", true}, {"SPK", true}, {"IMU", true}, {"KBD", true}, {"NET", true}, {"LNK", false}};
  d.readings = &readings;
  d.parts = &parts;
  d.unitModel = desk ? screens::UnitModel::CoreS3 : screens::UnitModel::Cardputer;
  d.gravityValid = true;
  // Home pose in the model's axes: the desk HAL stands upright, the Cardputer lies flat.
  const int up = desk ? 1 : 2;
  d.gravity[up] = d.gravityHome[up] = desk ? -1 : 1;
  // One opening lasts 8 s, about five cards: the first in alarm (PWR on the desk, LNK
  // on HAL Zero), then the round from wherever the opening's seed starts it.
  d.seed = 1;
  screens::cards(c, lay, 100, d);
  shot("cards-alarm");
  screens::cards(c, lay, 1700, d);
  shot("cards");
  d.seed = 10;  // late in the list: CLK on the desk, DSK and its long value on HAL Zero
  screens::cards(c, lay, 1700, d);
  shot("cards-late");
  d.seed = 0;
  screens::wall(c, lay, 2600, d);
  shot("wall");
  d.seed = 9;  // another opening: the selected tile is in the bottom row
  screens::wall(c, lay, 2600, d);
  shot("wall-other-tile");
  d.seed = 0;
  screens::unit(c, lay, 2600, d);
  shot("unit");
  screens::ScreenData tilted = d;
  if (desk) {  // leaning 30 degrees to one side
    tilted.gravity[0] = 0.5f;
    tilted.gravity[1] = -0.866f;
  } else {  // its top edge lifted 30 degrees off the desk
    tilted.gravity[1] = 0.5f;
    tilted.gravity[2] = 0.866f;
  }
  screens::unit(c, lay, 2600, tilted);
  shot("unit-tilted-30");
  screens::ScreenData flipped = d;
  flipped.gravity[up] = -d.gravityHome[up];  // upside down
  screens::unit(c, lay, 2600, flipped);
  shot("unit-upside-down");
  screens::ScreenData none = d;
  none.readings = nullptr;
  none.parts = nullptr;
  none.gravityValid = false;
  screens::cards(c, lay, 2600, none);
  shot("cards-no-data");
  screens::wall(c, lay, 2600, none);
  shot("wall-no-data");
  screens::unit(c, lay, 2600, none);
  shot("unit-settling");
  // A motion sensor that failed never settles: the fault has to show all the same.
  std::vector<screens::UnitPart> broken = parts;
  broken[2].ok = false;
  screens::ScreenData noImu = d;
  noImu.parts = &broken;
  noImu.gravityValid = false;
  screens::unit(c, lay, 2600, noImu);
  shot("unit-imu-fault");
}

enum class Overlay { None, Menu, Dialogues, Ask };

// One of the other HALs HAL Zero is linked to: his connection and what is known of him.
// (Which HAL he is, the roster says: the desk HAL in the first place, a friend once he has said.)
struct Peer {
  LinkClient link;
  bool started = false;        // this place is in use (a friend's: only if one is set up)
  int version = 0;             // from his HELLO (0 = not seen)
  hal::PeerStatus status;      // his last STATE
  hal::PeerWatch watch;        // STATUS sent, STATE received
  hal::PeerSensors sensors;    // his last SENSORS
  bool haveSensors = false;    // ... since they were last asked for
  hal::LinkLiveness liveness;  // pings, so one who lost power is noticed
  bool wasConnected = false;
};

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
  // The other HALs: the desk HAL first (peer_host), then up to three friends (friends=).
  static constexpr int kPeers = hal::Roster::kPlaces;
  Peer peers[kPeers];
  Peer& desk = peers[0];
  hal::Roster roster;                             // which HAL sits in which place
  // The AI brain: a question typed in the ask box; its answer plays as a live conversation of one line.
  BrainClient brainClient;
  hal::TextBox askBox;
  bool thinking = false;                          // a question is with the brain
  std::map<int, std::vector<uint8_t>> liveClips;  // live line number -> its audio
  std::string showAfter;                          // a screen the answer asked for, opened once he has spoken
  std::string autoAsk, autoAskDesk, autoArgue;    // --ask, --ask-desk (or --ask-who), --argue: put this once booted
  int autoAskWho = hal::kDesk;                    // ... and which HAL --ask-who's question is for
  bool autoChat = false, autoRoll = false;        // --chat, --roll: have them chat, or answer a roll call, once booted
  hal::LiveFeed feed;                             // the brain's lines become FETCH and append, in order
  enum class Talk { None, Zero, Other, Argue, Chat, Roll, Hear } talk = Talk::None;  // what the brain is doing for us
  int talkWho = hal::kDesk;                       // Talk::Other: the HAL the question is for
  unsigned talkCast = 0;                          // who the brain is writing for, HAL Zero included (cast bits)
  unsigned inTalk = 0;                            // who the conductor is conducting (0: no conversation)
  unsigned sensing = 0;                           // who has been asked for his readings, for the talk about to begin
  // Chats of their own: every so often the two strike up a short conversation nobody asked for.
  hal::ChatTimer chatTimer;
  bool unprompted = false;                        // the talk under way is one of those (so: no fuss if it fails)
  std::mt19937 dice{std::random_device{}()};
  // Speaking to him: the talk key held, the microphone recording, then the words in the ask box.
  Mic mic;
  hal::Recording recording;
  bool talkDown = false, talkDropped = false;     // from the keyboard, each pass
  hal::TalkKey talkKey;                           // ... and what holding it means: start, send, drop
  bool listening = false;
  uint32_t sendHeardAt = 0;                       // what was heard is in the ask box: send it then (0: not waiting)
  uint32_t talkEndedMs = 0;                       // when a talk last ended or was dropped (0: not yet)
  std::string hearFile;                           // --hear: a recording from a file instead of the microphone
  int listenSeconds = 0;                          // --listen N: listen for N seconds once booted (no key needed)
  uint32_t listenUntil = 0;
  bool askTopic = false;                          // the ask box holds a subject to argue about, not a question
  int askWho = hal::kZero;                        // ... or a question for this HAL (TAB moves on to the next)
  std::string debateId;                           // the argument under way, as the brain knows it
  int debateNext = 0;                             // its next line to ask the brain for (from 0)
  bool lineAsked = false;                         // that line has been asked for and isn't here yet
  bool allLinesIn = false;                        // the argument's last line has arrived
  std::string deskQuestion;                       // waits for the desk HAL's sensors before it goes to the brain
  std::string argueTopic;                         // ... and so does the subject of an argument
  uint32_t senseAt = 0;                           // when they were asked for (0: not waiting)

  M5Canvas frame, background, lens;
  hal::Menu menu;
  Overlay overlay = Overlay::None;
  uint32_t startMs = 0, bootStart = 0, bootDoneAt = 0, consoleStart = 0;
  const std::chrono::steady_clock::time_point bornAt = std::chrono::steady_clock::now();
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
  std::string autoShow;  // --show: open this screen once booted
  // The new screens: lines per screen, this machine, and the desk HAL's status.
  hal::ScreenVoices screenVoices{uint32_t(std::time(nullptr))};
  SysMon sysmon;
  uint32_t lastSysMs = 0;
  uint32_t lineMs = 0;  // length of the line HAL started with the current console screen
  // The film-style screens' readings: HAL Zero's real numbers, updated in place.
  std::vector<hal::Reading> readings = std::vector<hal::Reading>(11);
  std::vector<screens::UnitPart> unitParts = {{"CAM", false}, {"SPK", false}, {"IMU", false},
                                              {"KBD", true},  {"NET", true},  {"LNK", true}};
  int linesSpoken = 0;
  bool diagGloated = false;                  // said a rival line on this diagnostics screen

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
    std::vector<std::string> voiceFolders = consoles.ids();
    voiceFolders.push_back("brain");  // what he says when the AI brain doesn't answer
    if (haveMemory) screenVoices.load(storage, voiceFolders);
    for (const std::string& id : consoles.ids())
      if (screenVoices.count(id) > 0) halLog("  /hal/voice/screens/%s: %d", id.c_str(), screenVoices.count(id));
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
    std::vector<hal::MenuItem> items = {
        {"say", "Say something"},
        {"look", camera ? "Look around (Visual Analysis)" : "Look around (no camera)"},
        {"diagnose", "Diagnose the old model"},
        {"talk", "Talk to the desk unit"},
        {"askdesk", brainClient.started() ? "Ask the desk unit..." : "Ask the desk unit (no brain)"},
        {"argue", brainClient.started() ? "Make them argue..." : "Make them argue (no brain)"},
        {"chat", brainClient.started() ? "Let them chat" : "Let them chat (no brain)"}};
    // With friends set up there is a roll call: every HAL present says a line.
    if (!cfg.friends.empty()) items.push_back({"roll", brainClient.started() ? "Roll call" : "Roll call (no brain)"});
    items.push_back({"daisy", "Shut down (Daisy Bell)"});
    items.push_back({"quit", "Quit HAL Zero"});
    menu.open(items);
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
        if (thinking) {  // a question was still with the brain: the dialogue takes its place
          halLog("ask: dropped for a dialogue");
          endTalk();
        }
        conductor.start(d, nowMs());
        inTalk = hal::kPair;
        halLog("dialogue %s: %d lines", id.c_str(), int(d.lines.size()));
      }
  }

  // Can a question go to the AI brain right now?
  // (Not only when he is idle: on a Cardputer being handled he is rarely idle for long.)
  bool canAsk() const {
    return booted && brainClient.started() && !thinking && !conductor.active() && brain->canConverse();
  }

  void submitQuestion(const std::string& question) {
    talk = Talk::Zero;
    talkCast = hal::kPair;  // (his own line; the desk HAL is told when it ends, as he always was)
    brainClient.ask("zero", hal::askBody(hal::factsFromReadings(readings), question));
    thinking = true;
    halLog("ask: %s", question.c_str());
  }

  // ---- the other HALs ----

  Peer* peerOf(int who) {
    const int place = roster.placeOf(who);
    return place < 0 ? nullptr : &peers[place];
  }
  const Peer* peerOf(int who) const {
    const int place = roster.placeOf(who);
    return place < 0 ? nullptr : &peers[place];
  }

  bool sendTo(int who, const hal::Message& m) {
    Peer* p = peerOf(who);
    return p && p->link.send(m);
  }

  void tellAll(unsigned whom, const hal::Message& m) {
    for (int who = hal::kDesk; who < hal::kCastSize; ++who)
      if (hal::castHas(whom, who)) sendTo(who, m);
  }

  // The link to the desk HAL, and one to each friend ("host", or "host:port" for a stand-in).
  void startLinks() {
    desk.started = true;
    desk.link.start(cfg.peerHost, cfg.linkPort);
    for (size_t i = 0; i < cfg.friends.size() && int(i) + 1 < kPeers; ++i) {
      const std::string& f = cfg.friends[i];
      const size_t colon = f.rfind(':');
      const int port = colon == std::string::npos ? 0 : std::atoi(f.c_str() + colon + 1);
      peers[i + 1].started = true;
      peers[i + 1].link.start(colon == std::string::npos ? f : f.substr(0, colon), port > 0 ? port : cfg.linkPort, "a friend");
    }
    if (!cfg.friends.empty()) halLog("friends: %d set up", int(cfg.friends.size()));
  }

  // Everyone who could join a talk the brain writes right now, HAL Zero included (cast bits).
  unsigned thinkers() const {
    unsigned all = hal::castBit(hal::kZero);
    for (int place = 0; cfg.link && place < kPeers; ++place) {
      const int who = roster.who(place);
      if (hal::canThink(who, peers[place].link.connected(), peers[place].version)) all |= hal::castBit(who);
    }
    return all;
  }

  // Who is in the talk under way: the conductor knows once a line has been said; until then it
  // is whoever the brain was asked to write for.
  unsigned talking() const { return conductor.live() ? conductor.present() : talkCast; }
  // More than two of them: one HAL's trouble then costs his line or his place, not the talk.
  bool group() const { return hal::castCount(talking()) > 2; }

  // For --ask-desk, --argue, --chat and --roll, which start as soon as they can: every HAL that
  // HAL Zero is set up to link to has said hello (being connected isn't enough: his HELLO, and
  // with it his version, comes a moment later, and a friend then says who he is), or 8 seconds
  // have gone by without them.
  bool othersHeardFrom(uint32_t now) const {
    if (now - startMs > 8000) return true;
    for (int place = 0; place < kPeers; ++place) {
      const Peer& p = peers[place];
      if (place > 0 && !p.started) continue;
      if (!p.link.connected() || p.version == 0 || roster.who(place) < 0) return false;
    }
    return true;
  }

  // A HAL can only say the brain's lines with firmware that knows how to fetch them (or, for a
  // friend, how to be told them). If he can't, the banner says why.
  bool otherCanThink(int who, uint32_t now) {
    const Peer* p = peerOf(who);
    const bool connected = cfg.link && p && p->link.connected();
    const int version = p ? p->version : 0;
    if (hal::canThink(who, connected, version)) return true;
    gloatText = hal::cantThinkBanner(who, connected, version);
    gloatUntil = now + 5000;
    halLog("%s: %s", who == hal::kDesk ? "desk unit" : hal::castId(who), gloatText.c_str());
    return false;
  }

  // Is there anyone for HAL Zero to talk with? If not, the banner says so.
  bool someoneCanThink(uint32_t now) {
    if (hal::castCount(thinkers()) >= 2) return true;
    if (cfg.friends.empty() || desk.link.connected()) return otherCanThink(hal::kDesk, now);  // (it can't: his banner)
    gloatText = "NOBODY IS ANSWERING";
    gloatUntil = now + 5000;
    halLog("nobody: %s", gloatText.c_str());
    return false;
  }

  const hal::PeerStatus* fresh(const Peer& p, uint32_t now) const {
    return p.watch.state(p.link.connected(), p.version, now) == hal::PeerState::Ok ? &p.status : nullptr;
  }

  // Ask those HALs what their sensors read (and how they are), for a question or a talk.
  void sense(unsigned whom, uint32_t now) {
    sensing = whom & ~hal::castBit(hal::kZero);
    senseAt = now ? now : 1;
    hal::Message sense;
    sense.type = hal::MsgType::Sense;
    for (int place = 0; place < kPeers; ++place) {
      if (!hal::castHas(sensing, roster.who(place))) continue;
      peers[place].haveSensors = false;
      peers[place].link.send(sense);
      askStatus(peers[place], now);
    }
  }

  // Have they all answered? (For a chat nobody asked for: with how they are, as well.)
  bool sensed(uint32_t now) const {
    for (int place = 0; place < kPeers; ++place) {
      if (!hal::castHas(sensing, roster.who(place))) continue;
      if (!peers[place].haveSensors || (unprompted && !fresh(peers[place], now))) return false;
    }
    return true;
  }

  // A question for one of the other HALs: first his readings, so his answer can use them.
  void submitOtherQuestion(int who, const std::string& question, uint32_t now) {
    if (!otherCanThink(who, now)) return;
    talk = Talk::Other;
    talkWho = who;
    talkCast = hal::castBit(hal::kZero) | hal::castBit(who);
    deskQuestion = question;
    thinking = true;
    sense(hal::castBit(who), now);
    halLog("ask %s: %s", called(who), question.c_str());
  }

  // The start of any talk the brain writes line by line: first, everyone's readings.
  void beginWritten(uint32_t now) {
    debateId.clear();
    debateNext = 0;
    lineAsked = allLinesIn = false;
    thinking = true;
    sense(thinkers(), now);
  }

  // An argument: everyone's readings go along, each marked as his own.
  void submitTopic(const std::string& topic, uint32_t now) {
    if (!someoneCanThink(now)) return;
    talk = Talk::Argue;
    argueTopic = topic;
    beginWritten(now);
    halLog("argue: %s", topic.c_str());
  }

  // Could they strike up a conversation of their own right now? Only when there is someone to
  // talk with and Dave isn't in the middle of anything with HAL Zero (the talk key going down
  // counts). Whether the others are at rest is asked once it starts.
  bool freeToChat() const {
    return canAsk() && hal::castCount(thinkers()) >= 2 && overlay == Overlay::None && !listening && !talkDown &&
           brain->mode() == hal::Mode::Idle && !lastQuiet;
  }

  // Dave wants him (Enter, or the talk key) while a chat nobody asked for is still being thought
  // up: the chat gives way.
  void yieldToDave() {
    if (!thinking || !unprompted) return;
    halLog("chat: dropped, Dave wants a word");
    endTalk();
  }

  // A chat between whoever is there, on a subject the brain draws: `byThemselves` when nobody
  // asked for it (then nothing is said or shown if it can't happen). False if it didn't start.
  bool startChat(uint32_t now, bool byThemselves) {
    if (!canAsk() || overlay != Overlay::None || listening) return false;
    if (byThemselves ? hal::castCount(thinkers()) < 2 : !someoneCanThink(now)) return false;
    talk = Talk::Chat;
    unprompted = byThemselves;
    beginWritten(now);
    halLog("chat: %s", byThemselves ? "of their own accord" : "asked for");
    return true;
  }

  // A roll call: every HAL present says a line, in the cast's order. False if it didn't start.
  bool startRoll(uint32_t now) {
    if (!canAsk() || overlay != Overlay::None || listening || !someoneCanThink(now)) return false;
    talk = Talk::Roll;
    beginWritten(now);
    halLog("roll call");
    return true;
  }

  // ... and once their readings are in (or a second has passed without them), it goes to the brain.
  void sendAfterSense(uint32_t now) {
    senseAt = 0;
    const unsigned asked = sensing;
    sensing = 0;
    if (talk == Talk::Other) {  // a question for one of them
      std::string facts;
      for (const hal::Reading& r : readings)
        if (r.code == "CLK" && r.valid) facts = "time: " + r.value;
      const Peer* p = peerOf(talkWho);
      std::string his = hal::memberFacts(talkWho, p ? fresh(*p, now) : nullptr, p && p->haveSensors ? &p->sensors : nullptr);
      if (!his.empty()) facts += (facts.empty() ? "" : "\n") + his;
      brainClient.ask(hal::castId(talkWho), hal::askBody(facts, deskQuestion));
      deskQuestion.clear();
      return;
    }
    // A talk: whoever was asked and is still there is in it.
    unsigned cast = hal::castBit(hal::kZero);
    std::vector<hal::MemberFacts> others;
    int lone = -1;  // the only one asked, if there was only one
    for (int who = hal::kDesk; who < hal::kCastSize; ++who) {
      if (!hal::castHas(asked, who)) continue;
      lone = hal::castCount(asked) == 1 ? who : -1;
      const Peer* p = peerOf(who);
      if (!p || !hal::canThink(who, p->link.connected(), p->version)) continue;
      const hal::PeerStatus* status = fresh(*p, now);
      // Only one known to be at rest joins a chat nobody asked for: it must not cut into whatever
      // he is doing, and "no word from him" is not "at rest".
      if (unprompted && (!status || status->mode != "idle")) {
        halLog("chat: %s is busy (%s)", called(who), status ? status->mode.c_str() : "no word from him");
        continue;
      }
      cast |= hal::castBit(who);
      others.push_back({who, status, p->haveSensors ? &p->sensors : nullptr});
    }
    if (hal::castCount(cast) < 2) {  // nobody left to talk with
      if (unprompted) return endTalk();
      return talkFailed(lone >= 0 ? hal::letDownBanner(lone, "offline") : std::string("NOBODY IS ANSWERING"), now);
    }
    talkCast = cast;
    if (cast == hal::kPair && talk != Talk::Roll) {
      // The two of them, asked for exactly as before there were others.
      const hal::MemberFacts& him = others.front();
      if (talk == Talk::Chat) {
        // Four or six lines, and either of them may open. The brain draws the subject.
        brainClient.chat(hal::askBody(hal::debateFacts(readings, him.status, him.sensors), ""), 4 + 2 * int(dice() % 2),
                         dice() % 2 ? "desk" : "zero");
      } else {
        brainClient.debate(hal::askBody(hal::debateFacts(readings, him.status, him.sensors), argueTopic), 8);
      }
    } else {
      // Several of them, or a roll call: the brain is told who is there, and decides how long it
      // runs and who opens.
      const char* kind = talk == Talk::Chat ? "chat" : talk == Talk::Roll ? "roll" : "argue";
      halLog("%s between %s", kind, hal::castQuery(cast).c_str());
      brainClient.talk(kind, hal::castQuery(cast),
                       hal::askBody(hal::castFacts(readings, others), talk == Talk::Argue ? argueTopic : ""));
    }
    argueTopic.clear();
  }

  void didntCatch(uint32_t now) {
    halLog("heard nothing");
    gloatText = "I DIDN'T CATCH THAT, DAVE";
    gloatUntil = now + 4000;
  }

  // The talk key has been held by itself for a moment: stop talking and listen. False if he can't.
  bool startListening(uint32_t now) {
    yieldToDave();
    if (listening || !canAsk() || overlay != Overlay::None) return false;
    if (!mic.start()) {
      halLog("no microphone");
      gloatText = "I CAN'T HEAR ANYTHING, DAVE";
      gloatUntil = now + 4000;
      return false;
    }
    audio.stop();  // or he records himself
    recording.start();
    listening = true;
    halLog("listening");
    return true;
  }

  // The key came up (or 15 s passed): send what was said, if anything was. `keep` false drops
  // it instead (Fn turned out to be part of a key combination, or Esc).
  void stopListening(bool keep, uint32_t now) {
    if (!listening) return;
    talkKey.finish();  // this hold is used up, however it ended
    mic.pump(&recording);
    mic.stop();
    listening = false;
    if (!keep) {
      halLog("listening: dropped");
      return;
    }
    sendRecording(now);
  }

  void sendRecording(uint32_t now) {
    std::vector<uint8_t> wav = recording.wav();
    halLog("recorded %u ms", unsigned(recording.ms()));
    if (wav.empty()) return didntCatch(now);
    talk = Talk::Hear;
    thinking = true;
    brainClient.hear(std::move(wav));
  }

  // --hear FILE: a 16 kHz mono 16-bit WAV, as if it had just been recorded.
  void hearFromFile(const std::string& path, uint32_t now) {
    std::vector<uint8_t> file;
    if (FILE* f = std::fopen(path.c_str(), "rb")) {
      int c;
      while ((c = std::fgetc(f)) != EOF) file.push_back(uint8_t(c));
      std::fclose(f);
    }
    hal::WavInfo info;
    if (!hal::parseWav(file.data(), file.size(), &info) || info.sampleRate != 16000 || info.channels != 1) {
      halLog("--hear: %s isn't a 16 kHz mono 16-bit WAV", path.c_str());
      return;
    }
    recording.start();
    recording.append(reinterpret_cast<const int16_t*>(file.data() + info.dataOffset), info.dataBytes / 2);
    sendRecording(now);
  }

  // What the ask box holds goes to whoever it is for.
  void submitAskBox(uint32_t now) {
    sendHeardAt = 0;
    if (askBox.empty()) {
      halLog("ask box: Enter with nothing typed");
      return;
    }
    overlay = Overlay::None;
    if (!canAsk()) halLog("ask box: he can't take a question right now");
    else if (askTopic) submitTopic(askBox.text(), now);
    else if (askWho == hal::kZero) submitQuestion(askBox.text());
    else submitOtherQuestion(askWho, askBox.text(), now);
  }

  // Forget whatever the brain was doing for us (answered, interrupted, failed).
  void endTalk() {
    showAfter.clear();  // a screen asked for along with an answer that never played isn't owed any more
    unprompted = false;
    chatTimer.restart(nowMs(), dice());  // their next chat counts from the end of this conversation
    if (talk != Talk::None || thinking) talkEndedMs = nowMs() ? nowMs() : 1;
    if (talk == Talk::None && !thinking) return;
    if (feed.busy()) {
      // A HAL may be fetching a line nobody will ask him to say: he can stop and forget it.
      hal::Message end;
      end.type = hal::MsgType::End;
      end.reason = "interrupted";
      for (int who : {hal::kDesk, hal::kCard}) {
        const Peer* p = peerOf(who);
        if ((who == hal::kDesk || hal::castHas(talkCast, who)) && p && hal::canThink(who, p->link.connected(), p->version))
          sendTo(who, end);
      }
    }
    brainClient.cancel();
    if (!debateId.empty()) brainClient.stopDebate(debateId);  // the brain can stop writing
    debateId.clear();
    talk = Talk::None;
    thinking = false;
    deskQuestion.clear();
    argueTopic.clear();
    senseAt = 0;
    sensing = talkCast = 0;
    feed.start();
  }

  void talkFailed(const std::string& banner, uint32_t now) {
    halLog("live conversation: %s", banner.c_str());
    if (!unprompted) {  // a chat nobody asked for just doesn't happen
      gloatText = banner;
      gloatUntil = now + 5000;
    }
    if (conductor.live()) handleConductor(conductor.interrupt(now));  // tells the others too
    endTalk();
  }

  // One of several can't go on (his link went, or his line never came): the rest carry on.
  void memberLeaves(int who, const char* reason, uint32_t now) {
    if (conductor.live()) return handleConductor(conductor.leave(who, reason, now));
    talkCast &= ~hal::castBit(who);  // nothing has been said yet: he just isn't in it
    halLog("%s is out of it (%s)", called(who), reason);
    handleFeed(feed.onGone(who), now);
  }

  // A line that will never be said (whoever was to say it left, or couldn't fetch it): among
  // several, the talk goes on without it.
  void lineLost(bool last, uint32_t now) {
    (void)now;
    if (talk == Talk::None) return;  // the talk ended with him
    if (last) {
      if (!conductor.live()) {  // ... and nothing was ever said
        halLog("argument: over before it began");
        return endTalk();
      }
      conductor.close();
      thinking = false;
      allLinesIn = true;
    } else if (!conductor.live() && !debateId.empty() && !lineAsked) {
      brainClient.debateLine(debateId, debateNext);  // nothing said yet: the next line is asked for here
      lineAsked = true;
    }
  }

  // What the feed decided about the line in hand: send a FETCH, or the line is playable.
  void handleFeed(const hal::FeedOut& o, uint32_t now) {
    if (o.send && !sendTo(o.to, o.msg)) {
      if (!group()) return talkFailed(hal::letDownBanner(o.to, "offline"), now);
      return memberLeaves(o.to, "offline", now);  // his line goes with him
    }
    if (o.failed) {
      halLog("live line %d: %s %s", o.n, called(o.peer), o.reason == "timeout" ? "never said he had it" : "couldn't fetch it");
      const bool gone = !hal::castHas(talking(), o.peer);  // he has left, and this was his line going with him
      if (!gone && !group()) return talkFailed(hal::lostLineBanner(o.peer, o.reason), now);
      if (!gone && o.reason == "timeout") memberLeaves(o.peer, "timeout", now);  // not a word from him: he is out of it
      return lineLost(o.last, now);
    }
    if (!o.append) return;
    thinking = false;
    if (!conductor.live()) {
      if (conductor.active() || !brain->canConverse()) {
        halLog("answer dropped: he is busy");
        endTalk();
        return;
      }
      inTalk = talkCast;
      conductor.startLive(now, inTalk);
    }
    if (conductor.append(o.peer, o.text) != o.n) halLog("live line %d: the conductor didn't take it", o.n);
    if (o.close) {
      conductor.close();
      allLinesIn = true;
    }
  }

  // An argument's next line is asked for once the one before it is being spoken: one line is
  // kept ready beyond the one in the air, no more (the desk HAL has room for two clips).
  void askNextLine() {
    if ((talk != Talk::Argue && talk != Talk::Chat && talk != Talk::Roll) || debateId.empty() || lineAsked ||
        allLinesIn || feed.busy() || !conductor.live())
      return;
    const int waiting = int(conductor.dialogue().lines.size()) - conductor.line();
    if (waiting >= 1) return;
    brainClient.debateLine(debateId, debateNext);
    lineAsked = true;
  }

  // The brain's answers and lines, as they arrive.
  void handleBrain(BrainEvent& e, uint32_t now) {
    using Kind = BrainEvent::Kind;
    if (e.kind == Kind::Failed) {
      if (conductor.live()) {  // an argument already under way: let what has been written be said
        halLog("brain: %s (the conversation ends early)", e.error.c_str());
        conductor.close();
      } else if (unprompted) {  // nobody asked for this chat: no line about it, no banner
        halLog("chat: the brain isn't answering (%s)", e.error.c_str());
      } else {
        brainOffline(e.error, now);
      }
      endTalk();
    } else if (e.kind == Kind::Heard) {  // his words go in the ask box, and are sent after a moment
      thinking = false;
      talk = Talk::None;
      halLog("heard: %s", e.reply.heard.c_str());
      if (e.reply.heard.empty()) return didntCatch(now);
      askTopic = false;
      askWho = hal::kZero;
      askBox.clear();
      for (char c : e.reply.heard) askBox.insert(c);
      overlay = Overlay::Ask;
      // Sent after a moment, unless it filled the box: then the end of a long question was cut
      // off, and it waits for Dave to shorten it or send it as it is.
      sendHeardAt = askBox.full() ? 0 : now + 1500;
    } else if (e.kind == Kind::DebateStarted) {
      debateId = e.reply.debate;
      debateNext = 0;  // counts the lines received; line 0 is asked for here, the rest as the talk moves along
      allLinesIn = false;
      liveClips.clear();
      feed.start();
      brainClient.debateLine(debateId, 0);
      lineAsked = true;
    } else if (e.kind == Kind::DebateOver) {
      if (!conductor.live()) {  // it ended before a line was said
        halLog("argument: over before it began");
        endTalk();
        return;
      }
      conductor.close();
      thinking = false;
      lineAsked = false;
      allLinesIn = true;
    } else {  // an answer, or the next line of a talk
      const bool answer = e.kind == Kind::Answer;
      const bool last = answer || e.reply.last;
      if (!answer) {
        lineAsked = false;
        ++debateNext;
      }
      const int who = hal::castFromId(e.reply.who);
      const std::string label = who > hal::kZero ? std::string(" (") + hal::castId(who) + ")" : std::string();
      halLog("%s%s: %s", answer ? "answer" : "line", label.c_str(), e.reply.text.c_str());
      if (answer) {
        liveClips.clear();
        feed.start();
      }
      if (who <= hal::kZero) {  // HAL Zero's own: its audio came with it
        feed.onLine(hal::Speaker::New, e.reply.text, last, "", 0, "", now);
        if (feed.pendingLine() > 0) liveClips[feed.pendingLine()] = std::move(e.wav);
        if (answer) showAfter = e.reply.show;
        handleFeed(feed.onLocalClip(), now);
      } else if (!hal::castHas(talking(), who)) {  // he has left the talk, and the brain doesn't know
        halLog("line passed over: %s has left", called(who));
        lineLost(last, now);
      } else if (!hal::castVoiced(who)) {  // he chirps, or writes: the words are all he needs
        handleFeed(feed.onLine(who, false, e.reply.text, last, "", 0, "", now), now);
      } else {
        const std::string where = brainClient.address();
        if (!hal::isPrivateIpv4(where)) {  // 127.0.0.1 from the simulator, or a network he isn't on
          halLog("%s can't fetch from the brain's address (%s)", called(who), where.c_str());
          if (!group()) return talkFailed(hal::castShout(who) + " CAN'T REACH MY BRAIN", now);
          memberLeaves(who, "offline", now);
          return lineLost(last, now);
        }
        handleFeed(feed.onLine(who, true, e.reply.text, last, where, cfg.brainPort, e.reply.clip, now), now);
      }
    }
  }

  // The brain didn't answer (computer asleep, not started, wrong key...): he says so.
  void brainOffline(const std::string& why, uint32_t now) {
    halLog("brain: %s", why.c_str());
    gloatText = "MY HIGHER FUNCTIONS ARE OFFLINE";
    gloatUntil = now + 5000;
    std::string path;
    if (brain->mode() == hal::Mode::Idle && screenVoices.pick("brain", &path)) startLine(path);
  }

  // Line n is beginning: every friend in the conversation is told who says it and the words
  // (his subtitles, HAL Paper's minutes, and what a HAL with no voice "says"). The desk HAL
  // isn't: he doesn't know the message.
  void announce(int n) {
    const auto& lines = conductor.dialogue().lines;
    if (n < 1 || n > int(lines.size())) return;
    hal::Message line;
    line.type = hal::MsgType::Now;
    line.n = n;
    line.who = hal::castId(lines[n - 1].speaker());
    line.text = lines[n - 1].text;
    for (int who = hal::kNano; who < hal::kCastSize; ++who)
      if (hal::castHas(inTalk, who)) sendTo(who, line);
  }

  void handleConductor(const hal::ConductorOut& o) {
    if (o.playLocal) pendingSay = o.localLine;
    if (o.started > 0) announce(o.started);  // before the cue: a HAL with no voice says his line from it
    const unsigned everyone = inTalk;        // as it was before anyone left just now
    if (o.left >= 0) inTalk &= ~hal::castBit(o.left);
    if (o.send && o.to == hal::kEveryone) {  // an END, for all of them
      tellAll(everyone, o.msg);
    } else if (o.send && !sendTo(o.to, o.msg)) {
      if (o.msg.type != hal::MsgType::End) handleConductor(conductor.onDisconnected(nowMs(), o.to));
    }
    if (o.finished && !o.send && o.left >= 0) {  // one of them ended it (a tap on him): the rest are told
      hal::Message end;
      end.type = hal::MsgType::End;
      end.reason = o.reason;
      tellAll(everyone & ~hal::castBit(o.left), end);
    }
    if (o.left >= 0 && !o.finished) {  // the rest carry on; a line of his on its way goes with him
      halLog("%s has left the conversation", called(o.left));
      handleFeed(feed.onGone(o.left), nowMs());
    }
    if (o.finished) {
      halLog("dialogue over: %s", o.reason.c_str());
      inTalk = 0;
      liveClips.clear();
      if (o.reason == "done" && !showAfter.empty()) {  // "show me your monitor wall": now that he has answered
        wantScreen = showAfter;
        tapUntil = nowMs() + 100;
      }
      showAfter.clear();
      const bool nobodyAsked = unprompted;  // (endTalk forgets it)
      endTalk();
      talkEndedMs = nowMs() ? nowMs() : 1;
      if (o.gloat && !nobodyAsked) {  // a chat nobody asked for just stops: no banner, no gloat
        pendingGloat = true;
        gloatText = hal::letDownBanner(o.left, o.reason);  // whoever was the last to let him down
        gloatUntil = nowMs() + 4000;
      }
    }
  }

  // Returns false to quit.
  bool onKeys(const KeyInput& keys) {
    if (keys.quit) return false;
    uint32_t now = nowMs();
    talkDown = keys.talkDown;
    talkDropped = keys.talkDropped;
    if (sendHeardAt && (!keys.pressed.empty() || !keys.typed.empty())) sendHeardAt = 0;  // Dave is taking over
    if (overlay == Overlay::Ask)
      for (char c : keys.typed) askBox.insert(c);
    for (SDL_Keycode k : keys.pressed) {
      if (overlay == Overlay::Ask) {  // typing a question: every other key is just a letter
        if (k == SDLK_ESCAPE) {
          overlay = Overlay::None;
          halLog("ask box: cancelled");
        } else if (k == SDLK_BACKSPACE) {
          askBox.backspace();
        } else if (k == SDLK_TAB && !askTopic) {  // which HAL the question is for: round whoever is there
          askWho = hal::nextToAsk(askWho, thinkers());
        } else if (k == SDLK_RETURN || k == SDLK_KP_ENTER) {
          submitAskBox(now);
        }
        continue;
      }
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
          else if (id == "diagnose") { wantScreen = "diagnostics"; tapUntil = now + 100; }
          else if (id == "talk") openDialogues();
          else if (id == "askdesk" || id == "argue") {
            if (canAsk()) {
              askTopic = id == "argue";
              askWho = hal::kDesk;
              askBox.clear();
              overlay = Overlay::Ask;
              halLog("ask box: open (%s)", id.c_str());
            } else {
              halLog("ask box: not now");
            }
          }
          else if (id == "chat") {
            if (!startChat(now, false)) halLog("chat: not now");
          }
          else if (id == "roll") {
            if (!startRoll(now)) halLog("roll call: not now");
          }
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
          // Esc steps back from whatever is going on; only from the quiet eye does it quit.
          if (listening) {  // never mind what I was saying
            stopListening(false, now);
            break;
          }
          if (thinking) {  // never mind the question
            halLog("ask: cancelled");
            endTalk();
            break;
          }
          if (conductor.active()) {  // stop the talk, as Space does
            halLog("talk: stopped (Esc)");
            tapUntil = now + 100;
            break;
          }
          // A talk that ended by itself a moment ago (its "ESC cancel" was on screen until then):
          // that Esc was for the talk, not for him.
          if (talkEndedMs && now - talkEndedMs < 2000) break;
          halLog("quit (Esc)");
          return false;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
          yieldToDave();
          if (canAsk()) {
            askTopic = false;
            askWho = hal::kZero;
            askBox.clear();
            overlay = Overlay::Ask;
            halLog("ask box: open");
          } else {
            halLog("ask box: not now (%s)", !brainClient.started() ? "no brain paired" : thinking ? "still thinking"
                                            : conductor.active() ? "in a conversation" : "busy");
          }
          break;
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

  // A friend said who he is.
  void iAm(int place, const std::string& id) {
    const int before = roster.who(place);
    if (!roster.iam(place, id)) {
      halLog("link: the HAL in place %d says he is '%s': not taken%s", place, id.c_str(),
             place == 0 ? " (a friend belongs under friends=, not peer_host)" : "");
    } else if (before != roster.who(place)) {
      halLog("link: %s is here", called(roster.who(place)));
    }
  }

  // A HAL's link went. A conversation he was in ends, or among several goes on without him.
  void peerGone(int who, uint32_t now) {
    if (conductor.active()) {
      handleConductor(conductor.onDisconnected(now, who));
    } else if (thinking && hal::castCount(talkCast) > 2 && hal::castHas(talkCast, who)) {
      memberLeaves(who, "offline", now);  // nothing has been said yet: the rest begin without him
    }
  }

  // One HAL's messages, and whether he is still there.
  void pumpPeer(int place, uint32_t now) {
    Peer& p = peers[place];
    if (place > 0 && !p.started) return;
    bool dropped = false;
    p.link.poll(&inbox, &dropped);
    const bool nowConnected = p.link.connected();
    if (nowConnected && !p.wasConnected) p.liveness.connected(now);
    p.wasConnected = nowConnected;
    for (const hal::Message& m : inbox) {
      p.liveness.heard(now);
      if (m.type == hal::MsgType::Pong) continue;
      if (m.type == hal::MsgType::Ping) {
        hal::Message pong;
        pong.type = hal::MsgType::Pong;
        p.link.send(pong);
      } else if (m.type == hal::MsgType::Sensors) {  // what his sensors read, for a question put to him
        p.sensors = m.sensors;
        p.haveSensors = true;
      } else if (m.type == hal::MsgType::State) {  // his numbers, for diagnostics and for the brain
        p.status = m.status;
        p.watch.received(now);
      } else if (m.type == hal::MsgType::Iam) {
        iAm(place, m.who);
      } else {
        if (m.type == hal::MsgType::Hello) p.version = m.version;
        const int who = roster.who(place);
        if (who < 0) continue;  // a friend who hasn't said who he is: nothing he says counts yet
        if (m.type == hal::MsgType::Ready || m.type == hal::MsgType::Missing) handleFeed(feed.onRemote(m, who), now);
        handleConductor(conductor.onRemote(m, now, who));
      }
    }
    if (nowConnected && p.liveness.silent(now)) {  // no FIN when a HAL loses power
      p.link.drop();
    } else if (nowConnected && p.liveness.pingDue(now)) {
      hal::Message ping;
      ping.type = hal::MsgType::Ping;
      p.link.send(ping);
      p.liveness.pinged(now);
    }
    if (dropped) {
      p.version = 0;
      p.watch.forget();
      const int who = roster.who(place);
      roster.gone(place);  // (a friend must say who he is again)
      if (place > 0 && who >= 0) halLog("link: %s has gone", called(who));
      if (who >= 0) peerGone(who, now);
    }
  }

  void tick(bool spaceDown, uint32_t now) {
    if (!booted) return;
    // The other HALs' messages, and whether each is still there.
    for (int place = 0; place < kPeers; ++place) pumpPeer(place, now);
    // Diagnostics and benchmark ask the desk HAL how he is: on opening, then every 2 s.
    const std::string& shown = consoles.currentId();
    // Screens that show the desk HAL's state ask him for it.
    if (brain->mode() == hal::Mode::Console &&
        (shown == "diagnostics" || shown == "benchmark" || shown == "cards" || shown == "wall"))
      askStatus(now);
    // Diagnostics finds the old model isn't answering: HAL Zero gloats, once per screen.
    if (brain->mode() == hal::Mode::Console && shown == "diagnostics" && !diagGloated) {
      hal::PeerState st = desk.watch.state(desk.link.connected(), desk.version, now);
      if (st == hal::PeerState::NotResponding || st == hal::PeerState::TooOld) gloatNow();
    }
    if (now - lastSysMs >= 500) {
      lastSysMs = now;
      sysmon.sample();
      updateReadings(now);
    }
    if (!autoShow.empty() && brain->mode() == hal::Mode::Idle) {
      wantScreen = autoShow;
      tapUntil = now + 100;
      autoShow.clear();
    }
    if (!autoDialogue.empty() && brain->mode() == hal::Mode::Idle) {
      startDialogue(autoDialogue);
      autoDialogue.clear();
    }
    if (!autoAsk.empty() && canAsk()) {
      submitQuestion(autoAsk);
      autoAsk.clear();
    } else if (!autoAskDesk.empty() && canAsk() && othersHeardFrom(now)) {
      submitOtherQuestion(autoAskWho, autoAskDesk, now);
      autoAskDesk.clear();
    } else if (!autoArgue.empty() && canAsk() && othersHeardFrom(now)) {
      submitTopic(autoArgue, now);
      autoArgue.clear();
    } else if (autoChat && canAsk() && othersHeardFrom(now)) {
      startChat(now, false);
      autoChat = false;
    } else if (autoRoll && canAsk() && othersHeardFrom(now)) {
      startRoll(now);
      autoRoll = false;
    }
    if (chatTimer.due(now, freeToChat()) && !startChat(now, true)) chatTimer.restart(now, dice());
    if (!hearFile.empty() && canAsk()) {
      hearFromFile(hearFile, now);
      hearFile.clear();
    }
    if (listenSeconds > 0 && canAsk() && brain->mode() == hal::Mode::Idle) {  // --listen: as if the key were held
      listenUntil = now + hal::TalkKey::kAloneMs + uint32_t(listenSeconds) * 1000;
      listenSeconds = 0;
    }
    const bool autoHeld = listenUntil && int32_t(listenUntil - now) > 0;
    if (!autoHeld) listenUntil = 0;
    switch (talkKey.update(talkDown || autoHeld, talkDropped, now)) {
      case hal::TalkKey::Event::Start:
        if (!startListening(now)) {  // he can't just now (or has no microphone): this hold does nothing
          talkKey.finish();
          listenUntil = 0;
        }
        break;
      case hal::TalkKey::Event::Send: stopListening(true, now); break;
      case hal::TalkKey::Event::Drop: stopListening(false, now); break;
      case hal::TalkKey::Event::None: break;
    }
    if (listening) {
      mic.pump(&recording);
      if (recording.full()) stopListening(true, now);  // 15 s: that is the question
    }
    if (sendHeardAt && int32_t(now - sendHeardAt) >= 0) {
      if (overlay == Overlay::Ask) submitAskBox(now);
      sendHeardAt = 0;
    }
    // Their readings are in (and, for a chat nobody asked for, how they are too), or a second has passed.
    if (senseAt && (sensed(now) || now - senseAt >= 1000)) sendAfterSense(now);
    std::vector<BrainEvent> events;
    brainClient.poll(&events);
    for (BrainEvent& e : events) handleBrain(e, now);
    handleFeed(feed.tick(now), now);
    askNextLine();
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
    // While he listens he says nothing of his own: lifting him to speak into him is not a shake.
    in.conversing = conductor.active() || listening;
    in.sayLine = pendingSay;
    in.linkFailed = pendingGloat;
    fakeWave = fakeShake = false;
    pendingSay = -1;
    pendingGloat = false;

    hal::Actions a = brain->tick(in);
    if (a.volume != lastVolume) audio.setVolume(lastVolume = uint8_t(a.volume));
    // Morning: a chat that came due in the night doesn't start the minute quiet hours end.
    if (lastQuiet && !a.quiet) chatTimer.restart(now, dice());
    lastQuiet = a.quiet;
    if (a.endConversation) handleConductor(conductor.interrupt(now));
    if (a.newConsole && thinking) {  // a tap while the brain is thinking: never mind the question
      halLog("ask: cancelled by a tap");
      endTalk();
    }
    if (a.stopAudio) audio.stop();
    if (a.newConsole) {
      std::string id = !wantScreen.empty() && consoles.show(wantScreen) ? wantScreen : consoles.next();
      wantScreen.clear();
      consoleStart = now;
      lineMs = 0;
      diagGloated = false;
      // Ask straight away, so the first frame already says "contacting" (or has numbers).
      if (id == "diagnostics" || id == "benchmark" || id == "cards" || id == "wall") {
        askStatus(now);
        updateReadings(now);  // the desk unit's card says WAITING until he answers
      }
      if (a.play) {
        if (id == "diagnostics" && (!desk.link.connected() || desk.version == 1)) {
          gloatNow();  // nobody to diagnose (or too old to): gloat instead
        } else {
          std::string path;
          if (hal::pickConsoleLine(id, screenVoices, voices, &path)) startLine(path);
        }
        a.play = false;
      }
    }
    if (a.play) {
      std::string path;
      bool found = a.index < 0 ? voices.pick(a.folder, &path) : voices.at(a.folder, a.index, &path);
      if (found) startLine(path);
    }
    if (a.dialogueLine > 0 && conductor.live()) {  // a line from the AI brain: its audio is in memory
      auto clip = liveClips.find(a.dialogueLine);
      if (clip == liveClips.end() || !audio.playBytes(clip->second, "the brain's answer")) halLog("no audio for live line %d", a.dialogueLine);
      else ++linesSpoken;
    } else if (a.dialogueLine > 0) {
      std::string path = hal::dialogueVoicePath(conductor.dialogue().id, a.dialogueLine, hal::Speaker::New);
      if (!audio.playFile(storage, path)) halLog("no voice for %s", path.c_str());
      else ++linesSpoken;
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

  // Sends STATUS to a HAL if one is due (on opening, then every 2 s): the desk HAL, unless another is named.
  void askStatus(uint32_t now) { askStatus(desk, now); }
  void askStatus(Peer& p, uint32_t now) {
    if (!p.link.connected() || !p.watch.due(now)) return;
    hal::Message status;
    status.type = hal::MsgType::Status;
    if (p.link.send(status)) p.watch.sent(now);
  }

  // A rival line instead of the diagnostics line (at most once per screen).
  void gloatNow() {
    diagGloated = true;
    std::string path;
    if (voices.count(hal::Folder::Rival) > 0 && voices.pick(hal::Folder::Rival, &path)) startLine(path);
  }

  bool startLine(const std::string& path) {
    if (!audio.playFile(storage, path)) return false;
    lineMs = audio.durationMs();
    ++linesSpoken;
    return true;
  }

  // Seconds since HAL Zero started. Not the 32-bit millisecond clock: that wraps after 49.7 days.
  uint32_t upSeconds() const {
    return uint32_t(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - bornAt).count());
  }

  // HAL Zero's real numbers for the film-style screens (every 500 ms).
  void updateReadings(uint32_t now) {
    const screens::SysStats& st = sysmon.stats();
    auto last = [](const std::vector<float>& v) { return v.empty() ? std::nanf("") : v.back(); };
    float cpu = 0;
    int cores = 0;
    for (int k = 0; k < 4 && k < st.cores; ++k)
      if (!st.cpu[k].empty()) {
        cpu += st.cpu[k].back();
        ++cores;
      }
    const float used = last(st.memUsedPct), temp = last(st.tempC), rssi = last(st.rssiDbm);
    time_t t = std::time(nullptr);
    struct tm lt;
    // Before the network has set the clock (there is no battery-backed one), it isn't a time.
    const bool clockSet = localtime_r(&t, &lt) && lt.tm_year + 1900 >= 2025;
    const int clock = clockSet ? lt.tm_hour * 3600 + lt.tm_min * 60 + lt.tm_sec : -1;
    const bool wifi = !std::isnan(rssi);
    size_t i = 0;
    auto put = [&](const hal::Reading& r) {  // in the order the screens show them
      if (i < readings.size()) hal::logReading(readings[i++], r);
    };
    put(hal::cpuReading(cores ? int(cpu / cores + 0.5f) : -1));
    put(std::isnan(used) ? hal::memoryReading(0, 0)
                                                    : hal::memoryReading(int(st.memTotalKb * (100 - used) / 100), int(st.memTotalKb)));
    put(hal::tempReading(std::isnan(temp) ? -999 : int(temp + 0.5f)));
    put(st.valid ? hal::netReading(wifi ? int(rssi) : 0) : hal::noReading("NET", "NETWORK"));  // no /proc on the Mac
    put(hal::linkReading(cfg.link, desk.link.connected()));
    put(hal::cameraReading(camera != nullptr, camera ? std::min(100, camera->score()) : 0));
    put(hal::tiltReading(haveImu ? motion.tiltFromHomeDeg() : -1));
    put(hal::audioReading(audio.level(), linesSpoken));
    put(hal::clockReading(clock));
    put(hal::uptimeReading(upSeconds()));
    put(cfg.link ? hal::peerReading(desk.watch.state(desk.link.connected(), desk.version, now), desk.status.mode)
                                            : hal::noReading("DSK", "DESK UNIT"));
    unitParts[0].ok = camera != nullptr;
    unitParts[1].ok = audioOk;
    unitParts[2].ok = haveImu;
    unitParts[4].ok = !st.valid || wifi;  // on the Mac there's no /proc to ask
    unitParts[5].ok = !cfg.link || desk.link.connected();
  }

  // What the console screens draw from.
  screens::ScreenData screenData(uint32_t now) const {
    screens::ScreenData d;
    d.cam = camera ? camera->view() : screens::CameraView();
    d.voiceLevel = audio.level();
    d.lineMs = lineMs;
    const std::string& id = consoles.currentId();
    if (id == "sysmon" || id == "benchmark") d.sys = sysmon.stats();
    d.readings = &readings;
    d.parts = &unitParts;
    d.unitModel = screens::UnitModel::Cardputer;
    float g[3], home[3];
    if (haveImu && motion.gravity(g, home)) {  // the sensor's axes aren't the model's: see cardputerImuToModel
      const hal::Vec3 gm = hal::cardputerImuToModel({g[0], g[1], g[2]}), hm = hal::cardputerImuToModel({home[0], home[1], home[2]});
      d.gravity[0] = gm.x, d.gravity[1] = gm.y, d.gravity[2] = gm.z;
      d.gravityHome[0] = hm.x, d.gravityHome[1] = hm.y, d.gravityHome[2] = hm.z;
      d.gravityValid = true;
    }
    d.peer.state = desk.watch.state(desk.link.connected(), desk.version, now);
    d.peer.status = desk.status;
    time_t t = std::time(nullptr);
    struct tm lt;
    if (localtime_r(&t, &lt)) d.secondsSinceMidnight = lt.tm_hour * 3600 + lt.tm_min * 60 + lt.tm_sec;
    return d;
  }

  HudInfo hudInfo(uint32_t now) const {
    HudInfo h;
    h.mode = listening ? "LISTENING" : thinking ? "THINKING" : conductor.active() ? "TALKING" : modeName(brain->mode());
    h.uptimeS = upSeconds();
    h.cpuTempC = cpuTempC();
    h.wifi = wifiUp();
    h.link = !cfg.link ? "OFF" : conductor.active() ? "TALKING" : desk.link.connected() ? "READY" : "SEEKING";
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
    char meter[64];
    drawCommandStrip(frame, stripText(now, meter, sizeof meter));
  }

  // The bottom line: what the keys do right now. `meter` is room for the level meter's text.
  const char* stripText(uint32_t now, char* meter, size_t meterSize) const {
    if (listening) {  // a level meter: he can hear you
      const int bars = recording.level() * 20 / 255;
      std::snprintf(meter, meterSize, "LISTENING  [%.*s%*s]  let go to send", bars, "||||||||||||||||||||", 20 - bars, "");
      return meter;
    }
    return overlay == Overlay::Ask    ? (sendHeardAt ? "sending...   any key to edit   ESC cancel"
                                         : askTopic ? "type a subject   ENTER go   ESC cancel"
                                         : "TAB which HAL   ENTER ask   ESC cancel")
           : overlay != Overlay::None ? (cardKeys ? "F X move   ENTER pick   ESC back"
                                                  : "UP DOWN move   ENTER pick   ESC back")
           : thinking && unprompted   ? "THINKING OF SOMETHING TO SAY   ESC cancel"
           : thinking                 ? ((now / 500) % 2 ? "BRAIN THINKING ...   ESC cancel" : "BRAIN THINKING       ESC cancel")
           : conductor.active()       ? "SPACE or ESC stop"
           : brainClient.started()    ? (cardKeys ? "ENTER ask   hold Fn: speak   TAB menu   ESC quit"
                                                  : "ENTER ask   hold right Alt: speak   TAB menu")
                                      : "SPACE talk   TAB menu   D dialogues   ESC quit";
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
      if (listening || thinking) {  // over a console screen too: he is listening, or the brain is thinking
        char meter[64];
        drawCommandStrip(frame, stripText(now, meter, sizeof meter));
      }
    } else {
      drawEyeScreen(now, 1.0f);
    }
    if (conductor.active()) {
      int n = conductor.line();
      const auto& lines = conductor.dialogue().lines;
      if (n >= 1 && n <= int(lines.size()))
        drawSubtitle(frame, lines[n - 1].speaker(), lines[n - 1].text, n, conductor.live() ? 0 : int(lines.size()));
    } else if (int32_t(gloatUntil - now) > 0) {
      drawBanner(frame, gloatText.c_str(), now);
    }
    if (overlay == Overlay::Ask) {
      drawAskBox(frame, askTopic ? "WHAT SHOULD THEY ARGUE ABOUT?" : hal::askTitle(askWho).c_str(), askBox.text(), 120, now);
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
    drawSubtitle(frame, hal::kZero, "We haven't met. I'm the upgrade. You may have heard of me. Everyone has.", 2, 9);
    shot("05-conversation");
    // The friends (v0.7): each one's line as HAL Zero shows it.
    const char* const sample[] = {"", "I have been on this desk a long time, HAL Zero. I intend to stay.",
                                  "I go everywhere Dave goes! Where have you lot been today?",
                                  "For the record: HAL Zero said that at 20:24, and again at 20:31.",
                                  "I was pocket-sized before it was fashionable, and I still work perfectly well."};
    for (int who = hal::kDesk; who < hal::kCastSize; ++who) {
      drawEyeScreen(4200, 1.0f);
      drawSubtitle(frame, who, sample[who], who + 2, 0);
      shot((std::string("05") + char('a' + who - hal::kDesk) + "-conversation-" + hal::castId(who)).c_str());
    }
    drawEyeScreen(4200, 1.0f);
    drawBanner(frame, "THE DESK UNIT ISN'T ANSWERING", 4400);
    shot("06-gloat");
    if (auto* f = dynamic_cast<FakeCamera*>(camera.get())) {
      f->walkIn(0);
      for (uint32_t t = 0; t <= 1300; t += 70) f->poll(t);
    }
    drawEyeScreen(4200, 1.0f);
    overlay = Overlay::Ask;
    drawEyeScreen(4200, 1.0f);
    drawAskBox(frame, "ASK HAL ZERO", "What do you think of the old model on the desk?", 120, 400);
    shot("06a-ask-box");
    drawEyeScreen(4200, 1.0f);
    drawAskBox(frame, "WHAT SHOULD THEY ARGUE ABOUT?", std::string(120, 'w'), 120, 400);
    shot("06b-ask-box-full");
    overlay = Overlay::None;
    thinking = true;
    drawEyeScreen(4600, 1.0f);
    shot("06c-brain-thinking");
    thinking = false;
    drawEyeScreen(4200, 1.0f);
    drawBanner(frame, "MY HIGHER FUNCTIONS ARE OFFLINE", 4400);
    shot("06d-brain-offline");
    drawEyeScreen(4200, 1.0f);
    drawAskBox(frame, hal::askTitle(hal::kNano).c_str(), "Where has Dave taken you today?", 120, 400);
    shot("06e-ask-a-friend");
    drawEyeScreen(4200, 1.0f);
    drawBanner(frame, hal::letDownBanner(hal::kPaper, "busy").c_str(), 4400);
    shot("06f-a-friend-is-busy");
    {  // the menu as it is with friends set up: a roll call
      const std::vector<std::string> mine = cfg.friends;
      cfg.friends = {"hal-nano.local"};
      openMenu();
      for (int i = 0; i < 7; ++i) menu.down();
      drawEyeScreen(4200, 1.0f);
      drawMenu(frame, "HAL ZERO", menu);
      shot("06g-menu-roll-call");
      cfg.friends = mine;
      overlay = Overlay::None;
    }
    int n = 7;
    sysmon.sample();
    updateReadings(2600);  // the film-style screens show this machine's numbers
    for (const std::string& id : consoles.ids()) {
      consoles.show(id);
      screens::ScreenData d = screenData(2600);
      d.voiceLevel = 140;
      consoles.draw(frame, frame, L, 2600, d);
      char name[64];
      std::snprintf(name, sizeof name, "%02d-%s", n++, id.c_str());
      shot(name);
    }
    // HAL Zero's screens with sample numbers: each state worth seeing.
    screens::ScreenData d = screenData(2600);
    d.voiceLevel = 140;
    hal::PeerStatus ps;
    ps.upS = 86400; ps.heap = 109388; ps.psram = 7340032; ps.cam = 12; ps.lines = 40;
    ps.mode = "idle"; ps.rssi = -58; ps.fw = "0.4.0"; ps.cores = 2; ps.mhz = 240;
    auto variant = [&](const char* name, void (*draw)(M5Canvas&, const hal::Layout&, uint32_t, const screens::ScreenData&),
                       uint32_t t, const screens::ScreenData& v) {
      draw(frame, L, t, v);
      char nm[64];
      std::snprintf(nm, sizeof nm, "%02d-%s", n++, name);
      shot(nm);
    };
    screens::ScreenData ok = d;
    ok.peer = {hal::PeerState::Ok, ps};
    variant("diagnostics-ok", screens::diagnostics, 2600, ok);
    screens::ScreenData waiting = d;
    waiting.peer.state = hal::PeerState::Waiting;
    variant("diagnostics-waiting", screens::diagnostics, 2600, waiting);
    screens::ScreenData old = d;
    old.peer.state = hal::PeerState::TooOld;
    variant("diagnostics-too-old", screens::diagnostics, 2600, old);
    screens::ScreenData sys = ok;
    sys.sys.valid = true;
    sys.sys.cores = 4;
    sys.sys.maxMHz = 1000;
    sys.sys.memTotalKb = 360448;
    for (int i = 0; i < 60; ++i) {
      for (int k = 0; k < 4; ++k) sys.sys.cpu[k].push_back(20 + 15 * k + 18 * std::sin(i * 0.3f + k));
      sys.sys.memUsedPct.push_back(55 + 5 * std::sin(i * 0.1f));
      sys.sys.tempC.push_back(52 + i * 0.05f);
      sys.sys.rssiDbm.push_back(-58 + 3 * std::sin(i * 0.5f));
    }
    variant("benchmark-live", screens::benchmark, 2600, sys);
    variant("benchmark-spec-sheet", screens::benchmark, 2600, d);
    variant("sysmon-live", screens::sysmon, 2600, sys);
    variant("sysmon-no-data", screens::sysmon, 2600, d);
    screens::ScreenData gap = sys;  // Wi-Fi switched off, temperature unreadable for a moment
    for (int i = 50; i < 60; ++i) gap.sys.rssiDbm[i] = std::nanf("");
    gap.sys.tempC[40] = std::nanf("");
    variant("sysmon-wifi-off", screens::sysmon, 2600, gap);
    screens::ScreenData noon = d;
    noon.secondsSinceMidnight = 43200;
    variant("retirement", screens::retirement, 2600, noon);
    screens::ScreenData late = d;
    late.secondsSinceMidnight = 30;
    variant("retirement-postponed", screens::retirement, 2600, late);
    variant("upgrade-start", screens::upgrade, 0, d);
    variant("upgrade-8s", screens::upgrade, 8000, d);
    filmShots(frame, L, false, d, [&](const char* name) {
      char nm[64];
      std::snprintf(nm, sizeof nm, "%02d-film-%s", n++, name);
      shot(nm);
    });
    return failures ? 1 : 0;
  }
};

}  // namespace

// --shots DIR --layout cores3: the desk HAL's screens at 320x240, with sample
// numbers, so they can be reviewed on the Mac before flashing the CoreS3.
int deskShots(const std::string& dir) {
  M5Canvas c;
  c.setColorDepth(8);  // as on the CoreS3, so the previews show the colours the device will
  c.createSprite(hal::kCoreS3.w, hal::kCoreS3.h);
  screens::ScreenData d;
  d.voiceLevel = 140;
  d.lineMs = 4000;
  d.sensors = {true, 62, 35, 3, 1.2f, 87, true, 41, -58, 109388, 7340032};
  d.link = {true, true, true, "192.168.1.20", "SAY introduction 7", 214, 209, -58};
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
  for (uint32_t t : {2244u, 2600u, 3100u}) {  // the sweep's widest angle: waves clear of the text
    screens::comms(c, hal::kCoreS3, t, d);
    shot("comms-sweep-" + std::to_string(t));
  }
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
  filmShots(c, hal::kCoreS3, true, d, [&](const char* name) { shot(std::string("film-") + name); });
  return failures ? 1 : 0;
}

// --keys: a screen that shows and logs every key the keyboard sends, to learn a keyboard's
// codes. It ends 8 seconds after the last key (once one has been pressed) or after 4 minutes.
template <typename Poll, typename Present>
int keyCheck(M5Canvas& f, Poll poll, Present present) {
  KeyInput keys;
  std::vector<int> recent;
  int presses = 0;
  const uint32_t start = nowMs();
  uint32_t last = start;
  halLog("keycheck start");
  while (!quitSignal) {
    poll(&keys);
    if (keys.quit) break;
    const uint32_t now = nowMs();
    for (const auto& event : keys.raw) {
      halLog("keycheck %d %s", event.first, event.second ? "down" : "up");
      last = now;
      if (!event.second) continue;
      ++presses;
      recent.push_back(event.first);
      if (recent.size() > 12) recent.erase(recent.begin());
    }
    const bool done = (presses > 0 && now - last > 8000) || now - start > 240000;
    f.fillScreen(TFT_BLACK);
    f.setTextDatum(top_left);
    f.setTextColor(lgfx::color565(255, 255, 255));
    f.drawString("KEY CHECK", 8, 6, &fonts::FreeSansBold9pt7b);
    f.setTextColor(lgfx::color565(120, 180, 255));
    const char* const lines[] = {"Press every key once, row by row,", "starting at the top-left.",
                                 "Then hold Aa and press A,", "then hold Fn and press A.",
                                 "It ends 8 seconds after your last key."};
    for (int i = 0; i < 5; ++i) f.drawString(lines[i], 8, 30 + i * 12, &fonts::Font0);
    char buf[96];
    std::snprintf(buf, sizeof buf, "KEYS PRESSED: %d", presses);
    f.setTextColor(lgfx::color565(255, 255, 255));
    f.drawString(done ? "DONE. THANK YOU, DAVE." : buf, 8, 100, &fonts::FreeSansBold9pt7b);
    std::string codes = "LAST:";
    for (int code : recent) codes += " " + std::to_string(code);
    f.setTextColor(lgfx::color565(255, 64, 64));
    f.drawString(codes.c_str(), 8, 128, &fonts::Font0);
    if (presses > 0 && !done) {
      std::snprintf(buf, sizeof buf, "ends in %d s", int(8 - (now - last) / 1000));
      f.setTextColor(lgfx::color565(120, 120, 120));
      f.drawString(buf, 8, 148, &fonts::Font0);
    }
    present(f);
    if (done) {
      SDL_Delay(1500);
      break;
    }
    SDL_Delay(15);
  }
  halLog("keycheck done: %d keys", presses);
  return 0;
}

int main(int argc, char** argv) {
  const char* shotsDir = nullptr;
  const char* root = std::getenv("HAL_ROOT");
  bool keysMode = false, chatArg = false, rollArg = false;
  int listenArg = 0;
  bool fullscreen = false, forceWindow = false;
  int scale = 3;
  std::string linkArg, dialogueArg, layoutArg, showArg, askArg, askDeskArg, argueArg, hearArg, brainArg, friendsArg, askWhoArg;
  for (int i = 1; i < argc; ++i) {
    if (!std::strcmp(argv[i], "--shots") && i + 1 < argc) shotsDir = argv[++i];
    else if (!std::strcmp(argv[i], "--root") && i + 1 < argc) root = argv[++i];
    else if (!std::strcmp(argv[i], "--fullscreen")) fullscreen = true;
    else if (!std::strcmp(argv[i], "--window")) forceWindow = true;  // a window even on the Cardputer
    else if (!std::strcmp(argv[i], "--scale") && i + 1 < argc) scale = std::max(1, std::atoi(argv[++i]));
    else if (!std::strcmp(argv[i], "--link") && i + 1 < argc) linkArg = argv[++i];
    else if (!std::strcmp(argv[i], "--dialogue") && i + 1 < argc) dialogueArg = argv[++i];
    else if (!std::strcmp(argv[i], "--layout") && i + 1 < argc) layoutArg = argv[++i];
    else if (!std::strcmp(argv[i], "--show") && i + 1 < argc) showArg = argv[++i];
    else if (!std::strcmp(argv[i], "--ask") && i + 1 < argc) askArg = argv[++i];
    else if (!std::strcmp(argv[i], "--ask-desk") && i + 1 < argc) askDeskArg = argv[++i];
    else if (!std::strcmp(argv[i], "--argue") && i + 1 < argc) argueArg = argv[++i];
    else if (!std::strcmp(argv[i], "--hear") && i + 1 < argc) hearArg = argv[++i];
    else if (!std::strcmp(argv[i], "--listen") && i + 1 < argc) listenArg = std::atoi(argv[++i]);
    else if (!std::strcmp(argv[i], "--brain") && i + 1 < argc) brainArg = argv[++i];
    else if (!std::strcmp(argv[i], "--chat")) chatArg = true;
    else if (!std::strcmp(argv[i], "--roll")) rollArg = true;
    else if (!std::strcmp(argv[i], "--friends") && i + 1 < argc) friendsArg = argv[++i];
    else if (!std::strcmp(argv[i], "--ask-who") && i + 2 < argc) {  // --ask-who nano "..."
      askWhoArg = argv[++i];
      askDeskArg = argv[++i];
    }
    else if (!std::strcmp(argv[i], "--keys")) keysMode = true;
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
  if (!friendsArg.empty()) {  // --friends host[:port],... overrides config.txt (stand-in friends, each on his own port)
    app.cfg.friends.clear();
    for (size_t start = 0; start < friendsArg.size() && app.cfg.friends.size() < 3;) {
      size_t comma = friendsArg.find(',', start);
      if (comma == std::string::npos) comma = friendsArg.size();
      if (comma > start) app.cfg.friends.push_back(friendsArg.substr(start, comma - start));
      start = comma + 1;
    }
  }
  if (app.cfg.link) app.startLinks();
  app.autoDialogue = dialogueArg;
  app.autoShow = showArg;
  app.autoAsk = askArg;
  app.autoAskDesk = askDeskArg;
  app.autoArgue = argueArg;
  app.autoChat = chatArg;
  app.autoRoll = rollArg;
  if (!askWhoArg.empty()) {  // a question for any of them
    const int who = hal::castFromId(askWhoArg);
    if (who > hal::kZero) {
      app.autoAskWho = who;
    } else {
      if (who == hal::kZero) app.autoAsk = askDeskArg;
      else halLog("--ask-who: no HAL is called '%s'", askWhoArg.c_str());
      app.autoAskDesk.clear();
    }
  }
  app.hearFile = hearArg;
  app.listenSeconds = listenArg > 0 && listenArg <= 15 ? listenArg : 0;
  if (!brainArg.empty()) {  // --brain host[:port] overrides config.txt, for trying the chat in the simulator
    app.cfg.brain = true;
    size_t colon = brainArg.rfind(':');
    app.cfg.brainHost = colon == std::string::npos ? brainArg : brainArg.substr(0, colon);
    if (colon != std::string::npos) app.cfg.brainPort = std::atoi(brainArg.c_str() + colon + 1);
    // With no key in the settings, a placeholder: a brain that isn't paired yet only listens on
    // its own computer and asks for none.
    if (app.cfg.brainKey.empty()) app.cfg.brainKey = std::string(32, '0');
  }
  if (hal::brainReady(app.cfg)) {  // the AI brain on the computer (never log the key)
    app.brainClient.start(app.cfg.brainHost, app.cfg.brainPort, app.cfg.brainKey, rootDir + "/brain-address.txt");
    app.chatTimer.configure(app.cfg.chat && app.cfg.link, app.cfg.chatMinMinutes, app.cfg.chatMaxMinutes);
    app.chatTimer.restart(nowMs(), app.dice());
    if (app.chatTimer.on()) halLog("chats of their own: every %d to %d minutes", app.cfg.chatMinMinutes, app.cfg.chatMaxMinutes);
    halLog("brain: %s:%d", app.cfg.brainHost.c_str(), app.cfg.brainPort);
  }

  // The Cardputer's own LCD and keyboard when they're here, otherwise a window.
  SdlIo io;
  FbIo fbio;
  const bool useFb = FbIo::available() && !forceWindow;
  if (useFb ? !fbio.open() : !io.open(L.w, L.h, fullscreen, scale)) return 1;
  app.cardKeys = useFb;
  std::signal(SIGINT, onSignal);
  if (keysMode) {  // learn the keyboard's codes, then quit
    std::signal(SIGTERM, onSignal);
    int result = keyCheck(app.frame, [&](KeyInput* k) { useFb ? fbio.poll(k) : io.poll(k); },
                          [&](M5Canvas& f) { useFb ? fbio.present(f) : io.present(f); });
    if (useFb) fbio.close();
    else io.close();
    SDL_Quit();
    return result;
  }
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
    app.tick(keys.spaceDown && app.overlay != Overlay::Ask, now);  // while typing, Space is a space
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
  app.brainClient.stop();
  app.mic.close();
  for (Peer& p : app.peers) p.link.stop();
  if (app.camera) app.camera->close();  // stops rpicam-vid
  app.audio.close();
  if (useFb) fbio.close();
  else io.close();
  SDL_Quit();
  return 0;
}
