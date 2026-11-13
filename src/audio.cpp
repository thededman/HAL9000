#include "audio.h"

#include <M5Unified.h>
#include <SD.h>

namespace {
constexpr int kChannel = 0;
constexpr uint32_t kFrameMs = 20;
}  // namespace

void Audio::release() {
  // Never free a buffer the speaker task may still be reading: tell it to stop, give it time,
  // and if it still hasn't let go, keep the memory rather than risk it.
  if (buf_ && M5.Speaker.isPlaying(kChannel)) {
    M5.Speaker.stop(kChannel);
    uint32_t t0 = millis();
    while (M5.Speaker.isPlaying(kChannel) && millis() - t0 < 500) delay(1);
  }
  if (buf_) {
    if (!M5.Speaker.isPlaying(kChannel)) free(buf_);
    else Serial.println("audio: the speaker wouldn't let go of a clip; its memory is left alone");
  }
  buf_ = nullptr;
  env_.clear();
}

bool Audio::playFile(const char* path) {
  stop();
  release();

  File f = SD.open(path, FILE_READ);
  if (!f) {
    Serial.printf("skip %s: can't open\n", path);
    return false;
  }
  size_t len = f.size();
  buf_ = static_cast<uint8_t*>(ps_malloc(len));
  if (!buf_) {
    Serial.printf("skip %s: no memory for %u bytes\n", path, unsigned(len));
    f.close();
    return false;
  }
  size_t got = f.read(buf_, len);
  f.close();
  if (got != len) {
    Serial.printf("skip %s: short read\n", path);
    release();
    return false;
  }
  return start(len, path);
}

bool Audio::playBuffer(uint8_t* data, size_t len, const char* label) {
  stop();
  release();    // the clip that was playing, once the speaker has let go of it
  buf_ = data;  // ours now, and freed by release() like any other
  if (!buf_) return false;
  return start(len, label);
}

bool Audio::start(size_t len, const char* label) {
  if (!hal::parseWav(buf_, len, &info_)) {
    Serial.printf("skip %s: not a 16-bit PCM WAV\n", label);
    release();
    return false;
  }
  env_ = hal::loudnessEnvelope(buf_, info_, kFrameMs);
  const int16_t* pcm = reinterpret_cast<const int16_t*>(buf_ + info_.dataOffset);
  size_t samples = info_.dataBytes / 2;
  if (!M5.Speaker.playRaw(pcm, samples, info_.sampleRate, info_.channels == 2, 1, kChannel, true)) {
    Serial.printf("skip %s: speaker refused\n", label);
    release();
    return false;
  }
  startMs_ = millis();
  Serial.printf("play %s (%u ms)\n", label, unsigned(info_.durationMs));
  return true;
}

void Audio::stop() { M5.Speaker.stop(kChannel); }

bool Audio::isPlaying() const { return M5.Speaker.isPlaying(kChannel); }

uint8_t Audio::level() const {
  if (env_.empty() || !isPlaying()) return 0;
  size_t i = (millis() - startMs_) / kFrameMs;
  return i < env_.size() ? env_[i] : 0;
}

void Audio::setVolume(uint8_t v) {
  if (v == volume_) return;
  volume_ = v;
  M5.Speaker.setVolume(v);
}
