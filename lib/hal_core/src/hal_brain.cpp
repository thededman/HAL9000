#include "hal_brain.h"

#include "hal_clock.h"

namespace hal {
namespace {

constexpr uint32_t kConsoleMs = 8000;
constexpr uint32_t kMinClipMs = 200;          // a play counts as finished only after this
constexpr uint32_t kIdleQuietGapMs = 300000;  // no remark within 5 min of anything else
constexpr uint32_t kDarkMs = 5000;
constexpr uint32_t kDaisyLineMs = 4000;  // expected length, for the eye fade

Folder greetFolder(const Inputs& in) {
  if (!in.timeValid) return Folder::GreetAfternoon;
  switch (greetBucket(in.hour)) {
    case GreetBucket::Morning: return Folder::GreetMorning;
    case GreetBucket::Afternoon: return Folder::GreetAfternoon;
    default: return Folder::GreetEvening;
  }
}

}  // namespace

Brain::Brain(const Config& config, uint32_t seed, bool afterShutdown)
    : cfg_(config), rng_(seed), afterShutdown_(afterShutdown) {}

void Brain::setSequenceLengths(int plea, int daisy) {
  pleaLen_ = plea;
  daisyLen_ = daisy;
}

void Brain::play(Actions& a, Folder f, int index) {
  a.play = true;
  a.folder = f;
  a.index = index;
  markPlayed();
}

void Brain::markPlayed() {
  lastPlayMs_ = now_;
  havePlayed_ = true;
}

bool Brain::finished(const Inputs& in) const {
  return !in.audioPlaying && in.nowMs - lastPlayMs_ >= kMinClipMs;
}

void Brain::startPlea(Actions& a) {
  if (pleaLen_ <= 0) {
    startDaisy(a);
    return;
  }
  mode_ = Mode::Pleading;
  seqIndex_ = 0;
  play(a, Folder::Plea, 0);
}

void Brain::startDaisy(Actions& a) {
  seqIndex_ = 0;
  seqStartMs_ = now_;
  if (daisyLen_ <= 0) {
    mode_ = Mode::Dark;
    darkStartMs_ = now_;
    return;
  }
  mode_ = Mode::Daisy;
  play(a, Folder::Daisy, 0);
}

float Brain::daisyProgress() const {
  if (mode_ == Mode::Dark) return 1.0f;
  if (mode_ != Mode::Daisy || daisyLen_ <= 0) return 0.0f;
  float part = float(now_ - seqStartMs_) / kDaisyLineMs;
  if (part > 1.0f) part = 1.0f;
  return (seqIndex_ + part) / daisyLen_;
}

Actions Brain::tick(const Inputs& in) {
  now_ = in.nowMs;
  Actions a;
  a.quiet = in.timeValid && isQuietHours(in.minuteOfDay, cfg_.quietStartMin, cfg_.quietEndMin);
  a.volume = a.quiet ? cfg_.quietVolume : cfg_.volume;

  if (!idleScheduled_) {
    nextIdleAtMs_ = now_ + nextIdleDelayMs(rng_.random(), cfg_.idleMinMinutes, cfg_.idleMaxMinutes);
    idleScheduled_ = true;
  }

  // Touch: a tap is a short press-and-release; a hold fires once per press.
  const uint32_t holdMs = uint32_t(cfg_.holdSeconds) * 1000u;
  bool pressed = in.touchDown && !touchWasDown_;
  bool released = !in.touchDown && touchWasDown_;
  if (pressed) {
    touchStartMs_ = now_;
    holdFired_ = false;
  }
  bool tap = released && !holdFired_ && now_ - touchStartMs_ < holdMs;
  bool holdStart = in.touchDown && !pressed && !holdFired_ && now_ - touchStartMs_ >= holdMs;
  touchWasDown_ = in.touchDown;

  switch (mode_) {
    case Mode::Booting:
      play(a, afterShutdown_ ? Folder::Reboot : Folder::Boot);
      mode_ = Mode::Speaking;
      return a;

    case Mode::Idle:
    case Mode::Speaking:
    case Mode::Console: {
      const bool talking = in.conversing || dialogueNow_ > 0;
      if ((holdStart || tap) && talking) {  // Dave's finger beats any conversation
        a.endConversation = true;
        dialogueNow_ = 0;
      }
      if (holdStart) {
        holdFired_ = true;
        a.stopAudio = true;
        startPlea(a);
        return a;
      }
      if (tap) {
        a.stopAudio = true;
        a.newConsole = true;
        play(a, Folder::Tap);
        mode_ = Mode::Console;
        consoleStartMs_ = now_;
        return a;
      }
      if (in.sayLine > 0) {
        a.stopAudio = true;
        a.dialogueLine = in.sayLine;
        markPlayed();
        dialogueNow_ = in.sayLine;
        mode_ = Mode::Speaking;
        return a;
      }
      if (in.linkFailed) {
        a.stopAudio = true;
        dialogueNow_ = 0;
        play(a, Folder::Rival);
        mode_ = Mode::Speaking;
        return a;
      }
      bool shakeReady = !shaken_ || now_ - lastShakeMs_ >= uint32_t(cfg_.shakeCooldownSeconds) * 1000u;
      if (in.motion != MotionEvent::None && mode_ != Mode::Console && shakeReady && !in.conversing) {
        a.stopAudio = true;
        play(a, Folder::Shake);
        mode_ = Mode::Speaking;
        shaken_ = true;
        lastShakeMs_ = now_;
        return a;
      }
      if (mode_ == Mode::Speaking) {
        if (finished(in)) {
          if (dialogueNow_ > 0) a.dialogueDone = dialogueNow_;
          dialogueNow_ = 0;
          mode_ = Mode::Idle;
        }
        return a;
      }
      if (mode_ == Mode::Console) {
        if (now_ - consoleStartMs_ >= kConsoleMs && finished(in)) mode_ = Mode::Idle;
        return a;
      }
      // Idle
      bool greetReady = !greeted_ || now_ - lastGreetMs_ >= uint32_t(cfg_.greetCooldownMinutes) * 60000u;
      if (in.proximityEdge && !a.quiet && greetReady && !in.conversing) {
        play(a, greetFolder(in));
        mode_ = Mode::Speaking;
        greeted_ = true;
        lastGreetMs_ = now_;
        return a;
      }
      bool gapOk = !havePlayed_ || now_ - lastPlayMs_ >= kIdleQuietGapMs;
      bool idleDue = static_cast<int32_t>(now_ - nextIdleAtMs_) >= 0;  // wrap-safe after ~49.7 days
      if (!a.quiet && idleDue && gapOk && !in.conversing) {
        play(a, Folder::Idle);
        mode_ = Mode::Speaking;
        nextIdleAtMs_ = now_ + nextIdleDelayMs(rng_.random(), cfg_.idleMinMinutes, cfg_.idleMaxMinutes);
      }
      return a;
    }

    case Mode::Pleading:
      if (!in.touchDown) {
        a.stopAudio = true;
        play(a, Folder::Relief);
        mode_ = Mode::Speaking;
        return a;
      }
      if (finished(in)) {
        if (++seqIndex_ < pleaLen_) play(a, Folder::Plea, seqIndex_);
        else startDaisy(a);
      }
      return a;

    case Mode::Daisy:
      if (finished(in)) {
        if (++seqIndex_ < daisyLen_) {
          seqStartMs_ = now_;
          play(a, Folder::Daisy, seqIndex_);
        } else {
          mode_ = Mode::Dark;
          darkStartMs_ = now_;
        }
      }
      return a;

    case Mode::Dark:
      if (now_ - darkStartMs_ >= kDarkMs) a.restart = true;
      return a;
  }
  return a;
}

}  // namespace hal
