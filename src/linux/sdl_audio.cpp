#include "sdl_audio.h"

#include "hal_log.h"

namespace {
constexpr int kRate = 22050;  // HAL's clips; other rates are converted
constexpr uint32_t kFrameMs = 20;
}  // namespace

bool SdlAudio::begin() {
  if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
    halLog("SDL audio: %s", SDL_GetError());
    return false;
  }
  SDL_AudioSpec want{};
  want.freq = kRate;
  want.format = AUDIO_S16SYS;
  want.channels = 1;
  want.samples = 1024;
  dev_ = SDL_OpenAudioDevice(nullptr, 0, &want, &have_, 0);
  if (!dev_) {
    halLog("SDL audio device: %s", SDL_GetError());
    return false;
  }
  SDL_PauseAudioDevice(dev_, 0);
  return true;
}

bool SdlAudio::playFile(const hal::Storage& storage, const std::string& halPath) {
  if (!dev_) return false;
  stop();
  std::vector<uint8_t> file;
  hal::WavInfo info;
  if (!storage.readAll(halPath, &file) || !hal::parseWav(file.data(), file.size(), &info)) {
    halLog("skip %s: can't read it as a 16-bit PCM WAV", halPath.c_str());
    return false;
  }
  env_ = hal::loudnessEnvelope(file.data(), info, kFrameMs);

  // Convert to mono 22.05 kHz, applying the volume as we go.
  SDL_AudioStream* s = SDL_NewAudioStream(AUDIO_S16LSB, info.channels, info.sampleRate, AUDIO_S16SYS, 1, kRate);
  if (!s) return false;
  std::vector<int16_t> scaled(info.dataBytes / 2);
  const auto* pcm = reinterpret_cast<const int16_t*>(file.data() + info.dataOffset);
  for (size_t i = 0; i < scaled.size(); ++i) scaled[i] = int16_t(int32_t(pcm[i]) * volume_ / 255);
  SDL_AudioStreamPut(s, scaled.data(), int(scaled.size() * 2));
  SDL_AudioStreamFlush(s);
  std::vector<uint8_t> out(size_t(SDL_AudioStreamAvailable(s)));
  int got = SDL_AudioStreamGet(s, out.data(), int(out.size()));
  SDL_FreeAudioStream(s);
  if (got <= 0) return false;
  SDL_QueueAudio(dev_, out.data(), Uint32(got));
  queuedBytes_ = Uint32(got);
  halLog("play %s (%u ms)", halPath.c_str(), unsigned(info.durationMs));
  return true;
}

void SdlAudio::stop() {
  if (dev_) SDL_ClearQueuedAudio(dev_);
  queuedBytes_ = 0;
}

bool SdlAudio::isPlaying() const { return dev_ && SDL_GetQueuedAudioSize(dev_) > 0; }

uint8_t SdlAudio::level() const {
  if (!isPlaying() || env_.empty()) return 0;
  uint32_t played = queuedBytes_ - SDL_GetQueuedAudioSize(dev_);
  size_t i = size_t(played / 2) * 1000 / kRate / kFrameMs;
  return i < env_.size() ? env_[i] : 0;
}

void SdlAudio::close() {
  if (dev_) SDL_CloseAudioDevice(dev_);
  dev_ = 0;
}
