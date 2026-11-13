#include "mic.h"

#include "hal_log.h"

bool Mic::start() {
  if (!dev_ && !tried_) {
    tried_ = true;
    if (SDL_WasInit(SDL_INIT_AUDIO) == 0 && SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
      halLog("microphone: %s", SDL_GetError());
      return false;
    }
    SDL_AudioSpec want{}, have{};
    want.freq = hal::Recording::kRate;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 1024;
    dev_ = SDL_OpenAudioDevice(nullptr, 1, &want, &have, 0);  // SDL converts whatever the device gives
    if (!dev_) halLog("microphone: %s", SDL_GetError());
    else halLog("microphone: ready (%d Hz)", have.freq);
  }
  if (!dev_) return false;
  SDL_ClearQueuedAudio(dev_);
  SDL_PauseAudioDevice(dev_, 0);
  return true;
}

void Mic::pump(hal::Recording* into) {
  if (!dev_) return;
  int16_t buf[2048];
  while (true) {
    Uint32 got = SDL_DequeueAudio(dev_, buf, sizeof buf);
    if (got == 0) break;
    into->append(buf, got / 2);
  }
}

void Mic::stop() {
  if (dev_) SDL_PauseAudioDevice(dev_, 1);
}

void Mic::close() {
  if (dev_) SDL_CloseAudioDevice(dev_);
  dev_ = 0;
}
