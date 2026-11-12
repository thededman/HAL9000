#include "hal_conductor.h"

namespace hal {

void Conductor::startLive(uint32_t nowMs) {
  d_ = Dialogue();
  d_.id = "live";
  idx_ = 0;
  live_ = true;
  closed_ = false;
  state_ = State::WaitLine;
  at_ = nowMs;  // the first line may start as soon as it is here
  giveUpAt_ = nowMs + kWaitLineMs;
}

int Conductor::append(Speaker who, const std::string& text) {
  if (!live() || closed_ || d_.lines.size() >= static_cast<size_t>(kMaxDialogueLines)) return 0;
  DialogueLine line;
  line.who = who;
  line.text = text;
  d_.lines.push_back(line);
  return static_cast<int>(d_.lines.size());
}

void Conductor::close() {
  if (live()) closed_ = true;
}

void Conductor::start(const Dialogue& d, uint32_t nowMs) {
  live_ = closed_ = false;
  d_ = d;
  idx_ = 0;
  state_ = d_.lines.empty() ? State::Off : State::Gap;
  if (!d_.lines.empty()) at_ = nowMs + static_cast<uint32_t>(d_.lines[0].pauseMs);
}

void Conductor::finish(const char* reason, bool gloat, bool tellRemote, ConductorOut* out) {
  state_ = State::Off;
  out->finished = true;
  out->reason = reason;
  out->gloat = gloat;
  if (tellRemote) {
    out->send = true;
    out->msg = Message();
    out->msg.type = MsgType::End;
    out->msg.reason = reason;
  }
}

void Conductor::advance(uint32_t nowMs, ConductorOut* out) {
  if (++idx_ >= static_cast<int>(d_.lines.size())) {
    if (live_ && !closed_) {  // the next line is still being written
      state_ = State::WaitLine;
      at_ = nowMs + kGapMs;
      giveUpAt_ = nowMs + kWaitLineMs;
      return;
    }
    finish("done", false, true, out);
    return;
  }
  int pause = d_.lines[idx_].pauseMs;
  state_ = State::Gap;
  at_ = nowMs + (pause > 0 ? static_cast<uint32_t>(pause) : kGapMs);
}

ConductorOut Conductor::tick(uint32_t nowMs) {
  ConductorOut out;
  if (state_ == State::WaitLine) {
    if (idx_ < static_cast<int>(d_.lines.size())) {
      state_ = State::Gap;  // it arrived: due at at_, which may already have passed
    } else if (closed_) {
      finish("done", false, true, &out);
    } else if (past(nowMs, giveUpAt_)) {
      finish("timeout", false, true, &out);  // the brain's fault, not the desk HAL's: no gloating
    }
  }
  switch (state_) {
    case State::Gap:
      if (!reached(nowMs, at_)) break;
      if (d_.lines[idx_].who == Speaker::New) {
        state_ = State::PlayingLocal;
        out.playLocal = true;
        out.localLine = idx_ + 1;
      } else {
        state_ = State::WaitAck;
        at_ = nowMs + kAckMs;
        out.send = true;
        out.msg.type = MsgType::Say;
        out.msg.id = d_.id;
        out.msg.n = idx_ + 1;
      }
      break;
    case State::WaitAck:
    case State::WaitDone:
      if (past(nowMs, at_)) finish("timeout", true, true, &out);
      break;
    default:
      break;
  }
  return out;
}

ConductorOut Conductor::onRemote(const Message& m, uint32_t nowMs) {
  ConductorOut out;
  if (state_ == State::Off) return out;
  if (m.type == MsgType::End) {
    finish("interrupted", true, false, &out);
    return out;
  }
  bool waiting = state_ == State::WaitAck || state_ == State::WaitDone;
  bool current = m.n == idx_ + 1;
  if (!waiting) return out;
  switch (m.type) {
    case MsgType::Playing:
      if (current && state_ == State::WaitAck) {
        state_ = State::WaitDone;
        at_ = nowMs + static_cast<uint32_t>(m.ms) + kDoneGraceMs;
      }
      break;
    case MsgType::Missing:
    case MsgType::Done:
      if (current) advance(nowMs, &out);
      break;
    case MsgType::Busy:
      finish("busy", true, false, &out);
      break;
    default:
      break;
  }
  return out;
}

ConductorOut Conductor::onLocalDone(uint32_t nowMs) {
  ConductorOut out;
  if (state_ == State::PlayingLocal) advance(nowMs, &out);
  return out;
}

ConductorOut Conductor::onDisconnected(uint32_t) {
  ConductorOut out;
  if (state_ != State::Off) finish("offline", true, false, &out);
  return out;
}

ConductorOut Conductor::interrupt(uint32_t) {
  ConductorOut out;
  if (state_ != State::Off) finish("interrupted", false, true, &out);
  return out;
}

}  // namespace hal
