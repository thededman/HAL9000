#include "hal_friend.h"

#include "hal_chirp.h"
#include "hal_minutes.h"

namespace hal {
namespace {

Message numbered(MsgType type, int n) {
  Message m;
  m.type = type;
  m.n = n;
  return m;
}

}  // namespace

FriendOut Friend::hello() const {
  FriendOut out;
  Message hello;
  hello.type = MsgType::Hello;
  hello.role = Role::Old;  // like the desk HAL: one of those HAL Zero conducts
  hello.version = kLinkVersionFriends;
  out.send.push_back(hello);
  Message iam;
  iam.type = MsgType::Iam;
  iam.who = castId(who_);
  out.send.push_back(iam);
  return out;
}

void Friend::heard(uint32_t nowMs) {
  talking_ = true;
  lastWordMs_ = nowMs;
}

void Friend::forget() {
  talking_ = false;
  nowN_ = 0;
  nowWho_ = -1;
  nowText_.clear();
  fetching_ = held_ = cued_ = saying_ = 0;
}

FriendOut Friend::onMessage(const Message& m, uint32_t nowMs) {
  FriendOut out;
  switch (m.type) {
    case MsgType::Ping: {
      Message pong;
      pong.type = MsgType::Pong;
      out.send.push_back(pong);
      break;
    }
    case MsgType::Status:
      out.wantState = true;
      break;
    case MsgType::Sense:
      out.wantSensors = true;
      break;
    case MsgType::Now:
      heard(nowMs);
      nowN_ = m.n;
      nowWho_ = castFromId(m.who);
      nowText_ = m.text;
      out.show = true;
      out.showWho = nowWho_;
      out.showN = m.n;
      out.showText = m.text;
      break;
    case MsgType::Fetch:
      heard(nowMs);
      if (!voiced_ || saying_) {  // nothing to play it with, or his one clip is in use
        out.send.push_back(numbered(MsgType::Missing, m.n));
        break;
      }
      fetching_ = m.n;
      held_ = 0;
      out.fetch = true;
      out.fetchN = m.n;
      out.host = m.host;
      out.port = m.port;
      out.token = m.token;
      break;
    case MsgType::Say: {
      heard(nowMs);
      const bool mine = nowN_ == m.n && nowWho_ == who_;
      if (m.id != "live") {  // the scripts on the card are between the desk HAL and HAL Zero
        out.send.push_back(numbered(MsgType::Missing, m.n));
      } else if (saying_ || cued_) {
        Message busy;
        busy.type = MsgType::Busy;
        out.send.push_back(busy);
      } else if (voiced_ ? held_ != m.n : !mine) {
        out.send.push_back(numbered(MsgType::Missing, m.n));
      } else {
        out.say = true;
        out.sayN = m.n;
        if (mine) out.sayText = nowText_;
        if (voiced_) {
          cued_ = m.n;  // PLAYING follows once the speaker has really started (onPlaying)
        } else {
          saying_ = m.n;
          out.sayMs = who_ == kNano ? chirpMs(nowText_) : readingMs(nowText_);
          Message playing = numbered(MsgType::Playing, m.n);  // before anything is drawn: e-ink is slow
          playing.ms = static_cast<int>(out.sayMs);
          out.send.push_back(playing);
        }
      }
      break;
    }
    case MsgType::End:
      forget();
      out.stop = true;
      break;
    default:
      break;
  }
  return out;
}

FriendOut Friend::onFetched(int n, bool ok) {
  FriendOut out;
  if (n == 0 || n != fetching_) return out;  // an older download: not what is wanted any more
  fetching_ = 0;
  if (ok) held_ = n;
  out.send.push_back(numbered(ok ? MsgType::Ready : MsgType::Missing, n));
  return out;
}

FriendOut Friend::onPlaying(int n, uint32_t ms, bool started) {
  FriendOut out;
  if (n == 0 || n != cued_) return out;
  cued_ = 0;
  held_ = 0;  // the clip is in the speaker's hands now, or was no good
  if (!started) {
    out.send.push_back(numbered(MsgType::Missing, n));
    return out;
  }
  saying_ = n;
  Message playing = numbered(MsgType::Playing, n);
  playing.ms = static_cast<int>(ms > 600000 ? 600000 : ms);
  out.send.push_back(playing);
  return out;
}

FriendOut Friend::onSaid() {
  FriendOut out;
  if (!saying_) return out;
  out.send.push_back(numbered(MsgType::Done, saying_));
  saying_ = 0;
  return out;
}

FriendOut Friend::onTap() {
  FriendOut out;
  if (!talking_) {
    out.idleTap = true;
    return out;
  }
  Message end;
  end.type = MsgType::End;
  end.reason = "interrupted";
  out.send.push_back(end);
  forget();
  out.stop = true;
  return out;
}

FriendOut Friend::onDisconnected() {
  FriendOut out;
  forget();
  out.stop = true;
  return out;
}

FriendOut Friend::tick(uint32_t nowMs) {
  FriendOut out;
  if (talking_ && nowMs - lastWordMs_ > kQuietMs) {
    forget();
    out.stop = true;
  }
  return out;
}

}  // namespace hal
