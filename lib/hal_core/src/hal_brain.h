#pragma once
// HAL's state machine. The firmware feeds it one Inputs snapshot per loop and
// carries out the Actions it returns. No hardware code in here.
#include <cstdint>

#include "hal_config.h"
#include "hal_motion.h"
#include "hal_shuffle.h"
#include "hal_voice_index.h"

namespace hal {

enum class Mode { Booting, Idle, Speaking, Console, Pleading, Daisy, Dark };

struct Inputs {
  uint32_t nowMs = 0;
  bool touchDown = false;      // a finger is on the screen right now
  bool proximityEdge = false;  // someone just walked up (one tick)
  MotionEvent motion = MotionEvent::None;
  bool audioPlaying = false;
  bool timeValid = false;  // clock has been set
  int minuteOfDay = 0;
  int hour = 0;
};

struct Actions {
  bool play = false;
  Folder folder = Folder::Tap;
  int index = -1;  // -1 = shuffled pick, otherwise file number in name order
  bool stopAudio = false;
  bool newConsole = false;
  bool restart = false;
  int volume = 0;
  bool quiet = false;
};

class Brain {
 public:
  Brain(const Config& config, uint32_t seed, bool afterShutdown);
  void setSequenceLengths(int plea, int daisy);
  Actions tick(const Inputs& in);
  Mode mode() const { return mode_; }
  float daisyProgress() const;  // 0..1 while the eye fades during Daisy Bell

 private:
  void play(Actions& a, Folder f, int index = -1);
  bool finished(const Inputs& in) const;
  void startPlea(Actions& a);
  void startDaisy(Actions& a);

  Config cfg_;
  ShuffleBag rng_;
  bool afterShutdown_;
  Mode mode_ = Mode::Booting;
  int pleaLen_ = 0;
  int daisyLen_ = 0;
  uint32_t now_ = 0;

  uint32_t lastPlayMs_ = 0;
  bool havePlayed_ = false;

  bool touchWasDown_ = false;
  bool holdFired_ = false;
  uint32_t touchStartMs_ = 0;

  uint32_t consoleStartMs_ = 0;
  int seqIndex_ = 0;
  uint32_t seqStartMs_ = 0;
  uint32_t darkStartMs_ = 0;

  bool greeted_ = false;
  uint32_t lastGreetMs_ = 0;
  bool shaken_ = false;
  uint32_t lastShakeMs_ = 0;
  bool idleScheduled_ = false;
  uint32_t nextIdleAtMs_ = 0;
};

}  // namespace hal
