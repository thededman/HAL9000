#include "hal_conductor.h"

namespace hal {

void Conductor::start(const Dialogue& d, uint32_t nowMs) {
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
    finish("done", false, true, out);
    return;
  }
  int pause = d_.lines[idx_].pauseMs;
  state_ = State::Gap;
  at_ = nowMs + (pause > 0 ? static_cast<uint32_t>(pause) : kGapMs);
}

ConductorOut Conductor::tick(uint32_t nowMs) {
  ConductorOut out;
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
