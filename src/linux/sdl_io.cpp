#include "sdl_io.h"

#include "hal_log.h"

bool SdlIo::open(int w, int h, bool fullscreen, int scale) {
  w_ = w;
  h_ = h;
  if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
    halLog("SDL video: %s", SDL_GetError());
    return false;
  }
  Uint32 flags = fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0;
  window_ = SDL_CreateWindow("HAL Zero", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w * scale, h * scale, flags);
  if (!window_) {
    halLog("SDL window: %s", SDL_GetError());
    return false;
  }
  renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_PRESENTVSYNC);
  if (!renderer_) renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_SOFTWARE);
  if (!renderer_) {
    halLog("SDL renderer: %s", SDL_GetError());
    return false;
  }
  SDL_RenderSetLogicalSize(renderer_, w, h);  // keeps the 320x170 picture's shape on any screen
  texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, w, h);
  pixels_.resize(size_t(w) * h);
  if (fullscreen) SDL_ShowCursor(SDL_DISABLE);
  return texture_ != nullptr;
}

void SdlIo::present(M5Canvas& frame) {
  const auto* src = static_cast<const uint16_t*>(frame.getBuffer());
  for (size_t i = 0; i < pixels_.size(); ++i) pixels_[i] = uint16_t((src[i] >> 8) | (src[i] << 8));
  SDL_UpdateTexture(texture_, nullptr, pixels_.data(), w_ * 2);
  SDL_RenderClear(renderer_);
  SDL_RenderCopy(renderer_, texture_, nullptr, nullptr);
  SDL_RenderPresent(renderer_);
}

void SdlIo::poll(KeyInput* keys) {
  keys->pressed.clear();
  SDL_Event e;
  while (SDL_PollEvent(&e)) {
    if (e.type == SDL_QUIT) keys->quit = true;
    if (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) {
      bool down = e.type == SDL_KEYDOWN;
      if (e.key.keysym.sym == SDLK_SPACE) space_ = down;
      if (down && !e.key.repeat) keys->pressed.push_back(e.key.keysym.sym);
    }
  }
  keys->spaceDown = space_;
}

void SdlIo::close() {
  if (texture_) SDL_DestroyTexture(texture_);
  if (renderer_) SDL_DestroyRenderer(renderer_);
  if (window_) SDL_DestroyWindow(window_);
  texture_ = nullptr;
  renderer_ = nullptr;
  window_ = nullptr;
}
